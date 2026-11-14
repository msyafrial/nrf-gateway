#include "drop_log_service.hpp"
#include "mqtt_service.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <atomic>

static const char *TAG = "DROP_LOG";

namespace Iotaru::Core {

static SemaphoreHandle_t s_dropMutex = nullptr;
static DropLogEntry s_dropLogs[MAX_DROP_LOGS];
static size_t s_dropCount = 0;
static size_t s_dropHead = 0; // Circular write index O(1)
static uint32_t s_nextId = 1;
static bool s_initialized = false;

static constexpr char HEX_LUT[] = "0123456789ABCDEF";

static inline void fastBytesToHex(const uint8_t* in, size_t inLen, char* outHex) {
    for (size_t i = 0; i < inLen; ++i) {
        outHex[i * 2]     = HEX_LUT[(in[i] >> 4) & 0x0F];
        outHex[i * 2 + 1] = HEX_LUT[in[i] & 0x0F];
    }
    outHex[inLen * 2] = '\0';
}

static inline void fastMacToStr(const uint8_t* mac, char* outStr) {
    if (!mac) {
        outStr[0] = '-';
        outStr[1] = '\0';
        return;
    }
    for (int i = 0; i < 6; ++i) {
        outStr[i * 3]     = HEX_LUT[(mac[i] >> 4) & 0x0F];
        outStr[i * 3 + 1] = HEX_LUT[mac[i] & 0x0F];
        if (i < 5) outStr[i * 3 + 2] = ':';
    }
    outStr[17] = '\0';
}

// Counter statistik akumulatif sesi (Atomic thread-safe Core 0 vs Core 1)
static std::atomic<uint32_t> s_statTotalDropped{0};
static std::atomic<uint32_t> s_statTtlExpired{0};
static std::atomic<uint32_t> s_statRssiMin{0};
static std::atomic<uint32_t> s_statUuidMismatch{0};
static std::atomic<uint32_t> s_statMajorMismatch{0};
static std::atomic<uint32_t> s_statQueueOverflow{0};
static std::atomic<uint32_t> s_statMqttFailed{0};
static std::atomic<uint32_t> s_statUnknownFormat{0};

void DropLogService::init() {
    if (s_dropMutex == nullptr) {
        s_dropMutex = xSemaphoreCreateMutex();
    }
    s_dropCount = 0;
    s_dropHead = 0;
    s_nextId = 1;
    s_initialized = true;
    ESP_LOGI(TAG, "DropLogService diinisialisasi. Kapasitas: %u entri FIFO di RAM.", (unsigned int)MAX_DROP_LOGS);
}

void DropLogService::formatTimestamp(char* outBuf, size_t maxLen, uint32_t uptimeSec) {
    if (!outBuf || maxLen == 0) return;

    time_t now = 0;
    time(&now);

    // Jika waktu UNIX sudah sinkron (lebih dari 1 Januari 2024)
    if (now > 1704067200LL) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(outBuf, maxLen, "%Y-%m-%d %H:%M:%S", &timeinfo);
    } else {
        uint32_t hrs = uptimeSec / 3600;
        uint32_t mins = (uptimeSec % 3600) / 60;
        uint32_t secs = uptimeSec % 60;
        snprintf(outBuf, maxLen, "+%02u:%02u:%02u", (unsigned int)hrs, (unsigned int)mins, (unsigned int)secs);
    }
}

const char* DropLogService::reasonToString(DropReason reason) {
    switch (reason) {
        case DropReason::TTL_EXPIRED:       return "TTL Expired";
        case DropReason::RSSI_OUT_OF_RANGE: return "Below Min RSSI";
        case DropReason::UUID_MISMATCH:     return "UUID Mismatch";
        case DropReason::MAJOR_MISMATCH:    return "Major Mismatch";
        case DropReason::QUEUE_OVERFLOW:    return "Queue Full";
        case DropReason::MQTT_FAIL:         return "MQTT Publish Failed";
        case DropReason::UNKNOWN_FORMAT:    return "Unknown Format";
        default:                            return "Other Drop";
    }
}

const char* DropLogService::reasonToStatusClass(DropReason reason) {
    switch (reason) {
        case DropReason::TTL_EXPIRED:       return "slate";
        case DropReason::RSSI_OUT_OF_RANGE: return "warning";
        case DropReason::UUID_MISMATCH:     return "orange";
        case DropReason::MAJOR_MISMATCH:    return "orange";
        case DropReason::QUEUE_OVERFLOW:    return "danger";
        case DropReason::MQTT_FAIL:         return "danger";
        case DropReason::UNKNOWN_FORMAT:    return "orange";
        default:                            return "neutral";
    }
}

void DropLogService::record(const char* code, const char* tag, int8_t rssi, DropReason reason, uint32_t count, const char* mac, const char* raw) {
    if (reason != DropReason::MQTT_FAIL && !Network::MqttService::isConnected()) {
        return;
    }
    if (!s_initialized) {
        init();
    }
    if (count == 0) count = 1;

    // Perbarui counter akumulasi statistik secara atomic tanpa data-race
    s_statTotalDropped.fetch_add(count, std::memory_order_relaxed);
    switch (reason) {
        case DropReason::TTL_EXPIRED:       s_statTtlExpired.fetch_add(count, std::memory_order_relaxed); break;
        case DropReason::RSSI_OUT_OF_RANGE: s_statRssiMin.fetch_add(count, std::memory_order_relaxed); break;
        case DropReason::UUID_MISMATCH:     s_statUuidMismatch.fetch_add(count, std::memory_order_relaxed); break;
        case DropReason::MAJOR_MISMATCH:    s_statMajorMismatch.fetch_add(count, std::memory_order_relaxed); break;
        case DropReason::QUEUE_OVERFLOW:    s_statQueueOverflow.fetch_add(count, std::memory_order_relaxed); break;
        case DropReason::MQTT_FAIL:         s_statMqttFailed.fetch_add(count, std::memory_order_relaxed); break;
        case DropReason::UNKNOWN_FORMAT:    s_statUnknownFormat.fetch_add(count, std::memory_order_relaxed); break;
    }

    // Rate-limit penulisan ke circular buffer log saat flood filter (>40/dtk), counter statistik tetap 100% akurat
    static std::atomic<int64_t> s_lastLogUs{0};
    if (reason != DropReason::MQTT_FAIL && reason != DropReason::QUEUE_OVERFLOW) {
        int64_t nowUs = esp_timer_get_time();
        int64_t prevUs = s_lastLogUs.load(std::memory_order_relaxed);
        if ((nowUs - prevUs) < 25000LL) {
            return;
        }
        s_lastLogUs.store(nowUs, std::memory_order_relaxed);
    }

    // Mutex take non-blocking (maksimal 2 ms) agar tidak menghambat Core 0 UART ingestion
    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        uint32_t uptimeSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        static uint32_t s_cachedUptimeSec = 0xFFFFFFFF;
        static char s_cachedTimeStr[24] = {0};
        if (uptimeSec != s_cachedUptimeSec) {
            formatTimestamp(s_cachedTimeStr, sizeof(s_cachedTimeStr), uptimeSec);
            s_cachedUptimeSec = uptimeSec;
        }

        // Tulis langsung ke indeks head tanpa memmove besar (O(1) Circular Buffer)
        size_t writeIdx = s_dropHead;
        s_dropLogs[writeIdx].id = s_nextId++;
        s_dropLogs[writeIdx].uptime_sec = uptimeSec;
        strlcpy(s_dropLogs[writeIdx].time_str, s_cachedTimeStr, sizeof(s_dropLogs[writeIdx].time_str));
        strlcpy(s_dropLogs[writeIdx].code, code ? code : "BLE", sizeof(s_dropLogs[writeIdx].code));
        strlcpy(s_dropLogs[writeIdx].tag, tag ? tag : "-", sizeof(s_dropLogs[writeIdx].tag));
        strlcpy(s_dropLogs[writeIdx].mac, mac ? mac : "-", sizeof(s_dropLogs[writeIdx].mac));
        strlcpy(s_dropLogs[writeIdx].raw, raw ? raw : "-", sizeof(s_dropLogs[writeIdx].raw));
        s_dropLogs[writeIdx].rssi = rssi;
        s_dropLogs[writeIdx].reason = reason;

        s_dropHead = (s_dropHead + 1) % MAX_DROP_LOGS;
        if (s_dropCount < MAX_DROP_LOGS) {
            s_dropCount++;
        }

        xSemaphoreGive(s_dropMutex);
    }
}

void DropLogService::record(const BeaconPacket& p, DropReason reason, uint32_t count) {
    const char* code = (p.type == BeaconType::IBEACON_EXT) ? "ibeacon_ext" :
                       (p.type == BeaconType::IBEACON) ? "iBeacon" :
                       (p.type == BeaconType::EDDYSTONE) ? "Eddystone" :
                       (p.type == BeaconType::MOKO_SENSOR || p.type == BeaconType::MOKO_ALARM) ? "MOKO" : "BLE";
    char tagBuf[24];
    const RawBleBinaryFrame* raw = reinterpret_cast<const RawBleBinaryFrame*>(p.raw_frame);
    if (p.has_major) {
        snprintf(tagBuf, sizeof(tagBuf), "%05u-%05u", p.major, p.minor);
    } else {
        snprintf(tagBuf, sizeof(tagBuf), "%02X%02X", raw->mac[4], raw->mac[5]);
    }

    char macStr[18];
    fastMacToStr(raw->mac, macStr);

    char rawStr[86];
    fastBytesToHex(p.raw_frame, 42, rawStr);

    record(code, tagBuf, p.rssi, reason, count, macStr, rawStr);
}

size_t DropLogService::getLogs(DropLogEntry* outEntries, size_t maxCount) {
    if (!outEntries || maxCount == 0) return 0;
    size_t count = 0;

    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_dropCount, maxCount);
        // Susun riwayat dari entri terbaru ke terlama (newest-first)
        for (size_t i = 0; i < count; ++i) {
            size_t srcIdx = (s_dropHead + MAX_DROP_LOGS - 1 - i) % MAX_DROP_LOGS;
            outEntries[i] = s_dropLogs[srcIdx];
        }
        xSemaphoreGive(s_dropMutex);
    }
    return count;
}

size_t DropLogService::getLogCount() {
    size_t count = 0;
    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = s_dropCount;
        xSemaphoreGive(s_dropMutex);
    }
    return count;
}

void DropLogService::clearLogs() {
    // 1. Reset seluruh counter akumulasi statistik atomik secara instan dan tanpa jeda
    s_statTotalDropped.store(0, std::memory_order_relaxed);
    s_statTtlExpired.store(0, std::memory_order_relaxed);
    s_statRssiMin.store(0, std::memory_order_relaxed);
    s_statUuidMismatch.store(0, std::memory_order_relaxed);
    s_statMajorMismatch.store(0, std::memory_order_relaxed);
    s_statQueueOverflow.store(0, std::memory_order_relaxed);
    s_statMqttFailed.store(0, std::memory_order_relaxed);
    s_statUnknownFormat.store(0, std::memory_order_relaxed);

    // 2. Kosongkan riwayat circular array log jika mutex tersedia
    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_dropCount = 0;
        s_dropHead = 0;
        xSemaphoreGive(s_dropMutex);
    }
}

DropSummaryStats DropLogService::getSummaryStats() {
    DropSummaryStats stats;
    stats.total_dropped   = s_statTotalDropped.load(std::memory_order_relaxed);
    stats.ttl_expired     = s_statTtlExpired.load(std::memory_order_relaxed);
    stats.rssi_min        = s_statRssiMin.load(std::memory_order_relaxed);
    stats.uuid_mismatch   = s_statUuidMismatch.load(std::memory_order_relaxed);
    stats.major_mismatch  = s_statMajorMismatch.load(std::memory_order_relaxed);
    stats.queue_overflow  = s_statQueueOverflow.load(std::memory_order_relaxed);
    stats.mqtt_failed     = s_statMqttFailed.load(std::memory_order_relaxed);
    stats.unknown_format  = s_statUnknownFormat.load(std::memory_order_relaxed);
    return stats;
}

} // namespace Iotaru::Core
