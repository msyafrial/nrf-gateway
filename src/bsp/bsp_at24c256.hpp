#pragma once

#include "app_types.hpp"
#include "esp_err.h"
#include <cstdint>
#include <cstddef>

namespace Iotaru::Bsp {

constexpr uint32_t AT24C256_MAGIC = 0xA55A1074;

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
