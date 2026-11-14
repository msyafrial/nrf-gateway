#include "mqtt_service.hpp"
#include "net_manager.hpp"
#include "net_log_service.hpp"
#include "beacon_pipeline.hpp"
#include "config_store.hpp"
#include "app_time.hpp"
#include "bsp_led.hpp"
#include "bsp_at24c256.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_crc.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "psa/crypto.h"
#include "lwip/def.h"
#include <ctime>
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

static void saveCriticalSeqStore(const MqttSeqStore& inStore);

static uint16_t calculateSeqStoreChecksum(const MqttSeqStore& s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&s);
    constexpr size_t LEN = offsetof(MqttSeqStore, checksum);
    return esp_crc16_le(0, p, LEN);
}

static bool loadCriticalSeqStore(MqttSeqStore& outStore) {
    if (!Iotaru::Bsp::At24c256::isDetected()) return false;
    if (Iotaru::Bsp::At24c256::readBuffer(EEPROM_MQTT_SEQ_ADDR, reinterpret_cast<uint8_t*>(&outStore), sizeof(outStore)) != ESP_OK) {
        return false;
    }
    if (outStore.magic != MQTT_SEQ_MAGIC) return false;

    uint16_t crc = calculateSeqStoreChecksum(outStore);
    if (outStore.checksum != crc) {
        // Fallback backward-compatible: cek legacy XOR checksum
        uint16_t xor_cs = 0;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&outStore);
        constexpr size_t LEN = offsetof(MqttSeqStore, checksum);
        for (size_t i = 0; i < LEN; i++) {
            xor_cs ^= p[i];
        }
        if (outStore.checksum == xor_cs) {
            // Migrasi otomatis ke CRC16 di EEPROM
            saveCriticalSeqStore(outStore);
        } else {
            return false;
        }
    }
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
static char s_topicHeartbeat[64]  = "";
static std::atomic<int64_t> s_lastHeartbeatMs{0};
static std::atomic<int64_t> s_lastInboundActivityMs{0};
static std::atomic<bool> s_hasInboundActivity{false};
static char s_lastError[64]       = "None";
static SemaphoreHandle_t s_clientCtrlMutex = nullptr;
static portMUX_TYPE s_errMux = portMUX_INITIALIZER_UNLOCKED;

static void setLastErrorSafe(const char* msg) {
    portENTER_CRITICAL(&s_errMux);
    strlcpy(s_lastError, msg ? msg : "None", sizeof(s_lastError));
    portEXIT_CRITICAL(&s_errMux);
}

alignas(4) static uint8_t s_batchBuffer[MQTT_BATCH_BUFFER_SIZE];
static std::atomic<size_t> s_statBatchBufferLoad{0};
static std::atomic<size_t> s_statLastBatchPktCount{0};
static std::atomic<uint32_t> s_statWireBytesAcc{0};
static std::atomic<uint64_t> s_statTotalWireBytes{0};
static std::atomic<float> s_statBandwidthKBps{0.0f};
static int64_t s_lastBwCalcMs = 0;
static bool s_hasBwInit = false;

static inline void recordWireBytes(uint32_t bytes) {
    s_statWireBytesAcc += bytes;
    s_statTotalWireBytes += bytes;
}

static size_t calculatePubackWireBytes() {
    size_t mqttPubackLen = 4; // 2B MQTT Header (0x40, 0x02) + 2B MsgID
    size_t tcpIpLen = 40;     // 20B IPv4 + 20B TCP
    size_t l2Len = (g_config.connection_mode == 1) ? 18 : 36;
    return mqttPubackLen + tcpIpLen + l2Len;
}

static size_t calculateSubackWireBytes() {
    size_t mqttSubackLen = 5; // 2B MQTT Header + 2B MsgID + 1B ReturnCode
    size_t tcpIpLen = 40;     // 20B IPv4 + 20B TCP
    size_t l2Len = (g_config.connection_mode == 1) ? 18 : 36;
    return mqttSubackLen + tcpIpLen + l2Len;
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
    size_t l2Len = (g_config.connection_mode == 1) ? 18 : 36;
    return mqttTotal + tcpIpLen + l2Len;
}

void MqttService::updateTopicNames() {
    char dev_prefix[32] = {0};
    char device_id[32];

    char raw_dev_id[16];
    Core::ConfigStore::getDeviceId(raw_dev_id, sizeof(raw_dev_id));

    // Ekstraksi Prefix Topik:
    // 1. Jika ada dash '-': ambil karakter sebelum dash, ubah ke lowercase.
    // 2. Jika tidak ada dash: fail-safe ambil maksimal 5 karakter, ubah ke lowercase.
    const char* raw_model = g_config.device_model[0] ? g_config.device_model : "ar";
    const char* dash_pos = strchr(raw_model, '-');
    size_t max_chars = (dash_pos != nullptr) ? static_cast<size_t>(dash_pos - raw_model) : 5;
    if (max_chars > sizeof(dev_prefix) - 1) {
        max_chars = sizeof(dev_prefix) - 1;
    }

    size_t pIdx = 0;
    for (size_t i = 0; raw_model[i] != '\0' && raw_model[i] != '-' && pIdx < max_chars; ++i) {
        char c = raw_model[i];
        if (c >= 'A' && c <= 'Z') {
            dev_prefix[pIdx++] = static_cast<char>(c + ('a' - 'A'));
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            dev_prefix[pIdx++] = c;
        }
    }
    dev_prefix[pIdx] = '\0';
    if (pIdx == 0) {
        strlcpy(dev_prefix, "ar", sizeof(dev_prefix));
    }

    sanitizeToken(raw_dev_id, device_id, sizeof(device_id), true /* force lowercase */);

    snprintf(s_topicData,      sizeof(s_topicData),      "%s/data/%s",        dev_prefix, device_id);
    snprintf(s_topicStatus,    sizeof(s_topicStatus),    "%s/info/%s/status", dev_prefix, device_id);
    snprintf(s_topicCmd,       sizeof(s_topicCmd),       "%s/cmd/%s",         dev_prefix, device_id);
    snprintf(s_topicRes,       sizeof(s_topicRes),       "%s/res/%s",         dev_prefix, device_id);
    snprintf(s_topicHeartbeat, sizeof(s_topicHeartbeat), "%s/hb",             dev_prefix);

    ESP_LOGI(TAG, "Topik MQTT Dikonfigurasi (model='%s' -> prefix='%s', device_id='%s'):",
             raw_model, dev_prefix, device_id);
    ESP_LOGI(TAG, "  -> Data      : %s", s_topicData);
    ESP_LOGI(TAG, "  -> Status    : %s", s_topicStatus);
    ESP_LOGI(TAG, "  -> Cmd       : %s", s_topicCmd);
    ESP_LOGI(TAG, "  -> Res       : %s", s_topicRes);
    ESP_LOGI(TAG, "  -> Heartbeat : %s", s_topicHeartbeat);
}

static char s_lwtPayload[384] = {0};

static void buildStatusPayload(const char* status, char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;

    char device_id[32];
    Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));

    // Mode saat ini: "ethernet" atau "wifi"
    const char* mode = (g_config.connection_mode == 1) ? "ethernet" : "wifi";

    char ipwlan[16] = {0};
    NetworkManager::getWifiIpRaw(ipwlan, sizeof(ipwlan));

    char ipeth[16] = {0};
    NetworkManager::getEthernetIpRaw(ipeth, sizeof(ipeth));

    const char* model = g_config.device_model[0] ? g_config.device_model : "AR-27";
    const char* dev_ver = g_config.device_version[0] ? g_config.device_version : "v1.0.0";

    snprintf(outBuf, maxLen,
             "{\"status\":\"%s\",\"device_id\":\"%s\",\"model\":\"%s\",\"mode\":\"%s\","
             "\"ipwlan\":\"%s\",\"ipeth\":\"%s\",\"firm_ver\":\"%s\",\"ui_ver\":\"%s\",\"dev_ver\":\"%s\"}",
             status, device_id, model, mode, ipwlan, ipeth, FIRMWARE_VERSION, g_uiVersion, dev_ver);
}

static void formatMqttDisconnectDetail(esp_mqtt_event_handle_t event, char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';

    if (!event || !event->error_handle) {
        snprintf(outBuf, maxLen, "Session terminated by broker (TCP FIN)");
        return;
    }

    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        int sock_err = event->error_handle->esp_transport_sock_errno;
        int tls_err = event->error_handle->esp_tls_last_esp_err;

        switch (sock_err) {
            case 104: // ECONNRESET
                snprintf(outBuf, maxLen, "Connection reset by broker (ECONNRESET:104)");
                break;
            case 110: // ETIMEDOUT
                snprintf(outBuf, maxLen, "Connection response timed out (ETIMEDOUT:110)");
                break;
            case 113: // EHOSTUNREACH
                snprintf(outBuf, maxLen, "Broker host route unreachable (EHOSTUNREACH:113)");
                break;
            case 111: // ECONNREFUSED
                snprintf(outBuf, maxLen, "Broker port refused connection (ECONNREFUSED:111)");
                break;
            case 32:  // EPIPE
                snprintf(outBuf, maxLen, "Broken communication socket pipe (EPIPE:32)");
                break;
            case 128: // ENOTCONN
                snprintf(outBuf, maxLen, "Socket not connected to network (ENOTCONN:128)");
                break;
            case 118: // EHOSTDOWN
                snprintf(outBuf, maxLen, "Broker server down / offline (EHOSTDOWN:118)");
                break;
            default:
                if (tls_err != 0) {
                    if (tls_err == 0x8001) {
                        snprintf(outBuf, maxLen, "Broker DNS domain resolution failed (DNS Error: 0x8001)");
                    } else {
                        snprintf(outBuf, maxLen, "TLS encryption handshake failed (Error: 0x%04X)", tls_err);
                    }
                } else if (sock_err != 0) {
                    snprintf(outBuf, maxLen, "TCP transport layer failure (Errno: %d)", sock_err);
                } else {
                    snprintf(outBuf, maxLen, "Session terminated by broker (TCP FIN / Keepalive Timeout)");
                }
                break;
        }
    } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        switch (event->error_handle->connect_return_code) {
            case 1:
                snprintf(outBuf, maxLen, "MQTT protocol version rejected by broker (Code 1)");
                break;
            case 2:
                snprintf(outBuf, maxLen, "Client ID rejected by broker (Code 2)");
                break;
            case 3:
                snprintf(outBuf, maxLen, "Broker service unavailable / overloaded (Code 3)");
                break;
            case 4:
                snprintf(outBuf, maxLen, "Authentication failed: Invalid credentials (Code 4)");
                break;
            case 5:
                snprintf(outBuf, maxLen, "Authorization failed: ACL access restricted (Code 5)");
                break;
            default:
                snprintf(outBuf, maxLen, "Connection refused by broker (Return Code: %d)", event->error_handle->connect_return_code);
                break;
        }
    } else {
        snprintf(outBuf, maxLen, "Session terminated by broker (TCP FIN / Keepalive Timeout)");
    }
}

static void formatMqttErrorDetail(esp_mqtt_event_handle_t event, char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    outBuf[0] = '\0';
    if (event && event->error_handle) {
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            int sock_err = event->error_handle->esp_transport_sock_errno;
            int tls_err = event->error_handle->esp_tls_last_esp_err;
            if (tls_err != 0) {
                if (tls_err == 0x8001) {
                    snprintf(outBuf, maxLen, "Gagal Resolusi Domain Broker (DNS Error: 0x8001, sock_errno: %d)", sock_err);
                } else {
                    snprintf(outBuf, maxLen, "Kegagalan Handshake TLS (Error: 0x%04X, sock_errno: %d)", tls_err, sock_err);
                }
            } else if (sock_err != 0) {
                snprintf(outBuf, maxLen, "Kegagalan Lapisan Transport TCP (sock_errno: %d)", sock_err);
            } else {
                snprintf(outBuf, maxLen, "Kegagalan Lapisan Transport TCP");
            }
            return;
        }
    }
    snprintf(outBuf, maxLen, "Anomali Parsing Frame Paket Protokol MQTT");
}

void MqttService::onMqttEvent(void* arg, esp_event_base_t base, int32_t id, void* data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
        case MQTT_EVENT_CONNECTED: {
            s_mqttConnected = true;
            s_lastInboundActivityMs.store(monotonicMs(), std::memory_order_relaxed);
            s_hasInboundActivity.store(true, std::memory_order_release);
            setLastErrorSafe("Connected");
            ESP_LOGI(TAG, "Terhubung ke broker MQTT!");
            Bsp::LedManager::setComState(ComLedState::NORMAL);

            // Reset backoff interval network manager karena koneksi MQTT telah pulih sempurna
            NetworkManager::resetFastLinkBackoff();

            // Bersihkan antrean lama saat booting/offline agar tidak menimbulkan drop TTL palsu
            Core::BeaconPipeline::flushQueue();

            // 1. Kirim Birth Message (QoS 1, Retain 1)
            char birth_payload[384];
            buildStatusPayload("online", birth_payload, sizeof(birth_payload));

            int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicStatus, birth_payload, strlen(birth_payload), 1, 1);
            if (msgId >= 0) {
                recordWireBytes(calculateWireBytes(strlen(s_topicStatus), strlen(birth_payload), 1));
            }

            // 2. Subscribe ke Topik Perintah (QoS 1)
            esp_mqtt_client_subscribe(s_mqttClient, s_topicCmd, 1);
            ESP_LOGI(TAG, "Subscribed ke topik perintah: %s", s_topicCmd);

            // 3. Pancarkan Heartbeat Perdana Seketika (Topik <prefix>/hb, Payload device_id)
            s_lastHeartbeatMs.store(monotonicMs(), std::memory_order_relaxed);
            publishHeartbeat();

            char device_id[32];
            Core::ConfigStore::getDeviceId(device_id, sizeof(device_id));
            char det[128];
            const char* scheme = g_config.mqtt_use_ssl ? "mqtts" : "mqtt";
            snprintf(det, sizeof(det), "Broker: %s://%s:%d | ID: %s", scheme, g_config.mqtt_host, g_config.mqtt_port, device_id);
            NetLogService::log(NetCategory::MQTT, NetEvent::CONNECTED, det);
            break;
        }

        case MQTT_EVENT_DISCONNECTED: {
            s_mqttConnected = false;
            setLastErrorSafe("Disconnected");
            ESP_LOGW(TAG, "Terputus dari broker MQTT!");
            char det[80];
            formatMqttDisconnectDetail(event, det, sizeof(det));
            NetLogService::log(NetCategory::MQTT, NetEvent::DISCONNECTED, det);
            if (NetworkManager::isConnected()) {
                Bsp::LedManager::setComState(ComLedState::MQTT_OFFLINE);
            }
            break;
        }

        case MQTT_EVENT_PUBLISHED:
            s_lastInboundActivityMs.store(monotonicMs(), std::memory_order_relaxed);
            s_hasInboundActivity.store(true, std::memory_order_release);
            // Broker berhasil menerima data dan mengirimkan PUBACK (QoS 1 Ack downlink feedback)
            if (g_config.mqtt_qos == 1) {
                ESP_LOGD(TAG, "PUBACK diterima dari broker untuk MsgID=%d", event->msg_id);
                recordWireBytes(calculatePubackWireBytes());
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:
            s_lastInboundActivityMs.store(monotonicMs(), std::memory_order_relaxed);
            s_hasInboundActivity.store(true, std::memory_order_release);
            ESP_LOGD(TAG, "SUBACK diterima dari broker untuk MsgID=%d", event->msg_id);
            recordWireBytes(calculateSubackWireBytes());
            break;

        case MQTT_EVENT_DATA: {
            s_lastInboundActivityMs.store(monotonicMs(), std::memory_order_relaxed);
            s_hasInboundActivity.store(true, std::memory_order_release);
            if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
                ESP_LOGW(TAG, "Fragmented MQTT payload ignored (offset=%d, len=%d, total=%d)",
                         event->current_data_offset, event->data_len, event->total_data_len);
                break;
            }
            if (event->topic && event->topic_len > 0) {
                // Catat downlink wire bytes dari broker (termasuk TCP/IP dan MAC overhead)
                size_t varLen = 2 + event->topic_len + event->data_len;
                size_t fixHeaderLen = 1 + (varLen < 128 ? 1 : ((varLen < 16384) ? 2 : 3));
                size_t tcpIpLen = 40;
                size_t l2Len = (g_config.connection_mode == 1) ? 18 : 36;
                recordWireBytes(fixHeaderLen + varLen + tcpIpLen + l2Len);

                // Exact topic matching untuk mencegah sub-topic collision (VULN-SEC-08)
                if (event->topic_len == strlen(s_topicCmd) && strncmp(event->topic, s_topicCmd, event->topic_len) == 0) {
                    char payload[384] = {0};
                    size_t copyLen = (event->data_len < sizeof(payload) - 1) ? event->data_len : sizeof(payload) - 1;
                    memcpy(payload, event->data, copyLen);
                    ESP_LOGI(TAG, "Perintah MQTT Diterima: %s", payload);

                    auto pubResponse = [](const char* resBuf) {
                        int mId = esp_mqtt_client_publish(s_mqttClient, s_topicRes, resBuf, strlen(resBuf), 1, 0);
                        if (mId >= 0) {
                            recordWireBytes(calculateWireBytes(strlen(s_topicRes), strlen(resBuf), 1));
                        }
                        return mId;
                    };

                    cJSON *root = cJSON_Parse(payload);
                    if (!root) {
                        char errBuf[96];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":1,\"seq\":0}");
                        pubResponse(errBuf);
                        break;
                    }

                    cJSON *cmdItem = cJSON_GetObjectItem(root, "cmd");
                    cJSON *seqItem = cJSON_GetObjectItem(root, "seq");
                    cJSON *authItem = cJSON_GetObjectItem(root, "auth");

                    if (!cmdItem || !cJSON_IsString(cmdItem) || !seqItem || !cJSON_IsNumber(seqItem) || !authItem || !cJSON_IsString(authItem)) {
                        uint32_t fallbackSeq = (seqItem && cJSON_IsNumber(seqItem)) ? (uint32_t)seqItem->valuedouble : 0;
                        char errBuf[96];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":1,\"seq\":%u}", (unsigned int)fallbackSeq);
                        pubResponse(errBuf);
                        cJSON_Delete(root);
                        break;
                    }

                    const char* cmdStr = cmdItem->valuestring;
                    uint32_t seqVal = static_cast<uint32_t>(seqItem->valuedouble);
                    const char* authStr = authItem->valuestring;

                    char safeCmd[32] = {0};
                    sanitizeToken(cmdStr, safeCmd, sizeof(safeCmd), true);

                    // 1. Verifikasi Hash Dinamis SHA-256 (admin_user:admin_pass:cmd:seq)
                    if (!verifyDynamicAuth(cmdStr, seqVal, authStr)) {
                        ESP_LOGW(TAG, "Otentikasi perintah MQTT GAGAL! cmd='%s', seq=%u", safeCmd, (unsigned int)seqVal);
                        char errBuf[128];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":2,\"cmd\":\"%s\",\"seq\":%u}", safeCmd, (unsigned int)seqVal);
                        pubResponse(errBuf);
                        cJSON_Delete(root);
                        break;
                    }

                    // 2. Anti-Replay: Cek 10 Sequence Terakhir (EEPROM untuk reboot/identify, RAM untuk ping)
                    bool isCritical = (strcmp(cmdStr, "reboot") == 0 || strcmp(cmdStr, "identify") == 0);
                    if (!checkAndRecordSeq(seqVal, isCritical)) {
                        ESP_LOGW(TAG, "Perintah MQTT Ditolak: Duplicate Sequence #%u terdeteksi!", (unsigned int)seqVal);
                        char errBuf[128];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":3,\"cmd\":\"%s\",\"seq\":%u}", safeCmd, (unsigned int)seqVal);
                        pubResponse(errBuf);
                        cJSON_Delete(root);
                        break;
                    }

                    // 3. Eksekusi Perintah & Kirim Respon Kompak (Code: 0 untuk Success)
                    if (strcmp(cmdStr, "ping") == 0) {
                        uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
                        char resBuf[160];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"ok\",\"code\":0,\"cmd\":\"ping\",\"seq\":%u,\"timestamp\":%u}",
                                 (unsigned int)seqVal, (unsigned int)nowMs);
                        pubResponse(resBuf);
                    } else if (strcmp(cmdStr, "identify") == 0) {
                        Bsp::LedManager::triggerIdentify(3000);
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"ok\",\"code\":0,\"cmd\":\"identify\",\"seq\":%u}", (unsigned int)seqVal);
                        pubResponse(resBuf);
                    } else if (strcmp(cmdStr, "reboot") == 0) {
                        char resBuf[128];
                        snprintf(resBuf, sizeof(resBuf), "{\"res\":\"ok\",\"code\":0,\"cmd\":\"reboot\",\"seq\":%u}", (unsigned int)seqVal);
                        pubResponse(resBuf);
                        NetLogService::log(NetCategory::SYSTEM, NetEvent::REBOOT,
                                           "Reboot gateway diinstruksikan via MQTT Remote Command");
                        // Jalankan reboot di task terpisah dengan jeda 1000ms agar MQTT response ter-flush tuntas
                        xTaskCreatePinnedToCore([](void*) {
                            vTaskDelay(pdMS_TO_TICKS(1000));
                            NetworkManager::gracefulRestart();
                        }, "reboot_task", 3072, nullptr, 5, nullptr, 1);
                    } else {
                        char errBuf[128];
                        snprintf(errBuf, sizeof(errBuf), "{\"res\":\"err\",\"code\":4,\"cmd\":\"%s\",\"seq\":%u}", safeCmd, (unsigned int)seqVal);
                        pubResponse(errBuf);
                    }

                    cJSON_Delete(root);
                }
            }
            break;
        }

        case MQTT_EVENT_ERROR: {
            setLastErrorSafe("MQTT Protocol/Socket Error");
            ESP_LOGE(TAG, "MQTT Event Error terdeteksi!");
            char detErr[80];
            formatMqttErrorDetail(event, detErr, sizeof(detErr));
            NetLogService::log(NetCategory::MQTT, NetEvent::ERROR_EVT, detErr);
            break;
        }

        default:
            break;
    }
}

void MqttService::mqttPublisherTask(void* pvParameters) {
    esp_task_wdt_add(NULL);
    int64_t lastFlushTime = monotonicMs();
    size_t batchLen = 0;
    size_t batchCount = 0;
    uint32_t consecPubFail = 0;
    QueueHandle_t bQueue = Core::BeaconPipeline::getQueue();

    for (;;) {
        esp_task_wdt_reset();
        int64_t now = monotonicMs();

        // 0. Kalibrasi Laju Bandwidth per Detik Riil (Rolling 1000ms Sampling Window)
        if (!s_hasBwInit) {
            s_lastBwCalcMs = now;
            s_hasBwInit = true;
        } else {
            int64_t deltaMs = now - s_lastBwCalcMs;
            if (deltaMs >= 1000) {
                uint32_t bytesInWindow = s_statWireBytesAcc.exchange(0);
                float kbps = ((float)bytesInWindow / 1024.0f) / ((float)deltaMs / 1000.0f);
                s_statBandwidthKBps.store(kbps, std::memory_order_relaxed);
                s_lastBwCalcMs = now;
            }
        }

        // 1. Cek konektivitas jaringan
        if (!NetworkManager::isConnected()) {
            consecPubFail = 0;
            if (batchCount > 0) {
                Core::BeaconPipeline::recordMqttFailed(batchCount);
                batchLen = 0;
                batchCount = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (!s_mqttStarted && s_mqttClient != nullptr) {
            if (NetworkManager::isBootConfigMode()) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            esp_mqtt_client_start(s_mqttClient);
            s_mqttStarted = true;
        }

        // 2. Cek konektivitas MQTT
        if (!s_mqttConnected) {
            consecPubFail = 0;
            if (batchCount > 0) {
                Core::BeaconPipeline::recordMqttFailed(batchCount);
                batchLen = 0;
                batchCount = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        constexpr size_t frameLen = sizeof(MqttBleFrame44); // 44 bytes dengan Unix Timestamp (Solusi 3)

        // 3. Flush jika batch sudah penuh dari iterasi sebelumnya (atau tertahan backpressure)
        if (batchCount >= (size_t)g_config.batch_max || (batchLen + frameLen > MQTT_BATCH_BUFFER_SIZE)) {
            int qos = (g_config.mqtt_qos == 0) ? 0 : 1;
            int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                (const char*)s_batchBuffer,
                                                batchLen, qos, 0);
            if (msgId >= 0) {
                consecPubFail = 0;
                if (qos == 0) {
                    s_lastInboundActivityMs.store(now, std::memory_order_relaxed);
                    s_hasInboundActivity.store(true, std::memory_order_release);
                }
                recordWireBytes(calculateWireBytes(strlen(s_topicData), batchLen, qos));
                Bsp::LedManager::triggerTrafficBlip();
                Core::BeaconPipeline::recordMqttPublish(batchCount, 1);
                s_statLastBatchPktCount = batchCount;
                batchLen = 0;
                batchCount = 0;
                lastFlushTime = now;
            } else {
                consecPubFail++;
                if (consecPubFail >= 25) {
                    Core::BeaconPipeline::recordMqttFailed(batchCount);
                    batchLen = 0;
                    batchCount = 0;
                    lastFlushTime = now;
                } else {
                    // Non-destructive backpressure: tahan s_batchBuffer, beri jeda untuk PUBACK
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
            }
        }

        // 4. Tarik paket dari antrean Core Pipeline hingga mencapai batch_max
        time_t nowSec = time(NULL);
        uint32_t nowEpoch = (nowSec > 1700000000) ? static_cast<uint32_t>(nowSec) : 0;
        uint32_t netNowEpoch = (nowEpoch > 0) ? lwip_htonl(nowEpoch) : 0;
        BeaconPacket packet;

        uint32_t now32 = static_cast<uint32_t>(now);
        uint32_t fallbackUptimeSecNet = lwip_htonl(static_cast<uint32_t>(now / 1000LL));
        while (batchCount < (size_t)g_config.batch_max &&
               (batchLen + frameLen <= MQTT_BATCH_BUFFER_SIZE) &&
               bQueue != nullptr && xQueueReceive(bQueue, &packet, 0) == pdTRUE) {
            uint32_t age = elapsedMs32(now32, packet.timestamp);
            if (g_config.data_ttl > 0 && age > (uint32_t)g_config.data_ttl) {
                Core::BeaconPipeline::recordDroppedBeacon(packet, DropReason::TTL_EXPIRED);
                continue; // Buang paket kadaluarsa dan catat ke DropLog
            }

            MqttBleFrame44* outFrame = reinterpret_cast<MqttBleFrame44*>(s_batchBuffer + batchLen);
            outFrame->timestamp = (netNowEpoch > 0) ? netNowEpoch : fallbackUptimeSecNet;
            std::memcpy(outFrame->mac, &packet.raw_frame[1], 40);
            batchLen += frameLen;
            batchCount++;
        }

        // 5. Coba langsung publish jika batch sudah terkumpul penuh
        now = monotonicMs();
        if (batchCount >= (size_t)g_config.batch_max || (batchLen + frameLen > MQTT_BATCH_BUFFER_SIZE)) {
            int qos = (g_config.mqtt_qos == 0) ? 0 : 1;
            int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                (const char*)s_batchBuffer,
                                                batchLen, qos, 0);
            if (msgId >= 0) {
                consecPubFail = 0;
                if (qos == 0) {
                    s_lastInboundActivityMs.store(now, std::memory_order_relaxed);
                    s_hasInboundActivity.store(true, std::memory_order_release);
                }
                recordWireBytes(calculateWireBytes(strlen(s_topicData), batchLen, qos));
                Bsp::LedManager::triggerTrafficBlip();
                Core::BeaconPipeline::recordMqttPublish(batchCount, 1);
                s_statLastBatchPktCount = batchCount;
                batchLen = 0;
                batchCount = 0;
                lastFlushTime = now;
            } else {
                consecPubFail++;
                if (consecPubFail >= 25) {
                    Core::BeaconPipeline::recordMqttFailed(batchCount);
                    batchLen = 0;
                    batchCount = 0;
                    lastFlushTime = now;
                }
            }
        }
        // 6. Timer-based Batch Flush
        else if (batchCount > 0 && ((now - lastFlushTime) >= (int64_t)g_config.batch_flush)) {
            int qos = (g_config.mqtt_qos == 0) ? 0 : 1;
            int msgId = esp_mqtt_client_publish(s_mqttClient, s_topicData,
                                                (const char*)s_batchBuffer,
                                                batchLen, qos, 0);
            if (msgId >= 0) {
                consecPubFail = 0;
                if (qos == 0) {
                    s_lastInboundActivityMs.store(now, std::memory_order_relaxed);
                    s_hasInboundActivity.store(true, std::memory_order_release);
                }
                recordWireBytes(calculateWireBytes(strlen(s_topicData), batchLen, qos));
                Bsp::LedManager::triggerTrafficBlip();
                Core::BeaconPipeline::recordMqttPublish(batchCount, 1);
                s_statLastBatchPktCount = batchCount;
                batchLen = 0;
                batchCount = 0;
                lastFlushTime = now;
            } else {
                consecPubFail++;
                if (consecPubFail >= 25) {
                    Core::BeaconPipeline::recordMqttFailed(batchCount);
                    batchLen = 0;
                    batchCount = 0;
                    lastFlushTime = now;
                }
            }
        }

        // 7. Half-Open TCP / Outbox Full Failsafe Watchdog
        int64_t currentNow = monotonicMs();
        int64_t lastInbound = s_lastInboundActivityMs.load(std::memory_order_relaxed);
        bool pubackStalled = (g_config.mqtt_qos == 1 &&
                              Core::BeaconPipeline::getMqttSpeed() > 0 &&
                              s_hasInboundActivity.load(std::memory_order_acquire) &&
                              (currentNow - lastInbound) > 90000LL);
        if (consecPubFail >= 25 || pubackStalled) {
            ESP_LOGW(TAG, "Watchdog MQTT: %s -> Memicu forceReconnect()!",
                     (consecPubFail >= 25) ? "25x gagal publish beruntun (Outbox penuh)" : "Tiada PUBACK >90s (Half-open TCP)");
            consecPubFail = 0;
            s_lastInboundActivityMs.store(currentNow, std::memory_order_relaxed);
            s_hasInboundActivity.store(true, std::memory_order_release);
            forceReconnect();
        }

        // 8. Periodic Gateway Heartbeat (tiap 5000 ms / 5 detik) ke topik <prefix>/hb
        if (s_mqttConnected) {
            int64_t nowMs = monotonicMs();
            int64_t hbMs = (g_config.hb_interval >= 1 && g_config.hb_interval <= 300)
                           ? (int64_t)g_config.hb_interval * 1000LL
                           : 5000LL;
            int64_t lastHb = s_lastHeartbeatMs.load(std::memory_order_relaxed);
            if ((nowMs - lastHb) >= hbMs) {
                s_lastHeartbeatMs.store(nowMs, std::memory_order_relaxed);
                publishHeartbeat();
            }
        }

        s_statBatchBufferLoad = batchCount;

        // 9. Adaptive Task Delay: jika antrean antri banyak, yield 1 tick; jika sepi, jeda adaptif
        if (bQueue != nullptr && uxQueueMessagesWaiting(bQueue) >= (UBaseType_t)g_config.batch_max) {
            vTaskDelay(1);
        } else if (batchCount > 0) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void MqttService::init() {
    if (s_clientCtrlMutex == nullptr) {
        s_clientCtrlMutex = xSemaphoreCreateMutex();
    }
    updateTopicNames();

    if (strlen(g_config.mqtt_host) > 0 && g_config.mqtt_port > 0) {
        char mqttUri[128];
        const char* scheme = g_config.mqtt_use_ssl ? "mqtts" : "mqtt";
        snprintf(mqttUri, sizeof(mqttUri), "%s://%s:%d", scheme, g_config.mqtt_host, g_config.mqtt_port);

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

        if (g_config.mqtt_use_ssl) {
            mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }

        buildStatusPayload("offline", s_lwtPayload, sizeof(s_lwtPayload));
        mqtt_cfg.session.last_will.topic = s_topicStatus;
        mqtt_cfg.session.last_will.msg = s_lwtPayload;
        mqtt_cfg.session.last_will.msg_len = strlen(s_lwtPayload);
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = 1;
        mqtt_cfg.session.keepalive = (g_config.mqtt_keepalive >= 5 && g_config.mqtt_keepalive <= 65535) ? g_config.mqtt_keepalive : 30;
        mqtt_cfg.network.reconnect_timeout_ms = 3000;
        mqtt_cfg.network.timeout_ms = 10000;
        mqtt_cfg.buffer.size = 2048; // Inbound dihemat dari 8192 ke 2048 (+6 KB DRAM). Cukup untuk JSON cmd & PUBACK
        mqtt_cfg.buffer.out_size = 8192; // Tetap 8192 B untuk menjamin batch telemetri 150 beacon (6.6 KB) @ 1200 pkt/s QoS 1
        mqtt_cfg.outbox.limit = 8192; // Tetap 8192 B untuk in-flight QoS 1 buffer
        mqtt_cfg.task.priority = 6;
        mqtt_cfg.task.stack_size = 6144; // Dihemat dari 8192 ke 6144 (+2 KB DRAM, aman untuk TLS callback)

        s_mqttClient = esp_mqtt_client_init(&mqtt_cfg);
        if (s_mqttClient == nullptr) {
            ESP_LOGE(TAG, "Gagal menginisialisasi esp_mqtt_client untuk URI: %s", mqttUri);
        } else {
            esp_mqtt_client_register_event(s_mqttClient, MQTT_EVENT_ANY, &onMqttEvent, nullptr);

            if (NetworkManager::isBootConfigMode()) {
                ESP_LOGI(TAG, "Mode Config aktif: koneksi MQTT/MQTTS ditahan hingga Mode Config selesai.");
            } else if (NetworkManager::isConnected()) {
                esp_mqtt_client_start(s_mqttClient);
                s_mqttStarted = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "MQTT Broker Host belum dikonfigurasi.");
    }

    // Jalankan Publisher Task di Core 1 dengan prioritas 6 (Tertinggi di Core 1 untuk kestabilan telemetri)
    xTaskCreatePinnedToCore(mqttPublisherTask, "mqtt_pub_task", 4096, nullptr, 6, nullptr, 1);
}

bool MqttService::isConnected() { return s_mqttConnected; }

uint32_t MqttService::getLastInboundActivityMs() {
    return static_cast<uint32_t>(s_lastInboundActivityMs.load(std::memory_order_relaxed));
}

void MqttService::forceReconnect() {
    if (NetworkManager::isBootConfigMode()) {
        return;
    }
    if (s_clientCtrlMutex != nullptr && xSemaphoreTake(s_clientCtrlMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return;
    }
    if (s_mqttClient != nullptr) {
        if (!s_mqttStarted) {
            ESP_LOGI(TAG, "Memulai MQTT client pada koneksi jaringan pertama...");
            buildStatusPayload("offline", s_lwtPayload, sizeof(s_lwtPayload));
            esp_mqtt_client_start(s_mqttClient);
            s_mqttStarted = true;
        } else {
            ESP_LOGI(TAG, "Memicu force reconnect MQTT seketika (ala PubSubClient)...");
            esp_mqtt_client_reconnect(s_mqttClient);
        }
    }
    if (s_clientCtrlMutex != nullptr) {
        xSemaphoreGive(s_clientCtrlMutex);
    }
}

void MqttService::disconnect() {
    if (s_clientCtrlMutex != nullptr && xSemaphoreTake(s_clientCtrlMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return;
    }
    if (s_mqttClient != nullptr && s_mqttStarted) {
        ESP_LOGI(TAG, "Memutuskan sesi MQTT (Zero-Failover: Uplink terputus)...");
        esp_mqtt_client_disconnect(s_mqttClient);
    }
    if (s_clientCtrlMutex != nullptr) {
        xSemaphoreGive(s_clientCtrlMutex);
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
    return s_statBandwidthKBps.load(std::memory_order_relaxed);
}

uint64_t MqttService::getTotalWireBytes() {
    return s_statTotalWireBytes.load();
}

size_t MqttService::getBatchBufferLoad() { return s_statBatchBufferLoad; }
size_t MqttService::getLastBatchPktCount() { return s_statLastBatchPktCount; }
const char* MqttService::getLastError() { return s_lastError; }

} // namespace Iotaru::Network
