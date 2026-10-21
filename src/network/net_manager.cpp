#include "net_manager.hpp"
#include "micro_dhcp_server.hpp"
#include "udp_discovery.hpp"
#include "mqtt_service.hpp"
#include "net_log_service.hpp"
#include "bsp_led.hpp"
#include "config_store.hpp"
#include "esp_wifi.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "mdns.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip/etharp.h"
#include "lwip/dns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include <ctime>

static const char *TAG = "NET_MGR";

namespace Iotaru::Network {

enum class EthFsmState {
    DISCONNECTED,
    PROBING,
    ROUTER_LAN,
    DIRECT_PC,
    PEER_SLAVE
};

static IEthernetDriver* s_ethDriver = nullptr;
static esp_netif_t*     s_staNetif = nullptr;
static esp_netif_t*     s_apNetif = nullptr;

static EthFsmState s_ethFsmState = EthFsmState::DISCONNECTED;
static uint32_t    s_ethProbeStartMs = 0;
static uint32_t    s_ethProbeTimeoutMs = 12000;
static uint32_t    s_directPcStartMs = 0;
static uint32_t    s_slaveLastMasterCheckMs = 0;

static bool s_wifiDriverInitialized = false;
static bool s_wifiConnected = false;
static bool s_wifiHasIp = false;
static bool s_ntpSynced = false;
static bool s_manualConfigMode = false;
static bool s_apActive = false;
static volatile bool s_wifiScanDone = false;
static volatile bool s_wifiScanSuccess = false;

static uint8_t  s_cachedChannel = 0;
static uint8_t  s_cachedBssid[6] = {0};
static bool     s_hasCachedAp = false;
static uint8_t  s_fastRetryCount = 0;

static uint32_t s_disconStartTime = 0;
static uint32_t s_lastApActivityTime = 0;
static const uint32_t AP_INACTIVITY_TIMEOUT_MS = 60000; // 60 Detik
static uint32_t s_pendingApStopMillis = 0;

static bool s_dailyRestartExecutedToday = false;
static int  s_lastCheckedMinute = -1;

static void restoreWifiDnsAndDefaultRoute() {
    if (s_staNetif != nullptr) {
        esp_netif_set_default_netif(s_staNetif);

        esp_netif_dns_info_t dns_info = {};
        bool has_dns = false;
        if (esp_netif_get_dns_info(s_staNetif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK && dns_info.ip.u_addr.ip4.addr != 0) {
            has_dns = true;
        } else if (strlen(g_config.w_dns) > 0) {
            esp_netif_str_to_ip4(g_config.w_dns, &dns_info.ip.u_addr.ip4);
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            has_dns = true;
        } else {
            IP4_ADDR(&dns_info.ip.u_addr.ip4, 8, 8, 8, 8); // Google DNS fallback
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            has_dns = true;
        }

        if (has_dns) {
            esp_netif_set_dns_info(s_staNetif, ESP_NETIF_DNS_MAIN, &dns_info);
            ip_addr_t lwip_dns;
            ip_addr_set_ip4_u32(&lwip_dns, dns_info.ip.u_addr.ip4.addr);
            dns_setserver(0, &lwip_dns);

            ip_addr_t lwip_dns2;
            ip_addr_set_ip4_u32(&lwip_dns2, PP_HTONL(0x01010101)); // Cloudflare 1.1.1.1
            dns_setserver(1, &lwip_dns2);
        }
        ESP_LOGI(TAG, "Default Netif dikunci ke Wi-Fi STA dan DNS dipulihkan (Main & Backup).");

        if (!MqttService::isConnected()) {
            MqttService::forceReconnect();
        }
    }
}

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

static bool shouldWifiStaConnect() {
    if (s_manualConfigMode) return false;
    bool wifiModeActive = (g_config.connection_mode == static_cast<int>(ConnectionMode::WIFI));
    if (!wifiModeActive) return false;
    // Pada mode Wi-Fi, Wi-Fi Station adalah uplink utama (tidak diblokir oleh Ethernet Direct LAN)
    if (g_config.w_ssid[0] == '\0') return false;
    return true;
}

void NetworkManager::onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Interface Wi-Fi Station aktif");
                if (shouldWifiStaConnect()) {
                    ESP_LOGI(TAG, "Mencoba terhubung ke Wi-Fi '%s'...", g_config.w_ssid);
                    esp_wifi_connect();
                    Bsp::LedManager::setComState(ComLedState::CONNECTING);
                } else {
                    ESP_LOGI(TAG, "Wi-Fi Station dalam mode idle/pasif (Ethernet aktif / auto-connect dinonaktifkan)");
                }
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t* e = (wifi_event_sta_connected_t*)data;
                s_wifiConnected = true;
                s_cachedChannel = e->channel;
                memcpy(s_cachedBssid, e->bssid, 6);
                s_hasCachedAp = true;
                s_fastRetryCount = 0;
                esp_wifi_set_ps(WIFI_PS_NONE);
                esp_err_t bw_err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
                if (bw_err != ESP_OK) {
                    ESP_LOGW(TAG, "Gagal mengatur bandwidth Wi-Fi HT20: %s", esp_err_to_name(bw_err));
                }
                esp_wifi_set_max_tx_power(78);
                ESP_LOGI(TAG, "Terhubung ke SSID: %s (Channel: %d, BW: HT20, Max TX: 78)", e->ssid, e->channel);
                char det[80];
                snprintf(det, sizeof(det), "SSID: %s (Channel: %d, BW: HT20)", e->ssid, e->channel);
                NetLogService::log(NetCategory::WIFI, NetEvent::CONNECTED, det);
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* e = (wifi_event_sta_disconnected_t*)data;
                s_wifiConnected = false;
                s_wifiHasIp = false;

                char det[80];
                snprintf(det, sizeof(det), "Alasan: %s (%d)", getWifiDisconnectReasonName(e->reason), e->reason);
                NetLogService::log(NetCategory::WIFI, NetEvent::DISCONNECTED, det);

                if (shouldWifiStaConnect()) {
                    Bsp::LedManager::setComState(ComLedState::DISCONNECTED);
                    ESP_LOGW(TAG, "Wi-Fi Terputus! Alasan: %s (%d)", getWifiDisconnectReasonName(e->reason), e->reason);

                    if (s_disconStartTime == 0) {
                        s_disconStartTime = (uint32_t)(esp_timer_get_time() / 1000);
                    }

                    if (s_hasCachedAp && s_cachedChannel > 0 && s_fastRetryCount < 3) {
                        s_fastRetryCount++;
                        ESP_LOGI(TAG, "Fast Reconnect ke Channel %d (Percobaan %d/3)...", s_cachedChannel, s_fastRetryCount);
                        wifi_config_t sta_cfg = {};
                        if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
                            sta_cfg.sta.channel = s_cachedChannel;
                            sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
                            sta_cfg.sta.bssid_set = 0; // Bebas trap roaming / band steering
                            esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
                        }
                    } else if (s_fastRetryCount >= 3) {
                        ESP_LOGW(TAG, "Fast Reconnect gagal 3x, fallback ke Full Scan Channel 1-13...");
                        s_fastRetryCount = 0;
                        s_hasCachedAp = false;
                        wifi_config_t sta_cfg = {};
                        if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
                            sta_cfg.sta.channel = 0;
                            sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
                            sta_cfg.sta.bssid_set = 0;
                            esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
                        }
                    }
                    esp_wifi_connect();
                } else {
                    ESP_LOGD(TAG, "Wi-Fi Station terputus dalam mode pasif/idle. Alasan: %d", e->reason);
                }
                break;
            }

            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*)data;
                ESP_LOGI(TAG, "Klien terhubung ke SoftAP: MAC=%02X:%02X:%02X:%02X:%02X:%02X, AID=%d",
                         e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5], e->aid);
                recordApActivity();
                char det[80];
                snprintf(det, sizeof(det), "Klien terhubung (MAC: %02X:%02X:%02X:%02X:%02X:%02X)",
                         e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
                NetLogService::log(NetCategory::SOFTAP, NetEvent::CLIENT_JOIN, det);
                break;
            }

            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*)data;
                ESP_LOGI(TAG, "Klien terputus dari SoftAP: MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                         e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
                recordApActivity();
                char det[80];
                snprintf(det, sizeof(det), "Klien terputus (MAC: %02X:%02X:%02X:%02X:%02X:%02X)",
                         e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
                NetLogService::log(NetCategory::SOFTAP, NetEvent::CLIENT_LEAVE, det);
                break;
            }

            case WIFI_EVENT_SCAN_DONE: {
                wifi_event_sta_scan_done_t* e = (wifi_event_sta_scan_done_t*)data;
                s_wifiScanDone = true;
                s_wifiScanSuccess = (e->status == 0);
                ESP_LOGI(TAG, "Wi-Fi Scan selesai di seluruh channel (1-13). Status=%d, Total Ditemukan=%d AP",
                         e->status, e->number);
                break;
            }

            default:
                break;
        }
    }
}

void NetworkManager::getMdnsHostname(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;

    // Bersihkan model: hanya menyisakan huruf dan angka (lowercase), contoh: "SG-10" -> "sg10"
    char clean_model[32];
    size_t mIdx = 0;
    const char* raw_model = g_config.device_model[0] ? g_config.device_model : "SG-10";
    for (size_t i = 0; raw_model[i] != '\0' && mIdx < sizeof(clean_model) - 1; ++i) {
        char c = raw_model[i];
        if (c >= 'A' && c <= 'Z') {
            clean_model[mIdx++] = static_cast<char>(c + ('a' - 'A'));
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            clean_model[mIdx++] = c;
        }
    }
    clean_model[mIdx] = '\0';
    if (mIdx == 0) {
        strlcpy(clean_model, "sg10", sizeof(clean_model));
    }

    char device_id[32];
    Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));

    // Hostname mDNS: <clean_model>-<device_id> (contoh: sg10-b3462cf9)
    snprintf(outBuf, maxLen, "%s-%s", clean_model, device_id);
}

void NetworkManager::initMdns() {
    static bool s_mdnsInitialized = false;
    if (s_mdnsInitialized) return;

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Gagal menginisialisasi mDNS: 0x%x", err);
        return;
    }

    char hostname[64];
    getMdnsHostname(hostname, sizeof(hostname));

    char device_id[32];
    Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));

    mdns_hostname_set(hostname);

    char instance_name[64];
    const char* raw_model = g_config.device_model[0] ? g_config.device_model : "SG-10";
    snprintf(instance_name, sizeof(instance_name), "IOTARU %s Gateway", raw_model);
    mdns_instance_name_set(instance_name);

    // Registrasi Service HTTP port 80 untuk Zeroconf / Bonjour auto-discovery
    mdns_service_add("IOTARU Web Console", "_http", "_tcp", 80, NULL, 0);
    mdns_service_txt_item_set("_http", "_tcp", "model", raw_model);
    mdns_service_txt_item_set("_http", "_tcp", "firmware", FIRMWARE_VERSION);
    mdns_service_txt_item_set("_http", "_tcp", "device_id", device_id);

    s_mdnsInitialized = true;
    ESP_LOGI(TAG, "mDNS Service Aktif: http://%s.local (Port 80)", hostname);
}

void NetworkManager::onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == IP_EVENT && (id == IP_EVENT_STA_GOT_IP || id == IP_EVENT_ETH_GOT_IP)) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;

        if (id == IP_EVENT_ETH_GOT_IP) {
            // Jika sebelumnya dalam fase PROBING, ini adalah IP resmi dari Router DHCP
            if (s_ethFsmState == EthFsmState::PROBING) {
                s_ethFsmState = EthFsmState::ROUTER_LAN;
                MicroDhcpServer::stop();
                Bsp::LedManager::setConfigMode(false);
                ESP_LOGI(TAG, "Ethernet transisi ke state ROUTER_LAN (Mendapatkan IP dari Router DHCP)!");
            }

            // Pada mode Wi-Fi: kunci default netif dan DNS tetap pada Wi-Fi Station agar uplink data & MQTT tidak terganggu
            if (g_config.connection_mode == static_cast<int>(ConnectionMode::WIFI)) {
                ESP_LOGI(TAG, "Mode Wi-Fi aktif: Menjaga Wi-Fi STA sebagai Default Netif & DNS Uplink...");
                restoreWifiDnsAndDefaultRoute();
            }
        }

        if (id == IP_EVENT_STA_GOT_IP) {
            s_wifiHasIp = true;
            restoreWifiDnsAndDefaultRoute();
        }
        s_disconStartTime = 0; // Reset watchdog timer
        Bsp::LedManager::autoSync(true, true, MqttService::isConnected(), s_apActive);

        char ip_str[16], gw_str[16], mask_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        esp_ip4addr_ntoa(&event->ip_info.gw, gw_str, sizeof(gw_str));
        esp_ip4addr_ntoa(&event->ip_info.netmask, mask_str, sizeof(mask_str));

        const char* if_name = (id == IP_EVENT_STA_GOT_IP) ? "WI-FI" : "ETHERNET";
        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, ">>> SUKSES TERHUBUNG & MENDAPATKAN IP %s! <<<", if_name);
        ESP_LOGI(TAG, "  -> Alamat IP   : %s", ip_str);
        ESP_LOGI(TAG, "  -> Gateway     : %s", gw_str);
        ESP_LOGI(TAG, "  -> Subnet Mask : %s", mask_str);
        ESP_LOGI(TAG, "=======================================================");

        char det[80];
        snprintf(det, sizeof(det), "IP: %s | GW: %s", ip_str, gw_str);
        NetLogService::log((id == IP_EVENT_STA_GOT_IP) ? NetCategory::WIFI : NetCategory::ETHERNET, NetEvent::GOT_IP, det);

        // Aktifkan mDNS saat koneksi IP diperoleh
        initMdns();

        // Bangunkan MQTT Client seketika tanpa menunggu timer tidur (ala PubSubClient)
        MqttService::forceReconnect();
    }
}

void NetworkManager::onEthEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == ETH_EVENT) {
        switch (id) {
            case ETHERNET_EVENT_CONNECTED: {
                uint8_t mac[6] = {0};
                esp_read_mac(mac, ESP_MAC_ETH);
                uint32_t jitter = ((mac[4] << 8) | mac[5]) % 2000;
                // Beri waktu 10-12 detik untuk kedua mode agar router/switch kantor/rumah sempat handshake DHCP
                s_ethProbeTimeoutMs = 10000 + jitter;
                s_ethProbeStartMs = (uint32_t)(esp_timer_get_time() / 1000);
                s_ethFsmState = EthFsmState::PROBING;

                esp_netif_t* eth_netif = s_ethDriver ? s_ethDriver->getNetif() : nullptr;
                if (eth_netif) {
                    char hostname[64];
                    getMdnsHostname(hostname, sizeof(hostname));
                    esp_netif_set_hostname(eth_netif, hostname);
                    UdpDiscoveryService::start(eth_netif);
                }
                ESP_LOGI(TAG, "Ethernet Link Up terdeteksi. FSM: PROBING (Timeout: %lu ms)...",
                         (unsigned long)s_ethProbeTimeoutMs);
                break;
            }

            case ETHERNET_EVENT_DISCONNECTED: {
                ESP_LOGW(TAG, "Ethernet Link Down terdeteksi. Mereset fallback FSM...");
                MicroDhcpServer::stop();
                s_ethFsmState = EthFsmState::DISCONNECTED;
                s_ethProbeStartMs = 0;
                s_directPcStartMs = 0;
                s_slaveLastMasterCheckMs = 0;
                Bsp::LedManager::setConfigMode(false);
                Bsp::LedManager::autoSync(isLinkUp(), isConnected(), MqttService::isConnected(), s_apActive);

                esp_netif_t* eth_netif = s_ethDriver ? s_ethDriver->getNetif() : nullptr;
                if (eth_netif) {
                    esp_netif_ip_info_t zero_ip = {};
                    esp_netif_set_ip_info(eth_netif, &zero_ip);
                }

                if (g_config.connection_mode == static_cast<int>(ConnectionMode::WIFI)) {
                    restoreWifiDnsAndDefaultRoute();
                }
                break;
            }

            default:
                break;
        }
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
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onIpEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &onEthEvent, nullptr));

    bool ethPreferred = (g_config.connection_mode == static_cast<int>(ConnectionMode::ETHERNET));

    bool ethStarted = false;
    if (s_ethDriver != nullptr) {
        ESP_LOGI(TAG, "Memulai Ethernet Controller W5500 SPI DMA (Dual-Interface / Maintenance Ready)...");

        esp_ip4_addr_t ip = {}, gw = {}, mask = {}, dns = {}, dns2 = {};
        bool use_dhcp = true;

        if (ethPreferred) {
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
                use_dhcp = false;
            }
        } else {
            // Mode Wi-Fi: Ethernet siaga sebagai DHCP client untuk Direct LAN Maintenance Port
            use_dhcp = true;
        }

        char hostname[64];
        getMdnsHostname(hostname, sizeof(hostname));

        ethStarted = s_ethDriver->begin(
            ip.addr,
            gw.addr,
            mask.addr,
            dns.addr,
            use_dhcp,
            hostname,
            dns2.addr
        );
    }

    if (!ethPreferred || !ethStarted) {
        ESP_LOGI(TAG, "Menginisialisasi Wi-Fi Station...");
        initWifiStation();
    }

    initSntp();
}

void NetworkManager::ensureWifiDriverInitialized() {
    if (!s_wifiDriverInitialized) {
        if (s_staNetif == nullptr) {
            s_staNetif = esp_netif_create_default_wifi_sta();
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

        wifi_country_t country = {};
        strncpy(country.cc, "ID", sizeof(country.cc));
        country.schan = 1;
        country.nchan = 13;
        country.max_tx_power = 20;
        country.policy = WIFI_COUNTRY_POLICY_MANUAL;
        esp_wifi_set_country(&country);

        s_wifiDriverInitialized = true;
    }
}

void NetworkManager::initWifiStation() {
    ensureWifiDriverInitialized();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {};
    strlcpy((char*)wifi_config.sta.ssid, g_config.w_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, g_config.w_password, sizeof(wifi_config.sta.password));

    // Standar Industri & Sinyal Terbaik (Zero-RAM Cost):
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.rssi = -85;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_err_t bw_err = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    if (bw_err != ESP_OK) {
        ESP_LOGW(TAG, "Gagal mengatur bandwidth Wi-Fi HT20: %s", esp_err_to_name(bw_err));
    }
    esp_wifi_set_max_tx_power(78);
    ESP_LOGI(TAG, "Wi-Fi PS: None | Bandwidth: HT20 (20MHz) | Max TX Power: 78 (19.5 dBm)");
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

    Bsp::LedManager::setConfigMode(true);

    if (s_apNetif == nullptr) {
        s_apNetif = esp_netif_create_default_wifi_ap();
    }
    ensureWifiDriverInitialized();

    char moduleId[32];
    Core::ConfigStore::getModuleId(moduleId, sizeof(moduleId));

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, moduleId, sizeof(ap_config.ap.ssid));

    const char* apPass = (strlen(g_config.ap_password) >= 8) ? g_config.ap_password : "12345678";
    strncpy((char*)ap_config.ap.password, apPass, sizeof(ap_config.ap.password));
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 4;
    ap_config.ap.authmode = (strlen(apPass) >= 8) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    wifi_mode_t currentMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&currentMode);
    if (currentMode == WIFI_MODE_STA || currentMode == WIFI_MODE_APSTA) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    } else {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);
    ESP_LOGI(TAG, "SoftAP aktif: SSID='%s' | IP=192.168.4.1", ap_config.ap.ssid);

    char det[80];
    snprintf(det, sizeof(det), "Hotspot aktif: SSID %s (IP 192.168.4.1)", ap_config.ap.ssid);
    NetLogService::log(NetCategory::SOFTAP, NetEvent::STARTED, det);
}

void NetworkManager::stopConfigAP() {
    if (s_apActive) {
        ESP_LOGI(TAG, "Menonaktifkan SoftAP Fallback...");
        s_manualConfigMode = false;
        s_apActive = false;
        s_pendingApStopMillis = 0;

        NetLogService::log(NetCategory::SOFTAP, NetEvent::STOPPED, "Hotspot konfigurasi dinonaktifkan");

        if (s_ethFsmState != EthFsmState::DIRECT_PC && s_ethFsmState != EthFsmState::PEER_SLAVE) {
            Bsp::LedManager::setConfigMode(false);
        }

        bool ethPreferred = (g_config.connection_mode == static_cast<int>(ConnectionMode::ETHERNET));
        if (ethPreferred && s_ethDriver != nullptr && s_ethDriver->hasIp()) {
            esp_wifi_stop();
        } else {
            esp_wifi_set_mode(WIFI_MODE_STA);
        }
        Bsp::LedManager::autoSync(isLinkUp(), isConnected(), MqttService::isConnected(), false);
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

bool NetworkManager::isEthernetDirectPcMode() {
    return (s_ethFsmState == EthFsmState::DIRECT_PC);
}

bool NetworkManager::isEthernetPeerSlaveMode() {
    return (s_ethFsmState == EthFsmState::PEER_SLAVE);
}

bool NetworkManager::isConnected() {
    if (s_ethFsmState == EthFsmState::DIRECT_PC || s_ethFsmState == EthFsmState::PEER_SLAVE) return true;
    if (s_ethDriver && s_ethDriver->hasIp()) return true;
    return s_wifiHasIp;
}

bool NetworkManager::isLinkUp() {
    if (s_ethDriver && s_ethDriver->isConnected()) return true;
    return s_wifiConnected;
}

bool NetworkManager::isEthernetLinkUp() {
    return (s_ethDriver != nullptr && s_ethDriver->isConnected());
}

bool NetworkManager::isEthernetActive() {
    if (s_ethFsmState == EthFsmState::DIRECT_PC || s_ethFsmState == EthFsmState::PEER_SLAVE) return true;
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

void NetworkManager::getActiveMac(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';

    if (g_config.connection_mode == 1) {
        if (s_ethDriver) {
            s_ethDriver->getMac(outBuf, maxLen);
        }
        if (outBuf[0] == '\0') {
            uint8_t mac[6] = {0};
            if (esp_read_mac(mac, ESP_MAC_ETH) == ESP_OK) {
                snprintf(outBuf, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            }
        }
    } else {
        uint8_t mac[6] = {0};
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
            snprintf(outBuf, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    }
}

void NetworkManager::getWifiIp(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_staNetif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_staNetif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            esp_ip4addr_ntoa(&ip_info.ip, outBuf, maxLen);
            return;
        }
    }
    snprintf(outBuf, maxLen, "-");
}

void NetworkManager::getWifiMac(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(outBuf, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(outBuf, maxLen, "-");
    }
}

void NetworkManager::getEthernetIp(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_ethDriver && s_ethDriver->isHardwareDetected()) {
        if (!s_ethDriver->isConnected()) {
            snprintf(outBuf, maxLen, "Kabel Terputus (Link Down)");
            return;
        }
        s_ethDriver->getIp(outBuf, maxLen);
        if (strlen(outBuf) > 0 && strcmp(outBuf, "0.0.0.0") != 0) {
            return;
        }
        snprintf(outBuf, maxLen, "Menunggu IP (DHCP)...");
        return;
    }
    snprintf(outBuf, maxLen, "-");
}

void NetworkManager::getEthernetMac(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_ethDriver && s_ethDriver->isHardwareDetected()) {
        s_ethDriver->getMac(outBuf, maxLen);
        if (outBuf[0] != '\0') return;
    }
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_ETH) == ESP_OK) {
        snprintf(outBuf, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        snprintf(outBuf, maxLen, "-");
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

bool NetworkManager::isWifiScanDone() {
    return s_wifiScanDone;
}

void NetworkManager::resetWifiScanDone() {
    s_wifiScanDone = false;
    s_wifiScanSuccess = false;
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

void NetworkManager::checkEthernetFallback() {
    if (s_ethDriver == nullptr || !s_ethDriver->isConnected()) {
        return;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    esp_netif_t* eth_netif = s_ethDriver->getNetif();
    if (!eth_netif) return;

    if (s_ethFsmState == EthFsmState::PROBING) {
        // Cek jika user sengaja mengatur IP Statis di menu konfigurasi (DHCP mati) pada mode Ethernet
        if (g_config.connection_mode == static_cast<int>(ConnectionMode::ETHERNET) && !g_config.eth_dhcp && strlen(g_config.eth_ip) > 0) {
            s_ethFsmState = EthFsmState::ROUTER_LAN;
            return;
        }

        if (s_ethProbeStartMs > 0 && (now - s_ethProbeStartMs) >= s_ethProbeTimeoutMs) {
            ESP_LOGW(TAG, "DHCP Probe Timeout (%lu ms)! Memulai Fallback Direct LAN...",
                     (unsigned long)s_ethProbeTimeoutMs);

            // 1. Matikan Wi-Fi SoftAP terlebih dahulu sebelum mengubah IP Ethernet (Mencegah collision)
            if (isApActive()) {
                ESP_LOGI(TAG, "Mematikan Wi-Fi SoftAP sebelum mengaktifkan subnet LAN 192.168.4.x...");
                stopConfigAP();
            }
            vTaskDelay(pdMS_TO_TICKS(15));

            // 2. Stop DHCP client pada Ethernet
            esp_netif_dhcpc_stop(eth_netif);

            // 3. Thread-safe ARP Probe untuk mengecek apakah 192.168.4.1 sudah dipakai perangkat lain
            char if_name[16] = {0};
            esp_netif_get_netif_impl_name(eth_netif, if_name);

            bool ip_occupied = false;
            LOCK_TCPIP_CORE();
            struct netif* lwip_n = netif_find(if_name);
            if (lwip_n != nullptr) {
                ip4_addr_t target_ip;
                IP4_ADDR(&target_ip, 192, 168, 4, 1);
                etharp_request(lwip_n, &target_ip);
            }
            UNLOCK_TCPIP_CORE();

            vTaskDelay(pdMS_TO_TICKS(200));

            LOCK_TCPIP_CORE();
            if (lwip_n != nullptr) {
                ip4_addr_t target_ip;
                IP4_ADDR(&target_ip, 192, 168, 4, 1);
                struct eth_addr* eth_ret = nullptr;
                const ip4_addr* ip_ret = nullptr;
                s8_t idx = etharp_find_addr(lwip_n, &target_ip, &eth_ret, &ip_ret);
                ip_occupied = (idx >= 0);
            }
            UNLOCK_TCPIP_CORE();

            // 4. Set LED COM: Biru Kedip 1 Hz (Mode Konfigurasi Aktif)
            Bsp::LedManager::setConfigMode(true);

            if (!ip_occupied) {
                // MASTER NODE: Ambil 192.168.4.1 dan jalankan Micro-DHCP
                ESP_LOGI(TAG, "192.168.4.1 bebas -> SG-10 terpilih sebagai MASTER (Direct PC Mode)!");
                s_ethFsmState = EthFsmState::DIRECT_PC;
                s_directPcStartMs = now;

                esp_netif_ip_info_t ip_info = {};
                IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
                IP4_ADDR(&ip_info.gw, 0, 0, 0, 0); // Omit gateway
                IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
                esp_netif_set_ip_info(eth_netif, &ip_info);

                // Pada mode Wi-Fi, pastikan default route internet tetap berada pada Wi-Fi Station & pulihkan DNS lwIP
                if (g_config.connection_mode == static_cast<int>(ConnectionMode::WIFI)) {
                    restoreWifiDnsAndDefaultRoute();
                }

                MicroDhcpServer::start(eth_netif, 0xC0A80401, 0xC0A80402);

                NetLogService::log(NetCategory::ETHERNET, NetEvent::STARTED,
                                   "Direct LAN Terhubung (Master: 192.168.4.1, Micro-DHCP Aktif)");
            } else {
                // SLAVE / PEER NODE: Tetapkan IP unik berbasis MAC
                uint8_t mac[6] = {0};
                esp_read_mac(mac, ESP_MAC_ETH);
                uint8_t host_octet = 10 + (mac[5] % 240);
                if (host_octet == 1 || host_octet == 2) host_octet += 10;

                ESP_LOGW(TAG, "192.168.4.1 terdeteksi sibuk -> Menjadi SLAVE PEER dengan IP 192.168.4.%d", host_octet);
                s_ethFsmState = EthFsmState::PEER_SLAVE;
                s_slaveLastMasterCheckMs = now;

                esp_netif_ip_info_t ip_info = {};
                IP4_ADDR(&ip_info.ip, 192, 168, 4, host_octet);
                IP4_ADDR(&ip_info.gw, 0, 0, 0, 0);
                IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
                esp_netif_set_ip_info(eth_netif, &ip_info);

                if (g_config.connection_mode == static_cast<int>(ConnectionMode::WIFI)) {
                    restoreWifiDnsAndDefaultRoute();
                }

                char det[80];
                snprintf(det, sizeof(det), "Hub Terdeteksi (Slave Peer: 192.168.4.%d, Master 192.168.4.1)", host_octet);
                NetLogService::log(NetCategory::ETHERNET, NetEvent::STARTED, det);
            }

            // Inisialisasi mDNS pada IP baru
            initMdns();
        }
    } else if (s_ethFsmState == EthFsmState::DIRECT_PC) {
        // Pada mode Ethernet: jika belum ada PC yang mengambil lease setelah 30 detik, re-probe router kantor
        // Pada mode Wi-Fi: Direct PC tetap aktif selama kabel LAN terhubung (tanpa time-out 30s)
        bool ethPreferred = (g_config.connection_mode == static_cast<int>(ConnectionMode::ETHERNET));
        if (ethPreferred && !MicroDhcpServer::hasActiveClientLease()) {
            if (s_directPcStartMs > 0 && (now - s_directPcStartMs) >= 30000) {
                ESP_LOGI(TAG, "Direct PC idle 30s tanpa klien -> Re-probe router kantor...");
                MicroDhcpServer::stop();
                Bsp::LedManager::setConfigMode(false);

                // Reset IP ke 0.0.0.0 sebelum menyalakan kembali DHCP client
                esp_netif_ip_info_t zero_ip = {};
                esp_netif_set_ip_info(eth_netif, &zero_ip);

                esp_netif_dhcpc_start(eth_netif);

                uint8_t mac[6] = {0};
                esp_read_mac(mac, ESP_MAC_ETH);
                uint32_t jitter = ((mac[4] << 8) | mac[5]) % 2000;
                s_ethProbeTimeoutMs = 11500 + jitter;
                s_ethProbeStartMs = now;
                s_ethFsmState = EthFsmState::PROBING;
            }
        }
    } else if (s_ethFsmState == EthFsmState::PEER_SLAVE) {
        // Cek heartbeat Master setiap 30 detik: jika Master sudah pergi, kembali ke PROBING
        if (s_slaveLastMasterCheckMs > 0 && (now - s_slaveLastMasterCheckMs) >= 30000) {
            s_slaveLastMasterCheckMs = now;

            char if_name[16] = {0};
            esp_netif_get_netif_impl_name(eth_netif, if_name);

            bool master_alive = false;
            LOCK_TCPIP_CORE();
            struct netif* lwip_n = netif_find(if_name);
            if (lwip_n != nullptr) {
                ip4_addr_t master_ip;
                IP4_ADDR(&master_ip, 192, 168, 4, 1);
                etharp_request(lwip_n, &master_ip);
            }
            UNLOCK_TCPIP_CORE();

            vTaskDelay(pdMS_TO_TICKS(150));

            LOCK_TCPIP_CORE();
            if (lwip_n != nullptr) {
                ip4_addr_t master_ip;
                IP4_ADDR(&master_ip, 192, 168, 4, 1);
                struct eth_addr* eth_ret = nullptr;
                const ip4_addr* ip_ret = nullptr;
                s8_t idx = etharp_find_addr(lwip_n, &master_ip, &eth_ret, &ip_ret);
                master_alive = (idx >= 0);
            }
            UNLOCK_TCPIP_CORE();

            if (!master_alive) {
                ESP_LOGW(TAG, "Master 192.168.4.1 tidak lagi merespons -> Slave kembali ke PROBING!");
                Bsp::LedManager::setConfigMode(false);

                esp_netif_ip_info_t zero_ip = {};
                esp_netif_set_ip_info(eth_netif, &zero_ip);

                esp_netif_dhcpc_start(eth_netif);

                uint8_t mac[6] = {0};
                esp_read_mac(mac, ESP_MAC_ETH);
                uint32_t jitter = ((mac[4] << 8) | mac[5]) % 2000;
                s_ethProbeTimeoutMs = 11500 + jitter;
                s_ethProbeStartMs = now;
                s_ethFsmState = EthFsmState::PROBING;
            }
        }
    }
}

void NetworkManager::tickWatchdog() {
    checkNtpWatchdog();
    checkDisconWatchdog();
    checkApInactivityTimeout();
    checkDailyRestart();
    checkEthernetFallback();
}

} // namespace Iotaru::Network
