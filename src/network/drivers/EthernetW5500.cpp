#include "EthernetW5500.hpp"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_eth_netif_glue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "ETH_W5500";

namespace Iotaru::Network {

bool EthernetW5500::s_connected = false;
bool EthernetW5500::s_hasIp = false;
bool EthernetW5500::s_hardwareDetected = false;
esp_eth_handle_t EthernetW5500::s_eth_handle = nullptr;
esp_netif_t* EthernetW5500::s_eth_netif = nullptr;
esp_eth_netif_glue_handle_t EthernetW5500::s_eth_glue = nullptr;

EthernetW5500::EthernetW5500() {}
EthernetW5500::~EthernetW5500() {
    stop();
}

void EthernetW5500::preInit() {
    // Hardware reset pulse untuk chip W5500
    gpio_reset_pin((gpio_num_t)Pins::W5500_RST);
    gpio_set_direction((gpio_num_t)Pins::W5500_RST, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)Pins::W5500_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)Pins::W5500_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void EthernetW5500::hardResetPhy() {
    ESP_LOGW(TAG, "Melakukan Hard Reset PHY W5500...");
    gpio_set_level((gpio_num_t)Pins::W5500_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(30));
    gpio_set_level((gpio_num_t)Pins::W5500_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(80));
}

void EthernetW5500::onEthEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t*)data;

    switch (id) {
        case ETHERNET_EVENT_CONNECTED:
            s_connected = true;
            esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
            ESP_LOGI(TAG, "Kabel LAN W5500 Terhubung (Link Up) | MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            break;

        case ETHERNET_EVENT_DISCONNECTED:
            s_connected = false;
            s_hasIp = false;
            ESP_LOGW(TAG, "Kabel LAN W5500 Terputus (Link Down)!");
            break;

        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Driver Ethernet W5500 berhasil dimulai.");
            break;

        case ETHERNET_EVENT_STOP:
            s_connected = false;
            s_hasIp = false;
            ESP_LOGI(TAG, "Driver Ethernet W5500 dihentikan.");
            break;

        default:
            break;
    }
}

void EthernetW5500::onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
    if (id == IP_EVENT_ETH_GOT_IP) {
        s_hasIp = true;
        char ipStr[16], gwStr[16], maskStr[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ipStr, sizeof(ipStr));
        esp_ip4addr_ntoa(&event->ip_info.gw, gwStr, sizeof(gwStr));
        esp_ip4addr_ntoa(&event->ip_info.netmask, maskStr, sizeof(maskStr));

        ESP_LOGI(TAG, "=======================================================");
        ESP_LOGI(TAG, ">>> SUKSES TERHUBUNG & MENDAPATKAN IP W5500! <<<");
        ESP_LOGI(TAG, "  -> Alamat IP   : %s", ipStr);
        ESP_LOGI(TAG, "  -> Gateway     : %s", gwStr);
        ESP_LOGI(TAG, "  -> Subnet Mask : %s", maskStr);
        ESP_LOGI(TAG, "=======================================================");
    }
}

bool EthernetW5500::begin(uint32_t ip, uint32_t gw, uint32_t mask,
                          uint32_t dns, bool dhcp, const char* hostname,
                          uint32_t dns2) {
    ESP_LOGI(TAG, "Inisialisasi Hardware W5500 (SPI DMA)");
    ESP_LOGI(TAG, "Pinout: CS=%d, SCK=%d, MISO=%d, MOSI=%d, RST=%d, INT=%d",
             Pins::W5500_CS, Pins::W5500_SCK, Pins::W5500_MISO,
             Pins::W5500_MOSI, Pins::W5500_RST, Pins::W5500_INT);

    preInit();

    // 1. Install GPIO ISR Service jika belum ada
    gpio_install_isr_service(0);

    // Pull-up internal untuk INT pin (Active LOW open-drain)
    gpio_set_pull_mode((gpio_num_t)Pins::W5500_INT, GPIO_PULLUP_ONLY);

    // 2. Inisialisasi Bus SPI3 (VSPI) dengan DMA Buffer 4 KB
    spi_bus_config_t buscfg = {};
    buscfg.max_transfer_sz = 4096;
    buscfg.miso_io_num = (gpio_num_t)Pins::W5500_MISO;
    buscfg.mosi_io_num = (gpio_num_t)Pins::W5500_MOSI;
    buscfg.sclk_io_num = (gpio_num_t)Pins::W5500_SCK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Gagal inisialisasi SPI bus: 0x%x", ret);
        s_hardwareDetected = false;
        return false;
    }

    // 3. Konfigurasi SPI Device Interface W5500
    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits = 16;
    devcfg.address_bits = 8;
    devcfg.mode = 0;
    devcfg.clock_speed_hz = 33 * 1000 * 1000; // 33 MHz SPI Clock
    devcfg.spics_io_num = Pins::W5500_CS;
    devcfg.queue_size = 20;

    // 4. Instansiasi MAC & PHY W5500
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI3_HOST, &devcfg);
    w5500_config.base.int_gpio_num = Pins::W5500_INT;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.rx_task_stack_size = 4096;
    mac_config.rx_task_prio = 5; // Prioritas tinggi untuk pemrosesan ACK instan

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "Gagal membuat MAC instance W5500!");
        s_hardwareDetected = false;
        return false;
    }

    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = -1;
    phy_config.reset_gpio_num = Pins::W5500_RST;

    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "Gagal membuat PHY instance W5500!");
        mac->del(mac);
        s_hardwareDetected = false;
        return false;
    }

    // 5. Install Ethernet Driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ret = esp_eth_driver_install(&eth_config, &s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal install Ethernet driver: 0x%x", ret);
        mac->del(mac);
        phy->del(phy);
        s_hardwareDetected = false;
        return false;
    }

    // 6. Set Hardware MAC Address dari eFuse ESP32
    uint8_t mac_addr[6] = {0};
    esp_read_mac(mac_addr, ESP_MAC_ETH);
    esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);

    // 7. Buat & Hubungkan esp_netif (lwIP Stack)
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    if (!s_eth_netif) {
        ESP_LOGE(TAG, "Gagal membuat esp_netif instance W5500!");
        return false;
    }

    if (hostname && strlen(hostname) > 0) {
        esp_netif_set_hostname(s_eth_netif, hostname);
    }

    s_eth_glue = esp_eth_new_netif_glue(s_eth_handle);
    ret = esp_netif_attach(s_eth_netif, s_eth_glue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal attach esp_netif ke W5500: 0x%x", ret);
        s_hardwareDetected = false;
        return false;
    }

    // 8. Registrasi System Event Listeners
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &onEthEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &onIpEvent, nullptr));

    // 9. Konfigurasi Static IP vs DHCP
    if (!dhcp && ip != 0) {
        esp_netif_dhcpc_stop(s_eth_netif);
        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr = ip;
        ip_info.gw.addr = gw;
        ip_info.netmask.addr = mask;
        esp_netif_set_ip_info(s_eth_netif, &ip_info);

        esp_netif_dns_info_t dns_info;
        dns_info.ip.u_addr.ip4.addr = (dns != 0) ? dns : gw;
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns_info);

        if (dns2 != 0) {
            esp_netif_dns_info_t dns2_info;
            dns2_info.ip.u_addr.ip4.addr = dns2;
            dns2_info.ip.type = ESP_IPADDR_TYPE_V4;
            esp_netif_set_dns_info(s_eth_netif, ESP_NETIF_DNS_BACKUP, &dns2_info);
        }
        ESP_LOGI(TAG, "Mode Konfigurasi: IP Statis");
    } else {
        ESP_LOGI(TAG, "Mode Konfigurasi: DHCP Client");
    }

    // 10. Mulai Driver
    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal esp_eth_start: 0x%x", ret);
        s_hardwareDetected = false;
        return false;
    }

    s_hardwareDetected = true;
    ESP_LOGI(TAG, "Driver Ethernet W5500 siap bekerja.");
    return true;
}

void EthernetW5500::stop() {
    if (s_eth_handle != nullptr) {
        esp_eth_stop(s_eth_handle);
        s_connected = false;
        s_hasIp = false;
    }
}

bool EthernetW5500::isConnected() const {
    return s_connected;
}

bool EthernetW5500::hasIp() const {
    return s_hasIp;
}

bool EthernetW5500::isHardwareDetected() const {
    return s_hardwareDetected;
}

void EthernetW5500::getIp(char* outBuf, size_t maxLen) const {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_eth_netif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.ip, outBuf, maxLen);
        }
    }
}

void EthernetW5500::getGateway(char* outBuf, size_t maxLen) const {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_eth_netif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.gw, outBuf, maxLen);
        }
    }
}

void EthernetW5500::getSubnet(char* outBuf, size_t maxLen) const {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_eth_netif != nullptr) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_eth_netif, &ip_info) == ESP_OK) {
            esp_ip4addr_ntoa(&ip_info.netmask, outBuf, maxLen);
        }
    }
}

void EthernetW5500::getDns(char* outBuf, size_t maxLen) const {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (s_eth_netif != nullptr) {
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(s_eth_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, outBuf, maxLen);
        }
    }
}

void EthernetW5500::getMac(char* outBuf, size_t maxLen) const {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    uint8_t mac[6] = {0};
    if (s_eth_handle != nullptr && esp_eth_ioctl(s_eth_handle, ETH_CMD_G_MAC_ADDR, mac) == ESP_OK) {
        snprintf(outBuf, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

int EthernetW5500::linkSpeed() const {
    if (s_eth_handle == nullptr) return 0;
    eth_speed_t speed;
    if (esp_eth_ioctl(s_eth_handle, ETH_CMD_G_SPEED, &speed) == ESP_OK) {
        return (speed == ETH_SPEED_100M) ? 100 : 10;
    }
    return 0;
}

bool EthernetW5500::isFullDuplex() const {
    if (s_eth_handle == nullptr) return false;
    eth_duplex_t duplex;
    if (esp_eth_ioctl(s_eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex) == ESP_OK) {
        return (duplex == ETH_DUPLEX_FULL);
    }
    return false;
}

void EthernetW5500::printDiagnostic() const {
    char ip[16], gw[16], mask[16], mac[18];
    getIp(ip, sizeof(ip));
    getGateway(gw, sizeof(gw));
    getSubnet(mask, sizeof(mask));
    getMac(mac, sizeof(mac));

    ESP_LOGI(TAG, "--- [W5500 DIAGNOSTIK] ---");
    ESP_LOGI(TAG, "  Hardware Terdeteksi: %s", s_hardwareDetected ? "YA" : "TIDAK");
    ESP_LOGI(TAG, "  Link Status        : %s", s_connected ? "LINK UP" : "LINK DOWN");
    ESP_LOGI(TAG, "  IP Status          : %s (%s)", s_hasIp ? "OBTAINED" : "NO IP", ip);
    ESP_LOGI(TAG, "  Gateway            : %s", gw);
    ESP_LOGI(TAG, "  Subnet             : %s", mask);
    ESP_LOGI(TAG, "  MAC Address        : %s", mac);
    ESP_LOGI(TAG, "  Kecepatan          : %d Mbps (%s Duplex)", linkSpeed(), isFullDuplex() ? "Full" : "Half");
    ESP_LOGI(TAG, "--------------------------");
}

} // namespace Iotaru::Network
