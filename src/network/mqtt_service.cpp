#include "mqtt_service.hpp"
#include "net_manager.hpp"
#include "beacon_pipeline.hpp"
#include "config_store.hpp"
#include "bsp_led.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstring>
#include <cstdio>
#include <atomic>

static const char *TAG = "MQTT_SVC";

namespace Iotaru::Network {

static esp_mqtt_client_handle_t s_mqttClient = nullptr;
static std::atomic<bool> s_mqttConnected{false};
static std::atomic<bool> s_mqttStarted{false};

static char s_topicData[128]   = "";
static char s_topicStatus[128] = "";
static char s_topicCmd[128]    = "";
static char s_topicRes[128]    = "";
static char s_lastError[64]    = "None";

static uint8_t s_batchBuffer[MQTT_BATCH_BUFFER_SIZE];
static std::atomic<size_t> s_statBatchBufferLoad{0};
static std::atomic<size_t> s_statLastBatchPktCount{0};
static std::atomic<uint32_t> s_statWireBytesAcc{0};
static std::atomic<float> s_statBandwidthKBps{0.0f};

static void sanitizeToken(const char* input, char* output, size_t maxLen) {
    if (!input || !output || maxLen == 0) return;
    size_t outIdx = 0;
    for (size_t i = 0; input[i] != '\0' && outIdx < maxLen - 1; ++i) {
        char c = input[i];
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
    char cleanModel[32], cleanDevId[32];
    char rawDevId[32];
    Core::ConfigStore::getMqttClientId(rawDevId, sizeof(rawDevId));

    sanitizeToken(g_config.device_model[0] ? g_config.device_model : "SG-10", cleanModel, sizeof(cleanModel));
    sanitizeToken(rawDevId, cleanDevId, sizeof(cleanDevId));

    snprintf(s_topicData, sizeof(s_topicData), "%s/data/%s", cleanModel, cleanDevId);
    snprintf(s_topicStatus, sizeof(s_topicStatus), "%s/info/%s/status", cleanModel, cleanDevId);
    snprintf(s_topicCmd, sizeof(s_topicCmd), "%s/cmd/%s", cleanModel, cleanDevId);
    snprintf(s_topicRes, sizeof(s_topicRes), "%s/res/%s", cleanModel, cleanDevId);

    ESP_LOGI(TAG, "Topik MQTT Dikonfigurasi:");
    ESP_LOGI(TAG, "  -> Data   : %s", s_topicData);
    ESP_LOGI(TAG, "  -> Status : %s", s_topicStatus);
    ESP_LOGI(TAG, "  -> Cmd    : %s", s_topicCmd);
    ESP_LOGI(TAG, "  -> Res    : %s", s_topicRes);
}

void MqttService::onMqttEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED: {
            s_mqttConnected = true;
            strncpy(s_lastError, "Connected", sizeof(s_lastError));
            ESP_LOGI(TAG, "Terhubung ke broker MQTT!");

            // 1. Kirim Birth Message (QoS 1, Retain 1)
            char ipStr[16];
            NetworkManager::getActiveIp(ipStr, sizeof(ipStr));
            char birthPayload[256];
            char devId[32];
            Core::ConfigStore::getMqttClientId(devId, sizeof(devId));

            snprintf(birthPayload, sizeof(birthPayload),
                     "{\"status\":\"online\",\"device_id\":\"%s\",\"model\":\"%s\",\"ip\":\"%s\",\"firmware\":\"%s\"}",
                     devId, g_config.device_model, ipStr, FIRMWARE_VERSION);

            int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicStatus, birthPayload, strlen(birthPayload), 1, 1);
            if (msgId >= 0) {
                s_statWireBytesAcc += calculateWireBytes(strlen(s_topicStatus), strlen(birthPayload), 1);
            }

            // 2. Subscribe ke Topik Perintah (QoS 1)
            esp_mqtt_client_subscribe(s_mqttClient, s_topicCmd, 1);
            ESP_LOGI(TAG, "Subscribed ke topik perintah: %s", s_topicCmd);
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            s_mqttConnected = false;
            strncpy(s_lastError, "Disconnected", sizeof(s_lastError));
            ESP_LOGW(TAG, "Terputus dari broker MQTT!");
            break;

        case MQTT_EVENT_DATA: {
            if (event->topic && event->topic_len > 0) {
                if (strncmp(event->topic, s_topicCmd, event->topic_len) == 0) {
                    char payload[256] = {0};
                    size_t copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                    memcpy(payload, event->data, copyLen);
                    ESP_LOGI(TAG, "Perintah MQTT Diterima: %s", payload);

                    if (strstr(payload, "\"ping\"")) {
                        uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000);
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"pong\",\"timestamp\":%u,\"status\":\"ok\"}", (unsigned int)nowMs);
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                    } else if (strstr(payload, "\"identify\"")) {
                        Bsp::LedManager::setSysState(SysLedState::OTA); // Fast flash identify
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"identifying\",\"status\":\"ok\"}");
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                    } else if (strstr(payload, "\"reboot\"")) {
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"rebooting\",\"status\":\"ok\"}");
                        esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                    }
                }
            }
            break;
        }

        case MQTT_EVENT_ERROR:
            strncpy(s_lastError, "MQTT Protocol/Socket Error", sizeof(s_lastError));
            ESP_LOGE(TAG, "MQTT Event Error terdeteksi!");
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
                // Periksa apakah batch saat ini sudah penuh
                if ((batchLen + binLen > MQTT_BATCH_BUFFER_SIZE) ||
                    (batchCount >= (size_t)g_config.batch_max)) {
                    if (s_mqttConnected) {
                        int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                            (const char*)s_batchBuffer,
                                                            batchLen, 0, 0);
                        if (msgId >= 0) {
                            s_statWireBytesAcc += calculateWireBytes(strlen(s_topicData), batchLen, 0);
                            Bsp::LedManager::setComState(ComLedState::NORMAL);
                        }
                    }
                    s_statLastBatchPktCount = batchCount;
                    batchLen = 0;
                    batchCount = 0;
                    lastFlushTime = now;
                }

                memcpy(s_batchBuffer + batchLen, binBuffer, binLen);
                batchLen += binLen;
                batchCount++;

                if (batchCount >= (size_t)g_config.batch_max) {
                    if (s_mqttConnected) {
                        int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                            (const char*)s_batchBuffer,
                                                            batchLen, 0, 0);
                        if (msgId >= 0) {
                            s_statWireBytesAcc += calculateWireBytes(strlen(s_topicData), batchLen, 0);
                            Bsp::LedManager::setComState(ComLedState::NORMAL);
                        }
                    }
                    s_statLastBatchPktCount = batchCount;
                    batchLen = 0;
                    batchCount = 0;
                    lastFlushTime = now;
                    break;
                }
            }
        }

        // 4. Timer-based Batch Flush
        now = (uint32_t)(esp_timer_get_time() / 1000);
        if (batchCount > 0) {
            if ((now - lastFlushTime) >= (uint32_t)g_config.batch_flush) {
                if (s_mqttConnected) {
                    int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                        (const char*)s_batchBuffer,
                                                        batchLen, 0, 0);
                    if (msgId >= 0) {
                        s_statWireBytesAcc += calculateWireBytes(strlen(s_topicData), batchLen, 0);
                        Bsp::LedManager::setComState(ComLedState::NORMAL);
                    }
                }
                s_statLastBatchPktCount = batchCount;
                batchLen = 0;
                batchCount = 0;
                lastFlushTime = now;
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

        char clientId[32];
        Core::ConfigStore::getMqttClientId(clientId, sizeof(clientId));

        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = mqttUri;
        mqtt_cfg.credentials.client_id = clientId;
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
        mqtt_cfg.session.keepalive = 30;
        mqtt_cfg.network.reconnect_timeout_ms = 3000;

        s_mqttClient = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(s_mqttClient, MQTT_EVENT_ANY, &onMqttEvent, nullptr);

        if (NetworkManager::isConnected()) {
            esp_mqtt_client_start(s_mqttClient);
            s_mqttStarted = true;
        }
    } else {
        ESP_LOGW(TAG, "MQTT Broker Host belum dikonfigurasi.");
    }

    // Jalankan Publisher Task di Core 1 dengan prioritas 4
    xTaskCreatePinnedToCore(mqttPublisherTask, "mqtt_pub_task", 4096, nullptr, 4, nullptr, 1);
}

bool MqttService::isConnected() { return s_mqttConnected; }

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

float MqttService::getBandwidthKBps() {
    uint32_t bytesSec = s_statWireBytesAcc.exchange(0);
    s_statBandwidthKBps = (float)bytesSec / 1024.0f;
    return s_statBandwidthKBps;
}

size_t MqttService::getBatchBufferLoad() { return s_statBatchBufferLoad; }
size_t MqttService::getLastBatchPktCount() { return s_statLastBatchPktCount; }
const char* MqttService::getLastError() { return s_lastError; }

} // namespace Iotaru::Network
