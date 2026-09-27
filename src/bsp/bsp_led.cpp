#include "bsp_led.hpp"
#include "driver/gpio.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

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

static SysLedState s_sysState = SysLedState::BOOTING;
static ComLedState s_comState = ComLedState::CONNECTING;
static bool s_otaMode = false;
static bool s_configMode = false;
static bool s_bootDone = false;

static volatile int64_t s_trafficBlipUntilUs = 0;
static volatile int64_t s_identifyUntilUs = 0;

static inline uint32_t scaleColor(uint32_t color, uint8_t brightness) {
    uint8_t r = (uint8_t)((color >> 16) & 0xFF);
    uint8_t g = (uint8_t)((color >> 8) & 0xFF);
    uint8_t b = (uint8_t)(color & 0xFF);

    r = (uint8_t)(((uint16_t)r * brightness) / 255);
    g = (uint8_t)(((uint16_t)g * brightness) / 255);
    b = (uint8_t)(((uint16_t)b * brightness) / 255);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Low-level Bitbang WS2812B dengan CPU cycle counter (presisi 4.167 ns di 240 MHz)
void LedManager::writeRawPixels(uint32_t color0, uint32_t color1, uint8_t brightness) {
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

    gpio_set_direction((gpio_num_t)s_ledPin, GPIO_MODE_OUTPUT);

    // Timing 240 MHz:
    // T0H: ~400ns  (96 cycles)   | T0L: ~850ns  (204 cycles)
    // T1H: ~800ns  (192 cycles)  | T1L: ~450ns  (108 cycles)
    constexpr uint32_t CYCLES_T0H = 96;
    constexpr uint32_t CYCLES_T0L = 204;
    constexpr uint32_t CYCLES_T1H = 192;
    constexpr uint32_t CYCLES_T1L = 108;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    for (int byteIdx = 0; byteIdx < 6; ++byteIdx) {
        uint8_t b = grb[byteIdx];
        for (int bit = 7; bit >= 0; --bit) {
            bool isOne = (b >> bit) & 0x01;
            uint32_t highCycles = isOne ? CYCLES_T1H : CYCLES_T0H;
            uint32_t lowCycles  = isOne ? CYCLES_T1L : CYCLES_T0L;

            // Pin HIGH
            gpio_set_level((gpio_num_t)s_ledPin, 1);
            uint32_t start = esp_cpu_get_cycle_count();
            while ((esp_cpu_get_cycle_count() - start) < highCycles) {}

            // Pin LOW
            gpio_set_level((gpio_num_t)s_ledPin, 0);
            start = esp_cpu_get_cycle_count();
            while ((esp_cpu_get_cycle_count() - start) < lowCycles) {}
        }
    }

    portEXIT_CRITICAL(&mux);

    // Latch delay > 300 us agar WS2812 me-reset register
    esp_rom_delay_us(350);
}

void LedManager::init(int pin, uint8_t brightness) {
    s_ledPin = pin;
    s_brightness = brightness;
    s_sysState = SysLedState::BOOTING;
    s_comState = ComLedState::CONNECTING;

    gpio_reset_pin((gpio_num_t)s_ledPin);
    gpio_set_direction((gpio_num_t)s_ledPin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)s_ledPin, 0);

    // Matikan kedua LED seketika
    writeRawPixels(0, 0, s_brightness);

    // Buat Task LED di Core 1 (agar Core 0 murni menyerap stream UART 1 Mbps)
    if (s_ledTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            [](void*) {
                for (;;) {
                    LedManager::tick();
                    vTaskDelay(pdMS_TO_TICKS(20)); // 50 FPS
                }
            },
            "WS2812_Task",
            2048,
            nullptr,
            1,
            &s_ledTaskHandle,
            1 // Core 1
        );
    }
}

void LedManager::setSysState(SysLedState state) {
    s_sysState = state;
}

void LedManager::setComState(ComLedState state) {
    s_comState = state;
}

void LedManager::finishBoot() {
    s_bootDone = true;
    if (s_sysState == SysLedState::BOOTING) {
        s_sysState = SysLedState::NORMAL;
    }
}

void LedManager::setOtaMode(bool active) {
    s_otaMode = active;
    s_sysState = active ? SysLedState::OTA : (s_bootDone ? SysLedState::NORMAL : SysLedState::BOOTING);
}

void LedManager::setConfigMode(bool active) {
    s_configMode = active;
    if (active) {
        s_comState = ComLedState::SOFTAP;
    }
}

bool LedManager::isConfigMode() {
    return s_configMode;
}

void LedManager::clear() {
    writeRawPixels(0, 0, s_brightness);
}

void LedManager::setSolidColor(uint32_t colorSys, uint32_t colorCom) {
    writeRawPixels(colorSys, colorCom, s_brightness);
}

void LedManager::triggerTrafficBlip() {
    s_trafficBlipUntilUs = esp_timer_get_time() + 35000; // 35 ms
}

void LedManager::triggerIdentify(uint32_t durationMs) {
    s_identifyUntilUs = esp_timer_get_time() + ((int64_t)durationMs * 1000);
}

void LedManager::autoSync(bool netConnected, bool mqttConnected, bool isSoftAp) {
    if (s_otaMode) return;

    if (isSoftAp) {
        s_comState = ComLedState::SOFTAP;
    } else if (!netConnected) {
        s_comState = ComLedState::DISCONNECTED;
    } else if (!mqttConnected) {
        s_comState = ComLedState::MQTT_OFFLINE;
    } else {
        s_comState = ComLedState::NORMAL;
    }
}

void LedManager::tick() {
    int64_t nowUs = esp_timer_get_time();
    uint32_t nowMs = (uint32_t)(nowUs / 1000);

    // =================================================================
    // 1. EVALUASI LED 1: SYS
    // =================================================================
    uint32_t colorSys = COLOR_BLACK;

    if (s_identifyUntilUs > nowUs) {
        // Mode Identifikasi: Biru 4 Hz (125ms On, 125ms Off)
        colorSys = ((nowMs / 125) % 2 == 0) ? COLOR_BLUE : COLOR_BLACK;
    } else {
        switch (s_sysState) {
            case SysLedState::NORMAL:
                colorSys = COLOR_GREEN;
                break;
            case SysLedState::BOOTING:
                // Kuning 2 Hz (250ms On, 250ms Off)
                colorSys = ((nowMs / 250) % 2 == 0) ? COLOR_YELLOW : COLOR_BLACK;
                break;
            case SysLedState::OTA:
                // Oranye Fast Strobe 10 Hz (50ms On, 50ms Off)
                colorSys = ((nowMs / 50) % 2 == 0) ? COLOR_ORANGE : COLOR_BLACK;
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
    // 2. EVALUASI LED 2: COM
    // =================================================================
    uint32_t colorCom = COLOR_BLACK;

    switch (s_comState) {
        case ComLedState::NORMAL:
            // Hijau solid dengan traffic blip (padam sekejap 35ms)
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

        case ComLedState::SOFTAP:
            // Biru Kedip 1 Hz (500ms On, 500ms Off)
            colorCom = ((nowMs / 500) % 2 == 0) ? COLOR_BLUE : COLOR_BLACK;
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

    writeRawPixels(colorSys, colorCom, s_brightness);
}

} // namespace Iotaru::Bsp
