#pragma once

#include <cstdint>
#include <cstddef>

namespace Iotaru::Utils {

/**
 * @brief Pengelola pembentukan Hardware Device ID unik untuk IOTARU Gateway.
 * Menggabungkan eFuse MAC silikon + SPI Flash Hardware UID (Command 0x4B) + CRC32.
 * Menjamin 100% keunikan ID bahkan jika chip ESP32 clone memiliki MAC yang sama.
 */
class DeviceIdManager {
public:
    /**
     * @brief Membaca 64-bit Hardware Unique ID dari chip SPI Flash via API resmi ESP-IDF (Command 0x4B)
     * @return 64-bit UID fisik flash (0 jika tidak didukung/gagal)
     */
    static uint64_t readFlashUID();

    /**
     * @brief Membaca 6-byte base MAC address dari eFuse silikon ESP32
     */
    static void readMacAddress(uint8_t outMac[6]);

    /**
     * @brief Menghitung nilai hash 32-bit (CRC32) dari kombinasi MAC Address (6 byte) + Flash UID (8 byte)
     */
    static uint32_t calculateUniqueCrc();

    /**
     * @brief Menghasilkan Device ID unik sepanjang 8 karakter HEX (Zero Dynamic Allocation)
     * Format: "%08x" (contoh: "1a2b3c4d")
     * 
     * @param outBuf Buffer tujuan (wajib minimal 9 byte)
     * @param maxLen Ukuran buffer
     */
    static void generate(char* outBuf, size_t maxLen);
};

} // namespace Iotaru::Utils
