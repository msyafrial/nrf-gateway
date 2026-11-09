#pragma once

#include <cstdint>
#include <cstddef>
#include "esp_netif.h"

namespace Iotaru::Network {

class MicroDnsServer {
public:
    static bool start(esp_netif_t* netif = nullptr, uint32_t resolved_ip = 0xC0A80401);
    static void stop();
    static bool isRunning();

private:
    static void taskWorker(void* arg);
};

} // namespace Iotaru::Network

