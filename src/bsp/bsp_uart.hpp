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

    /**
     * @brief Kirim perintah kontrol start/stop scan ke nRF52840 via UART TX (5-byte frame).
     * @param mode_cmd 1 = SCAN_STATE_PASSIVE, 2 = SCAN_STATE_ACTIVE, 0 = SCAN_STATE_STOPPED
     */
    static esp_err_t sendScanCommand(uint8_t mode_cmd);

    /**
     * @brief Kirim perintah cek status (CMD_GET_STATUS = 0x02) ke nRF52840.
     */
    static esp_err_t sendGetStatusCommand();

    /**
     * @brief Kirim perintah soft reboot (CMD_SOFT_REBOOT = 0x03, param = 0x55) ke nRF52840.
     */
    static esp_err_t sendSoftRebootCommand();
};

} // namespace Iotaru::Bsp
