#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_netif_types.h"

namespace Iotaru::Network {

/**
 * @brief Hardware Abstraction Layer (HAL) untuk Controller Ethernet.
 * Mengisolasi logika chip SPI/RMII dari Network Manager utama.
 */
class IEthernetDriver {
public:
    virtual ~IEthernetDriver() = default;

    // Proteksi strapping pin atau reset awal sebelum inisialisasi bus
    virtual void preInit() = 0;

    // Inisialisasi bus SPI, interrupt GPIO, PHY, MAC, dan registrasi ke esp_netif
    virtual bool begin(uint32_t ip, uint32_t gw, uint32_t mask,
                       uint32_t dns, bool dhcp, const char* hostname,
                       uint32_t dns2 = 0) = 0;

    // Mematikan driver ethernet (misal fallback ke Wi-Fi / power saving)
    virtual void stop() = 0;

    // Status link layer (RJ45 kabel terhubung / link up)
    virtual bool isConnected() const = 0;

    // Status IP telah diperoleh (DHCP lease didapat atau Static IP aktif)
    virtual bool hasIp() const = 0;

    // Status chip controller terdeteksi dan merespons via SPI bus
    virtual bool isHardwareDetected() const = 0;

    // Formatted network attributes (zero dynamic memory allocation)
    virtual void getIp(char* outBuf, size_t maxLen) const = 0;
    virtual void getGateway(char* outBuf, size_t maxLen) const = 0;
    virtual void getSubnet(char* outBuf, size_t maxLen) const = 0;
    virtual void getDns(char* outBuf, size_t maxLen) const = 0;
    virtual void getMac(char* outBuf, size_t maxLen) const = 0;

    virtual int linkSpeed() const = 0;
    virtual bool isFullDuplex() const = 0;
    virtual const char* getDriverName() const = 0;
    virtual void printDiagnostic() const = 0;

    // Hard reset pulsa GPIO untuk pemulihan chip jika terjadi lock-up
    virtual void hardResetPhy() = 0;

    // Dapatkan pointer ke instance esp_netif yang diasosiasikan
    virtual esp_netif_t* getNetif() const = 0;
};

} // namespace Iotaru::Network
