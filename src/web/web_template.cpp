#include "web_template.hpp"
#include "emergency_page.hpp"
#include "web_server.hpp"
#include "app_config.hpp"
#include "config_store.hpp"
#include "net_manager.hpp"
#include "mqtt_service.hpp"
#include "IEthernetDriver.hpp"
#include <esp_wifi.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace Iotaru::Web::Template {

static char s_ui_version[32] = "v1.0.0";

void init() {
    FILE *f = fopen("/storage/version.json", "r");
    if (f) {
        char buf[128] = {0};
        size_t r = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (r > 0) {
            const char *pos = strstr(buf, "\"version\"");
            if (pos) {
                pos = strchr(pos, ':');
                if (pos) {
                    pos = strchr(pos, '"');
                    if (pos) {
                        pos++;
                        const char *end = strchr(pos, '"');
                        if (end) {
                            size_t len = static_cast<size_t>(end - pos);
                            if (len < sizeof(s_ui_version)) {
                                memcpy(s_ui_version, pos, len);
                                s_ui_version[len] = '\0';
                            }
                        }
                    }
                }
            }
        }
    }
}

const char* get_ui_version() {
    return s_ui_version;
}

static void html_escape(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    size_t out_idx = 0;
    while (*src && (out_idx + 1 < max_len)) {
        const char *rep = nullptr;
        size_t rep_len = 0;
        switch (*src) {
            case '&': rep = "&amp;"; rep_len = 5; break;
            case '<': rep = "&lt;"; rep_len = 4; break;
            case '>': rep = "&gt;"; rep_len = 4; break;
            case '"': rep = "&quot;"; rep_len = 6; break;
            case '\'': rep = "&#39;"; rep_len = 5; break;
            default: break;
        }
        if (rep) {
            if (out_idx + rep_len < max_len) {
                memcpy(dst + out_idx, rep, rep_len);
                out_idx += rep_len;
            } else {
                break;
            }
        } else {
            dst[out_idx++] = *src;
        }
        src++;
    }
    dst[out_idx] = '\0';
}

bool get_placeholder_value(std::string_view key, char *out_buf, size_t max_len) {
    if (!out_buf || max_len == 0) return false;
    out_buf[0] = '\0';

    if (key == "model" || key == "device_model") {
        html_escape(g_config.device_model[0] != '\0' ? g_config.device_model : "EMPTY", out_buf, max_len);
    } else if (key == "ap_ssid") {
        Core::ConfigStore::getModuleId(out_buf, max_len);
    } else if (key == "ap_banner_active") {
        snprintf(out_buf, max_len, "%s", Network::NetworkManager::isApActive() ? "1" : "0");
    } else if (key == "device_id") {
        Core::ConfigStore::getDeviceId(out_buf, max_len);
    } else if (key == "mdns_hostname") {
        Network::NetworkManager::getMdnsHostname(out_buf, max_len);
    } else if (key == "device_version") {
        html_escape(g_config.device_version, out_buf, max_len);
    } else if (key == "firmware_version") {
        snprintf(out_buf, max_len, "%s", FIRMWARE_VERSION);
    } else if (key == "ui_version") {
        snprintf(out_buf, max_len, "%s", s_ui_version);
    } else if (key == "ip_address") {
        Network::NetworkManager::getActiveIp(out_buf, max_len);
    } else if (key == "mac_address") {
        Network::NetworkManager::getActiveMac(out_buf, max_len);
    } else if (key == "wifi_ip") {
        Network::NetworkManager::getWifiIp(out_buf, max_len);
    } else if (key == "wifi_mac") {
        Network::NetworkManager::getWifiMac(out_buf, max_len);
    } else if (key == "eth_ip") {
        Network::NetworkManager::getEthernetIp(out_buf, max_len);
    } else if (key == "eth_mac") {
        Network::NetworkManager::getEthernetMac(out_buf, max_len);
    } else if (key == "eth_tiles_style") {
        bool eth_hw = Network::NetworkManager::isEthernetHardwareDetected();
        snprintf(out_buf, max_len, "%s", eth_hw ? "" : "display:none;");
    } else if (key == "mac_label") {
        snprintf(out_buf, max_len, (g_config.connection_mode == 1 || Network::NetworkManager::isEthernetDirectPcMode()) ? "MAC Address (LAN)" : "MAC Address (Wi-Fi)");
    } else if (key == "connection_mode") {
        snprintf(out_buf, max_len, "%d", g_config.connection_mode);
    } else if (key == "net_status") {
        bool is_conn = Network::NetworkManager::isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode()) {
            if (g_config.connection_mode == 0 && is_conn) {
                snprintf(out_buf, max_len, "Wi-Fi + Direct LAN (192.168.4.1)");
            } else {
                snprintf(out_buf, max_len, "Ethernet (Direct PC 192.168.4.1)");
            }
        } else if (Network::NetworkManager::isEthernetPeerSlaveMode()) {
            char ipBuf[32];
            Network::NetworkManager::getActiveIp(ipBuf, sizeof(ipBuf));
            snprintf(out_buf, max_len, "Ethernet (Peer Slave %s)", ipBuf);
        } else if (g_config.connection_mode == 1) {
            auto *drv = Network::NetworkManager::getEthernetDriver();
            if (is_conn && drv) {
                snprintf(out_buf, max_len, "Ethernet (%uM %s)",
                         static_cast<unsigned>(drv->linkSpeed()),
                         drv->isFullDuplex() ? "Full" : "Half");
            } else {
                snprintf(out_buf, max_len, "Ethernet (Disconnected)");
            }
        } else {
            snprintf(out_buf, max_len, "Wi-Fi (%s)", is_conn ? "Connected" : "Disconnected");
        }
    } else if (key == "net_badge_class") {
        if (Network::NetworkManager::isEthernetDirectPcMode() || Network::NetworkManager::isEthernetPeerSlaveMode()) {
            snprintf(out_buf, max_len, "direct");
        } else {
            snprintf(out_buf, max_len, "%s", Network::NetworkManager::isConnected() ? "conn" : "offline");
        }
    } else if (key == "signal_spec") {
        if (g_config.connection_mode == 1) {
            auto *drv = Network::NetworkManager::getEthernetDriver();
            if (drv && drv->isConnected()) {
                snprintf(out_buf, max_len, "%u Mbps (%s Duplex)",
                         static_cast<unsigned>(drv->linkSpeed()),
                         drv->isFullDuplex() ? "Full" : "Half");
            } else {
                snprintf(out_buf, max_len, "Cable Disconnected (Link Down)");
            }
        } else {
            if (Network::NetworkManager::isConnected()) {
                wifi_ap_record_t info;
                if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
                    snprintf(out_buf, max_len, "%d dBm", info.rssi);
                } else {
                    snprintf(out_buf, max_len, "-");
                }
            } else {
                snprintf(out_buf, max_len, "-");
            }
        }
    } else if (key == "uptime_sec") {
        uint64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
        snprintf(out_buf, max_len, "%llu", (unsigned long long)uptime_sec);
    } else if (key == "uptime_short") {
        uint64_t s = esp_timer_get_time() / 1000000ULL;
        unsigned days = s / 86400;
        unsigned hours = (s % 86400) / 3600;
        unsigned mins = (s % 3600) / 60;
        snprintf(out_buf, max_len, "%ud %02uh %02um", days, hours, mins);
    } else if (key == "uptime_detail") {
        uint64_t s = esp_timer_get_time() / 1000000ULL;
        unsigned days = s / 86400;
        unsigned hours = (s % 86400) / 3600;
        unsigned mins = (s % 3600) / 60;
        unsigned secs = s % 60;
        snprintf(out_buf, max_len, "%u %s %02u:%02u:%02u", days, days == 1 ? "Day" : "Days", hours, mins, secs);
    } else if (key == "free_heap_kb") {
        snprintf(out_buf, max_len, "%lu", (unsigned long)(esp_get_free_heap_size() / 1024));
    } else if (key == "net_bandwidth") {
        snprintf(out_buf, max_len, "%.1f", Network::MqttService::getBandwidthKBps());
    } else if (key == "header_mode_pill_class") {
        if (Network::NetworkManager::isEthernetDirectPcMode() || Network::NetworkManager::isEthernetPeerSlaveMode()) {
            snprintf(out_buf, max_len, "mode-smart");
        } else if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "mode-eth");
        } else {
            snprintf(out_buf, max_len, "mode-wifi");
        }
    } else if (key == "network_mode_desc") {
        if (Network::NetworkManager::isEthernetDirectPcMode() || Network::NetworkManager::isEthernetPeerSlaveMode()) {
            snprintf(out_buf, max_len, "Direct LAN Active — Technician direct access via IP 192.168.4.1");
        } else if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, Network::NetworkManager::isConnected() ? "Ethernet Mode (LAN) — Physical cable active as primary uplink" : "Ethernet Mode (LAN) — Waiting for physical cable link");
        } else {
            snprintf(out_buf, max_len, Network::NetworkManager::isConnected() ? "Wi-Fi Standalone Mode — Wireless link active as primary uplink" : "Wi-Fi Standalone Mode — Waiting for Wi-Fi connection");
        }
    } else if (key == "conn_mode_text") {
        if (Network::NetworkManager::isEthernetDirectPcMode() || Network::NetworkManager::isEthernetPeerSlaveMode()) {
            snprintf(out_buf, max_len, "SMART CONNECT");
        } else if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "ETHERNET");
        } else {
            snprintf(out_buf, max_len, "WI-FI");
        }
    } else if (key == "conn_mode_class") {
        if (Network::NetworkManager::isEthernetDirectPcMode() || Network::NetworkManager::isEthernetPeerSlaveMode()) {
            snprintf(out_buf, max_len, "mode-smart");
        } else if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "mode-eth");
        } else {
            snprintf(out_buf, max_len, "mode-wifi");
        }
    } else if (key == "conn_mqtt_class") {
        snprintf(out_buf, max_len, Network::MqttService::isConnected() ? "mqtt-online" : "mqtt-offline");
    } else if (key == "wifi_signal_spec") {
        if (Network::NetworkManager::isConnected() && g_config.connection_mode == 0) {
            wifi_ap_record_t info;
            if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
                snprintf(out_buf, max_len, "%d dBm", info.rssi);
            } else {
                snprintf(out_buf, max_len, "-");
            }
        } else if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "N/A (Radio Off)");
        } else {
            snprintf(out_buf, max_len, "-");
        }
    } else if (key == "wifi_role_label") {
        if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "Radio Inactive (LAN Mode)");
        } else if (Network::NetworkManager::isConnected()) {
            snprintf(out_buf, max_len, "Primary Link (WLAN)");
        } else {
            snprintf(out_buf, max_len, "Wi-Fi Disconnected");
        }
    } else if (key == "wifi_role_class") {
        if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "slate");
        } else if (Network::NetworkManager::isConnected()) {
            snprintf(out_buf, max_len, "emerald");
        } else {
            snprintf(out_buf, max_len, "amber");
        }
    } else if (key == "wifi_status_text") {
        if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "Disabled");
        } else if (Network::NetworkManager::isConnected()) {
            snprintf(out_buf, max_len, "Active");
        } else {
            snprintf(out_buf, max_len, "Offline");
        }
    } else if (key == "wifi_status_class") {
        if (g_config.connection_mode == 1) {
            snprintf(out_buf, max_len, "slate");
        } else if (Network::NetworkManager::isConnected()) {
            snprintf(out_buf, max_len, "active");
        } else {
            snprintf(out_buf, max_len, "amber");
        }
    } else if (key == "wifi_card_class") {
        snprintf(out_buf, max_len, (g_config.connection_mode != 1 && Network::NetworkManager::isConnected()) ? "active-online" : "");
    } else if (key == "eth_role_label") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode()) {
            snprintf(out_buf, max_len, "Direct LAN / Maintenance Port");
        } else if (link_up) {
            snprintf(out_buf, max_len, "Router LAN Connected");
        } else {
            snprintf(out_buf, max_len, "Secondary / LAN Port");
        }
    } else if (key == "eth_role_class") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode() || link_up) {
            snprintf(out_buf, max_len, "emerald");
        } else {
            snprintf(out_buf, max_len, "slate");
        }
    } else if (key == "eth_status_text") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode()) {
            snprintf(out_buf, max_len, "Direct PC");
        } else if (link_up) {
            snprintf(out_buf, max_len, "Active");
        } else {
            snprintf(out_buf, max_len, "Link Down");
        }
    } else if (key == "eth_status_class") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode() || link_up) {
            snprintf(out_buf, max_len, "active");
        } else {
            snprintf(out_buf, max_len, "amber");
        }
    } else if (key == "eth_card_class") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode() || link_up) {
            snprintf(out_buf, max_len, "active-online");
        } else {
            snprintf(out_buf, max_len, "link-down");
        }
    } else if (key == "eth_ip_display") {
        char eth_ip_buf[32];
        Network::NetworkManager::getEthernetIp(eth_ip_buf, sizeof(eth_ip_buf));
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode()) {
            snprintf(out_buf, max_len, "192.168.4.1");
        } else if (link_up && eth_ip_buf[0] != '\0') {
            snprintf(out_buf, max_len, "%s", eth_ip_buf);
        } else {
            snprintf(out_buf, max_len, "— (Unassigned)");
        }
    } else if (key == "eth_ip_class") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode() || link_up) {
            snprintf(out_buf, max_len, "emerald");
        } else {
            snprintf(out_buf, max_len, "unassigned");
        }
    } else if (key == "eth_copy_style") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        snprintf(out_buf, max_len, (Network::NetworkManager::isEthernetDirectPcMode() || link_up) ? "display:inline-flex;" : "display:none;");
    } else if (key == "eth_footer_label") {
        snprintf(out_buf, max_len, "Physical Link");
    } else if (key == "eth_footer_value") {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        bool link_up = drv && drv->isConnected();
        if (Network::NetworkManager::isEthernetDirectPcMode()) {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge active\">100M FD • Micro-DHCP</span>");
        } else if (link_up) {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge active\">%u Mbps %s-Duplex</span>",
                     static_cast<unsigned>(drv->linkSpeed()),
                     drv->isFullDuplex() ? "Full" : "Half");
        } else {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-link-text amber\">Cable Disconnected</span>");
        }
    } else if (key == "mqtt_status") {
        snprintf(out_buf, max_len, "%s", Network::MqttService::isConnected() ? "Connected" : "Disconnected");
    } else if (key == "mqtt_badge_class") {
        snprintf(out_buf, max_len, "%s", Network::MqttService::isConnected() ? "mqtt-online" : "mqtt-offline");
    } else if (key == "mqtt_broker_host") {
        if (g_config.mqtt_host[0] != '\0') {
            html_escape(g_config.mqtt_host, out_buf, max_len);
        } else {
            snprintf(out_buf, max_len, "—");
        }
    } else if (key == "mqtt_broker_port") {
        snprintf(out_buf, max_len, "%d", g_config.mqtt_port);
    } else if (key == "mqtt_tls_badge") {
        if (g_config.mqtt_use_ssl == 1) {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge active\"><svg width=\"11\" height=\"11\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" style=\"vertical-align:-1px;margin-right:3px;\"><rect x=\"3\" y=\"11\" width=\"18\" height=\"11\" rx=\"2\" ry=\"2\"/><path d=\"M7 11V7a5 5 0 0 1 10 0v4\"/></svg>TLS Active</span>");
        } else {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge slate\"><svg width=\"11\" height=\"11\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" style=\"vertical-align:-1px;margin-right:3px;\"><rect x=\"3\" y=\"11\" width=\"18\" height=\"11\" rx=\"2\" ry=\"2\"/><path d=\"M7 11V7a5 5 0 0 1 9.9 -1\"/></svg>Plain TCP</span>");
        }
    } else if (key == "mqtt_qos_badge") {
        if (g_config.mqtt_qos == 0) {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge amber\">QoS 0</span>");
        } else {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge active\">QoS 1</span>");
        }
    } else if (key == "mqtt_conn_badge") {
        if (Network::MqttService::isConnected()) {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge active\">● Connected</span>");
        } else {
            snprintf(out_buf, max_len, "<span class=\"sg-subcard-status-badge amber\">● Disconnected</span>");
        }
    } else if (key == "mqtt_card_class") {
        snprintf(out_buf, max_len, Network::MqttService::isConnected() ? "active-online" : "");
    } else if (key == "mqtt_icon_class") {
        snprintf(out_buf, max_len, Network::MqttService::isConnected() ? "emerald" : "slate");
    } else if (key == "mqtt_role_label") {
        snprintf(out_buf, max_len, Network::MqttService::isConnected() ? "Broker Connected — Beacon Uplink Active" : "Waiting for IoT Broker Connection");
    } else if (key == "user") {
        snprintf(out_buf, max_len, "Administrator");
    } else if (key == "admin_user") {
        html_escape(g_config.admin_user, out_buf, max_len);
    } else if (key == "admin_pass") {
        snprintf(out_buf, max_len, "••••••••");
    } else if (key == "user_user") {
        html_escape(g_config.user_user, out_buf, max_len);
    } else if (key == "user_pass") {
        snprintf(out_buf, max_len, "••••••••");
    } else if (key == "w_ssid") {
        html_escape(g_config.w_ssid, out_buf, max_len);
    } else if (key == "w_password") {
        snprintf(out_buf, max_len, "••••••••");
    } else if (key == "w_ip") {
        html_escape(g_config.w_ip, out_buf, max_len);
    } else if (key == "w_gateway") {
        html_escape(g_config.w_gateway, out_buf, max_len);
    } else if (key == "w_subnet") {
        html_escape(g_config.w_subnet, out_buf, max_len);
    } else if (key == "w_dns") {
        html_escape(g_config.w_dns, out_buf, max_len);
    } else if (key == "w_dns2") {
        html_escape(g_config.w_dns2, out_buf, max_len);
    } else if (key == "eth_ip") {
        html_escape(g_config.eth_ip, out_buf, max_len);
    } else if (key == "eth_gateway") {
        html_escape(g_config.eth_gateway, out_buf, max_len);
    } else if (key == "eth_subnet") {
        html_escape(g_config.eth_subnet, out_buf, max_len);
    } else if (key == "eth_dns") {
        html_escape(g_config.eth_dns, out_buf, max_len);
    } else if (key == "eth_dns2") {
        html_escape(g_config.eth_dns2, out_buf, max_len);
    } else if (key == "ap_password") {
        snprintf(out_buf, max_len, "••••••••");
    } else if (key == "mqtt_host") {
        html_escape(g_config.mqtt_host, out_buf, max_len);
    } else if (key == "mqtt_port") {
        snprintf(out_buf, max_len, "%d", g_config.mqtt_port);
    } else if (key == "mqtt_client_id") {
        Core::ConfigStore::getMqttClientId(out_buf, max_len);
    } else if (key == "id_module") {
        Core::ConfigStore::getModuleId(out_buf, max_len);
    } else if (key == "mqtt_user") {
        html_escape(g_config.mqtt_user, out_buf, max_len);
    } else if (key == "mqtt_password") {
        snprintf(out_buf, max_len, "••••••••");
    } else if (key == "mqtt_use_ssl") {
        snprintf(out_buf, max_len, "%s", g_config.mqtt_use_ssl ? "1" : "0");
    } else if (key == "mqtt_ssl_checked") {
        snprintf(out_buf, max_len, "%s", g_config.mqtt_use_ssl ? "checked" : "");
    } else if (key == "mqtt_qos") {
        snprintf(out_buf, max_len, "%d", g_config.mqtt_qos);
    } else if (key == "mqtt_qos_0_selected") {
        snprintf(out_buf, max_len, "%s", (g_config.mqtt_qos == 0) ? "selected" : "");
    } else if (key == "mqtt_qos_1_selected") {
        snprintf(out_buf, max_len, "%s", (g_config.mqtt_qos != 0) ? "selected" : "");
    } else if (key == "mqtt_keepalive") {
        snprintf(out_buf, max_len, "%d", (g_config.mqtt_keepalive >= 5 && g_config.mqtt_keepalive <= 65535) ? g_config.mqtt_keepalive : 30);
    } else if (key == "filter_uuid_en") {
        snprintf(out_buf, max_len, "%s", g_config.filter_uuid_en ? "1" : "0");
    } else if (key == "filter_uuid_checked") {
        snprintf(out_buf, max_len, "%s", g_config.filter_uuid_en ? "checked" : "");
    } else if (key == "uuid_container_dimmed") {
        snprintf(out_buf, max_len, "%s", g_config.filter_uuid_en ? "" : "dimmed");
    } else if (key == "filter_uuid_disabled") {
        snprintf(out_buf, max_len, "%s", g_config.filter_uuid_en ? "" : "disabled");
    } else if (key == "filter_uuid") {
        html_escape(g_config.filter_uuid, out_buf, max_len);
    } else if (key == "uuid_badge_class") {
        size_t ulen = strlen(g_config.filter_uuid);
        snprintf(out_buf, max_len, "%s", ulen == 32 ? "uuid-badge valid" : (ulen == 0 ? "uuid-badge neutral" : "uuid-badge invalid"));
    } else if (key == "uuid_badge_text") {
        size_t ulen = strlen(g_config.filter_uuid);
        if (ulen == 32) snprintf(out_buf, max_len, "32/32 Hex Valid");
        else if (ulen == 0) snprintf(out_buf, max_len, "0/32 Hex");
        else snprintf(out_buf, max_len, "%u/32 Hex", static_cast<unsigned>(ulen));
    } else if (key == "filter_major_en") {
        snprintf(out_buf, max_len, "%s", g_config.filter_major_en ? "1" : "0");
    } else if (key == "filter_major_checked") {
        snprintf(out_buf, max_len, "%s", g_config.filter_major_en ? "checked" : "");
    } else if (key == "major_container_dimmed") {
        snprintf(out_buf, max_len, "%s", g_config.filter_major_en ? "" : "dimmed");
    } else if (key == "filter_major_disabled") {
        snprintf(out_buf, max_len, "%s", g_config.filter_major_en ? "" : "disabled");
    } else if (key == "filter_major") {
        html_escape(g_config.filter_major, out_buf, max_len);
    } else if (key == "major_badge_class") {
        size_t mlen = strlen(g_config.filter_major);
        if (mlen == 0) {
            snprintf(out_buf, max_len, "uuid-badge neutral");
        } else {
            long val = strtol(g_config.filter_major, nullptr, 10);
            snprintf(out_buf, max_len, "%s", (val >= 0 && val <= 65535) ? "uuid-badge valid" : "uuid-badge invalid");
        }
    } else if (key == "major_badge_text") {
        size_t mlen = strlen(g_config.filter_major);
        if (mlen == 0) {
            snprintf(out_buf, max_len, "0 - 65535");
        } else {
            long val = strtol(g_config.filter_major, nullptr, 10);
            if (val >= 0 && val <= 65535) {
                snprintf(out_buf, max_len, "Major %ld Valid", val);
            } else {
                snprintf(out_buf, max_len, "Maks 65535");
            }
        }
    } else if (key == "rssi_min") {
        snprintf(out_buf, max_len, "%d", g_config.rssi_min);
    } else if (key == "rssi_val_abs" || key == "rssi_slider_val") {
        int abs_val = abs(g_config.rssi_min);
        if (abs_val < 30) abs_val = 30;
        if (abs_val > 100) abs_val = 100;
        snprintf(out_buf, max_len, "%d", abs_val);
    } else if (key == "rssi_slider_bg") {
        int abs_val = abs(g_config.rssi_min);
        if (abs_val < 30) abs_val = 30;
        if (abs_val > 100) abs_val = 100;
        int pct = static_cast<int>(((abs_val - 30.0f) / 70.0f) * 100.0f);
        snprintf(out_buf, max_len,
                 "linear-gradient(to right, #0284c7 0%%, #38bdf8 %d%%, #1e293b %d%%, #1e293b 100%%)",
                 pct, pct);
    } else if (key == "rssi_cat_class") {
        int abs_val = abs(g_config.rssi_min);
        if (abs_val <= 60) snprintf(out_buf, max_len, "rssi-cat-badge near");
        else if (abs_val <= 80) snprintf(out_buf, max_len, "rssi-cat-badge mid");
        else if (abs_val <= 95) snprintf(out_buf, max_len, "rssi-cat-badge far");
        else snprintf(out_buf, max_len, "rssi-cat-badge max");
    } else if (key == "rssi_cat_text") {
        int abs_val = abs(g_config.rssi_min);
        if (abs_val <= 60) snprintf(out_buf, max_len, "Kuat / Dekat");
        else if (abs_val <= 80) snprintf(out_buf, max_len, "Standar");
        else if (abs_val <= 95) snprintf(out_buf, max_len, "Luas / Jauh");
        else snprintf(out_buf, max_len, "Maksimal");
    } else if (key == "batch_max") {
        snprintf(out_buf, max_len, "%d", g_config.batch_max);
    } else if (key == "batch_flush") {
        snprintf(out_buf, max_len, "%d", g_config.batch_flush);
    } else if (key == "data_ttl") {
        snprintf(out_buf, max_len, "%d", g_config.data_ttl);
    } else if (key == "restart_schedule") {
        html_escape(g_config.restart_schedule, out_buf, max_len);
    } else if (key == "restart_sch_checked") {
        snprintf(out_buf, max_len, "%s", strlen(g_config.restart_schedule) >= 5 ? "checked" : "");
    } else if (key == "restart_sch_container_dimmed") {
        snprintf(out_buf, max_len, "%s", strlen(g_config.restart_schedule) >= 5 ? "" : "dimmed");
    } else if (key == "discon_rst") {
        snprintf(out_buf, max_len, "%d", g_config.discon_rst);
    } else if (key == "ntp_server") {
        html_escape(g_config.ntp_server, out_buf, max_len);
    } else {
        return false;
    }

    return true;
}

esp_err_t stream_emergency_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    const char *src = EMERGENCY_RECOVERY_HTML;
    char chunk[2048];
    size_t chunk_idx = 0;

    char ip_str[32] = {0};
    Network::NetworkManager::getActiveIp(ip_str, sizeof(ip_str));

    char escaped_model[64] = {0};
    html_escape((g_config.device_model[0] != '\0') ? g_config.device_model : "EMPTY", escaped_model, sizeof(escaped_model));
    char escaped_dev_id[64] = {0};
    html_escape(g_deviceId, escaped_dev_id, sizeof(escaped_dev_id));

    const char *net_mode = (g_config.connection_mode == 1) ? "Ethernet" : "Wi-Fi";
    const char *mqtt_stat = Network::MqttService::isConnected() ? "Terhubung (Online)" : "Terputus (Offline)";

    auto flush_chunk = [&](bool is_final = false) -> esp_err_t {
        if (chunk_idx > 0) {
            esp_err_t err = httpd_resp_send_chunk(req, chunk, chunk_idx);
            if (err != ESP_OK) return err;
            chunk_idx = 0;
        }
        if (is_final) {
            return httpd_resp_send_chunk(req, nullptr, 0);
        }
        return ESP_OK;
    };

    auto append_str = [&](const char *s) -> esp_err_t {
        size_t slen = strlen(s);
        for (size_t i = 0; i < slen; i++) {
            chunk[chunk_idx++] = s[i];
            if (chunk_idx >= sizeof(chunk) - 64) {
                esp_err_t err = flush_chunk();
                if (err != ESP_OK) return err;
            }
        }
        return ESP_OK;
    };

    while (*src) {
        if (*src == '%') {
            const char *end = strchr(src + 1, '%');
            if (end && (end - src < 40)) {
                size_t klen = static_cast<size_t>(end - (src + 1));
                char key[40];
                memcpy(key, src + 1, klen);
                key[klen] = '\0';

                if (strcmp(key, "DEVICE_MODEL") == 0) {
                    append_str(escaped_model);
                } else if (strcmp(key, "DEVICE_ID") == 0) {
                    append_str(escaped_dev_id);
                } else if (strcmp(key, "FIRMWARE_VERSION") == 0) {
                    append_str(FIRMWARE_VERSION);
                } else if (strcmp(key, "IP_ADDRESS") == 0) {
                    append_str(ip_str);
                } else if (strcmp(key, "NET_MODE") == 0) {
                    append_str(net_mode);
                } else if (strcmp(key, "MQTT_STATUS") == 0) {
                    append_str(mqtt_stat);
                } else {
                    chunk[chunk_idx++] = '%';
                    src++;
                    continue;
                }
                src = end + 1;
                continue;
            }
        }

        chunk[chunk_idx++] = *src++;
        if (chunk_idx >= sizeof(chunk) - 64) {
            esp_err_t err = flush_chunk();
            if (err != ESP_OK) return err;
        }
    }

    return flush_chunk(true);
}

esp_err_t stream_template_file(httpd_req_t *req, const char *filepath) {
    struct FileReadGuard {
        bool acquired;
        FileReadGuard() : acquired(Server::acquire_file_read()) {}
        ~FileReadGuard() {
            if (acquired) Server::release_file_read();
        }
    } file_guard;

    if (!file_guard.acquired) {
        return stream_emergency_page(req);
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        return stream_emergency_page(req);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    char in_buf[1024];
    char out_buf[2048];
    size_t out_idx = 0;

    auto flush_out = [&](bool is_final = false) -> esp_err_t {
        if (out_idx > 0) {
            esp_err_t err = httpd_resp_send_chunk(req, out_buf, out_idx);
            if (err != ESP_OK) return err;
            out_idx = 0;
        }
        if (is_final) {
            return httpd_resp_send_chunk(req, nullptr, 0);
        }
        return ESP_OK;
    };

    auto append_val = [&](const char *s) -> esp_err_t {
        size_t slen = strlen(s);
        for (size_t i = 0; i < slen; i++) {
            out_buf[out_idx++] = s[i];
            if (out_idx >= sizeof(out_buf) - 128) {
                esp_err_t err = flush_out();
                if (err != ESP_OK) return err;
            }
        }
        return ESP_OK;
    };

    char lookahead[64] = {0};
    size_t lookahead_len = 0;
    size_t flush_count = 0;

    while (true) {
        size_t bytes_read = fread(in_buf, 1, sizeof(in_buf), f);
        if (bytes_read == 0) break;

        for (size_t i = 0; i < bytes_read; i++) {
            char c = in_buf[i];
            if (lookahead_len > 0) {
                if (c == '%') {
                    lookahead[lookahead_len] = '\0';
                    char val[512];
                    if (get_placeholder_value(std::string_view(lookahead + 1), val, sizeof(val))) {
                        append_val(val);
                    } else {
                        // Not a placeholder, output verbatim
                        for (size_t k = 0; k < lookahead_len; k++) {
                            out_buf[out_idx++] = lookahead[k];
                        }
                        out_buf[out_idx++] = '%';
                    }
                    lookahead_len = 0;
                } else if (lookahead_len < sizeof(lookahead) - 2 && (isalnum(c) || c == '_')) {
                    lookahead[lookahead_len++] = c;
                } else {
                    // Syntax mismatch, flush lookahead verbatim
                    for (size_t k = 0; k < lookahead_len; k++) {
                        out_buf[out_idx++] = lookahead[k];
                    }
                    lookahead_len = 0;
                    out_buf[out_idx++] = c;
                }
            } else if (c == '%') {
                lookahead[0] = '%';
                lookahead_len = 1;
            } else {
                out_buf[out_idx++] = c;
            }

            if (out_idx >= sizeof(out_buf) - 128) {
                esp_err_t err = flush_out();
                if (err != ESP_OK) {
                    fclose(f);
                    return err;
                }
                if ((++flush_count % 4) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(1)); // Cooperative yield agar MQTT & task lain di Core 1 tidak tertekan
                }
            }
        }
    }

    if (lookahead_len > 0) {
        for (size_t k = 0; k < lookahead_len; k++) {
            out_buf[out_idx++] = lookahead[k];
        }
    }

    fclose(f);
    return flush_out(true);
}

} // namespace Iotaru::Web::Template
