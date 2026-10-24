#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_err.h"

namespace Iotaru::Network {

enum class NetCategory : uint8_t {
    WIFI,
    ETHERNET,
    MQTT,
    SOFTAP,
    SYSTEM
};

enum class NetEvent : uint8_t {
    CONNECTED,
    DISCONNECTED,
    GOT_IP,
    ERROR_EVT,
    STARTED,
    STOPPED,
    CLIENT_JOIN,
    CLIENT_LEAVE,
    BOOT,
    REBOOT,
    CONFIG_UPDATE,
    CONFIG_RESET,
    CONFIG_DISCARD,
    AUTH_UPDATE,
    LOG_CLEARED
};

constexpr size_t MAX_NET_LOGS = 50;

struct NetLogEntry {
    uint32_t id;
    uint32_t uptime_sec;
    char time_str[24];      // Kalender nyata "YYYY-MM-DD HH:MM:SS" atau uptime "+HH:MM:SS"
    NetCategory category;
    NetEvent event;
    char detail[80];        // Keterangan kontekstual (IP, SSID, Broker, Reason)
};

class NetLogService {
public:
    static void init();
    static void log(NetCategory cat, NetEvent evt, const char* detail);
    
    // Getter data log untuk REST API & Web UI
    static size_t getLogs(NetLogEntry* outEntries, size_t maxCount);
    static size_t getLogCount();
    static void clearLogs();
    
    // Konversi enum ke string representatif
    static const char* categoryToString(NetCategory cat);
    static const char* eventToString(NetEvent evt);
    static const char* eventToStatusClass(NetEvent evt);

    // Telemetri Stabilitas & Jaringan (Tersimpan di AT24C256 EEPROM / RAM)
    static uint32_t getBootCount();
    static const char* getLastResetReasonString();
    static uint32_t getWifiDisconnectCount();
    static uint32_t getEthDisconnectCount();
    static uint32_t getMqttDisconnectCount();
    static void recordDisconnect(NetCategory cat);
    static void resetDisconnectCounters();

    // Status media penyimpanan
    static const char* getStorageModeString();
    static bool isEepromDetected();

    // Lokasi penyimpanan file lama di LittleFS (Kini diabaikan demi proteksi Flash)
    static constexpr const char* LOG_FILE_PATH = "/storage/net_events.log";

private:
    static bool loadFromEeprom();
    static void saveToEeprom(const NetLogEntry& entry);
    static void formatTimestamp(char* outBuf, size_t maxLen, uint32_t uptimeSec);
};

} // namespace Iotaru::Network
