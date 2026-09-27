#pragma once

#include "app_types.hpp"
#include <cstdint>

namespace Iotaru::Bsp {

class LedManager {
public:
    static void init(int pin = Pins::LED_WS2812, uint8_t brightness = 40);
    
    // Kontrol Status LED
    static void setSysState(SysLedState state);
    static void setComState(ComLedState state);
    static void finishBoot();
    static void setOtaMode(bool active);
    static void setConfigMode(bool active);
    static bool isConfigMode();
    
    // Efek & Override
    static void setSolidColor(uint32_t colorSys, uint32_t colorCom);
    static void clear();
    static void triggerTrafficBlip();
    static void triggerIdentify(uint32_t durationMs = 3000);
    
    // Polling animasi 20ms (dipanggil otomatis oleh FreeRTOS task)
    static void tick();
    static void autoSync(bool netConnected, bool mqttConnected, bool isSoftAp);
    
    // Direct synchronous show untuk fase Zero-Boot Recovery (tanpa FreeRTOS task)
    static void writeRawPixels(uint32_t color0, uint32_t color1, uint8_t brightness = 40);
};

} // namespace Iotaru::Bsp
