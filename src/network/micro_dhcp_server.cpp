#include "micro_dhcp_server.hpp"
#include "app_config.hpp"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "MICRO_DHCPS";

namespace Iotaru::Network {

struct __attribute__((packed)) dhcp_raw_msg {
    uint8_t  op;           // 1 = BOOTREQUEST, 2 = BOOTREPLY
    uint8_t  htype;        // 1 = 10Mb Ethernet
    uint8_t  hlen;         // 6 = MAC length
    uint8_t  hops;         // 0
    uint32_t xid;          // Transaction ID
    uint16_t secs;         // Seconds elapsed
    uint16_t flags;        // 0x8000 = Broadcast
    uint32_t ciaddr;       // Client IP (RENEWING)
    uint32_t yiaddr;       // 'Your' (client) IP
    uint32_t siaddr;       // Next server IP
    uint32_t giaddr;       // Relay agent IP
    uint8_t  chaddr[16];   // Client MAC
    uint8_t  sname[64];    // Server host name
    uint8_t  file[128];    // Boot file name
    uint32_t cookie;       // 0x63825363 Magic Cookie
    uint8_t  options[308]; // Options payload
};

static volatile bool s_running = false;
static volatile bool s_hasActiveClientLease = false;
static esp_netif_t*  s_netif = nullptr;
static uint32_t      s_serverIp = 0xC0A80401; // 192.168.4.1 (Host Order)
static uint32_t      s_clientIp = 0xC0A80402; // 192.168.4.2 (Host Order)
static TaskHandle_t  s_taskHandle = nullptr;

bool MicroDhcpServer::start(esp_netif_t* netif, uint32_t server_ip, uint32_t client_ip) {
    if (s_running) {
        return true;
    }

    s_netif = netif;
    s_serverIp = server_ip;
    s_clientIp = client_ip;
    s_hasActiveClientLease = false;
    s_running = true;

    BaseType_t res = xTaskCreatePinnedToCore(
        taskWorker,
        "micro_dhcp_tsk",
        3072,
        nullptr,
        4,
        &s_taskHandle,
        1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Gagal membuat task Micro-DHCP!");
        s_running = false;
        return false;
    }

    ESP_LOGI(TAG, "Micro-DHCP Server dimulai (Gateway: 192.168.4.1, Lease: 192.168.4.2)");
    return true;
}

void MicroDhcpServer::stop() {
    if (!s_running) return;

    s_running = false;
    s_hasActiveClientLease = false;

    // Tunggu task berhenti dengan batas waktu 500ms
    for (int i = 0; i < 25 && s_taskHandle != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_taskHandle = nullptr;
    ESP_LOGI(TAG, "Micro-DHCP Server dihentikan");
}

bool MicroDhcpServer::isRunning() {
    return s_running;
}

bool MicroDhcpServer::hasActiveClientLease() {
    return s_hasActiveClientLease;
}

void MicroDhcpServer::resetClientLease() {
    s_hasActiveClientLease = false;
}

static void sendDhcpReply(int sock, const dhcp_raw_msg& req, uint8_t msg_type, uint32_t server_ip_be, uint32_t client_ip_be) {
    dhcp_raw_msg rep = {};
    rep.op = 2; // BOOTREPLY
    rep.htype = req.htype ? req.htype : 1;
    rep.hlen = req.hlen ? req.hlen : 6;
    rep.hops = 0;
    rep.xid = req.xid;
    rep.secs = 0;
    rep.flags = req.flags | htons(0x8000); // Pastikan Broadcast flag agar klien tanpa IP dapat menerima paket
    rep.ciaddr = 0;
    // Sesuai RFC 2131: Pada DHCPNAK, yiaddr WAJIB 0.0.0.0
    rep.yiaddr = (msg_type == 6) ? 0 : client_ip_be;
    rep.siaddr = 0; // RFC 2131: siaddr WAJIB 0.0.0.0 (Server ID ada di Opsi 54)
    rep.giaddr = 0;
    memcpy(rep.chaddr, req.chaddr, 16);
    rep.cookie = htonl(0x63825363);

    size_t opt_idx = 0;

    // Option 53: DHCP Message Type
    rep.options[opt_idx++] = 53;
    rep.options[opt_idx++] = 1;
    rep.options[opt_idx++] = msg_type;

    // Option 54: Server Identifier
    rep.options[opt_idx++] = 54;
    rep.options[opt_idx++] = 4;
    memcpy(&rep.options[opt_idx], &server_ip_be, 4);
    opt_idx += 4;

    if (msg_type != 6) { // Bukan NAK -> Sertakan Parameter Lease & Subnet Mask
        // Option 51: IP Address Lease Time (3600s)
        rep.options[opt_idx++] = 51;
        rep.options[opt_idx++] = 4;
        uint32_t lease_sec = htonl(3600);
        memcpy(&rep.options[opt_idx], &lease_sec, 4);
        opt_idx += 4;

        // Option 58: Renewal Time T1 (1800s / 50% lease)
        rep.options[opt_idx++] = 58;
        rep.options[opt_idx++] = 4;
        uint32_t t1_sec = htonl(1800);
        memcpy(&rep.options[opt_idx], &t1_sec, 4);
        opt_idx += 4;

        // Option 59: Rebinding Time T2 (3150s / 87.5% lease)
        rep.options[opt_idx++] = 59;
        rep.options[opt_idx++] = 4;
        uint32_t t2_sec = htonl(3150);
        memcpy(&rep.options[opt_idx], &t2_sec, 4);
        opt_idx += 4;

        // Option 1: Subnet Mask (255.255.255.0)
        rep.options[opt_idx++] = 1;
        rep.options[opt_idx++] = 4;
        uint32_t mask_val = htonl(0xFFFFFF00);
        memcpy(&rep.options[opt_idx], &mask_val, 4);
        opt_idx += 4;

        // Option 28: Broadcast Address (192.168.4.255)
        rep.options[opt_idx++] = 28;
        rep.options[opt_idx++] = 4;
        uint32_t bcast_val = htonl(0xC0A804FF);
        memcpy(&rep.options[opt_idx], &bcast_val, 4);
        opt_idx += 4;

        // CATATAN KRUSIAL: Option 3 (Router/Gateway) dan Option 6 (DNS)
        // SENGAJA DITIADAKAN agar koneksi Wi-Fi internet di laptop teknisi tidak terputus!
    }

    // Option 255: End
    rep.options[opt_idx++] = 255;

    // Standard BOOTP / DHCP Ethernet packet payload size adalah 548 bytes (sizeof(dhcp_raw_msg)).
    // Sisa payload options terisi 0x00 (DHO_PAD) sesuai RFC 2132, mencegah penolakan oleh driver NDIS Windows.
    size_t total_len = sizeof(dhcp_raw_msg);

    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(68); // BOOTP Client Port
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST); // 255.255.255.255

    // 1. Kirim ke Limited Broadcast 255.255.255.255
    sendto(sock, &rep, total_len, 0, (struct sockaddr*)&dest, sizeof(dest));

    // 2. Kirim juga ke Subnet Directed Broadcast 192.168.4.255 sebagai redundansi routing lwIP
    dest.sin_addr.s_addr = htonl(0xC0A804FF);
    sendto(sock, &rep, total_len, 0, (struct sockaddr*)&dest, sizeof(dest));
}

void MicroDhcpServer::taskWorker(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Gagal membuka socket UDP 67: %d", errno);
        s_running = false;
        s_taskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind ke interface dinamis lwIP ("en0") menggunakan struct ifreq resmi lwIP
    if (s_netif != nullptr) {
        char if_name[IFNAMSIZ] = {0};
        if (esp_netif_get_netif_impl_name(s_netif, if_name) == ESP_OK && strlen(if_name) > 0) {
            struct ifreq ifr = {};
            strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
            int ret = setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
            if (ret == 0) {
                ESP_LOGI(TAG, "Socket UDP 67 berhasil diikat ke netif: %s (SO_BINDTODEVICE OK)", if_name);
            } else {
                ESP_LOGE(TAG, "Gagal SO_BINDTODEVICE ke %s: errno %d", if_name, errno);
            }
        }
    }

    // Timeout terima paket 500ms agar task bisa memeriksa s_running
    struct timeval tv = {};
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(67); // BOOTP Server Port
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Gagal bind port 67: %d", errno);
        close(sock);
        s_running = false;
        s_taskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    uint32_t s_ip_be = htonl(s_serverIp);
    uint32_t c_ip_be = htonl(s_clientIp);

    dhcp_raw_msg rx_msg;

    while (s_running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(sock, &rx_msg, sizeof(rx_msg), 0, (struct sockaddr*)&from_addr, &from_len);

        if (len < (int)(sizeof(dhcp_raw_msg) - sizeof(rx_msg.options) + 4)) {
            continue; // Timeout atau paket terlalu pendek
        }

        if (rx_msg.op != 1) continue; // Bukan BOOTREQUEST
        if (ntohl(rx_msg.cookie) != 0x63825363) continue; // Magic Cookie tidak cocok

        // Parse Opsi DHCP
        uint8_t msg_type = 0;
        uint32_t opt50_ip = 0;
        uint32_t opt54_ip = 0;
        char opt12_hostname[64] = {0};
        size_t opt12_len = 0;

        size_t max_opt_len = len - (sizeof(dhcp_raw_msg) - sizeof(rx_msg.options));
        size_t i = 0;
        while (i < max_opt_len) {
            uint8_t code = rx_msg.options[i++];
            if (code == 0) continue; // Padding
            if (code == 255) break;  // End
            if (i >= max_opt_len) break;
            uint8_t opt_len = rx_msg.options[i++];
            if (i + opt_len > max_opt_len) break;

            if (code == 53 && opt_len == 1) { // DHCP Message Type
                msg_type = rx_msg.options[i];
            } else if (code == 50 && opt_len == 4) { // Requested IP
                memcpy(&opt50_ip, &rx_msg.options[i], 4);
            } else if (code == 54 && opt_len == 4) { // Server Identifier
                memcpy(&opt54_ip, &rx_msg.options[i], 4);
            } else if (code == 12 && opt_len > 0) { // Host Name
                opt12_len = (opt_len < sizeof(opt12_hostname) - 1) ? opt_len : (sizeof(opt12_hostname) - 1);
                memcpy(opt12_hostname, &rx_msg.options[i], opt12_len);
                opt12_hostname[opt12_len] = '\0';
            }
            i += opt_len;
        }

        // CATATAN KRUSIAL 1: Filter Hostname Option 12
        // Jika diawali prefix model gateway (misal "sg10-" atau model NVS), Master WAJIB MENGABAIKAN!
        // Jangan layani sesama gateway agar IP 192.168.4.2 100% khusus untuk PC teknisi!
        char dyn_prefix[32] = {0};
        size_t pIdx = 0;
        const char* raw_model = g_config.device_model[0] ? g_config.device_model : "sg10";
        for (size_t k = 0; raw_model[k] != '\0' && pIdx < sizeof(dyn_prefix) - 2; ++k) {
            char c = raw_model[k];
            if (c >= 'A' && c <= 'Z') dyn_prefix[pIdx++] = static_cast<char>(c + ('a' - 'A'));
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) dyn_prefix[pIdx++] = c;
        }
        dyn_prefix[pIdx++] = '-';
        dyn_prefix[pIdx] = '\0';

        bool is_peer_gw = (pIdx > 1 && opt12_len >= pIdx && strncmp(opt12_hostname, dyn_prefix, pIdx) == 0) ||
                          (opt12_len >= 5 && strncmp(opt12_hostname, "sg10-", 5) == 0);

        if (is_peer_gw) {
            ESP_LOGD(TAG, "Mengabaikan DHCP paket dari sesama gateway ('%s')", opt12_hostname);
            continue;
        }

        // CATATAN KRUSIAL 2: Penanganan DHCPINFORM (Option 53 = 8)
        // Layanan Windows NLA sering mengirim DHCPINFORM setelah dapat IP.
        // Abaikan paket ini, jangan kirim DHCPNAK!
        if (msg_type == 8) {
            ESP_LOGD(TAG, "DHCPINFORM dari PC diabaikan (Normal Windows NLA check)");
            continue;
        }

        if (msg_type == 1) { // DHCPDISCOVER
            ESP_LOGI(TAG, "Menerima DHCPDISCOVER -> Mengirim DHCPOFFER 192.168.4.2");
            sendDhcpReply(sock, rx_msg, 2, s_ip_be, c_ip_be); // 2 = DHCPOFFER
        } else if (msg_type == 3) { // DHCPREQUEST
            // Jika klien menyertakan Option 54 (Server Identifier) dan ditujukan ke server lain, abaikan
            if (opt54_ip != 0 && opt54_ip != s_ip_be) {
                ESP_LOGD(TAG, "DHCPREQUEST ditujukan ke server DHCP lain, diabaikan");
                continue;
            }

            // Sesuai RFC 2131:
            // Cek Option 50 jika ada; jika tidak ada (state RENEWING), ambil dari header ciaddr
            uint32_t target_ip = (opt50_ip != 0) ? opt50_ip : rx_msg.ciaddr;

            if (target_ip == c_ip_be) {
                ESP_LOGI(TAG, "Menerima DHCPREQUEST valid -> Mengirim DHCPACK (Lease Terkunci)");
                sendDhcpReply(sock, rx_msg, 5, s_ip_be, c_ip_be); // 5 = DHCPACK
                s_hasActiveClientLease = true;
            } else {
                ESP_LOGW(TAG, "Menerima DHCPREQUEST untuk IP asing -> Mengirim DHCPNAK seketika");
                sendDhcpReply(sock, rx_msg, 6, s_ip_be, 0); // 6 = DHCPNAK (yiaddr = 0.0.0.0)
            }
        }
    }

    close(sock);
    s_running = false;
    s_taskHandle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace Iotaru::Network
