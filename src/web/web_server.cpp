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

static const char *TAG = "WebServer";

namespace Iotaru::Web::Server {

static httpd_handle_t s_server = nullptr;
static bool s_fs_mounted = false;
static std::atomic<int> s_active_ws_subscribers{0};
static constexpr size_t MAX_WS_CLIENTS = 4;
static int s_ws_fds[MAX_WS_CLIENTS] = {-1, -1, -1, -1};
static SemaphoreHandle_t s_ws_mutex = nullptr;

static TaskHandle_t s_telemetry_task = nullptr;

bool is_filesystem_ready() {
    if (!s_fs_mounted) return false;
    struct stat st;
    return (stat("/storage/index.html", &st) == 0);
}

httpd_handle_t get_handle() {
    return s_server;
}

int get_active_ws_subscribers() {
    return s_active_ws_subscribers.load();
}

void set_active_ws_subscribers(int count) {
    s_active_ws_subscribers.store(count < 0 ? 0 : count);
}

// -------------------------------------------------------------
// WebSocket Helpers
// -------------------------------------------------------------
static void add_ws_fd(int fd) {
    if (!s_ws_mutex) return;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == -1 || s_ws_fds[i] == fd) {
            s_ws_fds[i] = fd;
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

static void remove_ws_fd(int fd) {
    if (!s_ws_mutex) return;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = -1;
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

void broadcast_ws_text(const char *payload) {
    if (!s_server || !payload || !s_ws_mutex) return;

    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.payload = (uint8_t *)payload;
    ws_pkt.len = strlen(payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        int fd = s_ws_fds[i];
        if (fd != -1) {
            esp_err_t ret = httpd_ws_send_frame_async(s_server, fd, &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "Gagal kirim frame WS ke fd %d, menutup...", fd);
                s_ws_fds[i] = -1;
            }
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        // Handshake validation
        UserRole role = Session::validate(req);
        if (role == UserRole::NONE) {
            ESP_LOGW(TAG, "Handshake WS ditolak: Tidak terotentikasi (fd %d)", fd);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Handshake WS berhasil (fd %d)", fd);
        add_ws_fd(fd);
        return ESP_OK;
    }

    uint8_t buf[256];
    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.payload = buf;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) {
        remove_ws_fd(fd);
        return ret;
    }

    buf[ws_pkt.len] = '\0';
    const char *msg = reinterpret_cast<const char *>(buf);

    if (strstr(msg, "\"action\":\"start\"") || strstr(msg, "\"action\": \"start\"")) {
        s_active_ws_subscribers++;
        ESP_LOGI(TAG, "WS Test Subscriber bertambah: %d", s_active_ws_subscribers.load());
    } else if (strstr(msg, "\"action\":\"stop\"") || strstr(msg, "\"action\": \"stop\"")) {
        int cur = s_active_ws_subscribers.load();
        if (cur > 0) s_active_ws_subscribers--;
        ESP_LOGI(TAG, "WS Test Subscriber berkurang: %d", s_active_ws_subscribers.load());
    }

    return ESP_OK;
}

// -------------------------------------------------------------
// Live Telemetry Broadcast
// -------------------------------------------------------------
void broadcast_live_ws_stats() {
    if (Ota::is_in_progress()) return; // Total silence during OTA flashing
    if (s_active_ws_subscribers.load() <= 0) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "read_speed", Core::BeaconPipeline::getReadSpeed());
    cJSON_AddNumberToObject(root, "mqtt_speed", Core::BeaconPipeline::getMqttSpeed());
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
    ESP_LOGI(TAG, "Web Telemetry broadcast task aktif (100ms interval)");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
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

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 7;
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

    // Register WebSocket route /ws (zero-initialized struct)
    httpd_uri_t uri_ws = {};
    uri_ws.uri = "/ws";
    uri_ws.method = HTTP_GET;
    uri_ws.handler = ws_handler;
    uri_ws.is_websocket = true;
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
