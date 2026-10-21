#include "bsp_led.hpp"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
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
static bool s_softApActive = false;
static bool s_linkUp = false;
static bool s_hasIp = false;
static bool s_mqttConnected = false;
static bool s_bootDone = false;

// Cache dirty-checking untuk mencegah spam transmisi bit-bang & tabrakan cache SPI flash
static uint32_t s_lastColorSys = 0xFFFFFFFF;
static uint32_t s_lastColorCom = 0xFFFFFFFF;
static uint8_t  s_lastBrightness = 0xFF;

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

// Low-level Bitbang WS2812B di IRAM dengan Direct Register Write & CPU cycle counter
// Ditempatkan di IRAM agar 100% imun terhadap SPI Flash Cache Miss / LittleFS read saat web dibuka!
IRAM_ATTR void LedManager::writeRawPixels(uint32_t color0, uint32_t color1, uint8_t brightness) {
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

    // Pointer register GPIO langsung untuk akses ultra-cepat (< 3 CPU cycle)
    uint32_t pinMask = (s_ledPin < 32) ? (1UL << s_ledPin) : (1UL << (s_ledPin - 32));
    volatile uint32_t *setReg = (volatile uint32_t *)((s_ledPin < 32) ? GPIO_OUT_W1TS_REG : GPIO_OUT1_W1TS_REG);
    volatile uint32_t *clrReg = (volatile uint32_t *)((s_ledPin < 32) ? GPIO_OUT_W1TC_REG : GPIO_OUT1_W1TC_REG);

    #ifndef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
    #define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240
    #endif

    constexpr uint32_t CYCLES_T0H = (320 * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) / 1000;
    constexpr uint32_t CYCLES_T0L = (850 * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) / 1000;
    constexpr uint32_t CYCLES_T1H = (800 * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) / 1000;
    constexpr uint32_t CYCLES_T1L = (380 * CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) / 1000;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    for (int byteIdx = 0; byteIdx < 6; ++byteIdx) {
        uint8_t b = grb[byteIdx];
        for (int bit = 7; bit >= 0; --bit) {
            bool isOne = (b >> bit) & 0x01;
            uint32_t highCycles = isOne ? CYCLES_T1H : CYCLES_T0H;
            uint32_t lowCycles  = isOne ? CYCLES_T1L : CYCLES_T0L;

            // Pin HIGH instan (direct register)
            uint32_t start = esp_cpu_get_cycle_count();
            *setReg = pinMask;
            while ((esp_cpu_get_cycle_count() - start) < highCycles) {}

            // Pin LOW instan (direct register)
            start = esp_cpu_get_cycle_count();
            *clrReg = pinMask;
            while ((esp_cpu_get_cycle_count() - start) < lowCycles) {}
        }
    }

    portEXIT_CRITICAL(&mux);

    // Latch delay > 300 us agar WS2812 me-reset shift register
    esp_rom_delay_us(350);
}

void LedManager::init(int pin, uint8_t brightness) {
    s_ledPin = pin;
    s_brightness = brightness;
    s_sysState = SysLedState::BOOTING;
    s_comState = ComLedState::CONNECTING;
    s_lastColorSys = 0xFFFFFFFF;
    s_lastColorCom = 0xFFFFFFFF;
    s_lastBrightness = 0xFF;

    gpio_reset_pin((gpio_num_t)s_ledPin);
    gpio_set_direction((gpio_num_t)s_ledPin, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)s_ledPin, 0);

    // Matikan kedua LED seketika
    writeRawPixels(0, 0, s_brightness);
}

void LedManager::startTask() {
    // Buat Task LED di Core 1 (Priority 3 agar tidak tercekik oleh background tasks)
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
            3,
            &s_ledTaskHandle,
            1 // Core 1
        );
    }
}

void LedManager::setSysState(SysLedState state) {
    s_sysState = state;
}

void LedManager::setComState(ComLedState state) {
    if (s_otaMode) return;
    s_comState = state;
}

void LedManager::finishBoot() {
    s_bootDone = true;
    if (s_sysState == SysLedState::BOOTING) {
        s_sysState = s_configMode ? SysLedState::CONFIG : SysLedState::NORMAL;
    }
}

void LedManager::setOtaMode(bool active) {
    s_otaMode = active;
    if (active) {
        s_sysState = SysLedState::OTA;
    } else {
        s_sysState = s_configMode ? SysLedState::CONFIG : (s_bootDone ? SysLedState::NORMAL : SysLedState::BOOTING);
    }
}

void LedManager::setConfigMode(bool active) {
    s_configMode = active;
    s_softApActive = active;
    if (active) {
        s_sysState = SysLedState::CONFIG;
    } else {
        s_sysState = s_bootDone ? SysLedState::NORMAL : SysLedState::BOOTING;
    }
}

bool LedManager::isConfigMode() {
    return s_configMode;
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
    if (s_trafficBlipUntilUs < now) {
        s_trafficBlipUntilUs = now + 90000; // 90 ms (~4-5 frame off @ 50 FPS, tegas dan jelas terlihat mata)
    }
}

void LedManager::triggerIdentify(uint32_t durationMs) {
    s_identifyUntilUs = esp_timer_get_time() + ((int64_t)durationMs * 1000);
}

void LedManager::autoSync(bool linkUp, bool ipAcquired, bool mqttConnected, bool isSoftAp) {
    if (s_otaMode) return;

    s_linkUp = linkUp;
    s_hasIp = ipAcquired;
    s_mqttConnected = mqttConnected;
    s_softApActive = isSoftAp;

    // Hirarki Prioritas Murni LED NET/COM:
    if (!s_linkUp) {
        // LEVEL 1: Link Fisik Terputus -> Merah Double Blink
        s_comState = ComLedState::DISCONNECTED;
    } else if (!s_hasIp) {
        // LEVEL 2: Link Up, Menunggu IP DHCP -> Kuning Kedip 2 Hz
        s_comState = ComLedState::CONNECTING;
    } else if (!s_mqttConnected) {
        // LEVEL 3: Dapat IP, Broker MQTT Belum Terhubung -> Cyan Kedip 1 Hz
        s_comState = ComLedState::MQTT_OFFLINE;
    } else {
        // LEVEL 4: Operasi Normal -> Hijau Solid + Traffic Blip 90ms
        s_comState = ComLedState::NORMAL;
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
