#include "bsp_led.hpp"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>

namespace Iotaru::Bsp {

// Palet Warna Standar (RGB format uint32_t: 0x00RRGGBB)
static constexpr uint32_t COLOR_BLACK  = 0x000000;
static constexpr uint32_t COLOR_GREEN  = 0x00FF00;
static constexpr uint32_t COLOR_YELLOW = 0xFFC800; // Kuning hangat
static constexpr uint32_t COLOR_ORANGE = 0xFF5000; // Oranye tajam
static constexpr uint32_t COLOR_BLUE   = 0x0000FF; // Biru murni
static constexpr uint32_t COLOR_CYAN   = 0x00C8FF; // Cyan / Biru muda
static constexpr uint32_t COLOR_RED    = 0xFF0000; // Merah murni

static int s_ledPin = Pins::LED_WS2812;
static uint8_t s_brightness = 40; // Default: ~15% elegan
static TaskHandle_t s_ledTaskHandle = nullptr;

static std::atomic<SysLedState> s_sysState{SysLedState::BOOTING};
static std::atomic<ComLedState> s_comState{ComLedState::CONNECTING};
static std::atomic<bool> s_otaMode{false};
static std::atomic<bool> s_configMode{false};
static std::atomic<bool> s_softApActive{false};
static std::atomic<bool> s_linkUp{false};
static std::atomic<bool> s_hasIp{false};
static std::atomic<bool> s_mqttConnected{false};
static std::atomic<bool> s_bootDone{false};
static std::atomic<bool> s_stopRequested{false};
static std::atomic<bool> s_taskExited{true};

// Cache dirty-checking untuk mencegah spam transmisi bit-bang & tabrakan cache SPI flash
static uint32_t s_lastColorSys = 0xFFFFFFFF;
static uint32_t s_lastColorCom = 0xFFFFFFFF;
static uint8_t  s_lastBrightness = 0xFF;

static std::atomic<int64_t> s_trafficBlipUntilUs{0};
static std::atomic<int64_t> s_identifyUntilUs{0};

static inline uint32_t scaleColor(uint32_t color, uint8_t brightness) {
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    r = (uint8_t)(((uint16_t)r * brightness) / 255);
    g = (uint8_t)(((uint16_t)g * brightness) / 255);
    b = (uint8_t)(((uint16_t)b * brightness) / 255);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static rmt_channel_handle_t s_rmtChan = nullptr;
static rmt_encoder_handle_t s_bytesEncoder = nullptr;

// Hardware RMT TX untuk WS2812B (0% interrupt masking, 0% CPU busy-wait, imun cache miss)
void LedManager::writeRawPixels(uint32_t color0, uint32_t color1, uint8_t brightness) {
    if (!s_rmtChan || !s_bytesEncoder) return;

    uint32_t c0 = scaleColor(color0, brightness);
    uint32_t c1 = scaleColor(color1, brightness);

    // WS2812B menggunakan urutan byte GRB (Green, Red, Blue)
    uint8_t grb[6];
    grb[0] = (uint8_t)((c0 >> 8) & 0xFF);  // G
    grb[1] = (uint8_t)((c0 >> 16) & 0xFF); // R
    grb[2] = (uint8_t)(c0 & 0xFF);         // B
    grb[3] = (uint8_t)((c1 >> 8) & 0xFF);  // G
    grb[4] = (uint8_t)((c1 >> 16) & 0xFF); // R
    grb[5] = (uint8_t)(c1 & 0xFF);         // B

    rmt_transmit_config_t txCfg = {};
    txCfg.loop_count = 0;
    if (rmt_transmit(s_rmtChan, s_bytesEncoder, grb, sizeof(grb), &txCfg) == ESP_OK) {
        rmt_tx_wait_all_done(s_rmtChan, 50);
    }
}

void LedManager::init(int pin, uint8_t brightness) {
    s_ledPin = pin;
    s_brightness = brightness;
    s_sysState.store(SysLedState::BOOTING, std::memory_order_relaxed);
    s_comState.store(ComLedState::CONNECTING, std::memory_order_relaxed);
    s_lastColorSys = 0xFFFFFFFF;
    s_lastColorCom = 0xFFFFFFFF;
    s_lastBrightness = 0xFF;

    if (!s_rmtChan) {
        rmt_tx_channel_config_t txChanCfg = {};
        txChanCfg.gpio_num = static_cast<gpio_num_t>(s_ledPin);
        txChanCfg.clk_src = RMT_CLK_SRC_DEFAULT;
        txChanCfg.resolution_hz = 10000000; // 10 MHz (1 tick = 100 ns)
        txChanCfg.mem_block_symbols = 64;
        txChanCfg.trans_queue_depth = 2;
        if (rmt_new_tx_channel(&txChanCfg, &s_rmtChan) == ESP_OK) {
            rmt_bytes_encoder_config_t bytesCfg = {};
            bytesCfg.bit0.duration0 = 3; // 300 ns HIGH
            bytesCfg.bit0.level0 = 1;
            bytesCfg.bit0.duration1 = 9; // 900 ns LOW
            bytesCfg.bit0.level1 = 0;
            bytesCfg.bit1.duration0 = 9; // 900 ns HIGH
            bytesCfg.bit1.level0 = 1;
            bytesCfg.bit1.duration1 = 3; // 300 ns LOW
            bytesCfg.bit1.level1 = 0;
            bytesCfg.flags.msb_first = 1;
            if (rmt_new_bytes_encoder(&bytesCfg, &s_bytesEncoder) == ESP_OK) {
                rmt_enable(s_rmtChan);
            }
        }
    }

    // Matikan kedua LED seketika
    writeRawPixels(0, 0, s_brightness);
}

void LedManager::startTask() {
    // Buat Task LED di Core 0 (Priority 3) terisolasi dari beban Core 1 (Wi-Fi, HTTPD & MQTTS)
    // agar kedipan indikator LED berjalan mulus tanpa risiko freeze saat web diakses
    if (s_ledTaskHandle == nullptr) {
        s_stopRequested.store(false, std::memory_order_release);
        s_taskExited.store(false, std::memory_order_release);
        xTaskCreatePinnedToCore(
            [](void*) {
                while (!s_stopRequested.load(std::memory_order_acquire)) {
                    LedManager::tick();
                    vTaskDelay(pdMS_TO_TICKS(20)); // 50 FPS
                }
                s_ledTaskHandle = nullptr;
                s_taskExited.store(true, std::memory_order_release);
                vTaskDelete(nullptr);
            },
            "WS2812_Task",
            3072, // 3 KB stack (Aman untuk timing bitbang & esp_rom_delay)
            nullptr,
            3,
            &s_ledTaskHandle,
            0 // Core 0 (Terisolasi dari beban Core 1)
        );
    }
}

void LedManager::stopTask() {
    if (s_ledTaskHandle != nullptr) {
        s_stopRequested.store(true, std::memory_order_release);
        // Tunggu secara kooperatif agar WS2812_Task selesai menulis frame terakhir sebelum dihapus
        for (int i = 0; i < 15 && !s_taskExited.load(std::memory_order_acquire); ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!s_taskExited.load(std::memory_order_acquire) && s_ledTaskHandle != nullptr) {
            TaskHandle_t th = s_ledTaskHandle;
            s_ledTaskHandle = nullptr;
            s_taskExited.store(true, std::memory_order_release);
            vTaskDelete(th);
        }
    }
}

void LedManager::setSysState(SysLedState state) {
    s_sysState.store(state, std::memory_order_release);
}

void LedManager::setComState(ComLedState state) {
    if (s_otaMode.load(std::memory_order_acquire)) return;
    s_comState.store(state, std::memory_order_release);
}

void LedManager::finishBoot() {
    s_bootDone.store(true, std::memory_order_release);
    if (s_sysState.load(std::memory_order_acquire) == SysLedState::BOOTING) {
        s_sysState.store(s_configMode.load(std::memory_order_acquire) ? SysLedState::CONFIG : SysLedState::NORMAL,
                         std::memory_order_release);
    }
}

void LedManager::setOtaMode(bool active) {
    s_otaMode.store(active, std::memory_order_release);
    if (active) {
        s_sysState.store(SysLedState::OTA, std::memory_order_release);
    } else {
        SysLedState next = s_configMode.load(std::memory_order_acquire)
            ? SysLedState::CONFIG
            : (s_bootDone.load(std::memory_order_acquire) ? SysLedState::NORMAL : SysLedState::BOOTING);
        s_sysState.store(next, std::memory_order_release);
    }
}

void LedManager::setConfigMode(bool active) {
    s_configMode.store(active, std::memory_order_release);
    s_softApActive.store(active, std::memory_order_release);
    if (active) {
        s_sysState.store(SysLedState::CONFIG, std::memory_order_release);
    } else {
        s_sysState.store(s_bootDone.load(std::memory_order_acquire) ? SysLedState::NORMAL : SysLedState::BOOTING,
                         std::memory_order_release);
    }
}

bool LedManager::isConfigMode() {
    return s_configMode.load(std::memory_order_acquire);
}

void LedManager::clear() {
    s_lastColorSys = 0;
    s_lastColorCom = 0;
    writeRawPixels(0, 0, s_brightness);
}

void LedManager::setSolidColor(uint32_t colorSys, uint32_t colorCom) {
    s_lastColorSys = colorSys;
    s_lastColorCom = colorCom;
    s_lastBrightness = s_brightness;
    writeRawPixels(colorSys, colorCom, s_brightness);
}

void LedManager::triggerTrafficBlip() {
    int64_t now = esp_timer_get_time();
    // Non-reentrant guard: jika sedang dalam fase blip, jangan tumpuk agar LED selalu sempat hijau di antara blip
    if (s_trafficBlipUntilUs.load(std::memory_order_relaxed) < now) {
        s_trafficBlipUntilUs.store(now + 90000, std::memory_order_relaxed); // 90 ms (~4-5 frame off @ 50 FPS)
    }
}

void LedManager::triggerIdentify(uint32_t durationMs) {
    s_identifyUntilUs.store(esp_timer_get_time() + ((int64_t)durationMs * 1000), std::memory_order_relaxed);
}

void LedManager::autoSync(bool linkUp, bool ipAcquired, bool mqttConnected, bool isSoftAp) {
    if (s_otaMode.load(std::memory_order_acquire)) return;

    s_linkUp.store(linkUp, std::memory_order_relaxed);
    s_hasIp.store(ipAcquired, std::memory_order_relaxed);
    s_mqttConnected.store(mqttConnected, std::memory_order_relaxed);
    s_softApActive.store(isSoftAp, std::memory_order_release);

    // Hirarki Prioritas Murni LED NET/COM:
    if (!linkUp) {
        // LEVEL 1: Link Fisik Terputus -> Merah Double Blink
        s_comState.store(ComLedState::DISCONNECTED, std::memory_order_release);
    } else if (!ipAcquired) {
        // LEVEL 2: Link Up, Menunggu IP DHCP -> Kuning Kedip 2 Hz
        s_comState.store(ComLedState::CONNECTING, std::memory_order_release);
    } else if (!mqttConnected) {
        // LEVEL 3: Dapat IP, Broker MQTT Belum Terhubung -> Cyan Kedip 1 Hz
        s_comState.store(ComLedState::MQTT_OFFLINE, std::memory_order_release);
    } else {
        // LEVEL 4: Operasi Normal -> Hijau Solid + Traffic Blip 90ms
        s_comState.store(ComLedState::NORMAL, std::memory_order_release);
    }
}

void LedManager::tick() {
    int64_t nowUs = esp_timer_get_time();
    uint32_t nowMs = (uint32_t)(nowUs / 1000);

    // =================================================================
    // 1. EVALUASI LED 1: SYS (Pixel 0)
    // =================================================================
    uint32_t colorSys = COLOR_BLACK;

    if (s_identifyUntilUs > nowUs) {
        // Prioritas 1: Mode Identifikasi (Biru 4 Hz: 125ms On, 125ms Off)
        colorSys = ((nowMs / 125) % 2 == 0) ? COLOR_BLUE : COLOR_BLACK;
    } else if (s_otaMode || s_sysState == SysLedState::OTA) {
        // Prioritas 2: OTA Flash (Oranye Fast Strobe 10 Hz: 50ms On, 50ms Off)
        colorSys = ((nowMs / 50) % 2 == 0) ? COLOR_ORANGE : COLOR_BLACK;
    } else if (s_configMode || s_softApActive || s_sysState == SysLedState::CONFIG) {
        // Prioritas 3: Mode Konfigurasi / SoftAP / Direct PC (Biru Kedip 1 Hz: 500ms On, 500ms Off)
        colorSys = ((nowMs / 500) % 2 == 0) ? COLOR_BLUE : COLOR_BLACK;
    } else {
        switch (s_sysState) {
            case SysLedState::NORMAL:
                colorSys = COLOR_GREEN;
                break;
            case SysLedState::BOOTING:
                // Kuning 2 Hz (250ms On, 250ms Off)
                colorSys = ((nowMs / 250) % 2 == 0) ? COLOR_YELLOW : COLOR_BLACK;
                break;
            case SysLedState::FAULT:
                colorSys = COLOR_RED;
                break;
            case SysLedState::OFF:
            default:
                colorSys = COLOR_BLACK;
                break;
        }
    }

    // =================================================================
    // 2. EVALUASI LED 2: COM / NET (Pixel 1 - Status Jaringan Murni)
    // =================================================================
    uint32_t colorCom = COLOR_BLACK;

    switch (s_comState) {
        case ComLedState::NORMAL:
            // Hijau solid dengan traffic blip (padam sekejap 90ms)
            if (s_trafficBlipUntilUs > nowUs) {
                colorCom = COLOR_BLACK;
            } else {
                colorCom = COLOR_GREEN;
            }
            break;

        case ComLedState::MQTT_OFFLINE:
            // Cyan Kedip 1 Hz (500ms On, 500ms Off)
            colorCom = ((nowMs / 500) % 2 == 0) ? COLOR_CYAN : COLOR_BLACK;
            break;

        case ComLedState::CONNECTING:
            // Kuning Kedip 2 Hz (250ms On, 250ms Off)
            colorCom = ((nowMs / 250) % 2 == 0) ? COLOR_YELLOW : COLOR_BLACK;
            break;

        case ComLedState::DISCONNECTED: {
            // Merah Double Blink (Siklus 1000ms: On 100ms, Off 100ms, On 100ms, Off 700ms)
            uint32_t phase = nowMs % 1000;
            if (phase < 100 || (phase >= 200 && phase < 300)) {
                colorCom = COLOR_RED;
            } else {
                colorCom = COLOR_BLACK;
            }
            break;
        }

        case ComLedState::OFF:
        default:
            colorCom = COLOR_BLACK;
            break;
    }

    // =================================================================
    // 3. DIRTY-CHECKING TRANSMISI PIXEL (ANTI-FLICKER & LOW CPU)
    // =================================================================
    // Hanya lakukan transmisi pulsa bit-bang jika warna atau brightness berubah!
    // Ini mengeliminasi 98% denyut sinyal yang berpotensi tabrakan dengan LittleFS read.
    if (colorSys != s_lastColorSys || colorCom != s_lastColorCom || s_brightness != s_lastBrightness) {
        s_lastColorSys = colorSys;
        s_lastColorCom = colorCom;
        s_lastBrightness = s_brightness;
        writeRawPixels(colorSys, colorCom, s_brightness);
    }
}

} // namespace Iotaru::Bsp
