#include "web_server.hpp"
#include "web_routes.hpp"
#include "session_mgr.hpp"
#include "web_template.hpp"
#include "ota_service.hpp"
#include "app_config.hpp"
#include "config_store.hpp"
#include "net_manager.hpp"
#include "mqtt_service.hpp"
#include "beacon_pipeline.hpp"
#include <esp_littlefs.h>
#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <esp_timer.h>

static const char *TAG = "WebServer";

static inline uint32_t get_millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

namespace Iotaru::Web::Server {

static httpd_handle_t s_server = nullptr;
static bool s_fs_mounted = false;

struct WsClientSlot {
    int fd;
    bool is_subscribed;
    uint32_t last_activity_ms;
    UserRole role;
    char token[Session::TOKEN_LEN + 1];
};

static constexpr size_t MAX_WS_CLIENTS = 4;
static WsClientSlot s_ws_slots[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_ws_mutex = nullptr;

static TaskHandle_t s_telemetry_task = nullptr;

static void init_ws_slots() {
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        s_ws_slots[i].fd = -1;
        s_ws_slots[i].is_subscribed = false;
        s_ws_slots[i].last_activity_ms = 0;
        s_ws_slots[i].role = UserRole::NONE;
        s_ws_slots[i].token[0] = '\0';
    }
}

static bool allocate_ws_slot(int fd, UserRole role, const char *token = nullptr) {
    if (!s_ws_mutex) return false;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    bool ok = false;
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_slots[i].fd == -1 || s_ws_slots[i].fd == fd) {
            s_ws_slots[i].fd = fd;
            s_ws_slots[i].is_subscribed = false;
            s_ws_slots[i].last_activity_ms = get_millis();
            s_ws_slots[i].role = role;
            if (token && token[0] != '\0') {
                strlcpy(s_ws_slots[i].token, token, sizeof(s_ws_slots[i].token));
            }
            ok = true;
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
    return ok;
}

static void release_ws_slot(int fd) {
    if (!s_ws_mutex) return;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_slots[i].fd == fd) {
            s_ws_slots[i].fd = -1;
            s_ws_slots[i].is_subscribed = false;
            s_ws_slots[i].last_activity_ms = 0;
            s_ws_slots[i].role = UserRole::NONE;
            s_ws_slots[i].token[0] = '\0';
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

bool is_filesystem_ready() {
    if (!s_fs_mounted) return false;
    struct stat st;
    return (stat("/storage/index.html", &st) == 0);
}

httpd_handle_t get_handle() {
    return s_server;
}

int get_active_ws_subscribers() {
    if (!s_ws_mutex) return 0;
    int count = 0;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_slots[i].fd != -1 && s_ws_slots[i].is_subscribed) {
            count++;
        }
    }
    xSemaphoreGive(s_ws_mutex);
    return count;
}

void set_active_ws_subscribers(int count) {
    (void)count; // Handled per client session slot
}

// -------------------------------------------------------------
// WebSocket Broadcasting (RFC 6455 & Anti-Backpressure)
// -------------------------------------------------------------
void broadcast_ws_text(const char *payload) {
    if (!s_server || !payload || !s_ws_mutex) return;

    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.payload = (uint8_t *)payload;
    ws_pkt.len = strlen(payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true; // Kepatuhan penuh RFC 6455

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_slots[i].fd != -1 && s_ws_slots[i].is_subscribed) {
            int fd = s_ws_slots[i].fd;
            esp_err_t ret = httpd_ws_send_frame_async(s_server, fd, &ws_pkt);
            if (ret != ESP_OK) {
                // Hanya putuskan jika koneksi socket memang sudah hangus/ditutup
                if (ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL || ret == ESP_ERR_NOT_FOUND) {
                    ESP_LOGD(TAG, "Koneksi WS fd %d terputus (ret=0x%x), membersihkan slot...", fd, ret);
                    s_ws_slots[i].fd = -1;
                    s_ws_slots[i].is_subscribed = false;
                    s_ws_slots[i].role = UserRole::NONE;
                }
            }
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

// -------------------------------------------------------------
// Pre-Handshake Verification (Otentikasi Aman RFC 6455)
// -------------------------------------------------------------
#ifdef CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
static esp_err_t ws_pre_handshake_cb(httpd_req_t *req) {
    char token[Session::TOKEN_LEN + 1] = {0};
    UserRole role = Session::validate(req, token, sizeof(token));
    if (role == UserRole::NONE) {
        ESP_LOGW(TAG, "WS Handshake ditolak: Tidak terotentikasi (URI: %s)", req->uri ? req->uri : "NULL");
        return ESP_FAIL;
    }
    int fd = httpd_req_to_sockfd(req);
    if (!allocate_ws_slot(fd, role, token)) {
        ESP_LOGW(TAG, "WS Handshake ditolak: Slot klien penuh (fd %d)", fd);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "WS Handshake disetujui (fd %d, role: %s)", fd,
             role == UserRole::ADMIN ? "admin" : "teknisi");
    return ESP_OK;
}
#endif

// -------------------------------------------------------------
// WebSocket Request & Frame Handler (Two-Step Ingestion)
// Dipanggil oleh ESP-IDF saat paket/frame WebSocket masuk
// -------------------------------------------------------------
static esp_err_t ws_handler(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);

    // 1. Tahap 1: Dapatkan metadata & ukuran frame yang masuk (max_len = 0)
    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Gagal membaca header frame WS (fd %d, ret=0x%x)", fd, ret);
        release_ws_slot(fd);
        return ret;
    }

    // 2. Tangani frame kontrol: PING -> balas PONG
    if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        httpd_ws_frame_t pong_pkt = {};
        pong_pkt.type = HTTPD_WS_TYPE_PONG;
        pong_pkt.final = true;
        return httpd_ws_send_frame_async(req->handle, fd, &pong_pkt);
    }

    // 3. Tangani frame kontrol: CLOSE
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "Client menutup koneksi WS (CLOSE frame fd %d)", fd);
        release_ws_slot(fd);
        return ESP_OK;
    }

    // 4. Tahap 2: Baca payload data teks jika ada
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0) {
        char stack_buf[256];
        char *buf = stack_buf;
        if (ws_pkt.len >= sizeof(stack_buf)) {
            buf = (char *)malloc(ws_pkt.len + 1);
            if (!buf) {
                ESP_LOGE(TAG, "OOM alokasi WS payload (%zu bytes)", ws_pkt.len);
                return ESP_ERR_NO_MEM;
            }
        }

        ws_pkt.payload = (uint8_t *)buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Gagal membaca payload frame WS (fd %d, ret=0x%x)", fd, ret);
            if (buf != stack_buf) free(buf);
            release_ws_slot(fd);
            return ret;
        }
        buf[ws_pkt.len] = '\0';

        // Pastikan slot teralokasi (fallback jika pre-handshake cb tidak aktif di menuconfig)
        allocate_ws_slot(fd, UserRole::ADMIN);

        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
            if (s_ws_slots[i].fd == fd) {
                s_ws_slots[i].last_activity_ms = get_millis();
                if (strstr(buf, "\"action\":\"start\"") || strstr(buf, "\"action\": \"start\"") || strstr(buf, "start")) {
                    s_ws_slots[i].is_subscribed = true;
                    Session::touch(s_ws_slots[i].token);
                    Core::BeaconPipeline::flushLiveBeacons();
                    ESP_LOGI(TAG, "WS Test Subscriber AKTIF & Live Buffer Di-flush (fd %d)", fd);
                } else if (strstr(buf, "\"action\":\"stop\"") || strstr(buf, "\"action\": \"stop\"") || strstr(buf, "stop")) {
                    s_ws_slots[i].is_subscribed = false;
                    ESP_LOGI(TAG, "WS Test Subscriber STOP (fd %d)", fd);
                }
                break;
            }
        }
        xSemaphoreGive(s_ws_mutex);

        if (buf != stack_buf) {
            free(buf);
        }
    }

    return ESP_OK;
}

// -------------------------------------------------------------
// Live Telemetry Broadcast (Adaptive & Zero-DRAM Leak)
// -------------------------------------------------------------
void broadcast_live_ws_stats() {
    if (Ota::is_in_progress()) return; // Total silence during OTA flashing

    // Periksa apakah ada minimal 1 subscriber aktif sebelum alokasi JSON
    bool has_subscribers = false;
    if (s_ws_mutex && xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
            if (s_ws_slots[i].fd != -1 && s_ws_slots[i].is_subscribed) {
                has_subscribers = true;
                // Auto-keepalive session saat "Start Test" berjalan!
                Session::touch(s_ws_slots[i].token);
            }
        }
        xSemaphoreGive(s_ws_mutex);
    }

    // Efisiensi Industri: Jika tidak ada yang menonton, hindari alokasi memori & CPU
    if (!has_subscribers) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "beacons_read", Core::BeaconPipeline::getTotalBeaconsRead());
    cJSON_AddNumberToObject(root, "read_speed", Core::BeaconPipeline::getReadSpeed());
    cJSON_AddNumberToObject(root, "mqtt_speed", Core::BeaconPipeline::getMqttSpeed());
    cJSON_AddNumberToObject(root, "mqtt_published", Core::BeaconPipeline::getTotalMqttPublished());
    cJSON_AddNumberToObject(root, "dropped_count", Core::BeaconPipeline::getTotalDropped());
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size() / 1024);
    cJSON_AddNumberToObject(root, "queue_load", Core::BeaconPipeline::getQueueLoad());
    cJSON_AddNumberToObject(root, "batch_load", Network::MqttService::getBatchBufferLoad());
    cJSON_AddNumberToObject(root, "batch_max", g_config.batch_max);
    cJSON_AddNumberToObject(root, "batch_pkt_size", Network::MqttService::getLastBatchPktCount());
    cJSON_AddNumberToObject(root, "net_bandwidth", Network::MqttService::getBandwidthKBps());
    cJSON_AddBoolToObject(root, "ap_active", Network::NetworkManager::isApActive());
    cJSON_AddNumberToObject(root, "ap_remain_sec", Network::NetworkManager::getApRemainingSeconds());

    cJSON *beacons_arr = cJSON_CreateArray();
    LiveBeaconItem live_buf[50];
    size_t count = Core::BeaconPipeline::copyLiveBeacons(live_buf, 50);

    for (size_t i = 0; i < count; i++) {
        cJSON *b = cJSON_CreateObject();
        cJSON_AddStringToObject(b, "code", live_buf[i].code);
        cJSON_AddStringToObject(b, "tag", live_buf[i].tag);
        cJSON_AddNumberToObject(b, "rssi", live_buf[i].rssi);
        if (strcmp(live_buf[i].code, "iBeacon") == 0 || live_buf[i].batt == 0) {
            cJSON_AddNullToObject(b, "batt");
        } else {
            cJSON_AddNumberToObject(b, "batt", live_buf[i].batt);
        }
        cJSON_AddItemToArray(beacons_arr, b);
    }
    cJSON_AddItemToObject(root, "beacons", beacons_arr);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        broadcast_ws_text(out);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

static void telemetry_task_func(void *arg) {
    ESP_LOGI(TAG, "Web Telemetry broadcast task aktif (250ms adaptive interval)");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(250));
        Ota::check_watchdog();
        broadcast_live_ws_stats();
    }
}

// -------------------------------------------------------------
// Server Lifecycle
// -------------------------------------------------------------
bool init() {
    ESP_LOGI(TAG, "Inisialisasi Filesystem LittleFS (/storage)...");

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/storage";
    conf.partition_label = "storage";
    conf.format_if_mount_failed = false;
    conf.read_only = 0;
    conf.dont_mount = 0;
    conf.grow_on_mount = 0;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal me-mount LittleFS: 0x%x (Mode Darurat Aktif)", ret);
        s_fs_mounted = false;
    } else {
        ESP_LOGI(TAG, "LittleFS berhasil dimount di /storage");
        s_fs_mounted = true;
    }

    Session::init();
    Template::init();
    Ota::init();

    if (!s_ws_mutex) {
        s_ws_mutex = xSemaphoreCreateMutex();
    }
    init_ws_slots();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 16; // Support 3 laptops (parallel keep-alive pool) + WS clients
    config.max_uri_handlers = 32;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.core_id = 1; // Core 1 for Web Server & MQTT (leaving Core 0 for UART pipeline)
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG, "Menjalankan native HTTP Server pada port %d...", config.server_port);
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Gagal memulai httpd server!");
        return false;
    }

    // Register WebSocket route /ws* (wildcard matches both /ws and /ws?token=...)
    httpd_uri_t uri_ws = {};
    uri_ws.uri = "/ws*";
    uri_ws.method = HTTP_GET;
    uri_ws.handler = ws_handler;
    uri_ws.is_websocket = true;
#ifdef CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
    uri_ws.ws_pre_handshake_cb = ws_pre_handshake_cb;
#endif
    httpd_register_uri_handler(s_server, &uri_ws);

    // Register all HTTP & REST routes
    Routes::register_all(s_server);

    // Start 100ms Telemetry Broadcast & Watchdog Task on Core 1
    xTaskCreatePinnedToCore(telemetry_task_func, "web_telemetry", 4096, nullptr, 4, &s_telemetry_task, 1);

    ESP_LOGI(TAG, "Web Server native ESP-IDF siap dan aktif.");
    return true;
}

void stop() {
    if (s_telemetry_task) {
        vTaskDelete(s_telemetry_task);
        s_telemetry_task = nullptr;
    }
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
    if (s_fs_mounted) {
        esp_vfs_littlefs_unregister("storage");
        s_fs_mounted = false;
    }
}

} // namespace Iotaru::Web::Server
