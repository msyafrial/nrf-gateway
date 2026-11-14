#include "beacon_pipeline.hpp"
#include "drop_log_service.hpp"
#include "mqtt_service.hpp"
#include "app_config.hpp"
#include "app_time.hpp"
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
static std::atomic<uint32_t> s_queueEpoch{0};
static Core0FilterSnapshot s_c0Filter;

static LiveBeaconItem s_liveBeaconBuf[LIVE_FIFO_MAX];
static std::atomic<size_t> s_liveBeaconCount{0};

static DroppedBeaconItem s_droppedBeaconBuf[DROPPED_FIFO_MAX];
static std::atomic<size_t> s_droppedBeaconCount{0};

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

// Internal streaming remainder buffer (hemat 448B .bss RAM, zero-copy parsing)
static uint8_t s_rxRem[64];
static size_t  s_rxRemLen = 0;

static constexpr char HEX_LUT[] = "0123456789ABCDEF";

static inline void fastBytesToHex(const uint8_t* in, size_t inLen, char* outHex) {
    for (size_t i = 0; i < inLen; ++i) {
        outHex[i * 2]     = HEX_LUT[(in[i] >> 4) & 0x0F];
        outHex[i * 2 + 1] = HEX_LUT[in[i] & 0x0F];
    }
    outHex[inLen * 2] = '\0';
}

static inline void fastMacToStr(const uint8_t* mac, char* outStr) {
    if (!mac) {
        outStr[0] = '-';
        outStr[1] = '\0';
        return;
    }
    for (int i = 0; i < 6; ++i) {
        outStr[i * 3]     = HEX_LUT[(mac[i] >> 4) & 0x0F];
        outStr[i * 3 + 1] = HEX_LUT[mac[i] & 0x0F];
        if (i < 5) outStr[i * 3 + 2] = ':';
    }
    outStr[17] = '\0';
}

static const char* getBeaconTypeCode(BeaconType type) {
    switch (type) {
        case BeaconType::IBEACON:     return "iBeacon";
        case BeaconType::IBEACON_EXT: return "ibeacon_ext";
        case BeaconType::EDDYSTONE:   return "Eddystone";
        case BeaconType::MOKO_SENSOR:
        case BeaconType::MOKO_ALARM:  return "MOKO";
        default:                      return "BLE";
    }
}

static void formatTag(char* outTag, size_t maxLen, bool has_major, uint16_t major, uint16_t minor, const uint8_t* mac) {
    if (has_major) {
        snprintf(outTag, maxLen, "%05u-%05u", major, minor);
    } else if (mac) {
        snprintf(outTag, maxLen, "%02X%02X", mac[4], mac[5]);
    } else {
        strlcpy(outTag, "-", maxLen);
    }
}

struct ParsedAdvInfo {
    BeaconType type = BeaconType::RAW_BLE;
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
                    info.has_uuid = true;
                    std::memcpy(info.uuid, &adData[4], 16);
                    info.has_major = true;
                    info.major = (uint16_t)((adData[20] << 8) | adData[21]);
                    info.minor = (uint16_t)((adData[22] << 8) | adData[23]);
                } else if (dataLen >= 23 && adData[2] == 0x01) {
                    info.type = BeaconType::IBEACON_EXT;
                    info.has_uuid = true;
                    std::memcpy(info.uuid, &adData[4], 16);
                    info.has_major = true;
                    info.major = (uint16_t)((adData[20] << 8) | adData[21]);
                    info.minor = (uint16_t)((adData[22] << 8) | adData[23]);
                }
            } else if (companyId == 0x0008 || companyId == 0xFEAA || companyId == 0xAAFE ||
                       (dataLen >= 10 && adData[0] == 0x24 && adData[1] == 0x01) ||
                       (dataLen >= 12 && adData[0] == 0x15 && adData[1] == 0x02)) {
                if (info.type == BeaconType::RAW_BLE) {
                    info.type = BeaconType::MOKO_SENSOR;
                }
            }
        }
        // 2. Service Data (0x16)
        else if (adType == 0x16 && dataLen >= 2) {
            uint16_t serviceUuid = (uint16_t)(adData[0] | (adData[1] << 8));
            if (serviceUuid == 0xFEAA) { // Eddystone
                info.type = BeaconType::EDDYSTONE;
                if (dataLen >= 3) {
                    uint8_t frameType = adData[2];
                    if (frameType == 0x00 && dataLen >= 19) { // UID
                        info.has_uuid = true;
                        std::memcpy(info.uuid, &adData[3], 16);
                    } else if (frameType == 0x20 && dataLen >= 5) { // TLM
                        uint16_t vbatt = (uint16_t)((adData[3] << 8) | adData[4]);
                        if (vbatt >= 2000 && vbatt <= 3300) {
                            info.battery = (uint8_t)(((vbatt - 2000) * 100) / 1300);
                            info.has_battery = true;
                        }
                    }
                }
            } else if (serviceUuid == 0xEA01) { // Official MOKO BXP Device/Sensor Info
                info.type = BeaconType::MOKO_SENSOR;
                if (dataLen >= 20 && adData[2] == 0x80) {
                    uint16_t vbatt = (uint16_t)((adData[18] << 8) | adData[19]);
                    if (vbatt >= 2000 && vbatt <= 3300) {
                        info.battery = (uint8_t)(((vbatt - 2000) * 100) / 1300);
                        info.has_battery = true;
                    }
                }
            } else if (serviceUuid == 0xFEE0) { // Official MOKO BXP-B Alarm Button
                info.type = BeaconType::MOKO_ALARM;
            } else if (serviceUuid == 0xEA00) { // Official MOKO BXP General Scan Response
                info.type = BeaconType::MOKO_SENSOR;
            } else if (serviceUuid == 0xFEAB) { // Official MOKO BeaconX Pro Multi-Frame
                info.type = BeaconType::MOKO_SENSOR;
            } else if (dataLen >= 22) { // Official Minew Format B (ibeacon_ext)
                info.type = BeaconType::IBEACON_EXT;
                info.has_uuid = true;
                std::memcpy(info.uuid, &adData[0], 16);
                info.has_major = true;
                info.major = (uint16_t)((adData[16] << 8) | adData[17]);
                info.minor = (uint16_t)((adData[18] << 8) | adData[19]);
                info.battery = adData[21];
                info.has_battery = true;
            }
        }

        idx += 1 + adLen;
    }
    return info;
}

static void handleRawBleFrame(const RawBleBinaryFrame* frame) {
    if (!frame) return;

    s_totalFramesIngress.fetch_add(1, std::memory_order_relaxed);

    uint32_t cfgGen = getConfigGeneration();
    if (s_c0Filter.gen != cfgGen) {
        copyFilterSnapshot(s_c0Filter);
    }

    ParsedAdvInfo adv = parseAdvStructures(frame->payload, frame->payload_len);

    // 1. Filter RSSI Universal
    bool passRssi = (frame->rssi >= s_c0Filter.rssi_min);
    if (!passRssi) {
        char tag[24];
        formatTag(tag, sizeof(tag), adv.has_major, adv.major, adv.minor, frame->mac);
        BeaconPipeline::recordDroppedBeacon(getBeaconTypeCode(adv.type), tag, frame->rssi,
                                            DropReason::RSSI_OUT_OF_RANGE,
                                            frame->mac, reinterpret_cast<const uint8_t*>(frame));
        return;
    }

    // 2. Conditional Filter UUID (HANYA jika paket memiliki UUID)
    if (s_c0Filter.filter_uuid_en && adv.has_uuid) {
        if (std::memcmp(adv.uuid, s_c0Filter.filter_uuid_bytes, 16) != 0) {
            char tag[24];
            formatTag(tag, sizeof(tag), adv.has_major, adv.major, adv.minor, frame->mac);
            BeaconPipeline::recordDroppedBeacon(getBeaconTypeCode(adv.type), tag, frame->rssi,
                                                DropReason::UUID_MISMATCH,
                                                frame->mac, reinterpret_cast<const uint8_t*>(frame));
            return;
        }
    }

    // 3. Conditional Filter Major (HANYA jika paket memiliki Major)
    if (s_c0Filter.filter_major_en && adv.has_major && s_c0Filter.major_count > 0) {
        bool majorMatch = false;
        for (uint8_t i = 0; i < s_c0Filter.major_count; ++i) {
            if (s_c0Filter.majors[i] == adv.major) {
                majorMatch = true;
                break;
            }
        }
        if (!majorMatch) {
            char tag[24];
            formatTag(tag, sizeof(tag), adv.has_major, adv.major, adv.minor, frame->mac);
            BeaconPipeline::recordDroppedBeacon(getBeaconTypeCode(adv.type), tag, frame->rssi,
                                                DropReason::MAJOR_MISMATCH,
                                                frame->mac, reinterpret_cast<const uint8_t*>(frame));
            return;
        }
    }

    // 4. Conditional Filter Reject Unknown BLE Format
    if (s_c0Filter.filter_unknown_en) {
        bool isKnown = (adv.type == BeaconType::IBEACON ||
                        adv.type == BeaconType::IBEACON_EXT ||
                        adv.type == BeaconType::EDDYSTONE ||
                        adv.type == BeaconType::MOKO_SENSOR ||
                        adv.type == BeaconType::MOKO_ALARM);
        if (!isKnown) {
            char tag[24];
            formatTag(tag, sizeof(tag), adv.has_major, adv.major, adv.minor, frame->mac);
            BeaconPipeline::recordDroppedBeacon(getBeaconTypeCode(adv.type), tag, frame->rssi,
                                                DropReason::UNKNOWN_FORMAT,
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
    packet.has_major = adv.has_major;
    packet.major = adv.major;
    packet.minor = adv.minor;
    std::memcpy(packet.raw_frame, frame, 42);

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
    if (s_liveBeaconCount.load(std::memory_order_relaxed) >= LIVE_FIFO_MAX) {
        return; // Fast-path: bypass mutex lock entirely jika FIFO sudah penuh
    }

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(2)) == pdTRUE) {
        size_t count = s_liveBeaconCount.load(std::memory_order_relaxed);
        if (count < LIVE_FIFO_MAX) {
            LiveBeaconItem& item = s_liveBeaconBuf[count];
            strncpy(item.code, getBeaconTypeCode(p.type), sizeof(item.code) - 1);
            item.code[sizeof(item.code) - 1] = '\0';

            const RawBleBinaryFrame* raw = reinterpret_cast<const RawBleBinaryFrame*>(p.raw_frame);
            formatTag(item.tag, sizeof(item.tag), p.has_major, p.major, p.minor, raw->mac);

            item.rssi = p.rssi;
            item.batt = p.battery;
            item.adv_type = raw->adv_type;
            std::memcpy(item.mac, raw->mac, 6);
            std::memcpy(item.raw_frame, p.raw_frame, 42);
            s_liveBeaconCount.store(count + 1, std::memory_order_relaxed);
        }
        xSemaphoreGive(s_beaconBufMutex);
    }
}

void BeaconPipeline::pruneExpired() {
    if (s_beaconQueue == nullptr || g_config.data_ttl <= 0) return;
    BeaconPacket front;
    uint32_t nowMs = static_cast<uint32_t>(Iotaru::monotonicMs());
    uint32_t epoch = s_queueEpoch.load(std::memory_order_acquire);

    while (s_queueEpoch.load(std::memory_order_acquire) == epoch &&
           xQueuePeek(s_beaconQueue, &front, 0) == pdTRUE) {
        if (Iotaru::elapsedMs32(nowMs, front.timestamp) > (uint32_t)g_config.data_ttl) {
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
        s_queueEpoch.fetch_add(1, std::memory_order_acq_rel);
        xQueueReset(s_beaconQueue);
        ESP_LOGI(TAG, "Antrean beacon di-flush bersih (MQTT connected).");
    }
}

// Fast Zero-Copy Stream State Machine (Core 0)
void BeaconPipeline::resetStreamParser() {
    s_rxRemLen = 0;
}

void BeaconPipeline::processBytes(const uint8_t* buffer, size_t len) {
    if (!buffer || len == 0) return;

    size_t inIdx = 0;

    // 1. Jika ada sisa byte dari chunk sebelumnya di s_rxRem, lengkapi frame terlebih dahulu
    while (s_rxRemLen > 0 && inIdx < len) {
        if (s_rxRem[0] != 0xAA && s_rxRem[0] != 0xBB) {
            size_t headPos = 0;
            for (size_t i = 1; i < s_rxRemLen; ++i) {
                if (s_rxRem[i] == 0xAA || s_rxRem[i] == 0xBB) {
                    headPos = i;
                    break;
                }
            }
            if (headPos > 0) {
                std::memmove(s_rxRem, s_rxRem + headPos, s_rxRemLen - headPos);
                s_rxRemLen -= headPos;
            } else {
                s_rxRemLen = 0;
                break;
            }
        }

        if (s_rxRem[0] == 0xAA) {
            size_t needed = sizeof(RawBleBinaryFrame) - s_rxRemLen;
            size_t take = std::min(needed, len - inIdx);
            std::memcpy(s_rxRem + s_rxRemLen, buffer + inIdx, take);
            s_rxRemLen += take;
            inIdx += take;

            if (s_rxRemLen == sizeof(RawBleBinaryFrame)) {
                if (Utils::verifyRawBleChecksum(s_rxRem)) {
                    handleRawBleFrame(reinterpret_cast<const RawBleBinaryFrame*>(s_rxRem));
                    s_rxRemLen = 0;
                } else {
                    s_checksumErrors.fetch_add(1, std::memory_order_relaxed);
                    std::memmove(s_rxRem, s_rxRem + 1, sizeof(RawBleBinaryFrame) - 1);
                    s_rxRemLen = sizeof(RawBleBinaryFrame) - 1;
                }
            }
        } else if (s_rxRem[0] == 0xBB) {
            size_t needed = sizeof(ControlRspFrame) - s_rxRemLen;
            size_t take = std::min(needed, len - inIdx);
            std::memcpy(s_rxRem + s_rxRemLen, buffer + inIdx, take);
            s_rxRemLen += take;
            inIdx += take;

            if (s_rxRemLen == sizeof(ControlRspFrame)) {
                if (Utils::verifyControlRspChecksum(s_rxRem)) {
                    const ControlRspFrame* rsp = reinterpret_cast<const ControlRspFrame*>(s_rxRem);
                    s_nrfScanState.store(rsp->scan_state, std::memory_order_relaxed);
                    s_totalFramesIngress.fetch_add(1, std::memory_order_relaxed);
                    ESP_LOGI(TAG, "nRF52840 Status Rsp: type=0x%02X, state=%u", rsp->rsp_type, rsp->scan_state);
                    s_rxRemLen = 0;
                } else {
                    std::memmove(s_rxRem, s_rxRem + 1, sizeof(ControlRspFrame) - 1);
                    s_rxRemLen = sizeof(ControlRspFrame) - 1;
                }
            }
        }
    }

    // 2. Proses frame utuh secara zero-copy langsung dari pointer buffer
    while (inIdx < len) {
        uint8_t h = buffer[inIdx];
        if (h == 0xAA) {
            if (len - inIdx < sizeof(RawBleBinaryFrame)) {
                size_t rem = len - inIdx;
                std::memcpy(s_rxRem, buffer + inIdx, rem);
                s_rxRemLen = rem;
                break;
            }
            const uint8_t* ptr = buffer + inIdx;
            if (Utils::verifyRawBleChecksum(ptr)) {
                const RawBleBinaryFrame* frame = reinterpret_cast<const RawBleBinaryFrame*>(ptr);
                handleRawBleFrame(frame);
                inIdx += sizeof(RawBleBinaryFrame);
            } else {
                s_checksumErrors.fetch_add(1, std::memory_order_relaxed);
                inIdx++;
            }
        } else if (h == 0xBB) {
            if (len - inIdx < sizeof(ControlRspFrame)) {
                size_t rem = len - inIdx;
                std::memcpy(s_rxRem, buffer + inIdx, rem);
                s_rxRemLen = rem;
                break;
            }
            const uint8_t* ptr = buffer + inIdx;
            if (Utils::verifyControlRspChecksum(ptr)) {
                const ControlRspFrame* rsp = reinterpret_cast<const ControlRspFrame*>(ptr);
                s_nrfScanState.store(rsp->scan_state, std::memory_order_relaxed);
                s_totalFramesIngress.fetch_add(1, std::memory_order_relaxed);
                ESP_LOGI(TAG, "nRF52840 Status Rsp: type=0x%02X, state=%u", rsp->rsp_type, rsp->scan_state);
                inIdx += sizeof(ControlRspFrame);
            } else {
                inIdx++;
            }
        } else {
            inIdx++;
        }
    }
}

size_t BeaconPipeline::copyLiveBeacons(LiveBeaconItem* outItems, size_t maxItems) {
    if (!outItems || maxItems == 0) return 0;
    size_t count = 0;

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_liveBeaconCount.load(std::memory_order_relaxed), maxItems);
        std::memcpy(outItems, s_liveBeaconBuf, count * sizeof(LiveBeaconItem));
        s_liveBeaconCount.store(0, std::memory_order_relaxed);
        xSemaphoreGive(s_beaconBufMutex);
    }
    return count;
}

void BeaconPipeline::flushLiveBeacons() {
    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_liveBeaconCount.store(0, std::memory_order_relaxed);
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

    s_readSpeed = static_cast<uint32_t>(curRead - s_lastReadSnap);
    s_scanSpeed = static_cast<uint32_t>(curIngress - s_lastIngressSnap);
    s_dropSpeed = static_cast<uint32_t>(curDrop - s_lastDropSnap);
    s_mqttSpeed = static_cast<uint32_t>(curPublished - s_lastMqttPublishedSnap);
    s_mqttBatchSpeed = static_cast<uint32_t>(curBatches - s_lastMqttBatchSnap);

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

    if (s_droppedBeaconCount.load(std::memory_order_relaxed) < DROPPED_FIFO_MAX) {
        if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            size_t count = s_droppedBeaconCount.load(std::memory_order_relaxed);
            if (count < DROPPED_FIFO_MAX) {
                DroppedBeaconItem& item = s_droppedBeaconBuf[count];
                strncpy(item.code, "MQTT", sizeof(item.code) - 1);
                item.code[sizeof(item.code) - 1] = '\0';
                snprintf(item.tag, sizeof(item.tag), "%lu pkt", (unsigned long)packetCount);
                item.rssi = 0;
                item.reason = DropReason::MQTT_FAIL;
                std::memset(item.mac, 0, 6);
                std::memset(item.raw_frame, 0, 42);
                s_droppedBeaconCount.store(count + 1, std::memory_order_relaxed);
            }
            xSemaphoreGive(s_beaconBufMutex);
        }
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

    static std::atomic<int64_t> s_lastDetailFormatUs{0};
    if (reason != DropReason::MQTT_FAIL && reason != DropReason::QUEUE_OVERFLOW) {
        int64_t nowUs = esp_timer_get_time();
        int64_t prevUs = s_lastDetailFormatUs.load(std::memory_order_relaxed);
        if ((nowUs - prevUs) < 25000LL) {
            DropLogService::record(code, tag, rssi, reason, 1, nullptr, nullptr);
            return;
        }
        s_lastDetailFormatUs.store(nowUs, std::memory_order_relaxed);
    }

    char macStr[18];
    fastMacToStr(mac, macStr);

    char rawStr[86];
    if (raw42) {
        fastBytesToHex(raw42, 42, rawStr);
    } else {
        rawStr[0] = '-';
        rawStr[1] = '\0';
    }

    DropLogService::record(code, tag, rssi, reason, 1, macStr, rawStr);

    if (s_droppedBeaconCount.load(std::memory_order_relaxed) < DROPPED_FIFO_MAX) {
        if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            size_t count = s_droppedBeaconCount.load(std::memory_order_relaxed);
            if (count < DROPPED_FIFO_MAX) {
                DroppedBeaconItem& item = s_droppedBeaconBuf[count];
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
                s_droppedBeaconCount.store(count + 1, std::memory_order_relaxed);
            }
            xSemaphoreGive(s_beaconBufMutex);
        }
    }
}

void BeaconPipeline::recordDroppedBeacon(const BeaconPacket& p, DropReason reason) {
    const char* code = getBeaconTypeCode(p.type);
    const RawBleBinaryFrame* raw = reinterpret_cast<const RawBleBinaryFrame*>(p.raw_frame);
    char tagBuf[24];
    formatTag(tagBuf, sizeof(tagBuf), p.has_major, p.major, p.minor, raw->mac);
    recordDroppedBeacon(code, tagBuf, p.rssi, reason, raw->mac, p.raw_frame);
}

size_t BeaconPipeline::copyDroppedBeacons(DroppedBeaconItem* outItems, size_t maxItems) {
    if (!outItems || maxItems == 0) return 0;
    size_t count = 0;

    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = std::min(s_droppedBeaconCount.load(std::memory_order_relaxed), maxItems);
        std::memcpy(outItems, s_droppedBeaconBuf, count * sizeof(DroppedBeaconItem));
        s_droppedBeaconCount.store(0, std::memory_order_relaxed);
        xSemaphoreGive(s_beaconBufMutex);
    }
    return count;
}

void BeaconPipeline::flushDroppedBeacons() {
    if (s_beaconBufMutex != nullptr && xSemaphoreTake(s_beaconBufMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_droppedBeaconCount.store(0, std::memory_order_relaxed);
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
