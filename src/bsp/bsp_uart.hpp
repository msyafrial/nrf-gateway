#pragma once

#include "app_types.hpp"
#include "esp_err.h"

namespace Iotaru::Bsp {

class UartManager {
public:
    /**
     * @brief Inisialisasi UART2 (1 Mbps, RX=16, TX=17, Buffer 4096B) dan spawn
     * FreeRTOS Event Task di Core 0 dengan prioritas tinggi.
     */
    static esp_err_t init();
    
    /**
     * @brief Dapatkan handle queue event UART
     */
    static void* getEventQueue();
};

} // namespace Iotaru::Bsp
