#pragma once

#include "IEthernetDriver.hpp"
#include "app_types.hpp"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "esp_event.h"

namespace Iotaru::Network {

class EthernetW5500 : public IEthernetDriver {
public:
    EthernetW5500();
    virtual ~EthernetW5500();

    void preInit() override;

    bool begin(uint32_t ip, uint32_t gw, uint32_t mask,
               uint32_t dns, bool dhcp, const char* hostname,
               uint32_t dns2 = 0) override;

    void stop() override;

    bool isConnected() const override;
    bool hasIp() const override;
    bool isHardwareDetected() const override;

    void getIp(char* outBuf, size_t maxLen) const override;
    void getGateway(char* outBuf, size_t maxLen) const override;
    void getSubnet(char* outBuf, size_t maxLen) const override;
    void getDns(char* outBuf, size_t maxLen) const override;
    void getMac(char* outBuf, size_t maxLen) const override;

    int linkSpeed() const override;
    bool isFullDuplex() const override;
    const char* getDriverName() const override { return "W5500 (SPI DMA)"; }
    void printDiagnostic() const override;

    void hardResetPhy() override;
    esp_netif_t* getNetif() const override { return s_eth_netif; }

    static void onEthEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

private:
    static bool s_connected;
    static bool s_hasIp;
    static bool s_hardwareDetected;
    static esp_eth_handle_t s_eth_handle;
    static esp_netif_t* s_eth_netif;
    static esp_eth_netif_glue_handle_t s_eth_glue;
};

} // namespace Iotaru::Network
