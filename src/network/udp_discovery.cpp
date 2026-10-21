#include "udp_discovery.hpp"
#include "net_manager.hpp"
#include "config_store.hpp"
#include "app_types.hpp"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "UDP_DISC";

namespace Iotaru::Network {

static volatile bool s_running = false;
static esp_netif_t*  s_netif = nullptr;
static TaskHandle_t  s_taskHandle = nullptr;

bool UdpDiscoveryService::start(esp_netif_t* eth_netif) {
    if (s_running) {
        return true;
    }

    s_netif = eth_netif;
    s_running = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        taskWorker,
        "udp_disc_tsk",
        3072,
        nullptr,
        3,
        &s_taskHandle,
        1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Gagal membuat task UDP Discovery!");
        s_running = false;
        return false;
    }

    ESP_LOGI(TAG, "UDP Discovery Service aktif di Port 30303");
    return true;
}

void UdpDiscoveryService::stop() {
    if (!s_running) return;

    s_running = false;
    for (int i = 0; i < 20 && s_taskHandle != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_taskHandle = nullptr;
    ESP_LOGI(TAG, "UDP Discovery Service dihentikan");
}

bool UdpDiscoveryService::isRunning() {
    return s_running;
}

void UdpDiscoveryService::taskWorker(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Gagal membuka socket UDP 30303: %d", errno);
        s_running = false;
        s_taskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind ke netif Ethernet spesifik agar balasan broadcast tidak bocor ke Wi-Fi
    if (s_netif != nullptr) {
        char if_name[IFNAMSIZ] = {0};
        if (esp_netif_get_netif_impl_name(s_netif, if_name) == ESP_OK && strlen(if_name) > 0) {
            struct ifreq ifr = {};
            strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
            setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
        }
    }

    struct timeval tv = {};
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500ms timeout
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(30303);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Gagal bind port 30303: %d", errno);
        close(sock);
        s_running = false;
        s_taskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    char rx_buf[256];

    while (s_running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0, (struct sockaddr*)&from_addr, &from_len);

        if (len <= 0) {
            continue; // Timeout
        }
        rx_buf[len] = '\0';

        // Siapkan Metadata Perangkat
        char device_id[32] = {0};
        Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));

        char ip_str[32] = {0};
        NetworkManager::getActiveIp(ip_str, sizeof(ip_str));

        char mac_str[32] = {0};
        NetworkManager::getActiveMac(mac_str, sizeof(mac_str));

        const char* model = g_config.device_model[0] ? g_config.device_model : "SG-10";

        // Tentukan Mode Operasional Jaringan
        const char* link_mode = "ROUTER_LAN";
        if (NetworkManager::isEthernetDirectPcMode()) {
            link_mode = "DIRECT_PC";
        } else if (NetworkManager::isEthernetPeerSlaveMode()) {
            link_mode = "PEER_SLAVE";
        } else if (!g_config.eth_dhcp && strlen(g_config.eth_ip) > 0) {
            link_mode = "STATIC_IP";
        } else if (g_config.connection_mode == 0) {
            link_mode = "WIFI";
        }

        char reply_json[384];
        snprintf(reply_json, sizeof(reply_json),
            "{\"device\":\"%s\",\"device_id\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\","
            "\"mode\":\"%s\",\"web_url\":\"http://%s\",\"firmware\":\"%s\",\"ui_version\":\"%s\"}\n",
            model, device_id, ip_str, mac_str, link_mode, ip_str, FIRMWARE_VERSION, "v1.0.5");

        // 1. Kirim balasan Unicast langsung ke pengirim (from_addr: IP PC + port ephemeral pengirim).
        // Ini SANGAT KRUSIAL agar lolos firewall stateful Windows/macOS/Linux
        // meskipun port Ethernet berstatus 'Public / Unidentified Network'.
        sendto(sock, reply_json, strlen(reply_json), 0, (struct sockaddr*)&from_addr, sizeof(from_addr));

        // 2. Kirim juga balasan Limited Broadcast (255.255.255.255) sebagai fallback
        struct sockaddr_in reply_dest = from_addr;
        reply_dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        sendto(sock, reply_json, strlen(reply_json), 0, (struct sockaddr*)&reply_dest, sizeof(reply_dest));

        ESP_LOGD(TAG, "Discovery reply dikirim ke port %d (IP: %s, Mode: %s)",
                 ntohs(from_addr.sin_port), ip_str, link_mode);
    }

    close(sock);
    s_running = false;
    s_taskHandle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace Iotaru::Network
