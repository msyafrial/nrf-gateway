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
#include "esp_timer.h"
#include "esp_crc.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

using namespace Iotaru;

struct CrashBreadcrumb {
    uint32_t magic;
    uint32_t uptime_sec;
    uint32_t free_heap_kb;
    uint16_t queue_load;
    uint16_t crc;
};
static constexpr uint32_t BREADCRUMB_MAGIC = 0x4252434D; // "BRCM"
RTC_NOINIT_ATTR static CrashBreadcrumb s_rtcBreadcrumb;

static uint16_t calcBreadcrumbCrc(const CrashBreadcrumb& b) {
    return esp_crc16_le(0, reinterpret_cast<const uint8_t*>(&b), offsetof(CrashBreadcrumb, crc));
}

static Network::EthernetW5500 s_ethW5500;

static void supervisorTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    uint32_t loopCount = 0;
    uint32_t nrfZeroIngressSecs = 0;
    uint32_t lastNrfFrames = Core::BeaconPipeline::getTotalFramesIngress();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();

        s_rtcBreadcrumb.magic = BREADCRUMB_MAGIC;
        s_rtcBreadcrumb.uptime_sec = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        s_rtcBreadcrumb.free_heap_kb = static_cast<uint32_t>(esp_get_free_heap_size() / 1024);
        s_rtcBreadcrumb.queue_load = static_cast<uint16_t>(Core::BeaconPipeline::getQueueLoad());
        s_rtcBreadcrumb.crc = calcBreadcrumbCrc(s_rtcBreadcrumb);

        // 1. Tick OTA watchdog, network & watchdog timers & EEPROM lifetime stats
        Web::Ota::check_watchdog();
        if (Web::Ota::is_pipeline_paused()) {
            if (Network::NetworkManager::isApActive()) {
                Network::NetworkManager::recordApActivity();
            }
        } else {
            Network::NetworkManager::tickWatchdog();
        }
        Network::NetLogService::tickPeriodicCommit();

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

        // 5. Health monitor & Self-Healing nRF52840 (ditahan sementara selama OTA aktif)
        uint32_t curNrfFrames = Core::BeaconPipeline::getTotalFramesIngress();
        if (Web::Ota::is_pipeline_paused()) {
            lastNrfFrames = curNrfFrames;
            nrfZeroIngressSecs = 0;
        } else {
            uint8_t targetScanMode = (g_config.scan_mode == 1) ? 0x01 : 0x02;
            uint8_t currentNrfState = Core::BeaconPipeline::getNrfScanState();

            if (curNrfFrames != lastNrfFrames) {
                lastNrfFrames = curNrfFrames;
                nrfZeroIngressSecs = 0;
            } else {
                nrfZeroIngressSecs++;
            }

            if (currentNrfState != targetScanMode) {
                if (loopCount % 3 == 0) {
                    ESP_LOGI(TAG, "nRF52840 state (%u) != target (%u). Mengirim ulang perintah scan...",
                             currentNrfState, targetScanMode);
                    Bsp::UartManager::sendScanCommand(targetScanMode);
                }
            } else if (nrfZeroIngressSecs >= 15) {
                if (nrfZeroIngressSecs == 15 || nrfZeroIngressSecs == 30) {
                    ESP_LOGD(TAG, "UART nRF52840 idle %lu detik -> polling status & re-sync scan mode",
                             (unsigned long)nrfZeroIngressSecs);
                    Bsp::UartManager::sendGetStatusCommand();
                    Bsp::UartManager::sendScanCommand(targetScanMode);
                } else if (nrfZeroIngressSecs >= 45) {
                    ESP_LOGW(TAG, "nRF52840 tidak merespons polling selama 45 detik! Memicu CMD_SOFT_REBOOT...");
                    Bsp::UartManager::sendSoftRebootCommand();
                    nrfZeroIngressSecs = 0;
                }
            }
        }
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, ">>> IOTARU SG-10 ESP32 Gateway (Native ESP-IDF) <<<");
    ESP_LOGI(TAG, "==================================================");

    esp_reset_reason_t rstReason = esp_reset_reason();
    if (rstReason == ESP_RST_PANIC || rstReason == ESP_RST_INT_WDT ||
        rstReason == ESP_RST_TASK_WDT || rstReason == ESP_RST_WDT ||
        rstReason == ESP_RST_BROWNOUT) {
        if (s_rtcBreadcrumb.magic == BREADCRUMB_MAGIC &&
            s_rtcBreadcrumb.crc == calcBreadcrumbCrc(s_rtcBreadcrumb)) {
            ESP_LOGW(TAG, "[CRASH-BREADCRUMB] ResetReason=%d | LastUptime=%lus | FreeHeap=%luKB | QLoad=%u",
                     (int)rstReason,
                     (unsigned long)s_rtcBreadcrumb.uptime_sec,
                     (unsigned long)s_rtcBreadcrumb.free_heap_kb,
                     (unsigned int)s_rtcBreadcrumb.queue_load);
        }
    }

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
        ESP_LOGW(TAG, "Mengeksekusi Factory Reset dari tombol bootloader (12 detik)...");

        // Indikator visual LED (Merah solid) proses erase
        Bsp::LedManager::writeRawPixels(0xFF0000, 0xFF0000);

        // 1. Inisialisasi Hardware I2C AT24C256
        Bsp::At24c256::init();

        // 2. Bersihkan log EEPROM & Drop Log RAM serta reset boot_count ke 0, pertahankan lifetime uptime
        Network::NetLogService::factoryResetLogsExceptLifetime();

        // 3. Inisialisasi NVS dan reset seluruh konfigurasi operasional ke standar pabrik
        Core::ConfigStore::initNvs();
        Core::ConfigStore::resetConfigToFactory();

        // 4. Feedback visual LED selesai (Kedip Hijau 600ms)
        Bsp::LedManager::writeRawPixels(0x00FF00, 0x00FF00);
        vTaskDelay(pdMS_TO_TICKS(600));
        Bsp::LedManager::writeRawPixels(0, 0);

        vTaskDelay(pdMS_TO_TICKS(400));
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
    Bsp::At24c256::startAsyncWriter();
    Web::Server::initStorage();
    Network::NetLogService::init();

    // 5. Inisialisasi Core Data Pipeline (Antrean & Buffer)
    Core::BeaconPipeline::init();

    // 6. Inisialisasi UART2 nRF52840 (1 Mbps, Task di Core 0)
    ESP_ERROR_CHECK(Bsp::UartManager::init());

    // Seluruh subsistem internal ESP32 siap sebelum terhubung ke jaringan.
    // Kirim perintah scan ke nRF52840 sesuai scan_mode dari NVS (1: Passive, 2: Active [Default])
    uint8_t startupScanMode = (g_config.scan_mode == 1) ? 0x01 : 0x02;
    ESP_LOGI(TAG, "Seluruh setup lokal ESP32 siap. Mengirim perintah scan (Mode %u: %s) ke nRF52840 sebelum koneksi jaringan...",
             startupScanMode, (startupScanMode == 2) ? "ACTIVE" : "PASSIVE");

    // Handshake pre-network: coba kirim dan tunggu respon ACK nRF52840 (maks ~3 detik jika nRF sedang booting)
    bool nrfSynced = false;
    for (int retry = 0; retry < 12; ++retry) {
        Bsp::UartManager::sendScanCommand(startupScanMode);
        vTaskDelay(pdMS_TO_TICKS(250));
        if (Core::BeaconPipeline::getNrfScanState() == startupScanMode) {
            ESP_LOGI(TAG, "nRF52840 terverifikasi aktif & sinkron di mode %u setelah %d ms.",
                     startupScanMode, (retry + 1) * 250);
            nrfSynced = true;
            break;
        }
    }
    if (!nrfSynced) {
        ESP_LOGW(TAG, "nRF52840 belum merespons handshake (kabel terbalik / belum terpasang / nRF offline). Startup dilanjutkan, auto-retry berjalan di background...");
    }

    if (enterConfigModeOnBoot) {
        Network::NetworkManager::setBootConfigMode(true);
    }

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

    // Konfigurasi Hardware Task Watchdog Timer (TWDT) 15 detik dengan auto-panic reboot
    esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms = 15000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic = true,
    };
    if (esp_task_wdt_reconfigure(&twdt_cfg) != ESP_OK) {
        esp_task_wdt_init(&twdt_cfg);
    }

    ESP_LOGI(TAG, "Pondasi sistem, MQTT, & Web Server siap dan aktif.");

    // 10. Jalankan Supervisor Task di Core 1 (Prioritas 2 di latar belakang)
    xTaskCreatePinnedToCore(supervisorTask, "supervisor_task", 4096, nullptr, 2, nullptr, 1);
}
