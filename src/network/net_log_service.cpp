#include "net_log_service.hpp"
#include "bsp_at24c256.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

static const char *TAG = "NET_LOG";

namespace Iotaru::Network {

static SemaphoreHandle_t s_logMutex = nullptr;
static NetLogEntry s_logs[MAX_NET_LOGS];
static size_t s_logCount = 0;
static uint32_t s_nextId = 1;
static bool s_initialized = false;
static int s_headSlot = -1; // Index slot terakhir yang ditulis di EEPROM AT24C256

// Debouncing state
static NetCategory s_lastCat = NetCategory::SYSTEM;
static NetEvent s_lastEvt = NetEvent::BOOT;
static uint32_t s_lastLogTimeSec = 0;

static const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "Power-on";
        case ESP_RST_EXT:       return "External Pin";
        case ESP_RST_SW:        return "Software Reboot";
        case ESP_RST_PANIC:     return "Software Panic/Crash";
        case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT:  return "Task Watchdog";
        case ESP_RST_WDT:       return "Other Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep Wakeup";
        case ESP_RST_BROWNOUT:  return "Brownout (Under-voltage)";
        case ESP_RST_SDIO:      return "SDIO Reset";
        default:                return "Unknown";
    }
}

const char* NetLogService::categoryToString(NetCategory cat) {
    switch (cat) {
        case NetCategory::WIFI:     return "Wi-Fi";
        case NetCategory::ETHERNET: return "Ethernet";
        case NetCategory::MQTT:     return "MQTT";
        case NetCategory::SOFTAP:   return "SoftAP";
        case NetCategory::SYSTEM:   return "System";
        default:                    return "Unknown";
    }
}

const char* NetLogService::eventToString(NetEvent evt) {
    switch (evt) {
        case NetEvent::CONNECTED:    return "CONNECTED";
        case NetEvent::DISCONNECTED: return "DISCONNECTED";
        case NetEvent::GOT_IP:       return "GOT IP";
        case NetEvent::ERROR_EVT:    return "ERROR";
        case NetEvent::STARTED:      return "STARTED";
        case NetEvent::STOPPED:      return "STOPPED";
        case NetEvent::CLIENT_JOIN:  return "CLIENT JOIN";
        case NetEvent::CLIENT_LEAVE: return "CLIENT LEAVE";
        case NetEvent::BOOT:         return "SYSTEM BOOT";
        default:                     return "UNKNOWN";
    }
}

const char* NetLogService::eventToStatusClass(NetEvent evt) {
    switch (evt) {
        case NetEvent::CONNECTED:
        case NetEvent::GOT_IP:
            return "success";
        case NetEvent::DISCONNECTED:
        case NetEvent::ERROR_EVT:
            return "danger";
        case NetEvent::STARTED:
        case NetEvent::BOOT:
            return "primary";
        case NetEvent::CLIENT_JOIN:
        case NetEvent::CLIENT_LEAVE:
        case NetEvent::STOPPED:
            return "warning";
        default:
            return "neutral";
    }
}

void NetLogService::formatTimestamp(char* outBuf, size_t maxLen, uint32_t uptimeSec) {
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

void NetLogService::init() {
    if (s_logMutex == nullptr) {
        s_logMutex = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(s_logMutex, portMAX_DELAY);
    if (!s_initialized) {
        s_initialized = true;

        if (Bsp::At24c256::isDetected()) {
            loadFromEeprom();
            ESP_LOGI(TAG, "Mode Penyimpanan: AT24C256 EEPROM (200-Slot Rolling Ring-Buffer, FIFO 50 Max). Log termuat: %zu / 50", s_logCount);
        } else {
            s_logCount = 0;
            s_nextId = 1;
            ESP_LOGW(TAG, "Mode Penyimpanan: RAM-Only Fallback (Zero Flash Wear). LittleFS TIDAK DITULIS.");
        }

        // Catat peristiwa Booting sistem
        esp_reset_reason_t reason = esp_reset_reason();
        char bootDetail[80];
        snprintf(bootDetail, sizeof(bootDetail), "Gateway IOTARU SG-10 aktif (Reset Reason: %s)", resetReasonToString(reason));

        // Insert boot event (internal without recursive mutex lock)
        uint32_t uptimeSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        char timeStr[24] = {0};
        formatTimestamp(timeStr, sizeof(timeStr), uptimeSec);

        // Geser entri lama ke bawah jika sudah ada isi di RAM (FIFO 50)
        if (s_logCount < MAX_NET_LOGS) {
            std::memmove(&s_logs[1], &s_logs[0], s_logCount * sizeof(NetLogEntry));
            s_logCount++;
        } else {
            std::memmove(&s_logs[1], &s_logs[0], (MAX_NET_LOGS - 1) * sizeof(NetLogEntry));
        }

        s_logs[0].id = s_nextId++;
        s_logs[0].uptime_sec = uptimeSec;
        strlcpy(s_logs[0].time_str, timeStr, sizeof(s_logs[0].time_str));
        s_logs[0].category = NetCategory::SYSTEM;
        s_logs[0].event = NetEvent::BOOT;
        strlcpy(s_logs[0].detail, bootDetail, sizeof(s_logs[0].detail));

        if (Bsp::At24c256::isDetected()) {
            saveToEeprom(s_logs[0]);
        }

        ESP_LOGI(TAG, "NetLogService siap. Total log tersimpan di RAM: %zu / %zu", s_logCount, MAX_NET_LOGS);
    }
    xSemaphoreGive(s_logMutex);
}

void NetLogService::log(NetCategory cat, NetEvent evt, const char* detail) {
    if (!detail) detail = "-";

    uint32_t uptimeSec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);

    if (s_logMutex == nullptr) {
        s_logMutex = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return; // Jangan blokir task pemanggil jika mutex sedang sibuk
    }

    // Anti-Flapping Debounce: Abaikan event yang persis sama dalam rentang < 2 detik
    if (s_lastCat == cat && s_lastEvt == evt && (uptimeSec - s_lastLogTimeSec) < 2) {
        xSemaphoreGive(s_logMutex);
        return;
    }

    s_lastCat = cat;
    s_lastEvt = evt;
    s_lastLogTimeSec = uptimeSec;

    // Defensive Guard: Jika log() dipanggil sebelum NetLogService::init()
    if (!s_initialized) {
        if (Bsp::At24c256::isDetected()) {
            loadFromEeprom();
        }
        s_initialized = true;
    }

    char timeStr[24] = {0};
    formatTimestamp(timeStr, sizeof(timeStr), uptimeSec);

    // Geser entri lama ke bawah untuk menyisipkan data terbaru di index 0 (FIFO Max 50 di RAM)
    if (s_logCount < MAX_NET_LOGS) {
        std::memmove(&s_logs[1], &s_logs[0], s_logCount * sizeof(NetLogEntry));
        s_logCount++;
    } else {
        std::memmove(&s_logs[1], &s_logs[0], (MAX_NET_LOGS - 1) * sizeof(NetLogEntry));
    }

    s_logs[0].id = s_nextId++;
    s_logs[0].uptime_sec = uptimeSec;
    strlcpy(s_logs[0].time_str, timeStr, sizeof(s_logs[0].time_str));
    s_logs[0].category = cat;
    s_logs[0].event = evt;
    strlcpy(s_logs[0].detail, detail, sizeof(s_logs[0].detail));

    // Rolling write ke EEPROM AT24C256 jika IC terdeteksi (Zero Flash Wear)
    if (Bsp::At24c256::isDetected()) {
        saveToEeprom(s_logs[0]);
    }
    xSemaphoreGive(s_logMutex);

    ESP_LOGI(TAG, "[%s] %s: %s", categoryToString(cat), eventToString(evt), detail);
}

size_t NetLogService::getLogs(NetLogEntry* outEntries, size_t maxCount) {
    if (!outEntries || maxCount == 0) return 0;
    size_t count = 0;

    if (s_logMutex && xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = (s_logCount < maxCount) ? s_logCount : maxCount;
        std::memcpy(outEntries, s_logs, count * sizeof(NetLogEntry));
        xSemaphoreGive(s_logMutex);
    }
    return count;
}

size_t NetLogService::getLogCount() {
    size_t count = 0;
    if (s_logMutex && xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        count = s_logCount;
        xSemaphoreGive(s_logMutex);
    }
    return count;
}

void NetLogService::clearLogs() {
    if (s_logMutex && xSemaphoreTake(s_logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_logCount = 0;
        s_nextId = 1;
        s_headSlot = -1;
        xSemaphoreGive(s_logMutex);
    } else {
        s_logCount = 0;
        s_nextId = 1;
        s_headSlot = -1;
    }

    if (Bsp::At24c256::isDetected()) {
        // Bersihkan magic marker pada slot EEPROM dengan cooperative yielding agar tidak starvation (VULN-MEM-05)
        for (size_t i = 0; i < Pins::AT24C256_TOTAL_SLOTS; i++) {
            uint32_t magic = 0, seq = 0;
            if (Bsp::At24c256::readSlotHeader(i, &magic, &seq) == ESP_OK && magic == Bsp::AT24C256_MAGIC) {
                Bsp::EepromSlotData emptyData = {};
                Bsp::At24c256::writeSlot(i, emptyData);
            }
            vTaskDelay(pdMS_TO_TICKS(1)); // Cooperative yield 1 tick tiap iterasi slot
        }
        ESP_LOGI(TAG, "Riwayat log EEPROM AT24C256 berhasil dibersihkan.");
    } else {
        ESP_LOGI(TAG, "Riwayat log di RAM berhasil dibersihkan (Flash safe).");
    }
}

bool NetLogService::loadFromEeprom() {
    if (!Bsp::At24c256::isDetected()) return false;

    struct SlotInfo {
        size_t slotIdx;
        uint32_t seqId;
    };

    // Alokasikan di static BSS untuk mencegah konsumsi stack 1.6 KB (VULN-MEM-03)
    static SlotInfo s_scanSlots[Pins::AT24C256_TOTAL_SLOTS];
    size_t validCount = 0;
    uint32_t maxSeq = 0;
    int headIdx = -1;

    for (size_t i = 0; i < Pins::AT24C256_TOTAL_SLOTS; i++) {
        uint32_t magic = 0;
        uint32_t seqId = 0;
        if (Bsp::At24c256::readSlotHeader(i, &magic, &seqId) == ESP_OK) {
            if (magic == Bsp::AT24C256_MAGIC && seqId > 0) {
                s_scanSlots[validCount++] = { i, seqId };
                if (seqId > maxSeq || headIdx == -1) {
                    maxSeq = seqId;
                    headIdx = static_cast<int>(i);
                }
            }
        }
    }

    if (validCount == 0) {
        s_headSlot = -1;
        s_logCount = 0;
        s_nextId = 1;
        return true;
    }

    // Urutkan slot valid dari sequence ID terbesar (terbaru) ke terkecil
    std::sort(s_scanSlots, s_scanSlots + validCount, [](const SlotInfo& a, const SlotInfo& b) {
        return a.seqId > b.seqId;
    });

    // Muat maksimal 50 log terbaru ke array s_logs di RAM (FIFO)
    size_t loaded = 0;
    for (size_t k = 0; k < validCount && loaded < MAX_NET_LOGS; k++) {
        Bsp::EepromSlotData slotData = {};
        if (Bsp::At24c256::readSlot(s_scanSlots[k].slotIdx, slotData) == ESP_OK) {
            // Dual-checksum verification: kompatibel dengan format 118-byte baru & 114-byte lama (VULN-LOG-01)
            uint16_t cs118 = 0;
            uint16_t cs114 = 0;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&slotData);
            for (size_t b = 0; b < 118; b++) {
                cs118 ^= p[b];
                if (b < 114) {
                    cs114 ^= p[b];
                }
            }

            if (slotData.checksum != cs118 && slotData.checksum != cs114) {
                ESP_LOGW(TAG, "Slot %zu korup (checksum mismatch: read=0x%04X, cs118=0x%04X, cs114=0x%04X), dilewati",
                         s_scanSlots[k].slotIdx, slotData.checksum, cs118, cs114);
                continue;
            }

            s_logs[loaded].id = slotData.seq_id;
            s_logs[loaded].uptime_sec = slotData.uptime_sec;
            strlcpy(s_logs[loaded].time_str, slotData.time_str, sizeof(s_logs[loaded].time_str));
            s_logs[loaded].category = static_cast<NetCategory>(slotData.category);
            s_logs[loaded].event = static_cast<NetEvent>(slotData.event);
            strlcpy(s_logs[loaded].detail, slotData.detail, sizeof(s_logs[loaded].detail));
            loaded++;
        }
    }

    s_logCount = loaded;
    s_headSlot = headIdx;
    s_nextId = maxSeq + 1;
    return true;
}

void NetLogService::saveToEeprom(const NetLogEntry& entry) {
    if (!Bsp::At24c256::isDetected()) return;

    // Rolling circular slot: tulis hanya ke slot berikutnya secara melingkar (Zero Shifting)
    int nextSlot = (s_headSlot + 1) % static_cast<int>(Pins::AT24C256_TOTAL_SLOTS);

    Bsp::EepromSlotData slotData = {};
    slotData.magic = Bsp::AT24C256_MAGIC;
    slotData.seq_id = entry.id;
    slotData.uptime_sec = entry.uptime_sec;
    strlcpy(slotData.time_str, entry.time_str, sizeof(slotData.time_str));
    slotData.category = static_cast<uint8_t>(entry.category);
    slotData.event = static_cast<uint8_t>(entry.event);
    strlcpy(slotData.detail, entry.detail, sizeof(slotData.detail));

    // Hitung checksum tepat pada offset checksum (118 bytes, mencakup seluruh detail 80B) (VULN-LOG-01)
    uint16_t cs = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&slotData);
    constexpr size_t CS_LEN = offsetof(Bsp::EepromSlotData, checksum);
    for (size_t i = 0; i < CS_LEN; i++) {
        cs ^= p[i];
    }
    slotData.checksum = cs;

    esp_err_t err = Bsp::At24c256::writeSlot(nextSlot, slotData);
    if (err == ESP_OK) {
        s_headSlot = nextSlot;
        ESP_LOGD(TAG, "[EEPROM-ROLLING] Tulis log #%u ke Slot %d (Addr 0x%04X)",
                 (unsigned int)entry.id, nextSlot, nextSlot * (int)Pins::AT24C256_SLOT_SIZE);
    } else {
        ESP_LOGE(TAG, "[EEPROM-ROLLING] Gagal menulis slot %d: %s", nextSlot, esp_err_to_name(err));
    }
}

bool NetLogService::isEepromDetected() {
    return Bsp::At24c256::isDetected();
}

const char* NetLogService::getStorageModeString() {
    if (Bsp::At24c256::isDetected()) {
        return "AT24C256 EEPROM (200-Slot Rolling Wear-Leveling, FIFO 50)";
    }
    return "RAM Only (Flash Protection Active)";
}

} // namespace Iotaru::Network
