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
    static void flushQueue();
    
    // Snapshot Live Beacons untuk WebSocket (Thread-Safe)
    static size_t copyLiveBeacons(LiveBeaconItem* outItems, size_t maxItems);
    static void   flushLiveBeacons();
    
    // Snapshot Dropped Beacons untuk WebSocket (Thread-Safe)
    static void   recordDroppedBeacon(const char* code, const char* tag, int8_t rssi, DropReason reason);
    static void   recordDroppedBeacon(const BeaconPacket& packet, DropReason reason);
    static size_t copyDroppedBeacons(DroppedBeaconItem* outItems, size_t maxItems);
    static void   flushDroppedBeacons();

    // Kalkulasi Metrik Throughput (Dipanggil tiap 1 detik oleh esp_timer)
    static void updateMetrics();

    // Pencatatan Metrik MQTT (Dipanggil oleh MqttService)
    static void recordMqttPublish(uint32_t packetCount = 1, uint32_t batchCount = 1);
    static void recordMqttFailed(uint32_t packetCount = 1);

    // Getter Metrik
    static uint32_t getReadSpeed();
    static uint32_t getMqttSpeed();
    static uint32_t getMqttBatchSpeed();
    static uint32_t getTotalBeaconsRead();
    static uint32_t getTotalMqttPublished();
    static uint32_t getTotalMqttBatches();
    static uint32_t getTotalMqttFailed();
    static uint32_t getTotalQueueDropped();
    static uint32_t getTotalDropped();
    static size_t   getQueueLoad();

    // Getter Counter Alasan Drop
    static uint32_t getDropTtlCount();
    static uint32_t getDropRssiCount();
    static uint32_t getDropUuidCount();
    static uint32_t getDropMajorCount();
    static uint32_t getDropQueueCount();
    static uint32_t getDropMqttCount();
    static void     resetDropCounters();
    
    // Handle queue untuk task consumer (MQTT publisher di Core 1)
    static QueueHandle_t getQueue();
};

} // namespace Iotaru::Core
