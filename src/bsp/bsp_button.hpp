#pragma once

#include "app_types.hpp"

namespace Iotaru::Bsp {

enum class BootAction {
    BOOT_NORMAL     = 0,
    ENTER_CONFIG_AP = 1,
    FACTORY_RESET   = 2
};

class ButtonManager {
public:
    /**
     * @brief Deteksi tombol pemulihan fisik (GPIO 34) saat boot.
     * Bersifat 100% synchronous & blocking tanpa bergantung pada background task FreeRTOS.
     * 
     * - 0 .. 5 detik : Batal (LED Merah -> Lepas = Boot Normal)
     * - 5 .. 10 detik: Mode Config Hotspot SoftAP (LED Biru -> Lepas = Mode Config)
     * - > 10 detik   : Factory Reset (LED Padam -> Lepas = Erase & Restart)
     */
    static BootAction checkBootRecovery(int buttonPin = Pins::BUTTON_RESET);
};

} // namespace Iotaru::Bsp
