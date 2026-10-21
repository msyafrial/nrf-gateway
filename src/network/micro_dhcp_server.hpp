#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_netif.h"

namespace Iotaru::Network {

class MicroDhcpServer {
public:
    static bool start(esp_netif_t* netif, uint32_t server_ip = 0xC0A80401, uint32_t client_ip = 0xC0A80402);
    static void stop();
    static bool isRunning();
    static bool hasActiveClientLease();
    static void resetClientLease();

private:
    static void taskWorker(void* arg);
};

} // namespace Iotaru::Network
