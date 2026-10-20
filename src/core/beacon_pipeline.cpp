#include "beacon_pipeline.hpp"
#include "drop_log_service.hpp"
#include "app_config.hpp"
#include "crc_utils.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <cstdio>
#include <atomic>

static const char *TAG = "BEACON_PIPE";

namespace Iotaru::Core {

static QueueHandle_t s_beaconQueue = nullptr;
static SemaphoreHandle_t s_liveBufMutex = nullptr;

static LiveBeaconItem s_liveBeaconBuf[LIVE_FIFO_MAX];
static size_t s_liveBeaconCount = 0;

static DroppedBeaconItem s_droppedBeaconBuf[DROPPED_FIFO_MAX];
static size_t s_droppedBeaconCount = 0;

static std::atomic<uint32_t> s_dropTtlExpired{0};
static std::atomic<uint32_t> s_dropRssiMin{0};
static std::atomic<uint32_t> s_dropUuidMismatch{0};
static std::atomic<uint32_t> s_dropMajorMismatch{0};
static std::atomic<uint32_t> s_dropQueueOverflow{0};
static std::atomic<uint32_t> s_dropMqttFailed{0};

static std::atomic<uint32_t> s_totalBeaconsRead{0};
static std::atomic<uint32_t> s_totalMqttPublished{0};
static std::atomic<uint32_t> s_totalMqttBatches{0};
static std::atomic<uint32_t> s_totalMqttFailed{0};
static std::atomic<uint32_t> s_totalQueueDropped{0};

static uint32_t s_readSpeed = 0;
static uint32_t s_mqttSpeed = 0;
static uint32_t s_mqttBatchSpeed = 0;
static uint32_t s_lastReadSnap = 0;
static uint32_t s_lastMqttPublishedSnap = 0;
static uint32_t s_lastMqttBatchSnap = 0;

// Internal streaming state machine buffer
static uint8_t s_rxFrameBuf[sizeof(BeaconBinaryFrame)];
static size_t  s_rxFrameIdx = 0;

void BeaconPipeline::init() {
    if (s_liveBufMutex == nullptr) {
        s_liveBufMutex = xSemaphoreCreateMutex();
    }
    if (s_beaconQueue == nullptr) {
        s_beaconQueue = xQueueCreate(BEACON_QUEUE_MAX, sizeof(BeaconPacket));
    }
    DropLogService::init();
    ESP_LOGI(TAG, "Inisialisasi antrean data beacon selesai (Kapasitas: %u item, Live FIFO: %u item)",
             (unsigned int)BEACON_QUEUE_MAX, (unsigned int)LIVE_FIFO_MAX);
}

void BeaconPipeline::push(const BeaconPacket& packet) {
    if (s_beaconQueue == nullptr) return;

    if (xQueueSend(s_beaconQueue, &packet, 0) != pdTRUE) {
        // Antrean penuh: buang item tertua (drop-oldest policy)
        BeaconPacket oldest;
        if (xQueueReceive(s_beaconQueue, &oldest, 0) == pdTRUE) {
            s_totalQueueDropped++;
            recordDroppedBeacon(oldest, DropReason::QUEUE_OVERFLOW);
            xQueueSend(s_beaconQueue, &packet, 0);
        }
    }
}

void BeaconPipeline::recordLiveBeacon(const BeaconPacket& p) {
    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_liveBeaconCount < LIVE_FIFO_MAX) {
            LiveBeaconItem& item = s_liveBeaconBuf[s_liveBeaconCount++];
            if (p.type == BeaconType::IBEACON) {
                strncpy(item.code, "iBeacon", sizeof(item.code));
            } else if (p.type == BeaconType::IBEACON_EXT || p.type == BeaconType::MINEW_B) {
                strncpy(item.code, "ibeacon_ext", sizeof(item.code) - 1);
                item.code[sizeof(item.code) - 1] = '\0';
            } else {
                strncpy(item.code, "BLE", sizeof(item.code));
            }
            snprintf(item.tag, sizeof(item.tag), "%05u-%05u", p.major, p.minor);
            item.rssi = p.rssi;
            item.batt = p.battery;
        }
        xSemaphoreGive(s_liveBufMutex);
    }
}

void BeaconPipeline::pruneExpired() {
    if (s_beaconQueue == nullptr) return;
    BeaconPacket front;
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);

    while (xQueuePeek(s_beaconQueue, &front, 0) == pdTRUE) {
        if ((nowMs - front.timestamp) > (uint32_t)g_config.data_ttl) {
            BeaconPacket discarded;
            if (xQueueReceive(s_beaconQueue, &discarded, 0) == pdTRUE) {
                s_totalQueueDropped++;
                recordDroppedBeacon(discarded, DropReason::TTL_EXPIRED);
            }
        } else {
            break;
        }
    }
}

bool BeaconPipeline::buildBinaryPayload(const BeaconPacket& p, uint8_t* outBuf, size_t& outLen) {
    if (p.type == BeaconType::IBEACON) {
        outBuf[0] = 0x00;
        outBuf[1] = (uint8_t)(p.major >> 8);
        outBuf[2] = (uint8_t)(p.major & 0xFF);
        outBuf[3] = (uint8_t)(p.minor >> 8);
        outBuf[4] = (uint8_t)(p.minor & 0xFF);
        outBuf[5] = (int8_t)p.rssi;
        outLen = 6;
        return true;
    } else if (p.type == BeaconType::IBEACON_EXT || p.type == BeaconType::MINEW_B) {
        outBuf[0] = 0x01;
        outBuf[1] = (uint8_t)(p.major >> 8);
        outBuf[2] = (uint8_t)(p.major & 0xFF);
        outBuf[3] = (uint8_t)(p.minor >> 8);
        outBuf[4] = (uint8_t)(p.minor & 0xFF);
        outBuf[5] = (int8_t)p.rssi;
        outBuf[6] = p.battery;
        outLen = 7;
        return true;
    }
    return false;
}

// Fast Stream State Machine (Core 0)
void BeaconPipeline::processBytes(const uint8_t* buffer, size_t len) {
    if (!buffer || len == 0) return;

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = buffer[i];

        if (s_rxFrameIdx == 0) {
            // Mencari sync byte header 0xAA
            if (b == 0xAA) {
                s_rxFrameBuf[s_rxFrameIdx++] = b;
            }
        } else {
            s_rxFrameBuf[s_rxFrameIdx++] = b;

            // Frame lengkap 24 byte tercapai
            if (s_rxFrameIdx == sizeof(BeaconBinaryFrame)) {
                if (Utils::verifyBeaconChecksum(s_rxFrameBuf)) {
                    const BeaconBinaryFrame* frame = (const BeaconBinaryFrame*)s_rxFrameBuf;

                    // Konversi Big-Endian ke Host Endian
                    uint16_t maj = (uint16_t)((frame->major >> 8) | (frame->major << 8));
                    uint16_t min = (uint16_t)((frame->minor >> 8) | (frame->minor << 8));

                    // Evaluasi Filter Hardware
                    char tagBuf[24];
                    snprintf(tagBuf, sizeof(tagBuf), "%05u-%05u", maj, min);
                    const char* codeStr = (frame->code == 0x01) ? "ibeacon_ext" : "iBeacon";

                    bool passRssi = (frame->rssi >= (int8_t)g_config.rssi_min);
                    if (!passRssi) {
                        recordDroppedBeacon(codeStr, tagBuf, frame->rssi, DropReason::RSSI_OUT_OF_RANGE);
                    } else if (g_config.filter_uuid_en && (std::memcmp(frame->uuid, g_config.filter_uuid_bytes, 16) != 0)) {
                        recordDroppedBeacon(codeStr, tagBuf, frame->rssi, DropReason::UUID_MISMATCH);
                    } else if (g_config.filter_major_en && !matchMajorFilter(maj, g_config.filter_major)) {
                        recordDroppedBeacon(codeStr, tagBuf, frame->rssi, DropReason::MAJOR_MISMATCH);
                    } else {
                        s_totalBeaconsRead++;

                        BeaconPacket packet;
                        packet.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
                        packet.type = (frame->code == 0x01) ? BeaconType::IBEACON_EXT : BeaconType::IBEACON;
                        packet.rssi = frame->rssi;
                        packet.battery = frame->battery;
                        packet.major = maj;
                        packet.minor = min;
                        std::memcpy(packet.uuid, frame->uuid, 16);

                        push(packet);
                        recordLiveBeacon(packet);
                    }
                }
                // Reset parser untuk frame berikutnya
                s_rxFrameIdx = 0;
            }
        }
    }
}

size_t BeaconPipeline::copyLiveBeacons(LiveBeaconItem* outItems, size_t maxItems) {
    if (!outItems || maxItems == 0) return 0;
    size_t count = 0;

    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_liveBeaconCount, maxItems);
        std::memcpy(outItems, s_liveBeaconBuf, count * sizeof(LiveBeaconItem));
        s_liveBeaconCount = 0; // Flush setelah disalin
        xSemaphoreGive(s_liveBufMutex);
    }
    return count;
}

void BeaconPipeline::flushLiveBeacons() {
    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_liveBeaconCount = 0;
        xSemaphoreGive(s_liveBufMutex);
    }
}

void BeaconPipeline::updateMetrics() {
    uint32_t curRead = s_totalBeaconsRead;
    uint32_t curPublished = s_totalMqttPublished;
    uint32_t curBatches = s_totalMqttBatches;

    s_readSpeed = curRead - s_lastReadSnap;
    s_mqttSpeed = curPublished - s_lastMqttPublishedSnap; // Murni paket per detik (pkt/s)!
    s_mqttBatchSpeed = curBatches - s_lastMqttBatchSnap;

    s_lastReadSnap = curRead;
    s_lastMqttPublishedSnap = curPublished;
    s_lastMqttBatchSnap = curBatches;
}

void BeaconPipeline::recordMqttPublish(uint32_t packetCount, uint32_t batchCount) {
    s_totalMqttPublished += packetCount;
    s_totalMqttBatches += batchCount;
}

void BeaconPipeline::recordMqttFailed(uint32_t packetCount) {
    s_totalMqttFailed += packetCount;
    s_dropMqttFailed += packetCount;

    char pktBuf[24];
    snprintf(pktBuf, sizeof(pktBuf), "%lu pkt", (unsigned long)packetCount);
    DropLogService::record("MQTT", pktBuf, 0, DropReason::MQTT_FAIL);

    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_droppedBeaconCount < DROPPED_FIFO_MAX) {
            DroppedBeaconItem& item = s_droppedBeaconBuf[s_droppedBeaconCount++];
            strncpy(item.code, "MQTT", sizeof(item.code) - 1);
            item.code[sizeof(item.code) - 1] = '\0';
            snprintf(item.tag, sizeof(item.tag), "%lu pkt", (unsigned long)packetCount);
            item.rssi = 0;
            item.reason = DropReason::MQTT_FAIL;
        }
        xSemaphoreGive(s_liveBufMutex);
    }
}

void BeaconPipeline::recordDroppedBeacon(const char* code, const char* tag, int8_t rssi, DropReason reason) {
    switch (reason) {
        case DropReason::TTL_EXPIRED:       s_dropTtlExpired++; break;
        case DropReason::RSSI_OUT_OF_RANGE: s_dropRssiMin++; break;
        case DropReason::UUID_MISMATCH:     s_dropUuidMismatch++; break;
        case DropReason::MAJOR_MISMATCH:    s_dropMajorMismatch++; break;
        case DropReason::QUEUE_OVERFLOW:    s_dropQueueOverflow++; break;
        case DropReason::MQTT_FAIL:         s_dropMqttFailed++; break;
    }

    DropLogService::record(code, tag, rssi, reason);

    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_droppedBeaconCount < DROPPED_FIFO_MAX) {
            DroppedBeaconItem& item = s_droppedBeaconBuf[s_droppedBeaconCount++];
            strncpy(item.code, code ? code : "BLE", sizeof(item.code) - 1);
            item.code[sizeof(item.code) - 1] = '\0';
            strncpy(item.tag, tag ? tag : "-", sizeof(item.tag) - 1);
            item.tag[sizeof(item.tag) - 1] = '\0';
            item.rssi = rssi;
            item.reason = reason;
        }
        xSemaphoreGive(s_liveBufMutex);
    }
}

void BeaconPipeline::recordDroppedBeacon(const BeaconPacket& p, DropReason reason) {
    const char* code = (p.type == BeaconType::IBEACON_EXT) ? "ibeacon_ext" : "iBeacon";
    char tagBuf[24];
    snprintf(tagBuf, sizeof(tagBuf), "%05u-%05u", p.major, p.minor);
    recordDroppedBeacon(code, tagBuf, p.rssi, reason);
}

size_t BeaconPipeline::copyDroppedBeacons(DroppedBeaconItem* outItems, size_t maxItems) {
    if (!outItems || maxItems == 0) return 0;
    size_t count = 0;

    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_droppedBeaconCount, maxItems);
        std::memcpy(outItems, s_droppedBeaconBuf, count * sizeof(DroppedBeaconItem));
        s_droppedBeaconCount = 0; // Flush setelah disalin
        xSemaphoreGive(s_liveBufMutex);
    }
    return count;
}

void BeaconPipeline::flushDroppedBeacons() {
    if (s_liveBufMutex != nullptr && xSemaphoreTake(s_liveBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_droppedBeaconCount = 0;
        xSemaphoreGive(s_liveBufMutex);
    }
}

uint32_t BeaconPipeline::getDropTtlCount() { return s_dropTtlExpired.load(); }
uint32_t BeaconPipeline::getDropRssiCount() { return s_dropRssiMin.load(); }
uint32_t BeaconPipeline::getDropUuidCount() { return s_dropUuidMismatch.load(); }
uint32_t BeaconPipeline::getDropMajorCount() { return s_dropMajorMismatch.load(); }
uint32_t BeaconPipeline::getDropQueueCount() { return s_dropQueueOverflow.load(); }
uint32_t BeaconPipeline::getDropMqttCount() { return s_dropMqttFailed.load(); }

uint32_t BeaconPipeline::getReadSpeed() { return s_readSpeed; }
uint32_t BeaconPipeline::getMqttSpeed() { return s_mqttSpeed; }
uint32_t BeaconPipeline::getMqttBatchSpeed() { return s_mqttBatchSpeed; }
uint32_t BeaconPipeline::getTotalBeaconsRead() { return s_totalBeaconsRead; }
uint32_t BeaconPipeline::getTotalMqttPublished() { return s_totalMqttPublished; }
uint32_t BeaconPipeline::getTotalMqttBatches() { return s_totalMqttBatches; }
uint32_t BeaconPipeline::getTotalMqttFailed() { return s_totalMqttFailed; }
uint32_t BeaconPipeline::getTotalQueueDropped() { return s_totalQueueDropped; }
uint32_t BeaconPipeline::getTotalDropped() { return s_totalQueueDropped + s_totalMqttFailed; }

size_t BeaconPipeline::getQueueLoad() {
    return (s_beaconQueue != nullptr) ? (size_t)uxQueueMessagesWaiting(s_beaconQueue) : 0;
}

QueueHandle_t BeaconPipeline::getQueue() {
    return s_beaconQueue;
}

} // namespace Iotaru::Core
