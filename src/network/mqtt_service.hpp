#pragma once

#include "app_types.hpp"
#include "app_config.hpp"
#include "mqtt_client.h"
#include <cstdint>
#include <cstddef>

namespace Iotaru::Network {

class MqttService {
public:
    static void init();
    static bool isConnected();
    static void updateTopicNames();

    static void getTopicData(char* outBuf, size_t maxLen);
    static void getTopicStatus(char* outBuf, size_t maxLen);
    static void getTopicCmd(char* outBuf, size_t maxLen);
    static void getTopicRes(char* outBuf, size_t maxLen);

    static float getBandwidthKBps();
    static uint64_t getTotalWireBytes();
    static size_t getBatchBufferLoad();
    static size_t getLastBatchPktCount();
    static const char* getLastError();

private:
    static void mqttPublisherTask(void* pvParameters);
    static void onMqttEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
    static bool buildBinaryPayload(const BeaconPacket& p, uint8_t* outBuf, size_t& outLen);
};

} // namespace Iotaru::Network
