#include "device_id.hpp"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "DEVICE_ID";

namespace Iotaru::Utils {

// Standard IEEE 802.3 CRC32 polynomial (0xEDB88320)
static uint32_t computeCrc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

uint64_t DeviceIdManager::readFlashUID() {
    uint64_t flashUid = 0;
    // Command 0x4B via driver SPI Flash resmi ESP-IDF (thread-safe, cache-safe, SPI0/SPI1)
    esp_err_t err = esp_flash_read_unique_chip_id(esp_flash_default_chip, &flashUid);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Gagal membaca Flash Hardware UID (0x%x) atau chip tidak mendukung command 0x4B", err);
        return 0;
    }
    return flashUid;
}

void DeviceIdManager::readMacAddress(uint8_t outMac[6]) {
    if (!outMac) return;
    esp_read_mac(outMac, ESP_MAC_WIFI_STA);
}

uint32_t DeviceIdManager::calculateUniqueCrc() {
    uint8_t mac[6] = {0};
    readMacAddress(mac);

    uint64_t flashUid = readFlashUID();

    // Gabungkan 6 byte MAC + 8 byte Flash UID = 14 byte buffer biner (Zero Dynamic Allocation)
    uint8_t rawBuffer[14];
    memcpy(rawBuffer, mac, 6);
    memcpy(rawBuffer + 6, &flashUid, 8);

    uint32_t crc = computeCrc32(rawBuffer, sizeof(rawBuffer));

    ESP_LOGI(TAG, "Hardware Ident: MAC=%02X:%02X:%02X:%02X:%02X:%02X | FlashUID=0x%016llX | CRC32=0x%08lX",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             (unsigned long long)flashUid, (unsigned long)crc);

    return crc;
}

void DeviceIdManager::generate(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen < 9) return;

    uint32_t crc = calculateUniqueCrc();
    snprintf(outBuf, maxLen, "%08lx", (unsigned long)crc);

    // Pastikan seluruh karakter device_id selalu lowercase
    for (size_t i = 0; outBuf[i] != '\0'; i++) {
        if (outBuf[i] >= 'A' && outBuf[i] <= 'Z') {
            outBuf[i] = static_cast<char>(outBuf[i] + ('a' - 'A'));
        }
    }
}

} // namespace Iotaru::Utils
