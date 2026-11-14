#include "bsp_at24c256.hpp"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstring>
#include <atomic>

static const char *TAG = "AT24C256";

namespace Iotaru::Bsp {

static std::atomic<bool> s_isDetected{false};
static i2c_master_bus_handle_t s_i2cBusHandle = nullptr;
static i2c_master_dev_handle_t s_eepromDevHandle = nullptr;
static SemaphoreHandle_t s_i2cMutex = nullptr;

void At24c256::recoverBus() {
    // Cek apakah pin SDA tertahan LOW oleh EEPROM (akibat soft reboot saat transaksi belum selesai)
    gpio_set_direction(static_cast<gpio_num_t>(Pins::I2C_SDA), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(Pins::I2C_SDA), GPIO_PULLUP_ONLY);
    gpio_set_direction(static_cast<gpio_num_t>(Pins::I2C_SCL), GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(static_cast<gpio_num_t>(Pins::I2C_SCL), GPIO_PULLUP_ONLY);

    gpio_set_level(static_cast<gpio_num_t>(Pins::I2C_SCL), 1);
    esp_rom_delay_us(10);

    if (gpio_get_level(static_cast<gpio_num_t>(Pins::I2C_SDA)) == 0) {
        ESP_LOGW(TAG, "I2C SDA tertahan LOW pasca-reboot! Memancarkan 9 pulsa pemulihan clock SCL...");
        for (int i = 0; i < 9; i++) {
            gpio_set_level(static_cast<gpio_num_t>(Pins::I2C_SCL), 0);
            esp_rom_delay_us(10);
            gpio_set_level(static_cast<gpio_num_t>(Pins::I2C_SCL), 1);
            esp_rom_delay_us(10);
            if (gpio_get_level(static_cast<gpio_num_t>(Pins::I2C_SDA)) == 1) {
                ESP_LOGI(TAG, "I2C SDA berhasil bebas (HIGH) pada pulsa ke-%d", i + 1);
                break;
            }
        }
        // Kirim kondisi STOP I2C (SDA LOW -> HIGH saat SCL HIGH)
        gpio_set_direction(static_cast<gpio_num_t>(Pins::I2C_SDA), GPIO_MODE_OUTPUT_OD);
        gpio_set_level(static_cast<gpio_num_t>(Pins::I2C_SDA), 0);
        esp_rom_delay_us(10);
        gpio_set_level(static_cast<gpio_num_t>(Pins::I2C_SCL), 1);
        esp_rom_delay_us(10);
        gpio_set_level(static_cast<gpio_num_t>(Pins::I2C_SDA), 1);
        esp_rom_delay_us(10);
    }

    // Kembalikan pin ke konfigurasi default sebelum dialihkan ke driver I2C ESP-IDF
    gpio_reset_pin(static_cast<gpio_num_t>(Pins::I2C_SDA));
    gpio_reset_pin(static_cast<gpio_num_t>(Pins::I2C_SCL));
}

bool At24c256::probe() {
    if (!s_i2cBusHandle) return false;
    esp_err_t ret = i2c_master_probe(s_i2cBusHandle, Pins::AT24C256_I2C_ADDR, 50);
    return (ret == ESP_OK);
}

esp_err_t At24c256::waitForWriteComplete(uint32_t timeoutMs) {
    if (!s_i2cBusHandle) return ESP_ERR_INVALID_STATE;
    // Jeda minimal awal agar IC memulai siklus tulis internal (1-2 ms)
    vTaskDelay(pdMS_TO_TICKS(2));

    uint32_t startMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    while ((static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) - startMs) < timeoutMs) {
        if (i2c_master_probe(s_i2cBusHandle, Pins::AT24C256_I2C_ADDR, 10) == ESP_OK) {
            return ESP_OK; // IC EEPROM merespons ACK! Siklus tulis internal selesai.
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t At24c256::init() {
    if (s_i2cMutex == nullptr) {
        s_i2cMutex = xSemaphoreCreateMutex();
    }

    // 1. Pemulihan hardware bus I2C (bebaskan SDA jika tertahan)
    recoverBus();

    // 2. Inisialisasi I2C master bus
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

    // 3. Multi-attempt probe (3x dengan jeda 15 ms untuk toleransi power settling)
    for (int attempt = 1; attempt <= 3; attempt++) {
        s_isDetected = probe();
        if (s_isDetected) break;
        vTaskDelay(pdMS_TO_TICKS(15));
    }

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

static bool isDetectedLocked() {
    if (s_isDetected.load(std::memory_order_acquire) && s_eepromDevHandle != nullptr) {
        return true;
    }

    // Dynamic runtime re-probe jika saat boot terjadi fluktuasi daya awal (caller sudah memegang s_i2cMutex)
    if (s_i2cBusHandle != nullptr &&
        i2c_master_probe(s_i2cBusHandle, Pins::AT24C256_I2C_ADDR, 50) == ESP_OK) {
        if (!s_eepromDevHandle) {
            i2c_device_config_t dev_config = {};
            dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            dev_config.device_address = Pins::AT24C256_I2C_ADDR;
            dev_config.scl_speed_hz = Pins::I2C_FREQ_HZ;
            if (i2c_master_bus_add_device(s_i2cBusHandle, &dev_config, &s_eepromDevHandle) == ESP_OK) {
                s_isDetected.store(true, std::memory_order_release);
                ESP_LOGI(TAG, "✅ [DYNAMIC-DETECT] IC EEPROM AT24C256 terdeteksi kembali di runtime!");
            }
        } else {
            s_isDetected.store(true, std::memory_order_release);
        }
    }
    return s_isDetected.load(std::memory_order_acquire) && (s_eepromDevHandle != nullptr);
}

bool At24c256::isDetected() {
    if (s_isDetected.load(std::memory_order_acquire) && s_eepromDevHandle != nullptr) {
        return true;
    }
    if (!s_i2cMutex) return false;
    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }
    bool detected = isDetectedLocked();
    xSemaphoreGive(s_i2cMutex);
    return detected;
}

esp_err_t At24c256::writePage(uint16_t memAddr, const uint8_t* data, size_t len) {
    if (!data || len == 0 || len > Pins::AT24C256_PAGE_SIZE || !s_i2cMutex) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t packet[66];
    packet[0] = static_cast<uint8_t>((memAddr >> 8) & 0xFF);
    packet[1] = static_cast<uint8_t>(memAddr & 0xFF);
    memcpy(packet + 2, data, len);

    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!isDetectedLocked()) {
        xSemaphoreGive(s_i2cMutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_transmit(
        s_eepromDevHandle,
        packet,
        len + 2,
        pdMS_TO_TICKS(50)
    );

    // KUNCI MUTEX DITAHAN HINGGA SIKLUS TULIS INTERNAL SELESAI (Acknowledge Polling)
    if (ret == ESP_OK) {
        waitForWriteComplete(15);
    } else if (ret == ESP_ERR_TIMEOUT && s_i2cBusHandle) {
        i2c_master_bus_reset(s_i2cBusHandle);
    }

    xSemaphoreGive(s_i2cMutex);
    return ret;
}

esp_err_t At24c256::readBuffer(uint16_t memAddr, uint8_t* outBuf, size_t len) {
    if (!outBuf || len == 0 || !s_i2cMutex) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t addrBuf[2] = {
        static_cast<uint8_t>((memAddr >> 8) & 0xFF),
        static_cast<uint8_t>(memAddr & 0xFF)
    };

    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!isDetectedLocked()) {
        xSemaphoreGive(s_i2cMutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_transmit_receive(
        s_eepromDevHandle,
        addrBuf,
        sizeof(addrBuf),
        outBuf,
        len,
        pdMS_TO_TICKS(100)
    );

    // Toleransi jitter bus: coba 1x retry jika NACK/Timeout sesaat
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT && s_i2cBusHandle) {
            i2c_master_bus_reset(s_i2cBusHandle);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        ret = i2c_master_transmit_receive(
            s_eepromDevHandle,
            addrBuf,
            sizeof(addrBuf),
            outBuf,
            len,
            pdMS_TO_TICKS(100)
        );
    }

    xSemaphoreGive(s_i2cMutex);
    return ret;
}

esp_err_t At24c256::writeSlot(size_t slotIdx, const EepromSlotData& data) {
    if (slotIdx >= Pins::AT24C256_TOTAL_SLOTS || !s_i2cMutex) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t slotAddr = static_cast<uint16_t>(slotIdx * Pins::AT24C256_SLOT_SIZE);
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(&data);

    // KUNCI MUTEX SEKALIGUS UNTUK KEDUA HALAMAN (PAGE 1 & PAGE 2) SECARA ATOMIK
    // Mencegah interupsi antar task yang memicu kegagalan penulisan checksum di Halaman 2
    if (xSemaphoreTake(s_i2cMutex, pdMS_TO_TICKS(400)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!isDetectedLocked()) {
        xSemaphoreGive(s_i2cMutex);
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Tulis Halaman 1 (64 Byte pertama)
    uint8_t packet[66];
    packet[0] = static_cast<uint8_t>((slotAddr >> 8) & 0xFF);
    packet[1] = static_cast<uint8_t>(slotAddr & 0xFF);
    memcpy(packet + 2, rawBytes, Pins::AT24C256_PAGE_SIZE);

    esp_err_t ret = i2c_master_transmit(s_eepromDevHandle, packet, Pins::AT24C256_PAGE_SIZE + 2, pdMS_TO_TICKS(50));
    if (ret == ESP_OK) {
        waitForWriteComplete(15);
    } else {
        xSemaphoreGive(s_i2cMutex);
        ESP_LOGE(TAG, "Gagal menulis EEPROM slot %u halaman 1 (addr=0x%04X): %s",
                 (unsigned int)slotIdx, slotAddr, esp_err_to_name(ret));
        return ret;
    }

    // 2. Tulis Halaman 2 (64 Byte kedua: sisa teks, checksum offset 118, padding)
    uint16_t slotAddr2 = slotAddr + Pins::AT24C256_PAGE_SIZE;
    packet[0] = static_cast<uint8_t>((slotAddr2 >> 8) & 0xFF);
    packet[1] = static_cast<uint8_t>(slotAddr2 & 0xFF);
    memcpy(packet + 2, rawBytes + Pins::AT24C256_PAGE_SIZE, Pins::AT24C256_PAGE_SIZE);

    ret = i2c_master_transmit(s_eepromDevHandle, packet, Pins::AT24C256_PAGE_SIZE + 2, pdMS_TO_TICKS(50));
    if (ret == ESP_OK) {
        waitForWriteComplete(15);
    } else {
        ESP_LOGE(TAG, "Gagal menulis EEPROM slot %u halaman 2 (addr=0x%04X): %s",
                 (unsigned int)slotIdx, slotAddr2, esp_err_to_name(ret));
    }

    xSemaphoreGive(s_i2cMutex);
    return ret;
}

esp_err_t At24c256::readSlot(size_t slotIdx, EepromSlotData& outData) {
    if (!isDetected() || slotIdx >= Pins::AT24C256_TOTAL_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t slotAddr = static_cast<uint16_t>(slotIdx * Pins::AT24C256_SLOT_SIZE);
    return readBuffer(slotAddr, reinterpret_cast<uint8_t*>(&outData), sizeof(EepromSlotData));
}

esp_err_t At24c256::readSlotHeader(size_t slotIdx, uint32_t* outMagic, uint32_t* outSeqId) {
    if (!isDetected() || slotIdx >= Pins::AT24C256_TOTAL_SLOTS || !outMagic || !outSeqId) {
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

esp_err_t At24c256::wipeAll(uint8_t fillByte) {
    if (!isDetected() || !s_eepromDevHandle) {
        ESP_LOGE(TAG, "Gagal wipe EEPROM: Hardware AT24C256 tidak terdeteksi");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t fillBuffer[Pins::AT24C256_PAGE_SIZE];
    memset(fillBuffer, fillByte, sizeof(fillBuffer));

    constexpr size_t TOTAL_PAGES = (32 * 1024) / Pins::AT24C256_PAGE_SIZE; // 512 halaman @ 64 byte
    for (size_t page = 0; page < TOTAL_PAGES; page++) {
        uint16_t memAddr = static_cast<uint16_t>(page * Pins::AT24C256_PAGE_SIZE);
        esp_err_t err = writePage(memAddr, fillBuffer, sizeof(fillBuffer));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Gagal wipe EEPROM pada halaman %zu (addr 0x%04X): %s",
                     page, memAddr, esp_err_to_name(err));
            return err;
        }

        // Cooperative yield tiap 4 halaman agar FreeRTOS scheduler dan watchdog lancar
        if ((page % 4) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    ESP_LOGI(TAG, "Wipe seluruh memori EEPROM AT24C256 (32 KB, 512 halaman) berhasil diselesaikan.");
    return ESP_OK;
}

struct EepromAsyncJob {
    bool isSlot;
    size_t slotIdx;
    uint16_t memAddr;
    size_t len;
    union {
        EepromSlotData slot;
        uint8_t page[Pins::AT24C256_PAGE_SIZE];
    } payload;
};

static QueueHandle_t s_eepromWriteQueue = nullptr;
static TaskHandle_t s_eepromWriterTask = nullptr;
static std::atomic<bool> s_eepromWriting{false};

static void eepromWriterTask(void*) {
    EepromAsyncJob job;
    for (;;) {
        if (xQueueReceive(s_eepromWriteQueue, &job, portMAX_DELAY) == pdTRUE) {
            s_eepromWriting.store(true, std::memory_order_release);
            if (job.isSlot) {
                At24c256::writeSlot(job.slotIdx, job.payload.slot);
            } else {
                At24c256::writePage(job.memAddr, job.payload.page, job.len);
            }
            s_eepromWriting.store(false, std::memory_order_release);
        }
    }
}

void At24c256::startAsyncWriter() {
    if (s_eepromWriteQueue == nullptr) {
        s_eepromWriteQueue = xQueueCreate(8, sizeof(EepromAsyncJob));
    }
    if (s_eepromWriteQueue != nullptr && s_eepromWriterTask == nullptr) {
        xTaskCreatePinnedToCore(eepromWriterTask, "eeprom_writer", 2560, nullptr, 2, &s_eepromWriterTask, 1);
    }
}

bool At24c256::enqueueSlotWrite(size_t slotIdx, const EepromSlotData& data) {
    if (!isDetected()) return false;
    if (s_eepromWriteQueue == nullptr) {
        return writeSlot(slotIdx, data) == ESP_OK;
    }
    EepromAsyncJob job = {};
    job.isSlot = true;
    job.slotIdx = slotIdx;
    job.payload.slot = data;
    if (xQueueSend(s_eepromWriteQueue, &job, pdMS_TO_TICKS(10)) == pdTRUE) {
        return true;
    }
    return false;
}

bool At24c256::enqueuePageWrite(uint16_t memAddr, const uint8_t* data, size_t len) {
    if (!isDetected() || !data || len == 0 || len > Pins::AT24C256_PAGE_SIZE) return false;
    if (s_eepromWriteQueue == nullptr) {
        return writePage(memAddr, data, len) == ESP_OK;
    }
    EepromAsyncJob job = {};
    job.isSlot = false;
    job.memAddr = memAddr;
    job.len = len;
    memcpy(job.payload.page, data, len);
    if (xQueueSend(s_eepromWriteQueue, &job, pdMS_TO_TICKS(10)) == pdTRUE) {
        return true;
    }
    return false;
}

void At24c256::flushAsyncWrites(uint32_t timeoutMs) {
    if (s_eepromWriteQueue == nullptr) return;
    uint32_t waited = 0;
    while (waited < timeoutMs) {
        if (uxQueueMessagesWaiting(s_eepromWriteQueue) == 0 &&
            !s_eepromWriting.load(std::memory_order_acquire)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
}

} // namespace Iotaru::Bsp
