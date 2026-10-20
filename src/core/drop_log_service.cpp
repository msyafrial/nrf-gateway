#include "drop_log_service.hpp"
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
static uint32_t s_nextId = 1;
static bool s_initialized = false;

// Counter statistik akumulatif sesi (Atomic thread-safe Core 0 vs Core 1)
static std::atomic<uint32_t> s_statTotalDropped{0};
static std::atomic<uint32_t> s_statTtlExpired{0};
static std::atomic<uint32_t> s_statRssiMin{0};
static std::atomic<uint32_t> s_statUuidMismatch{0};
static std::atomic<uint32_t> s_statMajorMismatch{0};
static std::atomic<uint32_t> s_statQueueOverflow{0};
static std::atomic<uint32_t> s_statMqttFailed{0};

void DropLogService::init() {
    if (s_dropMutex == nullptr) {
        s_dropMutex = xSemaphoreCreateMutex();
    }
    s_dropCount = 0;
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
        case DropReason::TTL_EXPIRED:       return "TTL Kadaluarsa";
        case DropReason::RSSI_OUT_OF_RANGE: return "Di Bawah RSSI Min";
        case DropReason::UUID_MISMATCH:     return "UUID Tidak Cocok";
        case DropReason::MAJOR_MISMATCH:    return "Major Tidak Cocok";
        case DropReason::QUEUE_OVERFLOW:    return "Antrean Penuh";
        case DropReason::MQTT_FAIL:         return "Gagal Publish MQTT";
        default:                            return "Drop Lainnya";
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
        default:                            return "neutral";
    }
}

void DropLogService::record(const char* code, const char* tag, int8_t rssi, DropReason reason) {
    if (!s_initialized) {
        init();
    }

    // Perbarui counter akumulasi statistik secara atomic tanpa data-race
    s_statTotalDropped.fetch_add(1, std::memory_order_relaxed);
    switch (reason) {
        case DropReason::TTL_EXPIRED:       s_statTtlExpired.fetch_add(1, std::memory_order_relaxed); break;
        case DropReason::RSSI_OUT_OF_RANGE: s_statRssiMin.fetch_add(1, std::memory_order_relaxed); break;
        case DropReason::UUID_MISMATCH:     s_statUuidMismatch.fetch_add(1, std::memory_order_relaxed); break;
        case DropReason::MAJOR_MISMATCH:    s_statMajorMismatch.fetch_add(1, std::memory_order_relaxed); break;
        case DropReason::QUEUE_OVERFLOW:    s_statQueueOverflow.fetch_add(1, std::memory_order_relaxed); break;
        case DropReason::MQTT_FAIL:         s_statMqttFailed.fetch_add(1, std::memory_order_relaxed); break;
    }

    // Mutex take non-blocking (maksimal 2 ms) agar tidak menghambat Core 0 UART ingestion
    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        uint32_t uptimeSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        char timeStr[24] = {0};
        formatTimestamp(timeStr, sizeof(timeStr), uptimeSec);

        // Geser entri lama ke bawah (FIFO Rolling Buffer)
        if (s_dropCount < MAX_DROP_LOGS) {
            std::memmove(&s_dropLogs[1], &s_dropLogs[0], s_dropCount * sizeof(DropLogEntry));
            s_dropCount++;
        } else {
            std::memmove(&s_dropLogs[1], &s_dropLogs[0], (MAX_DROP_LOGS - 1) * sizeof(DropLogEntry));
        }

        // Simpan data drop terbaru pada indeks 0
        s_dropLogs[0].id = s_nextId++;
        s_dropLogs[0].uptime_sec = uptimeSec;
        strlcpy(s_dropLogs[0].time_str, timeStr, sizeof(s_dropLogs[0].time_str));
        strlcpy(s_dropLogs[0].code, code ? code : "BLE", sizeof(s_dropLogs[0].code));
        strlcpy(s_dropLogs[0].tag, tag ? tag : "-", sizeof(s_dropLogs[0].tag));
        s_dropLogs[0].rssi = rssi;
        s_dropLogs[0].reason = reason;

        xSemaphoreGive(s_dropMutex);
    }
}

void DropLogService::record(const BeaconPacket& p, DropReason reason) {
    const char* code = (p.type == BeaconType::IBEACON_EXT) ? "ibeacon_ext" : "iBeacon";
    char tagBuf[24];
    snprintf(tagBuf, sizeof(tagBuf), "%05u-%05u", p.major, p.minor);
    record(code, tagBuf, p.rssi, reason);
}

size_t DropLogService::getLogs(DropLogEntry* outEntries, size_t maxCount) {
    if (!outEntries || maxCount == 0) return 0;
    size_t count = 0;

    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_dropCount, maxCount);
        std::memcpy(outEntries, s_dropLogs, count * sizeof(DropLogEntry));
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
    if (s_dropMutex != nullptr && xSemaphoreTake(s_dropMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_dropCount = 0;
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
    return stats;
}

} // namespace Iotaru::Core
