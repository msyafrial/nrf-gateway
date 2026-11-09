#include "beacon_pipeline.hpp"
#include "drop_log_service.hpp"
#include "mqtt_service.hpp"
#include "app_config.hpp"
#include "config_store.hpp"
#include "crc_utils.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <atomic>

static const char *TAG = "BEACON_PIPE";

namespace Iotaru::Core {

static QueueHandle_t s_beaconQueue = nullptr;
static SemaphoreHandle_t s_beaconBufMutex = nullptr;

static LiveBeaconItem s_liveBeaconBuf[LIVE_FIFO_MAX];
static size_t s_liveBeaconCount = 0;

static DroppedBeaconItem s_droppedBeaconBuf[DROPPED_FIFO_MAX];
static size_t s_droppedBeaconCount = 0;

static std::atomic<uint8_t> s_nrfScanState{0}; // 0 = STOPPED / Disconnected bawaan (hanya berubah saat terima ACK dari nRF)

static std::atomic<uint32_t> s_dropTtlExpired{0};
static std::atomic<uint32_t> s_dropRssiMin{0};
static std::atomic<uint32_t> s_dropUuidMismatch{0};
static std::atomic<uint32_t> s_dropMajorMismatch{0};
static std::atomic<uint32_t> s_dropUnknownFormat{0};
static std::atomic<uint32_t> s_dropQueueOverflow{0};
static std::atomic<uint32_t> s_dropMqttFailed{0};

static std::atomic<uint32_t> s_totalFramesIngress{0};
static std::atomic<uint32_t> s_totalBeaconsRead{0};
static std::atomic<uint32_t> s_totalMqttPublished{0};
static std::atomic<uint32_t> s_totalMqttBatches{0};
static std::atomic<uint32_t> s_totalMqttFailed{0};
static std::atomic<uint32_t> s_totalQueueDropped{0};
static std::atomic<uint32_t> s_checksumErrors{0};

static std::atomic<uint32_t> s_readSpeed{0};
static std::atomic<uint32_t> s_scanSpeed{0};
static std::atomic<uint32_t> s_dropSpeed{0};
static std::atomic<uint32_t> s_mqttSpeed{0};
static std::atomic<uint32_t> s_mqttBatchSpeed{0};
static uint32_t s_lastReadSnap = 0;
static uint32_t s_lastIngressSnap = 0;
static uint32_t s_lastDropSnap = 0;
static uint32_t s_lastMqttPublishedSnap = 0;
static uint32_t s_lastMqttBatchSnap = 0;

// Internal streaming state machine buffer
static uint8_t s_rxBuf[512];
static size_t  s_rxBufLen = 0;

struct ParsedAdvInfo {
    BeaconType type = BeaconType::RAW_BLE;
    char       code[16] = "BLE";
    char       tag[24] = "-";
    uint8_t    battery = 0;
    uint16_t   major = 0;
    uint16_t   minor = 0;
    uint8_t    uuid[16] = {0};
    bool       has_uuid = false;
    bool       has_major = false;
    bool       has_battery = false;
};

static ParsedAdvInfo parseAdvStructures(const uint8_t* payload, uint8_t len) {
    ParsedAdvInfo info;
    if (!payload || len == 0) return info;
    if (len > 31) len = 31;

    uint8_t idx = 0;
    while (idx < len) {
        uint8_t adLen = payload[idx];
        if (adLen == 0 || (idx + 1 + adLen) > len) {
            break;
        }
        uint8_t adType = payload[idx + 1];
        const uint8_t* adData = &payload[idx + 2];
        uint8_t dataLen = adLen - 1;

        // 1. Manufacturer Specific Data (0xFF)
        if (adType == 0xFF && dataLen >= 2) {
            uint16_t companyId = (uint16_t)(adData[0] | (adData[1] << 8));
            if (companyId == 0x004C) { // Apple Inc.
                if (dataLen >= 23 && adData[2] == 0x02 && adData[3] == 0x15) {
                    info.type = BeaconType::IBEACON;
                    strlcpy(info.code, "iBeacon", sizeof(info.code));
                    info.has_uuid = true;
                    std::memcpy(info.uuid, &adData[4], 16);
                    info.has_major = true;
                    info.major = (uint16_t)((adData[20] << 8) | adData[21]);
                    info.minor = (uint16_t)((adData[22] << 8) | adData[23]);
                    snprintf(info.tag, sizeof(info.tag), "%05u-%05u", info.major, info.minor);
                } else if (dataLen >= 23 && adData[2] == 0x01) {
                    info.type = BeaconType::IBEACON_EXT;
                    strlcpy(info.code, "ibeacon_ext", sizeof(info.code));
                    info.has_uuid = true;
                    std::memcpy(info.uuid, &adData[4], 16);
                    info.has_major = true;
                    info.major = (uint16_t)((adData[20] << 8) | adData[21]);
                    info.minor = (uint16_t)((adData[22] << 8) | adData[23]);
                    snprintf(info.tag, sizeof(info.tag), "%05u-%05u", info.major, info.minor);
                }
            } else if (companyId == 0x0008 || companyId == 0xFEAA || companyId == 0xAAFE ||
                       (dataLen >= 10 && adData[0] == 0x24 && adData[1] == 0x01) ||
                       (dataLen >= 12 && adData[0] == 0x15 && adData[1] == 0x02)) {
                if (info.type == BeaconType::RAW_BLE) {
                    info.type = BeaconType::MOKO_SENSOR;
                    strlcpy(info.code, "MOKO", sizeof(info.code));
                    snprintf(info.tag, sizeof(info.tag), "Mfg:%04X", companyId);
                }
            }
        }
        // 2. Service Data (0x16)
        else if (adType == 0x16 && dataLen >= 2) {
            uint16_t serviceUuid = (uint16_t)(adData[0] | (adData[1] << 8));
            if (serviceUuid == 0xFEAA) { // Eddystone
                info.type = BeaconType::EDDYSTONE;
                strlcpy(info.code, "Eddystone", sizeof(info.code));
                if (dataLen >= 3) {
                    uint8_t frameType = adData[2];
                    if (frameType == 0x00 && dataLen >= 19) { // UID
                        info.has_uuid = true;
                        std::memcpy(info.uuid, &adData[3], 16);
                        strlcpy(info.tag, "UID", sizeof(info.tag));
                    } else if (frameType == 0x10) { // URL
                        strlcpy(info.tag, "URL", sizeof(info.tag));
                    } else if (frameType == 0x20 && dataLen >= 5) { // TLM
                        uint16_t vbatt = (uint16_t)((adData[3] << 8) | adData[4]);
                        if (vbatt >= 2000 && vbatt <= 3300) {
                            info.battery = (uint8_t)(((vbatt - 2000) * 100) / 1300);
                            info.has_battery = true;
                        }
                        strlcpy(info.tag, "TLM", sizeof(info.tag));
                    }
                }
            } else if (serviceUuid == 0xEA01) { // Official MOKO BXP Device/Sensor Info
                info.type = BeaconType::MOKO_SENSOR;
                strlcpy(info.code, "MOKO", sizeof(info.code));
                strlcpy(info.tag, "BXP-Info", sizeof(info.tag));
                if (dataLen >= 20 && adData[2] == 0x80) {
                    uint16_t vbatt = (uint16_t)((adData[18] << 8) | adData[19]);
                    if (vbatt >= 2000 && vbatt <= 3300) {
                        info.battery = (uint8_t)(((vbatt - 2000) * 100) / 1300);
                        info.has_battery = true;
                    }
                }
            } else if (serviceUuid == 0xFEE0) { // Official MOKO BXP-B Alarm Button
                info.type = BeaconType::MOKO_ALARM;
                strlcpy(info.code, "MOKO", sizeof(info.code));
                strlcpy(info.tag, "BXP-Alarm", sizeof(info.tag));
            } else if (serviceUuid == 0xEA00) { // Official MOKO BXP General Scan Response
                info.type = BeaconType::MOKO_SENSOR;
                strlcpy(info.code, "MOKO", sizeof(info.code));
                strlcpy(info.tag, "BXP-ScanRsp", sizeof(info.tag));
            } else if (serviceUuid == 0xFEAB) { // Official MOKO BeaconX Pro Multi-Frame
                info.type = BeaconType::MOKO_SENSOR;
                strlcpy(info.code, "MOKO", sizeof(info.code));
                uint8_t subType = (dataLen >= 3) ? adData[2] : 0x00;
                if (subType == 0x40) strlcpy(info.tag, "BXP-Info", sizeof(info.tag));
                else if (subType == 0x60) strlcpy(info.tag, "BXP-Acc", sizeof(info.tag));
                else if (subType == 0x70) strlcpy(info.tag, "BXP-TH", sizeof(info.tag));
                else strlcpy(info.tag, "BXP-Pro", sizeof(info.tag));
            } else if (dataLen >= 22) { // Official Minew Format B (ibeacon_ext)
                info.type = BeaconType::IBEACON_EXT;
                strlcpy(info.code, "ibeacon_ext", sizeof(info.code));
                info.has_uuid = true;
                std::memcpy(info.uuid, &adData[0], 16);
                info.has_major = true;
                info.major = (uint16_t)((adData[16] << 8) | adData[17]);
                info.minor = (uint16_t)((adData[18] << 8) | adData[19]);
                snprintf(info.tag, sizeof(info.tag), "%05u-%05u", info.major, info.minor);
                info.battery = adData[21];
                info.has_battery = true;
            }
        }
        // 3. Complete/Shortened Local Name (0x08, 0x09)
        else if ((adType == 0x08 || adType == 0x09) && dataLen > 0) {
            if (info.type == BeaconType::RAW_BLE && info.tag[0] == '-') {
                size_t cpyLen = std::min((size_t)dataLen, sizeof(info.tag) - 1);
                std::memcpy(info.tag, adData, cpyLen);
                info.tag[cpyLen] = '\0';
            }
        }

        idx += 1 + adLen;
    }
    return info;
}

static void handleRawBleFrame(const RawBleBinaryFrame* frame) {
    if (!frame) return;

    s_totalFramesIngress.fetch_add(1, std::memory_order_relaxed);

    ParsedAdvInfo adv = parseAdvStructures(frame->payload, frame->payload_len);

    if (adv.tag[0] == '-' && adv.tag[1] == '\0') {
        snprintf(adv.tag, sizeof(adv.tag), "%02X%02X", frame->mac[4], frame->mac[5]);
    }

    // 1. Filter RSSI Universal
    bool passRssi = (frame->rssi >= (int8_t)g_config.rssi_min);
    if (!passRssi) {
        BeaconPipeline::recordDroppedBeacon(adv.code, adv.tag, frame->rssi, DropReason::RSSI_OUT_OF_RANGE,
                                            frame->mac, reinterpret_cast<const uint8_t*>(frame));
        return;
    }

    // 2. Conditional Filter UUID (HANYA jika paket memiliki UUID)
    if (g_config.filter_uuid_en && adv.has_uuid) {
        if (std::memcmp(adv.uuid, g_config.filter_uuid_bytes, 16) != 0) {
            BeaconPipeline::recordDroppedBeacon(adv.code, adv.tag, frame->rssi, DropReason::UUID_MISMATCH,
                                                frame->mac, reinterpret_cast<const uint8_t*>(frame));
            return;
        }
    }

    // 3. Conditional Filter Major (HANYA jika paket memiliki Major)
    if (g_config.filter_major_en && adv.has_major) {
        if (!Iotaru::matchMajorFilter(adv.major, g_config.filter_major)) {
            BeaconPipeline::recordDroppedBeacon(adv.code, adv.tag, frame->rssi, DropReason::MAJOR_MISMATCH,
                                                frame->mac, reinterpret_cast<const uint8_t*>(frame));
            return;
        }
    }

    // 4. Conditional Filter Reject Unknown BLE Format
    if (g_config.filter_unknown_en) {
        bool isKnown = (adv.type == BeaconType::IBEACON ||
                        adv.type == BeaconType::IBEACON_EXT ||
                        adv.type == BeaconType::EDDYSTONE ||
                        adv.type == BeaconType::MOKO_SENSOR ||
                        adv.type == BeaconType::MOKO_ALARM);
        if (!isKnown) {
            BeaconPipeline::recordDroppedBeacon(adv.code, adv.tag, frame->rssi, DropReason::UNKNOWN_FORMAT,
                                                frame->mac, reinterpret_cast<const uint8_t*>(frame));
            return;
        }
    }

    // Lolos seluruh filter!
    s_totalBeaconsRead++;

    BeaconPacket packet;
    packet.timestamp = (uint32_t)(esp_timer_get_time() / 1000);
    packet.type = adv.type;
    packet.rssi = frame->rssi;
    packet.battery = adv.battery;
    packet.major = adv.major;
    packet.minor = adv.minor;
    packet.has_uuid = adv.has_uuid;
    packet.has_major = adv.has_major;
    packet.adv_type = frame->adv_type;
    std::memcpy(packet.mac, frame->mac, 6);
    std::memcpy(packet.raw_frame, frame, 42);
    if (adv.has_uuid) {
        std::memcpy(packet.uuid, adv.uuid, 16);
    } else {
        std::memset(packet.uuid, 0, 16);
    }

    BeaconPipeline::push(packet);
    BeaconPipeline::recordLiveBeacon(packet);
}

void BeaconPipeline::init() {
    if (s_beaconBufMutex == nullptr) {
        s_beaconBufMutex = xSemaphoreCreateMutex();
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
            if (Network::MqttService::isConnected()) {
                s_totalQueueDropped++;
                recordDroppedBeacon(oldest, DropReason::QUEUE_OVERFLOW);
            }
            xQueueSend(s_beaconQueue, &packet, 0);
        }
    }
}

void BeaconPipeline::recordLiveBeacon(const BeaconPacket& p) {
    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_liveBeaconCount < LIVE_FIFO_MAX) {
            LiveBeaconItem& item = s_liveBeaconBuf[s_liveBeaconCount++];
            if (p.type == BeaconType::IBEACON) {
                strncpy(item.code, "iBeacon", sizeof(item.code) - 1);
            } else if (p.type == BeaconType::IBEACON_EXT) {
                strncpy(item.code, "ibeacon_ext", sizeof(item.code) - 1);
            } else if (p.type == BeaconType::EDDYSTONE) {
                strncpy(item.code, "Eddystone", sizeof(item.code) - 1);
            } else if (p.type == BeaconType::MOKO_SENSOR || p.type == BeaconType::MOKO_ALARM) {
                strncpy(item.code, "MOKO", sizeof(item.code) - 1);
            } else {
                strncpy(item.code, "BLE", sizeof(item.code) - 1);
            }
            item.code[sizeof(item.code) - 1] = '\0';

            if (p.has_major) {
                snprintf(item.tag, sizeof(item.tag), "%05u-%05u", p.major, p.minor);
            } else {
                snprintf(item.tag, sizeof(item.tag), "%02X%02X", p.mac[4], p.mac[5]);
            }

            item.rssi = p.rssi;
            item.batt = p.battery;
            item.adv_type = p.adv_type;
            std::memcpy(item.mac, p.mac, 6);
            std::memcpy(item.raw_frame, p.raw_frame, 42);
        }
        xSemaphoreGive(s_beaconBufMutex);
    }
}

void BeaconPipeline::pruneExpired() {
    if (s_beaconQueue == nullptr || g_config.data_ttl <= 0) return;
    BeaconPacket front;
    uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);

    while (xQueuePeek(s_beaconQueue, &front, 0) == pdTRUE) {
        if ((nowMs - front.timestamp) > (uint32_t)g_config.data_ttl) {
            BeaconPacket discarded;
            if (xQueueReceive(s_beaconQueue, &discarded, 0) == pdTRUE) {
                if (Network::MqttService::isConnected()) {
                    s_totalQueueDropped++;
                    recordDroppedBeacon(discarded, DropReason::TTL_EXPIRED);
                }
            }
        } else {
            break;
        }
    }
}

void BeaconPipeline::flushQueue() {
    if (s_beaconQueue != nullptr) {
        xQueueReset(s_beaconQueue);
        ESP_LOGI(TAG, "Antrean beacon di-flush bersih (MQTT connected).");
    }
}

// Fast Stream State Machine (Core 0)
void BeaconPipeline::resetStreamParser() {
    s_rxBufLen = 0;
}

void BeaconPipeline::processBytes(const uint8_t* buffer, size_t len) {
    if (!buffer || len == 0) return;

    size_t inIdx = 0;
    while (inIdx < len) {
        size_t toCopy = std::min(len - inIdx, sizeof(s_rxBuf) - s_rxBufLen);
        if (toCopy > 0) {
            std::memcpy(s_rxBuf + s_rxBufLen, buffer + inIdx, toCopy);
            s_rxBufLen += toCopy;
            inIdx += toCopy;
        }

        size_t readIdx = 0;
        while (readIdx < s_rxBufLen) {
            uint8_t h = s_rxBuf[readIdx];
            if (h == 0xAA) {
                if (s_rxBufLen - readIdx < sizeof(RawBleBinaryFrame)) {
                    break; // Tunggu kelengkapan 42B
                }
                const uint8_t* ptr = s_rxBuf + readIdx;
                if (Utils::verifyRawBleChecksum(ptr)) {
                    const RawBleBinaryFrame* frame = reinterpret_cast<const RawBleBinaryFrame*>(ptr);
                    handleRawBleFrame(frame);
                    readIdx += sizeof(RawBleBinaryFrame);
                } else {
                    s_checksumErrors.fetch_add(1, std::memory_order_relaxed);
                    readIdx++;
                }
            } else if (h == 0xBB) {
                if (s_rxBufLen - readIdx < sizeof(ControlRspFrame)) {
                    break; // Tunggu kelengkapan 4B
                }
                const uint8_t* ptr = s_rxBuf + readIdx;
                if (Utils::verifyControlRspChecksum(ptr)) {
                    const ControlRspFrame* rsp = reinterpret_cast<const ControlRspFrame*>(ptr);
                    s_nrfScanState.store(rsp->scan_state, std::memory_order_relaxed);
                    s_totalFramesIngress.fetch_add(1, std::memory_order_relaxed);
                    ESP_LOGI(TAG, "nRF52840 Status Rsp: type=0x%02X, state=%u", rsp->rsp_type, rsp->scan_state);
                    readIdx += sizeof(ControlRspFrame);
                } else {
                    readIdx++;
                }
            } else {
                readIdx++;
            }
        }

        if (readIdx > 0) {
            size_t remaining = s_rxBufLen - readIdx;
            if (remaining > 0) {
                std::memmove(s_rxBuf, s_rxBuf + readIdx, remaining);
            }
            s_rxBufLen = remaining;
        } else if (s_rxBufLen == sizeof(s_rxBuf)) {
            std::memmove(s_rxBuf, s_rxBuf + 1, sizeof(s_rxBuf) - 1);
            s_rxBufLen = sizeof(s_rxBuf) - 1;
        }
    }
}

size_t BeaconPipeline::copyLiveBeacons(LiveBeaconItem* outItems, size_t maxItems) {
    if (!outItems || maxItems == 0) return 0;
    size_t count = 0;

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_liveBeaconCount, maxItems);
        std::memcpy(outItems, s_liveBeaconBuf, count * sizeof(LiveBeaconItem));
        s_liveBeaconCount = 0;
        xSemaphoreGive(s_beaconBufMutex);
    }
    return count;
}

void BeaconPipeline::flushLiveBeacons() {
    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_liveBeaconCount = 0;
        xSemaphoreGive(s_beaconBufMutex);
    }
}

uint8_t BeaconPipeline::getNrfScanState() {
    return s_nrfScanState.load(std::memory_order_relaxed);
}

void BeaconPipeline::updateMetrics() {
    uint32_t curRead = s_totalBeaconsRead;
    uint32_t curPublished = s_totalMqttPublished;
    uint32_t curBatches = s_totalMqttBatches;
    uint32_t curIngress = s_totalFramesIngress.load(std::memory_order_relaxed);
    uint32_t curDrop = getTotalDropped();

    s_readSpeed = (curRead >= s_lastReadSnap) ? (curRead - s_lastReadSnap) : 0;
    s_scanSpeed = (curIngress >= s_lastIngressSnap) ? (curIngress - s_lastIngressSnap) : 0;
    s_dropSpeed = (curDrop >= s_lastDropSnap) ? (curDrop - s_lastDropSnap) : 0;
    s_mqttSpeed = (curPublished >= s_lastMqttPublishedSnap) ? (curPublished - s_lastMqttPublishedSnap) : 0;
    s_mqttBatchSpeed = (curBatches >= s_lastMqttBatchSnap) ? (curBatches - s_lastMqttBatchSnap) : 0;

    s_lastReadSnap = curRead;
    s_lastIngressSnap = curIngress;
    s_lastDropSnap = curDrop;
    s_lastMqttPublishedSnap = curPublished;
    s_lastMqttBatchSnap = curBatches;
}

void BeaconPipeline::recordMqttPublish(uint32_t packetCount, uint32_t batchCount) {
    s_totalMqttPublished += packetCount;
    s_totalMqttBatches += batchCount;
}

void BeaconPipeline::recordMqttFailed(uint32_t packetCount) {
    if (packetCount == 0) return;

    s_totalMqttFailed += packetCount;
    s_dropMqttFailed += packetCount;

    char pktBuf[24];
    snprintf(pktBuf, sizeof(pktBuf), "%lu pkt", (unsigned long)packetCount);
    DropLogService::record("MQTT", pktBuf, 0, DropReason::MQTT_FAIL, packetCount);

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_droppedBeaconCount < DROPPED_FIFO_MAX) {
            DroppedBeaconItem& item = s_droppedBeaconBuf[s_droppedBeaconCount++];
            strncpy(item.code, "MQTT", sizeof(item.code) - 1);
            item.code[sizeof(item.code) - 1] = '\0';
            snprintf(item.tag, sizeof(item.tag), "%lu pkt", (unsigned long)packetCount);
            item.rssi = 0;
            item.reason = DropReason::MQTT_FAIL;
            std::memset(item.mac, 0, 6);
            std::memset(item.raw_frame, 0, 42);
        }
        xSemaphoreGive(s_beaconBufMutex);
    }
}

void BeaconPipeline::recordDroppedBeacon(const char* code, const char* tag, int8_t rssi, DropReason reason,
                                         const uint8_t* mac, const uint8_t* raw42) {
    if (reason != DropReason::MQTT_FAIL && !Network::MqttService::isConnected()) {
        return;
    }

    switch (reason) {
        case DropReason::TTL_EXPIRED:       s_dropTtlExpired++; break;
        case DropReason::RSSI_OUT_OF_RANGE: s_dropRssiMin++; break;
        case DropReason::UUID_MISMATCH:     s_dropUuidMismatch++; break;
        case DropReason::MAJOR_MISMATCH:    s_dropMajorMismatch++; break;
        case DropReason::UNKNOWN_FORMAT:    s_dropUnknownFormat++; break;
        case DropReason::QUEUE_OVERFLOW:    s_dropQueueOverflow++; break;
        case DropReason::MQTT_FAIL:         s_dropMqttFailed++; break;
    }

    char macStr[18] = "-";
    if (mac) {
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    char rawStr[86] = "-";
    if (raw42) {
        for (int i = 0; i < 42; ++i) {
            snprintf(rawStr + i * 2, sizeof(rawStr) - i * 2, "%02X", raw42[i]);
        }
    }

    DropLogService::record(code, tag, rssi, reason, 1, macStr, rawStr);

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (s_droppedBeaconCount < DROPPED_FIFO_MAX) {
            DroppedBeaconItem& item = s_droppedBeaconBuf[s_droppedBeaconCount++];
            strncpy(item.code, code ? code : "BLE", sizeof(item.code) - 1);
            item.code[sizeof(item.code) - 1] = '\0';
            strncpy(item.tag, tag ? tag : "-", sizeof(item.tag) - 1);
            item.tag[sizeof(item.tag) - 1] = '\0';
            item.rssi = rssi;
            item.reason = reason;
            if (mac) {
                std::memcpy(item.mac, mac, 6);
            } else {
                std::memset(item.mac, 0, 6);
            }
            if (raw42) {
                std::memcpy(item.raw_frame, raw42, 42);
            } else {
                std::memset(item.raw_frame, 0, 42);
            }
        }
        xSemaphoreGive(s_beaconBufMutex);
    }
}

void BeaconPipeline::recordDroppedBeacon(const BeaconPacket& p, DropReason reason) {
    const char* code = (p.type == BeaconType::IBEACON_EXT) ? "ibeacon_ext" :
                       (p.type == BeaconType::IBEACON) ? "iBeacon" :
                       (p.type == BeaconType::EDDYSTONE) ? "Eddystone" :
                       (p.type == BeaconType::MOKO_SENSOR || p.type == BeaconType::MOKO_ALARM) ? "MOKO" : "BLE";
    char tagBuf[24];
    if (p.has_major) {
        snprintf(tagBuf, sizeof(tagBuf), "%05u-%05u", p.major, p.minor);
    } else {
        snprintf(tagBuf, sizeof(tagBuf), "%02X%02X", p.mac[4], p.mac[5]);
    }
    recordDroppedBeacon(code, tagBuf, p.rssi, reason, p.mac, p.raw_frame);
}

size_t BeaconPipeline::copyDroppedBeacons(DroppedBeaconItem* outItems, size_t maxItems) {
    if (!outItems || maxItems == 0) return 0;
    size_t count = 0;

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_droppedBeaconCount, maxItems);
        std::memcpy(outItems, s_droppedBeaconBuf, count * sizeof(DroppedBeaconItem));
        s_droppedBeaconCount = 0;
        xSemaphoreGive(s_beaconBufMutex);
    }
    return count;
}

void BeaconPipeline::flushDroppedBeacons() {
    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_droppedBeaconCount = 0;
        xSemaphoreGive(s_beaconBufMutex);
    }
}

uint32_t BeaconPipeline::getDropTtlCount() { return s_dropTtlExpired.load(); }
uint32_t BeaconPipeline::getDropRssiCount() { return s_dropRssiMin.load(); }
uint32_t BeaconPipeline::getDropUuidCount() { return s_dropUuidMismatch.load(); }
uint32_t BeaconPipeline::getDropMajorCount() { return s_dropMajorMismatch.load(); }
uint32_t BeaconPipeline::getDropUnknownCount() { return s_dropUnknownFormat.load(); }
uint32_t BeaconPipeline::getDropQueueCount() { return s_dropQueueOverflow.load(); }
uint32_t BeaconPipeline::getDropMqttCount() { return s_dropMqttFailed.load(); }

uint32_t BeaconPipeline::getReadSpeed() { return s_readSpeed.load(std::memory_order_relaxed); }
uint32_t BeaconPipeline::getScanSpeed() { return s_scanSpeed.load(std::memory_order_relaxed); }
uint32_t BeaconPipeline::getDropSpeed() { return s_dropSpeed.load(std::memory_order_relaxed); }
uint32_t BeaconPipeline::getMqttSpeed() { return s_mqttSpeed.load(std::memory_order_relaxed); }
uint32_t BeaconPipeline::getMqttBatchSpeed() { return s_mqttBatchSpeed.load(std::memory_order_relaxed); }
uint32_t BeaconPipeline::getTotalFramesIngress() { return s_totalFramesIngress.load(std::memory_order_relaxed); }
uint32_t BeaconPipeline::getTotalBeaconsRead() { return s_totalBeaconsRead; }
uint32_t BeaconPipeline::getTotalMqttPublished() { return s_totalMqttPublished; }
uint32_t BeaconPipeline::getTotalMqttBatches() { return s_totalMqttBatches; }
uint32_t BeaconPipeline::getTotalMqttFailed() { return s_totalMqttFailed; }
uint32_t BeaconPipeline::getTotalQueueDropped() { return s_totalQueueDropped; }
uint32_t BeaconPipeline::getTotalDropped() {
    return s_dropTtlExpired.load(std::memory_order_relaxed) +
           s_dropRssiMin.load(std::memory_order_relaxed) +
           s_dropUuidMismatch.load(std::memory_order_relaxed) +
           s_dropMajorMismatch.load(std::memory_order_relaxed) +
           s_dropUnknownFormat.load(std::memory_order_relaxed) +
           s_dropQueueOverflow.load(std::memory_order_relaxed) +
           s_dropMqttFailed.load(std::memory_order_relaxed);
}

void BeaconPipeline::resetDropCounters() {
    s_totalFramesIngress.store(0, std::memory_order_relaxed);
    s_dropTtlExpired.store(0, std::memory_order_relaxed);
    s_dropRssiMin.store(0, std::memory_order_relaxed);
    s_dropUuidMismatch.store(0, std::memory_order_relaxed);
    s_dropMajorMismatch.store(0, std::memory_order_relaxed);
    s_dropUnknownFormat.store(0, std::memory_order_relaxed);
    s_dropQueueOverflow.store(0, std::memory_order_relaxed);
    s_dropMqttFailed.store(0, std::memory_order_relaxed);
    s_totalQueueDropped.store(0, std::memory_order_relaxed);
    s_totalMqttFailed.store(0, std::memory_order_relaxed);
    s_checksumErrors.store(0, std::memory_order_relaxed);
    s_lastIngressSnap = 0;
    s_lastDropSnap = 0;
    s_scanSpeed = 0;
    s_dropSpeed = 0;
    flushDroppedBeacons();
}

size_t BeaconPipeline::getQueueLoad() {
    return (s_beaconQueue != nullptr) ? (size_t)uxQueueMessagesWaiting(s_beaconQueue) : 0;
}

QueueHandle_t BeaconPipeline::getQueue() {
    return s_beaconQueue;
}

} // namespace Iotaru::Core
