#include "bsp_at24c256.hpp"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>

static const char *TAG = "AT24C256";

namespace Iotaru::Bsp {

static bool s_isDetected = false;
static i2c_master_bus_handle_t s_i2cBusHandle = nullptr;
static i2c_master_dev_handle_t s_eepromDevHandle = nullptr;
static SemaphoreHandle_t s_i2cMutex = nullptr;

bool At24c256::probe() {
    if (!s_i2cBusHandle) return false;
    esp_err_t ret = i2c_master_probe(s_i2cBusHandle, Pins::AT24C256_I2C_ADDR, 50);
    return (ret == ESP_OK);
}

esp_err_t At24c256::init() {
    if (s_i2cMutex == nullptr) {
        s_i2cMutex = xSemaphoreCreateMutex();
    }

    if (!s_i2cBusHandle) {
        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = static_cast<gpio_num_t>(Pins::I2C_SDA);
        bus_config.scl_io_num = static_cast<gpio_num_t>(Pins::I2C_SCL);
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

        esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2cBusHandle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Gagal menginisialisasi I2C master bus: %s", esp_err_to_name(err));
            return err;
        }
    }

    // Auto-detect IC AT24C256 di alamat 0x50
    s_isDetected = probe();
    if (s_isDetected) {
        if (!s_eepromDevHandle) {
            i2c_device_config_t dev_config = {};
            dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dev_config.device_address = Pins::AT24C256_I2C_ADDR;
            dev_config.scl_speed_hz = Pins::I2C_FREQ_HZ;

            esp_err_t err = i2c_master_bus_add_device(s_i2cBusHandle, &dev_config, &s_eepromDevHandle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Gagal mendaftarkan device AT24C256 ke bus: %s", esp_err_to_name(err));
                s_isDetected = false;
                return err;
            }
        }

        ESP_LOGI(TAG, "==========================================================");
        ESP_LOGI(TAG, "✅ [AUTO-DETECT] IC EEPROM AT24C256 Terdeteksi (Addr: 0x%02X, SDA:%d, SCL:%d)",
                 Pins::AT24C256_I2C_ADDR, Pins::I2C_SDA, Pins::I2C_SCL);
        ESP_LOGI(TAG, "Kapasitas : 32 KB (256 Kbit) | Rolling Slots: %u (128B/slot)",
                 (unsigned int)Pins::AT24C256_TOTAL_SLOTS);
        ESP_LOGI(TAG, "Proteksi  : Aktif (LittleFS Flash TIDAK AKAN ditulisi log)");
        ESP_LOGI(TAG, "==========================================================");
    } else {
        ESP_LOGW(TAG, "==========================================================");
        ESP_LOGW(TAG, "⚠️ [AUTO-DETECT] IC EEPROM AT24C256 TIDAK TERPASANG (Addr: 0x%02X)", Pins::AT24C256_I2C_ADDR);
        ESP_LOGW(TAG, "Fallback  : Mode RAM-Only (FIFO 50 entri)");
        ESP_LOGW(TAG, "Proteksi  : Aktif (LittleFS Flash TIDAK AKAN ditulisi demi mencegah keausan)");
        ESP_LOGW(TAG, "==========================================================");
    }

    return ESP_OK;
}

bool At24c256::isDetected() {
    return s_isDetected;
}

esp_err_t At24c256::writePage(uint16_t memAddr, const uint8_t* data, size_t len) {
    if (!s_isDetected || !s_eepromDevHandle || !data || len == 0 || len > Pins::AT24C256_PAGE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t packet[66];
    packet[0] = static_cast<uint8_t>((memAddr >> 8) & 0xFF);
    packet[1] = static_cast<uint8_t>(memAddr & 0xFF);
    memcpy(packet + 2, data, len);

    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_transmit(
        s_eepromDevHandle,
        packet,
        len + 2,
        pdMS_TO_TICKS(50)
    );
    xSemaphoreGive(s_i2cMutex);

    if (ret == ESP_OK) {
        // Beri jeda write cycle time EEPROM (~5-10 ms)
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ret;
}

esp_err_t At24c256::readBuffer(uint16_t memAddr, uint8_t* outBuf, size_t len) {
    if (!s_isDetected || !s_eepromDevHandle || !outBuf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t addrBuf[2] = {
        static_cast<uint8_t>((memAddr >> 8) & 0xFF),
        static_cast<uint8_t>(memAddr & 0xFF)
    };

    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_transmit_receive(
        s_eepromDevHandle,
        addrBuf,
        sizeof(addrBuf),
        outBuf,
        len,
        pdMS_TO_TICKS(100)
    );
    xSemaphoreGive(s_i2cMutex);

    return ret;
}

esp_err_t At24c256::writeSlot(size_t slotIdx, const EepromSlotData& data) {
    if (!s_isDetected || slotIdx >= Pins::AT24C256_TOTAL_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t slotAddr = static_cast<uint16_t>(slotIdx * Pins::AT24C256_SLOT_SIZE);
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(&data);

    // Tulis halaman 1 (64 Byte pertama)
    esp_err_t ret = writePage(slotAddr, rawBytes, Pins::AT24C256_PAGE_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal menulis EEPROM slot %u halaman 1 (addr=0x%04X): %s",
                 (unsigned int)slotIdx, slotAddr, esp_err_to_name(ret));
        return ret;
    }

    // Tulis halaman 2 (64 Byte kedua)
    ret = writePage(slotAddr + Pins::AT24C256_PAGE_SIZE, rawBytes + Pins::AT24C256_PAGE_SIZE, Pins::AT24C256_PAGE_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal menulis EEPROM slot %u halaman 2 (addr=0x%04X): %s",
                 (unsigned int)slotIdx, slotAddr + Pins::AT24C256_PAGE_SIZE, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t At24c256::readSlot(size_t slotIdx, EepromSlotData& outData) {
    if (!s_isDetected || slotIdx >= Pins::AT24C256_TOTAL_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t slotAddr = static_cast<uint16_t>(slotIdx * Pins::AT24C256_SLOT_SIZE);
    return readBuffer(slotAddr, reinterpret_cast<uint8_t*>(&outData), sizeof(EepromSlotData));
}

esp_err_t At24c256::readSlotHeader(size_t slotIdx, uint32_t* outMagic, uint32_t* outSeqId) {
    if (!s_isDetected || slotIdx >= Pins::AT24C256_TOTAL_SLOTS || !outMagic || !outSeqId) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t slotAddr = static_cast<uint16_t>(slotIdx * Pins::AT24C256_SLOT_SIZE);
    uint8_t buf[8];
    esp_err_t ret = readBuffer(slotAddr, buf, sizeof(buf));
    if (ret == ESP_OK) {
        memcpy(outMagic, buf, 4);
        memcpy(outSeqId, buf + 4, 4);
    }
    return ret;
}

} // namespace Iotaru::Bsp
