#pragma once

#include "app_types.hpp"
#include "esp_err.h"
#include <cstdint>
#include <cstddef>

namespace Iotaru::Bsp {

constexpr uint32_t AT24C256_MAGIC = 0xA55A1074;
constexpr uint16_t EEPROM_SYSTEM_STATS_ADDR = 0x6440;
constexpr uint32_t SYSTEM_STATS_MAGIC = 0x53544154; // "STAT"

struct __attribute__((packed)) EepromSlotData {
    uint32_t magic;         // 0xA55A1074
    uint32_t seq_id;        // Monotonic sequence ID
    uint32_t uptime_sec;    // Uptime
    char time_str[24];      // "YYYY-MM-DD HH:MM:SS"
    uint8_t category;       // NetCategory
    uint8_t event;          // NetEvent
    char detail[80];        // Detail teks
    uint16_t checksum;      // Checksum
    uint8_t reserved[8];    // Padding agar genap 128 bytes
};

static_assert(sizeof(EepromSlotData) == Pins::AT24C256_SLOT_SIZE, "EepromSlotData must be exactly 128 bytes");

struct __attribute__((packed)) SystemStatsStore {
    uint32_t magic;              // 0x53544154
    uint32_t boot_count;         // Total kumulatif reboot ESP32
    uint32_t wifi_disconnects;   // Total diskoneksi Wi-Fi
    uint32_t eth_disconnects;    // Total diskoneksi Ethernet
    uint32_t mqtt_disconnects;   // Total diskoneksi MQTT
    uint32_t last_boot_uptime;   // Timestamp uptime boot terakhir
    uint8_t  last_reset_reason;  // esp_reset_reason_t
    uint8_t  reserved[37];       // Padding cadangan
    uint16_t checksum;           // CRC16-LE
};

static_assert(sizeof(SystemStatsStore) == Pins::AT24C256_PAGE_SIZE, "SystemStatsStore must be exactly 64 bytes");


class At24c256 {
public:
    static esp_err_t init();
    static bool isDetected();
    static esp_err_t writeSlot(size_t slotIdx, const EepromSlotData& data);
    static esp_err_t readSlot(size_t slotIdx, EepromSlotData& outData);
    static esp_err_t readSlotHeader(size_t slotIdx, uint32_t* outMagic, uint32_t* outSeqId);
    static esp_err_t writePage(uint16_t memAddr, const uint8_t* data, size_t len);
    static esp_err_t readBuffer(uint16_t memAddr, uint8_t* outBuf, size_t len);

private:
    static bool probe();
};

} // namespace Iotaru::Bsp
