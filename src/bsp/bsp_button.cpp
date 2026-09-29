#include "bsp_button.hpp"
#include "bsp_led.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BOOT_RECOVERY";

namespace Iotaru::Bsp {

BootAction ButtonManager::checkBootRecovery(int buttonPin) {
    gpio_reset_pin((gpio_num_t)buttonPin);
    gpio_set_direction((gpio_num_t)buttonPin, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(50)); // Stabilisasi pull-up eksternal

    // Verifikasi 5 sampel berurutan (mencegah pin floating)
    bool buttonPressed = true;
    for (int i = 0; i < 5; ++i) {
        if (gpio_get_level((gpio_num_t)buttonPin) != 0) {
            buttonPressed = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!buttonPressed) {
        ESP_LOGI(TAG, "Pin Recovery %d HIGH (Pull-up normal = %d). Melanjutkan boot...", 
                 buttonPin, gpio_get_level((gpio_num_t)buttonPin));
        return BootAction::BOOT_NORMAL;
    }

    ESP_LOGW(TAG, "Pin Recovery %d terbaca LOW (0V)! Tombol terdeteksi ditekan.", buttonPin);

    ESP_LOGW(TAG, "=======================================================");
    ESP_LOGW(TAG, "Tombol recovery terdeteksi ditekan saat power-on!");
    ESP_LOGW(TAG, "Seluruh proses booting DITAHAN (100%% Synchronous Blocking)");
    ESP_LOGW(TAG, "> 0 - 5 detik : Batal (Merah Solid -> Lepas = Boot Normal)");
    ESP_LOGW(TAG, "> 5 - 10 detik: Mode Config AP (Biru Solid -> Lepas = SoftAP)");
    ESP_LOGW(TAG, "> 10 detik    : Factory Reset (LED Padam -> Lepas = Reset)");
    ESP_LOGW(TAG, "=======================================================");

    // Detik 0: Nyalakan Merah Solid di kedua LED
    LedManager::writeRawPixels(0xFF0000, 0xFF0000);

    int64_t pressStartUs = esp_timer_get_time();
    bool solidBlueActive = false;
    uint32_t lastReportSec = 0;

    while (gpio_get_level((gpio_num_t)buttonPin) == 0) {
        int64_t elapsedMs = (esp_timer_get_time() - pressStartUs) / 1000;
        uint32_t elapsedSec = (uint32_t)(elapsedMs / 1000);

        // Detik ke-5: Beralih ke Biru Solid
        if (elapsedMs >= 5000 && !solidBlueActive) {
            solidBlueActive = true;
            LedManager::writeRawPixels(0x0000FF, 0x0000FF);
            ESP_LOGI(TAG, ">>> 5 DETIK TERCAPAI: LED BERUBAH MENJADI BIRU SOLID! <<<");
            ESP_LOGI(TAG, "Lepas sekarang untuk Mode Config AP, atau tahan hingga 10 detik untuk Factory Reset.");
        }

        if (elapsedSec > lastReportSec) {
            lastReportSec = elapsedSec;
            if (elapsedSec < 5) {
                LedManager::writeRawPixels(0xFF0000, 0xFF0000);
                ESP_LOGI(TAG, "Menahan tombol: %lu / 5 detik [MERAH SOLID] (Lepas = Batal)...", (unsigned long)elapsedSec);
            } else if (elapsedSec < 10) {
                LedManager::writeRawPixels(0x0000FF, 0x0000FF);
                ESP_LOGI(TAG, "Menahan tombol: %lu / 10 detik [BIRU SOLID] (Lepas = Mode Config, Tahan = Reset Total)...", (unsigned long)elapsedSec);
            }
        }

        // Detik ke-10: LED Padam Total, tunggu tombol dilepas
        if (elapsedMs >= 10000) {
            ESP_LOGW(TAG, "=======================================================");
            ESP_LOGW(TAG, ">>> 10 DETIK TERCAPAI: LED BIRU PADAM! <<<");
            ESP_LOGW(TAG, "Indikator visual padam total. Menunggu tombol DILEPAS untuk Factory Reset...");
            ESP_LOGW(TAG, "=======================================================");

            LedManager::writeRawPixels(0, 0);

            // Tunggu hingga tombol dilepas oleh pengguna
            while (gpio_get_level((gpio_num_t)buttonPin) == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }

            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce pasca pelepasan
            ESP_LOGW(TAG, "Tombol telah dilepas! Mengeksekusi Factory Reset...");

            // Feedback visual sejenak (Merah)
            LedManager::writeRawPixels(0xFF0000, 0xFF0000);
            vTaskDelay(pdMS_TO_TICKS(500));
            LedManager::writeRawPixels(0, 0);

            return BootAction::FACTORY_RESET;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Tombol dilepas sebelum 10 detik
    int64_t totalHeldMs = (esp_timer_get_time() - pressStartUs) / 1000;
    LedManager::writeRawPixels(0, 0);

    if (totalHeldMs >= 5000) {
        ESP_LOGI(TAG, "Tombol dilepas pada %lld ms (di atas 5 detik) -> Masuk ke Mode Config SoftAP!", (long long)totalHeldMs);
        return BootAction::ENTER_CONFIG_AP;
    } else {
        ESP_LOGI(TAG, "Tombol dilepas sebelum 5 detik (%lld ms) -> Dibatalkan, melanjutkan boot normal.", (long long)totalHeldMs);
        return BootAction::BOOT_NORMAL;
    }
}

} // namespace Iotaru::Bsp
