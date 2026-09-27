#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_attr.h"

namespace Iotaru::Utils {

/**
 * @brief Fast XOR Checksum untuk frame biner nRF52840 (Header 0xAA sampai byte N-2)
 * Diberi atribut IRAM_ATTR agar berjalan di internal SRAM (zero wait-state).
 */
inline IRAM_ATTR uint8_t calculateXorChecksum(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    // XOR dimulai dari byte index 1 (setelah header 0xAA) sampai sebelum byte checksum
    for (size_t i = 1; i < length - 1; ++i) {
        crc ^= data[i];
    }
    return crc;
}

/**
 * @brief Verifikasi instan integritas frame biner 25 byte.
 */
inline IRAM_ATTR bool verifyBeaconChecksum(const uint8_t* frame25) {
    return calculateXorChecksum(frame25, 25) == frame25[24];
}

} // namespace Iotaru::Utils
