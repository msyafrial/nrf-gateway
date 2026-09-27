#include "net_manager.hpp"
#include "bsp_led.hpp"
#include "config_store.hpp"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include <ctime>

static const char *TAG = "NET_MGR";

namespace Iotaru::Network {

static IEthernetDriver* s_ethDriver = nullptr;
static esp_netif_t*     s_staNetif = nullptr;
static esp_netif_t*     s_apNetif = nullptr;

static bool s_wifiConnected = false;
static bool s_wifiHasIp = false;
static bool s_ntpSynced = false;
static bool s_manualConfigMode = false;
static bool s_apActive = false;

static uint32_t s_disconStartTime = 0;
static uint32_t s_lastApActivityTime = 0;
static const uint32_t AP_INACTIVITY_TIMEOUT_MS = 60000; // 60 Detik
static uint32_t s_pendingApStopMillis = 0;

static bool s_dailyRestartExecutedToday = false;
static int  s_lastCheckedMinute = -1;

const char* NetworkManager::getWifiDisconnectReasonName(uint8_t reason) {
    switch (reason) {
        case 1:   return "UNSPECIFIED";
        case 2:   return "AUTH_EXPIRE";
        case 3:   return "AUTH_LEAVE";
        case 4:   return "ASSOC_EXPIRE";
        case 5:   return "ASSOC_TOOMANY";
        case 6:   return "NOT_AUTHED";
        case 7:   return "NOT_ASSOCED";
        case 8:   return "ASSOC_LEAVE";
        case 9:   return "ASSOC_NOT_AUTHED";
        case 10:  return "DISASSOC_PWRCAP_BAD";
        case 11:  return "DISASSOC_SUPCHAN_BAD";
        case 12:  return "BSS_TRANSITION";
        case 13:  return "IE_INVALID";
        case 14:  return "MIC_FAILURE";
        case 15:  return "4WAY_HANDSHAKE_TIMEOUT (Salah Password / Interferensi Kuat)";
        case 16:  return "GROUP_KEY_UPDATE_TIMEOUT";
        case 17:  return "IE_IN_4WAY_DIFFERS";
        case 18:  return "GROUP_CIPHER_INVALID";
        case 19:  return "PAIRWISE_CIPHER_INVALID";
        case 20:  return "AKMP_INVALID";
        case 21:  return "UNSUPP_RSN_IE_VERSION";
        case 22:  return "INVALID_RSN_IE_CAP";
        case 23:  return "802_1X_AUTH_FAILED";
        case 24:  return "CIPHER_SUITE_REJECTED";
        case 200: return "BEACON_TIMEOUT (AP Di Luar Jangkauan / Radio Padam)";
        case 201: return "NO_AP_FOUND (SSID Tidak Ditemukan / Router Mati)";
        case 202: return "AUTH_FAIL (Password Wi-Fi Salah)";
        case 203: return "ASSOC_FAIL";
        case 204: return "HANDSHAKE_TIMEOUT";
        case 205: return "CONNECTION_FAIL";
        case 206: return "AP_TSF_RESET";
        case 207: return "ROAMING";
        default:  return "UNKNOWN";
    }
}

static void sntpSyncCallback(struct timeval *tv) {
    s_ntpSynced = true;
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "SNTP sinkronisasi berhasil: %s", strftime_buf);
}

void NetworkManager::onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Interface Wi-Fi Station aktif, mencoba terhubung...");
                esp_wifi_connect();
                Bsp::LedManager::setComState(ComLedState::CONNECTING);
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t* e = (wifi_event_sta_connected_t*)data;
                s_wifiConnected = true;
                ESP_LOGI(TAG, "Terhubung ke SSID: %s (Channel: %d)", e->ssid, e->channel);
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* e = (wifi_event_sta_disconnected_t*)data;
                s_wifiConnected = false;
                s_wifiHasIp = false;
                Bsp::LedManager::setComState(ComLedState::DISCONNECTED);
                ESP_LOGW(TAG, "Wi-Fi Terputus! Alasan: %s (%d)", getWifiDisconnectReasonName(e->reason), e->reason);

                if (s_disconStartTime == 0) {
                    s_disconStartTime = (uint32_t)(esp_timer_get_time() / 1000);
                }

                if (!s_manualConfigMode) {
                    esp_wifi_connect();
                }
                break;
            }

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)data;
                ESP_LOGI(TAG, "Klien terhubung ke SoftAP: MAC=%02X:%02X:%02X:%02X:%02X:%02X, AID=%d",
                         e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5], e->aid);
                recordApActivity();
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*)data;
                ESP_LOGI(TAG, "Klien terputus dari SoftAP: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                         e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
                recordApActivity();
                break;
            }

            default:
                break;
        }
    }
}

void NetworkManager::onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
        s_wifiHasIp = true;
        s_disconStartTime = 0; // Reset watchdog timer
        Bsp::LedManager::setComState(ComLedState::NORMAL);

        char ipStr[16], gwStr[16], maskStr[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ipStr, sizeof(ipStr));
        esp_ip4addr_ntoa(&event->ip_info.gw, gwStr, sizeof(gwStr));
        esp_ip4addr_ntoa(&event->ip_info.netmask, maskStr, sizeof(maskStr));

        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, ">>> SUKSES TERHUBUNG & MENDAPATKAN IP WI-FI! <<<");
        ESP_LOGI(TAG, "  -> Alamat IP   : %s", ipStr);
        ESP_LOGI(TAG, "  -> Gateway     : %s", gwStr);
        ESP_LOGI(TAG, "  -> Subnet Mask : %s", maskStr);
        ESP_LOGI(TAG, "=======================================================");
    }
}

void NetworkManager::earlyInit(IEthernetDriver* driver) {
    s_ethDriver = driver;
    if (s_ethDriver) {
        s_ethDriver->preInit();
    }
}

void NetworkManager::init(IEthernetDriver* driver) {
    s_ethDriver = driver;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onIpEvent, nullptr));

    bool ethPreferred = (g_config.connection_mode == static_cast<int>(ConnectionMode::ETHERNET));

    bool ethStarted = false;
    if (ethPreferred && s_ethDriver != nullptr) {
        ESP_LOGI(TAG, "Memulai Ethernet Controller W5500 SPI DMA...");

        esp_ip4_addr_t ip = {}, gw = {}, mask = {}, dns = {}, dns2 = {};
        if (!g_config.eth_dhcp && strlen(g_config.eth_ip) > 0) {
            esp_netif_str_to_ip4(g_config.eth_ip, &ip);
            esp_netif_str_to_ip4(g_config.eth_gateway, &gw);
            esp_netif_str_to_ip4(g_config.eth_subnet, &mask);
            if (strlen(g_config.eth_dns) > 0) {
                esp_netif_str_to_ip4(g_config.eth_dns, &dns);
            } else {
                dns = gw;
            }
            if (strlen(g_config.eth_dns2) > 0) {
                esp_netif_str_to_ip4(g_config.eth_dns2, &dns2);
            }
        }

        char clientId[32];
        Core::ConfigStore::getMqttClientId(clientId, sizeof(clientId));

        ethStarted = s_ethDriver->begin(
            ip.addr,
            gw.addr,
            mask.addr,
            dns.addr,
            g_config.eth_dhcp != 0,
            clientId,
            dns2.addr
        );
    }

    if (!ethPreferred || !ethStarted) {
        ESP_LOGI(TAG, "Menginisialisasi Wi-Fi Station...");
        initWifiStation();
    }

    initSntp();
}

void NetworkManager::initWifiStation() {
    if (s_staNetif == nullptr) {
        s_staNetif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, g_config.w_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, g_config.w_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Static IP jika DHCP tidak diaktifkan
    if (!g_config.w_dhcp && strlen(g_config.w_ip) > 0) {
        esp_netif_dhcpc_stop(s_staNetif);
        esp_netif_ip_info_t ip_info = {};
        esp_netif_str_to_ip4(g_config.w_ip, &ip_info.ip);
        esp_netif_str_to_ip4(g_config.w_gateway, &ip_info.gw);
        esp_netif_str_to_ip4(g_config.w_subnet, &ip_info.netmask);
        esp_netif_set_ip_info(s_staNetif, &ip_info);

        esp_netif_dns_info_t dns_info = {};
        if (strlen(g_config.w_dns) > 0) {
            esp_netif_str_to_ip4(g_config.w_dns, &dns_info.ip.u_addr.ip4);
        } else {
            dns_info.ip.u_addr.ip4 = ip_info.gw;
        }
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_staNetif, ESP_NETIF_DNS_MAIN, &dns_info);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

void NetworkManager::initSntp() {
    ESP_LOGI(TAG, "Menginisialisasi SNTP (WIB GMT+7, pool.ntp.org)...");

    setenv("TZ", "WIB-7", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_setservername(2, "time.google.com");
    sntp_set_time_sync_notification_cb(sntpSyncCallback);
    esp_sntp_init();
}

void NetworkManager::startConfigAP() {
    ESP_LOGI(TAG, "Mengaktifkan On-Demand SoftAP Fallback...");
    s_manualConfigMode = true;
    s_apActive = true;
    recordApActivity();

    Bsp::LedManager::setComState(ComLedState::SOFTAP);

    if (s_apNetif == nullptr) {
        s_apNetif = esp_netif_create_default_wifi_ap();
    }

    char moduleId[32];
    Core::ConfigStore::getModuleId(moduleId, sizeof(moduleId));

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, moduleId, sizeof(ap_config.ap.ssid));

    const char* apPass = (strlen(g_config.ap_password) >= 8) ? g_config.ap_password : "12345678";
    strncpy((char*)ap_config.ap.password, apPass, sizeof(ap_config.ap.password));
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = (strlen(apPass) >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "SoftAP aktif: SSID='%s' | IP=192.168.4.1", ap_config.ap.ssid);
}

void NetworkManager::stopConfigAP() {
    if (s_apActive) {
        ESP_LOGI(TAG, "Menonaktifkan SoftAP Fallback...");
        s_manualConfigMode = false;
        s_apActive = false;
        s_pendingApStopMillis = 0;
        esp_wifi_set_mode(WIFI_MODE_STA);
        Bsp::LedManager::setComState(ComLedState::NORMAL);
    }
}

void NetworkManager::scheduleStopConfigAP(uint32_t delayMs) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    s_pendingApStopMillis = now + delayMs;
}

void NetworkManager::recordApActivity() {
    s_lastApActivityTime = (uint32_t)(esp_timer_get_time() / 1000);
}

bool NetworkManager::isConfigMode() { return s_manualConfigMode; }
bool NetworkManager::isApActive() { return s_apActive; }

uint32_t NetworkManager::getApRemainingSeconds() {
    if (!s_apActive) return 0;
    uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000) - s_lastApActivityTime;
    if (elapsed >= AP_INACTIVITY_TIMEOUT_MS) return 0;
    return (AP_INACTIVITY_TIMEOUT_MS - elapsed) / 1000;
}

bool NetworkManager::isConnected() {
    if (s_ethDriver && s_ethDriver->hasIp()) return true;
    return s_wifiHasIp;
}

bool NetworkManager::isEthernetActive() {
    return (s_ethDriver != nullptr && s_ethDriver->hasIp());
}

bool NetworkManager::isEthernetHardwareDetected() {
    return (s_ethDriver != nullptr && s_ethDriver->isHardwareDetected());
}

void NetworkManager::printEthernetDiagnostic() {
    if (s_ethDriver) s_ethDriver->printDiagnostic();
}

void NetworkManager::getActiveIp(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';

    if (isEthernetActive()) {
        s_ethDriver->getIp(outBuf, maxLen);
    } else if (s_staNetif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_staNetif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.ip, outBuf, maxLen);
        }
    }
}

ConnectionMode NetworkManager::getActiveConnectionMode() {
    return static_cast<ConnectionMode>(g_config.connection_mode);
}

void NetworkManager::getActiveWifiSsid(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strncpy(outBuf, g_config.w_ssid, maxLen);
    outBuf[maxLen - 1] = '\0';
}

void NetworkManager::rollbackPendingNetworkConfig() {
    Core::ConfigStore::loadConfig();
}

bool NetworkManager::isNtpSynced() {
    return s_ntpSynced;
}

IEthernetDriver* NetworkManager::getEthernetDriver() {
    return s_ethDriver;
}

void NetworkManager::checkNtpWatchdog() {
    // Dipantau otomatis via callback SNTP
}

void NetworkManager::checkDisconWatchdog() {
    if (g_config.discon_rst <= 0) {
        s_disconStartTime = 0;
        return;
    }
    if (isConnected() || s_apActive) {
        s_disconStartTime = 0;
        return;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_disconStartTime == 0) {
        s_disconStartTime = now;
        return;
    }

    uint32_t elapsedSec = (now - s_disconStartTime) / 1000;
    uint32_t maxSec = (uint32_t)g_config.discon_rst * 60;

    if (elapsedSec >= maxSec) {
        ESP_LOGE(TAG, "Watchdog Diskoneksi Terpicu! Terputus > %d menit. Melakukan Restart Sistem...",
                 g_config.discon_rst);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
}

void NetworkManager::checkApInactivityTimeout() {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    // Jadwal delayed stop
    if (s_pendingApStopMillis > 0 && now >= s_pendingApStopMillis) {
        stopConfigAP();
        return;
    }

    // Auto-timeout jika tidak ada aktivitas selama 60 detik
    if (s_apActive && s_manualConfigMode) {
        if (now - s_lastApActivityTime > AP_INACTIVITY_TIMEOUT_MS) {
            ESP_LOGW(TAG, "SoftAP tidak aktif selama 60 detik. Menghentikan SoftAP secara otomatis.");
            stopConfigAP();
        }
    }
}

void NetworkManager::checkDailyRestart() {
    if (strlen(g_config.restart_schedule) < 5 || !s_ntpSynced) return;

    int targetH = 0, targetM = 0;
    if (sscanf(g_config.restart_schedule, "%2d:%2d", &targetH, &targetM) != 2) return;

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_min != s_lastCheckedMinute) {
        s_lastCheckedMinute = timeinfo.tm_min;
        if (timeinfo.tm_hour == targetH && timeinfo.tm_min == targetM) {
            if (!s_dailyRestartExecutedToday) {
                s_dailyRestartExecutedToday = true;
                ESP_LOGI(TAG, "Jadwal restart harian (%02d:%02d) tercapai. Melakukan restart terencana...",
                         targetH, targetM);
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            s_dailyRestartExecutedToday = false;
        }
    }
}

void NetworkManager::tickWatchdog() {
    checkNtpWatchdog();
    checkDisconWatchdog();
    checkApInactivityTimeout();
    checkDailyRestart();
}

} // namespace Iotaru::Network
