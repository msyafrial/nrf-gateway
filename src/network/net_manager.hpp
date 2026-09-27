#pragma once

#include "app_types.hpp"
#include "app_config.hpp"
#include "IEthernetDriver.hpp"
#include <cstdint>
#include <cstddef>

namespace Iotaru::Network {

class NetworkManager {
public:
    static void earlyInit(IEthernetDriver* driver = nullptr);
    static void init(IEthernetDriver* driver = nullptr);

    // Watchdog dipanggil periodik dari main loop / supervisor task
    static void tickWatchdog();

    static bool isConnected();
    static bool isEthernetActive();
    static bool isEthernetHardwareDetected();
    static void printEthernetDiagnostic();

    static void getActiveIp(char* outBuf, size_t maxLen);
    static ConnectionMode getActiveConnectionMode();
    static void getActiveWifiSsid(char* outBuf, size_t maxLen);
    static void rollbackPendingNetworkConfig();

    // SoftAP Fallback & On-Demand Configuration Mode
    static void startConfigAP();
    static void stopConfigAP();
    static void scheduleStopConfigAP(uint32_t delayMs = 1000);
    static void recordApActivity();
    static bool isConfigMode();
    static bool isApActive();
    static uint32_t getApRemainingSeconds();

    static bool isNtpSynced();
    static IEthernetDriver* getEthernetDriver();

    static const char* getWifiDisconnectReasonName(uint8_t reason);

private:
    static void initWifiStation();
    static void initSntp();
    static void onWifiEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
    static void onIpEvent(void* arg, esp_event_base_t base, int32_t id, void* data);

    static void checkNtpWatchdog();
    static void checkDisconWatchdog();
    static void checkApInactivityTimeout();
    static void checkDailyRestart();
};

} // namespace Iotaru::Network
