#include "micro_dns_server.hpp"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include <cstring>
#include <cctype>
#include <memory>

static const char *TAG = "MICRO_DNS";

namespace Iotaru::Network {

struct __attribute__((packed)) DnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
};

static std::atomic<bool> s_running{false};
static esp_netif_t*      s_netif = nullptr;
static uint32_t          s_resolvedIp = 0xC0A80401; // 192.168.4.1 (Host Order)
static TaskHandle_t      s_taskHandle = nullptr;
static SemaphoreHandle_t s_exitSem = nullptr;

bool MicroDnsServer::start(esp_netif_t* netif, uint32_t resolved_ip) {
    if (s_running.load(std::memory_order_acquire)) {
        return true;
    }

    if (s_exitSem == nullptr) {
        s_exitSem = xSemaphoreCreateBinary();
    } else {
        xSemaphoreTake(s_exitSem, 0);
    }

    s_netif = netif;
    s_resolvedIp = resolved_ip;
    s_running.store(true, std::memory_order_release);

    BaseType_t res = xTaskCreatePinnedToCore(
        taskWorker,
        "micro_dns_tsk",
        3072, // 3 KB stack aman; rx/tx buffer 768B dialokasikan on-demand di heap saat AP aktif
        nullptr,
        3,    // Prioritas 3 (setara Web Server)
        &s_taskHandle,
        1     // Core 1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Gagal membuat task Micro-DNS!");
        s_running.store(false, std::memory_order_release);
        return false;
    }

    ESP_LOGI(TAG, "Micro-DNS Server dimulai (Port 53 -> Resolv: 0x%08X)", (unsigned int)resolved_ip);
    return true;
}

void MicroDnsServer::stop() {
    if (!s_running.load(std::memory_order_acquire) && s_taskHandle == nullptr) return;

    s_running.store(false, std::memory_order_release);

    if (s_exitSem != nullptr && s_taskHandle != nullptr) {
        xSemaphoreTake(s_exitSem, pdMS_TO_TICKS(800));
    }
    s_taskHandle = nullptr;
    ESP_LOGI(TAG, "Micro-DNS Server dihentikan");
}

bool MicroDnsServer::isRunning() {
    return s_running.load(std::memory_order_acquire);
}

void MicroDnsServer::taskWorker(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Gagal membuat socket UDP port 53: errno %d", errno);
        s_running.store(false, std::memory_order_release);
        s_taskHandle = nullptr;
        if (s_exitSem) xSemaphoreGive(s_exitSem);
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Ikat ke interface SoftAP jika handle netif tersedia
    if (s_netif != nullptr) {
        char if_name[IFNAMSIZ] = {0};
        if (esp_netif_get_netif_impl_name(s_netif, if_name) == ESP_OK && strlen(if_name) > 0) {
            struct ifreq ifr = {};
            strncpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name) - 1);
            setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
        }
    }

    // Timeout terima paket 500ms agar task rutin mengecek status s_running
    struct timeval tv = {};
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(53); // DNS Port
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Gagal bind port 53: %d", errno);
        close(sock);
        s_running = false;
        s_taskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    constexpr size_t DNS_BUF_SZ = 384;
    auto rx_buf = std::make_unique<uint8_t[]>(DNS_BUF_SZ);
    auto tx_buf = std::make_unique<uint8_t[]>(DNS_BUF_SZ);
    if (!rx_buf || !tx_buf) {
        ESP_LOGE(TAG, "Gagal alokasi buffer DNS di heap!");
        close(sock);
        s_running = false;
        s_taskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    while (s_running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int len = recvfrom(sock, rx_buf.get(), DNS_BUF_SZ, 0, (struct sockaddr*)&from_addr, &from_len);

        if (len < (int)(sizeof(DnsHeader) + 5)) {
            continue; // Paket terlalu pendek atau timeout
        }

        const DnsHeader* req_hdr = reinterpret_cast<const DnsHeader*>(rx_buf.get());
        uint16_t flags = ntohs(req_hdr->flags);
        if ((flags & 0x8000) != 0) {
            continue; // Bukan query (abaikan paket respon)
        }

        uint16_t qdcount = ntohs(req_hdr->qdcount);
        if (qdcount < 1) {
            continue;
        }

        // Parse Question Name (QNAME) - dukung label normal & RFC 1035 compression pointer (0xC0)
        size_t offset = sizeof(DnsHeader);
        bool qname_valid = false;
        while (offset < (size_t)len) {
            uint8_t label_len = rx_buf[offset];
            if (label_len == 0) {
                offset++;
                qname_valid = true;
                break;
            }
            if ((label_len & 0xC0) == 0xC0) {
                if (offset + 2 <= (size_t)len) {
                    offset += 2;
                    qname_valid = true;
                }
                break;
            }
            if (label_len > 63 || (offset + 1 + label_len) > (size_t)len) {
                break;
            }
            offset += 1 + label_len;
        }

        if (!qname_valid) {
            continue; // Malformed QNAME
        }

        if (offset + 4 > (size_t)len) {
            continue; // Kurang QTYPE atau QCLASS
        }

        uint16_t qtype = (static_cast<uint16_t>(rx_buf[offset]) << 8) | rx_buf[offset + 1];
        offset += 4; // Lewati QTYPE (2B) dan QCLASS (2B)

        if (offset + 16 > DNS_BUF_SZ) {
            continue; // Cegah buffer overflow tx_buf (384B)
        }

        // Salin Header & Question asli ke buffer balasan
        memcpy(tx_buf.get(), rx_buf.get(), offset);
        DnsHeader* res_hdr = reinterpret_cast<DnsHeader*>(tx_buf.get());

        if (qtype == 1) {
            // TIPE A (IPv4): Balas dengan IP SoftAP Gateway (192.168.4.1)
            res_hdr->flags = htons(0x8180); // Response, Authoritative, Recursion Available, No Error
            res_hdr->qdcount = htons(1);
            res_hdr->ancount = htons(1);
            res_hdr->nscount = 0;
            res_hdr->arcount = 0;

            size_t ans_offset = offset;
            // Kompresi DNS Pointer ke QNAME (offset 12 = 0x000C)
            tx_buf[ans_offset++] = 0xC0;
            tx_buf[ans_offset++] = 0x0C;
            // TYPE = A (0x0001)
            tx_buf[ans_offset++] = 0x00;
            tx_buf[ans_offset++] = 0x01;
            // CLASS = IN (0x0001)
            tx_buf[ans_offset++] = 0x00;
            tx_buf[ans_offset++] = 0x01;
            // TTL = 60 detik
            tx_buf[ans_offset++] = 0x00;
            tx_buf[ans_offset++] = 0x00;
            tx_buf[ans_offset++] = 0x00;
            tx_buf[ans_offset++] = 0x3C;
            // RDLENGTH = 4 Byte
            tx_buf[ans_offset++] = 0x00;
            tx_buf[ans_offset++] = 0x04;
            // RDATA = Alamat IPv4 Big-Endian
            uint32_t ip_be = htonl(s_resolvedIp);
            memcpy(&tx_buf[ans_offset], &ip_be, 4);
            ans_offset += 4;

            sendto(sock, tx_buf.get(), ans_offset, 0, (struct sockaddr*)&from_addr, from_len);
        } else if (qtype == 28) {
            // TIPE AAAA (IPv6): Balas NOERROR dengan 0 Answer agar klien segera fallback ke IPv4
            res_hdr->flags = htons(0x8180);
            res_hdr->qdcount = htons(1);
            res_hdr->ancount = 0;
            res_hdr->nscount = 0;
            res_hdr->arcount = 0;

            sendto(sock, tx_buf.get(), offset, 0, (struct sockaddr*)&from_addr, from_len);
        } else {
            // Tipe lain: Kembalikan respon NXDOMAIN
            res_hdr->flags = htons(0x8183); // NXDOMAIN
            res_hdr->qdcount = htons(1);
            res_hdr->ancount = 0;
            res_hdr->nscount = 0;
            res_hdr->arcount = 0;

            sendto(sock, tx_buf.get(), offset, 0, (struct sockaddr*)&from_addr, from_len);
        }
    }

    close(sock);
    s_running.store(false, std::memory_order_release);
    s_taskHandle = nullptr;
    if (s_exitSem) xSemaphoreGive(s_exitSem);
    vTaskDelete(nullptr);
}

} // namespace Iotaru::Network

