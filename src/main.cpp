#include "app_types.hpp"
#include "app_config.hpp"
#include "bsp_button.hpp"
#include "bsp_led.hpp"
#include "bsp_uart.hpp"
#include "bsp_at24c256.hpp"
#include "beacon_pipeline.hpp"
#include "config_store.hpp"
#include "net_manager.hpp"
#include "mqtt_service.hpp"
#include "EthernetW5500.hpp"
#include "web_server.hpp"
#include "net_log_service.hpp"
#include "ota_service.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

using namespace Iotaru;

static Network::EthernetW5500 s_ethW5500;

static void supervisorTask(void *pvParameters) {
    uint32_t loopCount = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 1. Tick network & watchdog timers
        Network::NetworkManager::tickWatchdog();

        // 2. Update throughput metrics & bersihkan paket kadaluarsa saat offline (Zero-Loss Pruning)
        Core::BeaconPipeline::updateMetrics();
        if (!Network::MqttService::isConnected()) {
            Core::BeaconPipeline::pruneExpired();
        }

        // 3. Auto-sync LED state to physical network & MQTT broker reality
        Bsp::LedManager::autoSync(
            Network::NetworkManager::isLinkUp(),
            Network::NetworkManager::isConnected(),
            Network::MqttService::isConnected(),
            Network::NetworkManager::isApActive()
        );

        // 4. Periodic Diagnostic Log (tiap 5 detik)
        if (++loopCount % 5 == 0) {
            char activeIp[16] = {0};
            Network::NetworkManager::getActiveIp(activeIp, sizeof(activeIp));
            ESP_LOGI(TAG, "[MONITOR] IP: %s | Read: %u/s (Tot: %u) | MQTT: %u/s (BW: %.2f KB/s, Buff: %u) | Antrean: %u",
                     activeIp[0] ? activeIp : "None",
                     (unsigned int)Core::BeaconPipeline::getReadSpeed(),
                     (unsigned int)Core::BeaconPipeline::getTotalBeaconsRead(),
                     (unsigned int)Core::BeaconPipeline::getMqttSpeed(),
                     Network::MqttService::getBandwidthKBps(),
                     (unsigned int)Network::MqttService::getBatchBufferLoad(),
                     (unsigned int)Core::BeaconPipeline::getQueueLoad());
        }
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, ">>> IOTARU SG-10 ESP32 Gateway (Native ESP-IDF) <<<");
    ESP_LOGI(TAG, "==================================================");

    // Set zona waktu WIB (UTC+7) di awal eksekusi sistem
    setenv("TZ", "WIB-7", 1);
    tzset();

    // 0. Early Network Driver Protection & LED WS2812B Hardware Init
    Network::NetworkManager::earlyInit(&s_ethW5500);
    Bsp::LedManager::init(Pins::LED_WS2812);

    // 1. Cek tombol recovery fisik (Zero Boot Init - 100% Synchronous Blocking)
    Bsp::BootAction bootAction = Bsp::ButtonManager::checkBootRecovery(Pins::BUTTON_RESET);
    bool enterConfigModeOnBoot = (bootAction == Bsp::BootAction::ENTER_CONFIG_AP);

    if (bootAction == Bsp::BootAction::FACTORY_RESET) {
        ESP_LOGW(TAG, "Mengeksekusi Factory Reset dari tombol bootloader...");
        Core::ConfigStore::initNvs();
        Core::ConfigStore::resetConfigToFactory();
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    if (enterConfigModeOnBoot) {
        Bsp::LedManager::setConfigMode(true);
    }

    // Mulai task FreeRTOS WS2812B setelah tombol boot recovery selesai dievaluasi
    Bsp::LedManager::startTask();

    // 3. Inisialisasi NVS Flash & Konfigurasi
    ESP_ERROR_CHECK(Core::ConfigStore::initNvs());
    Core::ConfigStore::initMetadata();
    Core::ConfigStore::initNvsConfiguration();
    Core::ConfigStore::loadConfig();

    char device_id[32], module_id[32];
    Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));
    Core::ConfigStore::getModuleId(module_id, sizeof(module_id));
    ESP_LOGI(TAG, "Gateway Identity: Model='%s', Module='%s', DeviceID='%s'", g_config.device_model, module_id, device_id);

    // 4. Inisialisasi Hardware I2C AT24C256, Filesystem LittleFS & NetLogService
    Bsp::At24c256::init();
    Web::Server::initStorage();
    Network::NetLogService::init();

    // 5. Inisialisasi Core Data Pipeline (Antrean & Buffer)
    Core::BeaconPipeline::init();

    // 6. Inisialisasi UART2 nRF52840 (1 Mbps, Task di Core 0)
    ESP_ERROR_CHECK(Bsp::UartManager::init());

    // 7. Inisialisasi Network Stack (Ethernet W5500 SPI DMA / Wi-Fi STA / SNTP)
    Network::NetworkManager::init(&s_ethW5500);

    // 8. Inisialisasi MQTT Publisher Service (Core 1 Task)
    Network::MqttService::init();

    // 9. Inisialisasi Native ESP-IDF Web Server & REST API (Core 1)
    Web::Server::init();

    if (enterConfigModeOnBoot) {
        Network::NetworkManager::startConfigAP();
    }

    // 9. Tandai Booting Dasar Selesai & Batalkan OTA Rollback jika stabil
    Bsp::LedManager::finishBoot();
    Web::Ota::mark_app_valid();
    ESP_LOGI(TAG, "Pondasi sistem, MQTT, & Web Server siap dan aktif.");

    // 10. Jalankan Supervisor Task di Core 1 (Prioritas 2 di latar belakang)
    xTaskCreatePinnedToCore(supervisorTask, "supervisor_task", 4096, nullptr, 2, nullptr, 1);
}
