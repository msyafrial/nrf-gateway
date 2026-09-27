#pragma once

#include "app_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <cstdint>
#include <cstddef>

namespace Iotaru::Core {

class BeaconPipeline {
public:
    static void init();
    
    // Core 0 fast ingestion processor
    static void processBytes(const uint8_t* buffer, size_t len);
    
    // Antrean & Buffer Telemetri
    static void push(const BeaconPacket& packet);
    static void recordLiveBeacon(const BeaconPacket& packet);
    static void pruneExpired();
    static bool buildBinaryPayload(const BeaconPacket& packet, uint8_t* outBuf, size_t& outLen);
    
    // Snapshot Live Beacons untuk WebSocket (Thread-Safe)
    static size_t copyLiveBeacons(LiveBeaconItem* outItems, size_t maxItems);
    
    // Kalkulasi Metrik Throughput (Dipanggil tiap 1 detik oleh esp_timer)
    static void updateMetrics();

    // Getter Metrik
    static uint32_t getReadSpeed();
    static uint32_t getMqttSpeed();
    static uint32_t getTotalBeaconsRead();
    static uint32_t getTotalMqttPublished();
    static uint32_t getTotalMqttFailed();
    static size_t   getQueueLoad();
    
    // Handle queue untuk task consumer (MQTT publisher di Core 1)
    static QueueHandle_t getQueue();
};

} // namespace Iotaru::Core
