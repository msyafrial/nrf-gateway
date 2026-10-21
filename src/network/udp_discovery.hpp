#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_netif.h"

namespace Iotaru::Network {

class UdpDiscoveryService {
public:
    static bool start(esp_netif_t* eth_netif);
    static void stop();
    static bool isRunning();

private:
    static void taskWorker(void* arg);
};

} // namespace Iotaru::Network
