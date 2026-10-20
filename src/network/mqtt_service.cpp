#include "mqtt_service.hpp"
#include "net_manager.hpp"
#include "net_log_service.hpp"
#include "beacon_pipeline.hpp"
#include "config_store.hpp"
#include "bsp_led.hpp"
#include "bsp_at24c256.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "psa/crypto.h"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <atomic>

static const char *TAG = "MQTT_SVC";

// -------------------------------------------------------------
// MQTT Anti-Replay & Dynamic Auth (VULN-SEC-08)
// -------------------------------------------------------------
constexpr uint16_t EEPROM_MQTT_SEQ_ADDR = 0x6400;
constexpr uint32_t MQTT_SEQ_MAGIC = 0x53455131; // "SEQ1"

struct __attribute__((packed)) MqttSeqStore {
    uint32_t magic;      // 0x53455131
    uint8_t count;       // 0 to 10
    uint8_t reserved[3];
    uint32_t seqs[10];   // 40 bytes
    uint16_t checksum;   // 2 bytes
    uint8_t pad[14];     // Total: 64 bytes (exact 1 AT24C256 page)
};

static uint32_t s_recentSeqRam[10] = {0};
static uint8_t s_recentCountRam = 0;

static uint16_t calculateSeqStoreChecksum(const MqttSeqStore& s) {
    uint16_t cs = 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
    constexpr size_t LEN = offsetof(MqttSeqStore, checksum);
    for (size_t i = 0; i < LEN; i++) {
        cs ^= p[i];
    }
    return cs;
}

static bool loadCriticalSeqStore(MqttSeqStore& outStore) {
    if (!Iotaru::Bsp::At24c256::isDetected()) return false;
    if (Iotaru::Bsp::At24c256::readBuffer(EEPROM_MQTT_SEQ_ADDR, reinterpret_cast<uint8_t*>(&outStore), sizeof(outStore)) != ESP_OK) {
        return false;
    }
    if (outStore.magic != MQTT_SEQ_MAGIC) return false;
    if (outStore.checksum != calculateSeqStoreChecksum(outStore)) return false;
    if (outStore.count > 10) outStore.count = 10;
    return true;
}

static void saveCriticalSeqStore(const MqttSeqStore& inStore) {
    if (!Iotaru::Bsp::At24c256::isDetected()) return;
    MqttSeqStore toSave = inStore;
    toSave.magic = MQTT_SEQ_MAGIC;
    toSave.checksum = calculateSeqStoreChecksum(toSave);
    Iotaru::Bsp::At24c256::writePage(EEPROM_MQTT_SEQ_ADDR, reinterpret_cast<const uint8_t*>(&toSave), sizeof(toSave));
}

// Cek dan simpan sequence number (Rolling FIFO 10 items)
// Mengembalikan true jika valid (seq belum pernah ada di 10 data terakhir)
// Mengembalikan false jika duplicate (sudah pernah diterima di 10 data terakhir)
static bool checkAndRecordSeq(uint32_t seq, bool isCritical) {
    if (isCritical && Iotaru::Bsp::At24c256::isDetected()) {
        MqttSeqStore store = {};
        if (!loadCriticalSeqStore(store)) {
            store.magic = MQTT_SEQ_MAGIC;
            store.count = 0;
            memset(store.seqs, 0, sizeof(store.seqs));
        }

        // Cek apakah seq sudah ada di 10 entri terakhir
        for (uint8_t i = 0; i < store.count; i++) {
            if (store.seqs[i] == seq) {
                return false; // DUPLICATE SEQ
            }
        }

        // Geser rolling FIFO 10 ke kanan
        if (store.count < 10) {
            memmove(&store.seqs[1], &store.seqs[0], store.count * sizeof(uint32_t));
            store.count++;
        } else {
            memmove(&store.seqs[1], &store.seqs[0], 9 * sizeof(uint32_t));
        }
        store.seqs[0] = seq;

        saveCriticalSeqStore(store);
        return true;
    } else {
        // RAM-only store (untuk ping berfrekuensi tinggi atau saat EEPROM absen)
        for (uint8_t i = 0; i < s_recentCountRam; i++) {
            if (s_recentSeqRam[i] == seq) {
                return false; // DUPLICATE SEQ
            }
        }

        if (s_recentCountRam < 10) {
            memmove(&s_recentSeqRam[1], &s_recentSeqRam[0], s_recentCountRam * sizeof(uint32_t));
            s_recentCountRam++;
        } else {
            memmove(&s_recentSeqRam[1], &s_recentSeqRam[0], 9 * sizeof(uint32_t));
        }
        s_recentSeqRam[0] = seq;
        return true;
    }
}

static bool verifyDynamicAuth(const char* cmd, uint32_t seq, const char* receivedAuth) {
    if (!cmd || !receivedAuth || strlen(receivedAuth) != 64) return false;

    char input[256];
    snprintf(input, sizeof(input), "%s:%s:%s:%u",
             Iotaru::g_config.admin_user, Iotaru::g_config.admin_pass, cmd, (unsigned int)seq);

    psa_crypto_init();
    uint8_t hash[32];
    size_t hashLen = 0;
    psa_status_t status = psa_hash_compute(PSA_ALG_SHA_256,
                                           reinterpret_cast<const uint8_t*>(input),
                                           strlen(input),
                                           hash,
                                           sizeof(hash),
                                           &hashLen);
    if (status != PSA_SUCCESS || hashLen != 32) return false;

    char expectedHex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(expectedHex + (i * 2), 3, "%02x", hash[i]);
    }
    expectedHex[64] = '\0';

    // Constant-time compare
    unsigned char diff = 0;
    for (size_t i = 0; i < 64; i++) {
        char c1 = tolower(static_cast<unsigned char>(receivedAuth[i]));
        char c2 = expectedHex[i];
        diff |= (c1 ^ c2);
    }
    return (diff == 0);
}

namespace Iotaru::Network {

static esp_mqtt_client_handle_t s_mqttClient = nullptr;
static std::atomic<bool> s_mqttConnected{false};
static std::atomic<bool> s_mqttStarted{false};

static char s_topicData[128]      = "";
static char s_topicStatus[128]    = "";
static char s_topicCmd[128]       = "";
static char s_topicRes[128]       = "";
static char s_topicHeartbeat[64]  = "CRHC3048";
static uint32_t s_lastHeartbeatMs = 0;
static char s_lastError[64]       = "None";

static uint8_t s_batchBuffer[MQTT_BATCH_BUFFER_SIZE];
static std::atomic<size_t> s_statBatchBufferLoad{0};
static std::atomic<size_t> s_statLastBatchPktCount{0};
static std::atomic<uint32_t> s_statWireBytesAcc{0};
static std::atomic<uint64_t> s_statTotalWireBytes{0};
static std::atomic<float> s_statBandwidthKBps{0.0f};

static inline void recordWireBytes(uint32_t bytes) {
    s_statWireBytesAcc += bytes;
    s_statTotalWireBytes += bytes;
}

static void sanitizeToken(const char* input, char* output, size_t maxLen, bool forceLower = false) {
    if (!input || !output || maxLen == 0) return;
    size_t outIdx = 0;
    for (size_t i = 0; input[i] != '\0' && outIdx < maxLen - 1; ++i) {
        char c = input[i];
        if (forceLower && (c >= 'A' && c <= 'Z')) {
            c = static_cast<char>(c + ('a' - 'A'));
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            output[outIdx++] = c;
        } else {
            output[outIdx++] = '_';
        }
    }
    output[outIdx] = '\0';
}

static size_t calculateWireBytes(size_t topicLen, size_t payloadLen, uint8_t qos) {
    size_t varLen = (topicLen > 0 ? (2 + topicLen) : 0) + (qos > 0 ? 2 : 0) + payloadLen;
    size_t fixHeaderLen = 1 + (varLen < 128 ? 1 : ((varLen < 16384) ? 2 : 3));
    size_t mqttTotal = fixHeaderLen + varLen;
    size_t tcpIpLen = 40; // 20B IPv4 + 20B TCP
    size_t l2Len = NetworkManager::isEthernetActive() ? 18 : 36;
    size_t totalBytes = mqttTotal + tcpIpLen + l2Len;
    if (qos > 0) totalBytes += (tcpIpLen + l2Len);
    return totalBytes;
}

bool MqttService::buildBinaryPayload(const BeaconPacket& p, uint8_t* outBuf, size_t& outLen) {
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

void MqttService::updateTopicNames() {
    char dev_model[32];
    char device_id[32];

    char raw_dev_id[16];
    Core::ConfigStore::getDeviceId(raw_dev_id, sizeof(raw_dev_id));

    sanitizeToken(g_config.device_model[0] ? g_config.device_model : "SG-10", dev_model, sizeof(dev_model));
    sanitizeToken(raw_dev_id, device_id, sizeof(device_id), true /* force lowercase */);

    snprintf(s_topicData,      sizeof(s_topicData),      "%s/data/%s",        dev_model, device_id);
    snprintf(s_topicStatus,    sizeof(s_topicStatus),    "%s/info/%s/status", dev_model, device_id);
    snprintf(s_topicCmd,       sizeof(s_topicCmd),       "%s/cmd/%s",         dev_model, device_id);
    snprintf(s_topicRes,       sizeof(s_topicRes),       "%s/res/%s",         dev_model, device_id);
    snprintf(s_topicHeartbeat, sizeof(s_topicHeartbeat), "CRHC3048");

    ESP_LOGI(TAG, "Topik MQTT Dikonfigurasi (dev_model='%s', device_id='%s'):", dev_model, device_id);
    ESP_LOGI(TAG, "  -> Data      : %s", s_topicData);
    ESP_LOGI(TAG, "  -> Status    : %s", s_topicStatus);
    ESP_LOGI(TAG, "  -> Cmd       : %s", s_topicCmd);
    ESP_LOGI(TAG, "  -> Res       : %s", s_topicRes);
    ESP_LOGI(TAG, "  -> Heartbeat : %s", s_topicHeartbeat);
}

void MqttService::onMqttEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED: {
            s_mqttConnected = true;
            strncpy(s_lastError, "Connected", sizeof(s_lastError));
            ESP_LOGI(TAG, "Terhubung ke broker MQTT!");
            Bsp::LedManager::setComState(ComLedState::NORMAL);

            // 1. Kirim Birth Message (QoS 1, Retain 1)
            char ip_str[16];
            NetworkManager::getActiveIp(ip_str, sizeof(ip_str));
            char birth_payload[256];
            char device_id[32];
            Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));

            snprintf(birth_payload, sizeof(birth_payload),
                     "{\"status\":\"online\",\"device_id\":\"%s\",\"model\":\"%s\",\"ip\":\"%s\",\"firmware\":\"%s\"}",
                     device_id, g_config.device_model, ip_str, FIRMWARE_VERSION);

            int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicStatus, birth_payload, strlen(birth_payload), 1, 1);
            if (msgId >= 0) {
                recordWireBytes(calculateWireBytes(strlen(s_topicStatus), strlen(birth_payload), 1));
            }

            // 2. Subscribe ke Topik Perintah (QoS 1)
            esp_mqtt_client_subscribe(s_mqttClient, s_topicCmd, 1);
            ESP_LOGI(TAG, "Subscribed ke topik perintah: %s", s_topicCmd);

            // 3. Pancarkan Heartbeat Perdana Seketika (Topik CRHC3048, Payload device_id)
            s_lastHeartbeatMs = (uint32_t)(esp_timer_get_time() / 1000);
            publishHeartbeat();

            char det[128];
            snprintf(det, sizeof(det), "Broker: %s:%d | ID: %s", g_config.mqtt_host, g_config.mqtt_port, device_id);
            NetLogService::log(NetCategory::MQTT, NetEvent::CONNECTED, det);
            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            s_mqttConnected = false;
            strncpy(s_lastError, "Disconnected", sizeof(s_lastError));
            ESP_LOGW(TAG, "Terputus dari broker MQTT!");
            char det[128];
            if (event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                snprintf(det, sizeof(det), "TCP Err: sock_errno=%d, tls_err=0x%x",
                         event->error_handle->esp_transport_sock_errno,
                         event->error_handle->esp_tls_last_esp_err);
            } else if (event->error_handle && event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                snprintf(det, sizeof(det), "Conn Refused: code=%d", event->error_handle->connect_return_code);
            } else {
                snprintf(det, sizeof(det), "Terputus dari broker MQTT");
            }
            NetLogService::log(NetCategory::MQTT, NetEvent::DISCONNECTED, det);
            if (NetworkManager::isConnected()) {
                Bsp::LedManager::setComState(ComLedState::MQTT_OFFLINE);
            }
            break;
        }

        case MQTT_EVENT_PUBLISHED:
            // Broker berhasil menerima data dan mengirimkan PUBACK (QoS 1 Ack)
            ESP_LOGD(TAG, "PUBACK diterima dari broker untuk MsgID=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            if (event->topic && event->topic_len > 0) {
                // Exact topic matching untuk mencegah sub-topic collision (VULN-SEC-08)
                if (event->topic_len == strlen(s_topicCmd) && strncmp(event->topic, s_topicCmd, event->topic_len) == 0) {
                    char payload[384] = {0};
                    size_t copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                    memcpy(payload, event->data, copyLen);
                    ESP_LOGI(TAG, "Perintah MQTT Diterima: %s", payload);

                    cJSON *root = cJSON_Parse(payload);
                    if (!root) {
                        char errBuf[96];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":1,\"seq\":0}");
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, errBuf, strlen(errBuf), 1, 0);
                        break;
                    }

                    cJSON *cmdItem = cJSON_GetObjectItem(root, "cmd");
                    cJSON *seqItem = cJSON_GetObjectItem(root, "seq");
                    cJSON *authItem = cJSON_GetObjectItem(root, "auth");

                    if (!cmdItem || !cJSON_IsString(cmdItem) || !seqItem || !cJSON_IsNumber(seqItem) || !authItem || !cJSON_IsString(authItem)) {
                        uint32_t fallbackSeq = (seqItem && cJSON_IsNumber(seqItem)) ? (uint32_t)seqItem->valuedouble : 0;
                        char errBuf[96];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":1,\"seq\":%u}", (unsigned int)fallbackSeq);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, errBuf, strlen(errBuf), 1, 0);
                        cJSON_Delete(root);
                        break;
                    }

                    const char* cmdStr = cmdItem->valuestring;
                    uint32_t seqVal = static_cast<uint32_t>(seqItem->valuedouble);
                    const char* authStr = authItem->valuestring;

                    // 1. Verifikasi Hash Dinamis SHA-256 (admin_user:admin_pass:cmd:seq)
                    if (!verifyDynamicAuth(cmdStr, seqVal, authStr)) {
                        ESP_LOGW(TAG, "Otentikasi perintah MQTT GAGAL! cmd='%s', seq=%u", cmdStr, (unsigned int)seqVal);
                        char errBuf[128];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":2,\"cmd\":\"%s\",\"seq\":%u}", cmdStr, (unsigned int)seqVal);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, errBuf, strlen(errBuf), 1, 0);
                        cJSON_Delete(root);
                        break;
                    }

                    // 2. Anti-Replay: Cek 10 Sequence Terakhir (EEPROM untuk reboot/identify, RAM untuk ping)
                    bool isCritical = (strcmp(cmdStr, "reboot") == 0 || strcmp(cmdStr, "identify") == 0);
                    if (!checkAndRecordSeq(seqVal, isCritical)) {
                        ESP_LOGW(TAG, "Perintah MQTT Ditolak: Duplicate Sequence #%u terdeteksi!", (unsigned int)seqVal);
                        char errBuf[128];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":3,\"cmd\":\"%s\",\"seq\":%u}", cmdStr, (unsigned int)seqVal);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, errBuf, strlen(errBuf), 1, 0);
                        cJSON_Delete(root);
                        break;
                    }

                    // 3. Eksekusi Perintah & Kirim Respon Kompak (Code: 0 untuk Success)
                    if (strcmp(cmdStr, "ping") == 0) {
                        uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
                        char resBuf[160];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"ok\",\"code\":0,\"cmd\":\"ping\",\"seq\":%u,\"timestamp\":%u}",
                                 (unsigned int)seqVal, (unsigned int)nowMs);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                    } else if (strcmp(cmdStr, "identify") == 0) {
                        Bsp::LedManager::triggerIdentify(3000);
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"ok\",\"code\":0,\"cmd\":\"identify\",\"seq\":%u}", (unsigned int)seqVal);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                    } else if (strcmp(cmdStr, "reboot") == 0) {
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"ok\",\"code\":0,\"cmd\":\"reboot\",\"seq\":%u}", (unsigned int)seqVal);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                        // Jalankan reboot di task terpisah dengan jeda 1000ms agar MQTT response ter-flush tuntas
                        xTaskCreate([](void*) {
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            esp_restart();
                        }, "reboot_task", 3072, nullptr, 5, nullptr);
                    } else {
                        char errBuf[128];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":4,\"cmd\":\"%s\",\"seq\":%u}", cmdStr, (unsigned int)seqVal);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, errBuf, strlen(errBuf), 1, 0);
                    }

                    cJSON_Delete(root);
                }
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            strncpy(s_lastError, "MQTT Protocol/Socket Error", sizeof(s_lastError));
            ESP_LOGE(TAG, "MQTT Event Error terdeteksi!");
            NetLogService::log(NetCategory::MQTT, NetEvent::ERROR_EVT, "MQTT Protocol / Socket Error");
            break;

        default:
            break;
    }
}

void MqttService::mqttPublisherTask(void* pvParameters) {
    uint32_t lastFlushTime = (uint32_t)(esp_timer_get_time() / 1000);
    size_t batchLen = 0;
    size_t batchCount = 0;
    QueueHandle_t bQueue = Core::BeaconPipeline::getQueue();

    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        // 1. Cek konektivitas jaringan
        if (!NetworkManager::isConnected()) {
            batchLen = 0;
            batchCount = 0;
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (!s_mqttStarted && s_mqttClient != nullptr) {
            esp_mqtt_client_start(s_mqttClient);
            s_mqttStarted = true;
        }

        // 2. Cek konektivitas MQTT
        if (!s_mqttConnected) {
            batchLen = 0;
            batchCount = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 3. Tarik paket dari antrean Core Pipeline
        BeaconPacket packet;
        while (bQueue != nullptr && xQueueReceive(bQueue, &packet, 0) == pdTRUE) {
            uint32_t age = now - packet.timestamp;
            if (g_config.data_ttl > 0 && age > (uint32_t)g_config.data_ttl) {
                continue; // Buang paket kadaluarsa
            }

            uint8_t binBuffer[16];
            size_t binLen = 0;
            if (buildBinaryPayload(packet, binBuffer, binLen)) {
                // Periksa apakah batch saat ini sudah penuh sebelum menambah item baru
                if ((batchLen + binLen > MQTT_BATCH_BUFFER_SIZE) ||
                    (batchCount >= (size_t)g_config.batch_max)) {
                    if (s_mqttConnected) {
                        int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                            (const char*)s_batchBuffer,
                                                            batchLen, 1, 0);
                        if (msgId >= 0) {
                            recordWireBytes(calculateWireBytes(strlen(s_topicData), batchLen, 1));
                            Bsp::LedManager::triggerTrafficBlip();
                            Core::BeaconPipeline::recordMqttPublish(batchCount, 1);
                            s_statLastBatchPktCount = batchCount;
                        } else {
                            Core::BeaconPipeline::recordMqttFailed(batchCount);
                        }
                    } else {
                        Core::BeaconPipeline::recordMqttFailed(batchCount);
                    }
                    // Reset batch buffer selalu dilakukan untuk mencegah buffer overflow
                    batchLen = 0;
                    batchCount = 0;
                    lastFlushTime = now;
                }

                memcpy(s_batchBuffer + batchLen, binBuffer, binLen);
                batchLen += binLen;
                batchCount++;
            }
        }

        // 4. Timer-based Batch Flush
        now = (uint32_t)(esp_timer_get_time() / 1000);
        if (batchCount > 0) {
            if ((now - lastFlushTime) >= (uint32_t)g_config.batch_flush) {
                if (s_mqttConnected) {
                    int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                        (const char*)s_batchBuffer,
                                                        batchLen, 1, 0);
                    if (msgId >= 0) {
                        recordWireBytes(calculateWireBytes(strlen(s_topicData), batchLen, 1));
                        Bsp::LedManager::triggerTrafficBlip();
                        Core::BeaconPipeline::recordMqttPublish(batchCount, 1);
                        s_statLastBatchPktCount = batchCount;
                    } else {
                        Core::BeaconPipeline::recordMqttFailed(batchCount);
                    }
                } else {
                    Core::BeaconPipeline::recordMqttFailed(batchCount);
                }
                // Reset batch buffer selalu dilakukan agar data basi tidak menumpuk saat offline
                batchLen = 0;
                batchCount = 0;
                lastFlushTime = now;
            }
        }

        // 5. Periodic Gateway Heartbeat (tiap 5000 ms / 5 detik) ke topik CRHC3048
        if (s_mqttConnected) {
            uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
            if (nowMs - s_lastHeartbeatMs >= 5000) {
                s_lastHeartbeatMs = nowMs;
                publishHeartbeat();
            }
        }

        s_statBatchBufferLoad = batchCount;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void MqttService::init() {
    updateTopicNames();

    if (strlen(g_config.mqtt_host) > 0 && g_config.mqtt_port > 0) {
        char mqttUri[128];
        snprintf(mqttUri, sizeof(mqttUri), "mqtt://%s:%d", g_config.mqtt_host, g_config.mqtt_port);

        char client_id[32];
        Core::ConfigStore::getDeviceId(client_id, sizeof(client_id));

        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = mqttUri;
        mqtt_cfg.credentials.client_id = client_id;
        if (strlen(g_config.mqtt_user) > 0) {
            mqtt_cfg.credentials.username = g_config.mqtt_user;
        }
        if (strlen(g_config.mqtt_password) > 0) {
            mqtt_cfg.credentials.authentication.password = g_config.mqtt_password;
        }

        mqtt_cfg.session.last_will.topic = s_topicStatus;
        mqtt_cfg.session.last_will.msg = "{\"status\":\"offline\"}";
        mqtt_cfg.session.last_will.msg_len = 20;
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = 1;
        mqtt_cfg.session.keepalive = 15;
        mqtt_cfg.session.message_retransmit_timeout = 500;
        mqtt_cfg.network.reconnect_timeout_ms = 500;
        mqtt_cfg.network.timeout_ms = 5000;
        mqtt_cfg.network.tcp_keep_alive_cfg.keep_alive_enable = false;
        mqtt_cfg.buffer.size = 4096;
        mqtt_cfg.buffer.out_size = 4096;
        mqtt_cfg.outbox.limit = 4096;
        mqtt_cfg.task.priority = 6;
        mqtt_cfg.task.stack_size = 6144;

        s_mqttClient = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(s_mqttClient, MQTT_EVENT_ANY, &onMqttEvent, nullptr);

        if (NetworkManager::isConnected()) {
            esp_mqtt_client_start(s_mqttClient);
            s_mqttStarted = true;
        }
    } else {
        ESP_LOGW(TAG, "MQTT Broker Host belum dikonfigurasi.");
    }

    // Jalankan Publisher Task di Core 1 dengan prioritas 6 (Tertinggi di Core 1 untuk kestabilan telemetri)
    xTaskCreatePinnedToCore(mqttPublisherTask, "mqtt_pub_task", 4096, nullptr, 6, nullptr, 1);
}

bool MqttService::isConnected() { return s_mqttConnected; }

void MqttService::forceReconnect() {
    if (s_mqttClient != nullptr) {
        ESP_LOGI(TAG, "Memicu force reconnect MQTT seketika (ala PubSubClient)...");
        esp_mqtt_client_reconnect(s_mqttClient);
    }
}

void MqttService::getTopicData(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strncpy(outBuf, s_topicData, maxLen);
    outBuf[maxLen - 1] = '\0';
}

void MqttService::getTopicStatus(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strncpy(outBuf, s_topicStatus, maxLen);
    outBuf[maxLen - 1] = '\0';
}

void MqttService::getTopicCmd(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strncpy(outBuf, s_topicCmd, maxLen);
    outBuf[maxLen - 1] = '\0';
}

void MqttService::getTopicRes(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strncpy(outBuf, s_topicRes, maxLen);
    outBuf[maxLen - 1] = '\0';
}

void MqttService::getTopicHeartbeat(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strncpy(outBuf, s_topicHeartbeat, maxLen);
    outBuf[maxLen - 1] = '\0';
}

void MqttService::publishHeartbeat() {
    if (!s_mqttConnected || s_mqttClient == nullptr) return;

    char deviceId[32] = {0};
    Core::ConfigStore::getDeviceId(deviceId, sizeof(deviceId));
    size_t payloadLen = strlen(deviceId);
    if (payloadLen == 0) return;

    int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicHeartbeat, deviceId, payloadLen, 0, 0);
    if (msgId >= 0) {
        recordWireBytes(calculateWireBytes(strlen(s_topicHeartbeat), payloadLen, 0));
        ESP_LOGI(TAG, "[HEARTBEAT] Emitted to %s: '%s'", s_topicHeartbeat, deviceId);
    } else {
        ESP_LOGW(TAG, "[HEARTBEAT] Gagal memancarkan heartbeat ke %s", s_topicHeartbeat);
    }
}

float MqttService::getBandwidthKBps() {
    uint32_t bytesSec = s_statWireBytesAcc.exchange(0);
    s_statBandwidthKBps = (float)bytesSec / 1024.0f;
    return s_statBandwidthKBps;
}

uint64_t MqttService::getTotalWireBytes() {
    return s_statTotalWireBytes.load();
}

size_t MqttService::getBatchBufferLoad() { return s_statBatchBufferLoad; }
size_t MqttService::getLastBatchPktCount() { return s_statLastBatchPktCount; }
const char* MqttService::getLastError() { return s_lastError; }

} // namespace Iotaru::Network
