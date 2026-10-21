#pragma once

#include "app_types.hpp"
#include <cstdint>
#include <cstddef>

namespace Iotaru::Core {

constexpr size_t MAX_DROP_LOGS = 50;

struct DropLogEntry {
    uint32_t id;            // Auto-increment ID log
    uint32_t uptime_sec;    // Uptime ESP32 saat drop terjadi
    char time_str[24];      // Format waktu aktual: "YYYY-MM-DD HH:MM:SS" atau "+HH:MM:SS"
    char code[16];          // "iBeacon", "ibeacon_ext", "MQTT"
    char tag[24];           // Major-Minor "10001-00042" atau "10 pkt"
    int8_t rssi;            // Nilai sinyal dBm
    DropReason reason;      // Enum alasan drop
};

struct DropSummaryStats {
    uint32_t total_dropped;
    uint32_t ttl_expired;
    uint32_t rssi_min;
    uint32_t uuid_mismatch;
    uint32_t major_mismatch;
    uint32_t queue_overflow;
    uint32_t mqtt_failed;
};

class DropLogService {
public:
    static void init();
    static void record(const char* code, const char* tag, int8_t rssi, DropReason reason, uint32_t count = 1);
    static void record(const BeaconPacket& packet, DropReason reason, uint32_t count = 1);

    static size_t getLogs(DropLogEntry* outEntries, size_t maxCount);
    static size_t getLogCount();
    static void clearLogs();
    static DropSummaryStats getSummaryStats();

    static const char* reasonToString(DropReason reason);
    static const char* reasonToStatusClass(DropReason reason);
    static void formatTimestamp(char* outBuf, size_t maxLen, uint32_t uptimeSec);
};

} // namespace Iotaru::Core
