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
static std::atomic<bool> s_fs_mounted{false};
static std::atomic<bool> s_fs_maintenance{false};
static std::atomic<int> s_active_file_reads{0};

bool acquire_file_read() {
    // Pola atomic anti-TOCTOU: Increment terlebih dahulu baru periksa status
    s_active_file_reads.fetch_add(1, std::memory_order_seq_cst);
    if (s_fs_maintenance.load(std::memory_order_seq_cst) || !s_fs_mounted.load(std::memory_order_seq_cst)) {
        s_active_file_reads.fetch_sub(1, std::memory_order_seq_cst);
        return false;
    }
    return true;
}

void release_file_read() {
    s_active_file_reads.fetch_sub(1, std::memory_order_seq_cst);
}

int get_active_file_reads() {
    return s_active_file_reads.load(std::memory_order_seq_cst);
}

void set_maintenance_mode(bool enable) {
    s_fs_maintenance.store(enable, std::memory_order_seq_cst);
}

bool is_in_maintenance() {
    return s_fs_maintenance.load(std::memory_order_seq_cst);
}

void mark_filesystem_unmounted() {
    s_fs_mounted.store(false, std::memory_order_seq_cst);
}

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
static std::atomic<bool> s_telemetry_stop{false};

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
            if (token && strlen(token) == Session::TOKEN_LEN) {
                strlcpy(s_ws_slots[i].token, token, sizeof(s_ws_slots[i].token));
            } else {
                s_ws_slots[i].token[0] = '\0';
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
    if (!s_fs_mounted.load(std::memory_order_seq_cst) || s_fs_maintenance.load(std::memory_order_seq_cst)) return false;
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

    int active_fds[MAX_WS_CLIENTS];
    size_t fd_count = 0;

    // 1. Salin daftar FD aktif di dalam critical section singkat
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_ws_slots[i].fd != -1 && s_ws_slots[i].is_subscribed) {
            active_fds[fd_count++] = s_ws_slots[i].fd;
        }
    }
    xSemaphoreGive(s_ws_mutex);

    if (fd_count == 0) return;

    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.payload = (uint8_t *)payload;
    ws_pkt.len = strlen(payload);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true; // Kepatuhan penuh RFC 6455

    // 2. Kirim frame di luar mutex untuk mencegah deadlock
    for (size_t i = 0; i < fd_count; i++) {
        int fd = active_fds[i];
        esp_err_t ret = httpd_ws_send_frame_async(s_server, fd, &ws_pkt);
        if (ret == ESP_ERR_INVALID_STATE || ret == ESP_FAIL || ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGD(TAG, "Koneksi WS fd %d terputus (ret=0x%x), membersihkan slot...", fd, ret);
            release_ws_slot(fd);
        }
    }
}

// -------------------------------------------------------------
// Pre-Handshake Verification (Otentikasi Aman RFC 6455)
// -------------------------------------------------------------
#ifdef CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
static esp_err_t ws_pre_handshake_cb(httpd_req_t *req) {
    if (Network::NetworkManager::isApActive()) {
        Network::NetworkManager::recordApActivity();
    }
    // Validasi Header Origin (Mitigasi Cross-Site WebSocket Hijacking / CSWSH)
    size_t origin_len = httpd_req_get_hdr_value_len(req, "Origin");
    if (origin_len > 0) {
        char origin_buf[128];
        if (origin_len < sizeof(origin_buf) && httpd_req_get_hdr_value_str(req, "Origin", origin_buf, sizeof(origin_buf)) == ESP_OK) {
            if (strcmp(origin_buf, "null") != 0) {
                const char *origin_host = origin_buf;
                if (strncmp(origin_host, "http://", 7) == 0) {
                    origin_host += 7;
                } else if (strncmp(origin_host, "https://", 8) == 0) {
                    origin_host += 8;
                }
                char clean_origin[64] = {0};
                strlcpy(clean_origin, origin_host, sizeof(clean_origin));
                char *colon = strchr(clean_origin, ':');
                if (colon) *colon = '\0';
                char *slash = strchr(clean_origin, '/');
                if (slash) *slash = '\0';

                char host_buf[64] = {0};
                size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
                if (host_len > 0 && host_len < sizeof(host_buf)) {
                    httpd_req_get_hdr_value_str(req, "Host", host_buf, sizeof(host_buf));
                    char *h_colon = strchr(host_buf, ':');
                    if (h_colon) *h_colon = '\0';
                }

                bool allowed = false;
                if (host_buf[0] != '\0' && strcasecmp(clean_origin, host_buf) == 0) {
                    allowed = true;
                } else if (strcmp(clean_origin, "localhost") == 0 || strcmp(clean_origin, "127.0.0.1") == 0) {
                    allowed = true;
                }

                if (!allowed) {
                    ESP_LOGW(TAG, "WS Handshake DITOLAK (CSWSH): Origin '%s' tidak cocok dengan Host '%s'", origin_buf, host_buf);
                    return ESP_FAIL;
                }
            }
        }
    }

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
    if (Network::NetworkManager::isApActive()) {
        Network::NetworkManager::recordApActivity();
    }
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
        if (ws_pkt.len > 1024) {
            ESP_LOGW(TAG, "WS payload terlalu besar (%zu bytes), koneksi ditolak (fd %d)", ws_pkt.len, fd);
            release_ws_slot(fd);
            return ESP_ERR_INVALID_SIZE;
        }

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

        // Verifikasi sesi WebSocket: Cek apakah socket fd ini sudah terotentikasi (VULN-SEC-01)
        bool slot_authenticated = false;
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        for (size_t i = 0; i < MAX_WS_CLIENTS; i++) {
            if (s_ws_slots[i].fd == fd && s_ws_slots[i].role != UserRole::NONE) {
                slot_authenticated = true;
                break;
            }
        }
        xSemaphoreGive(s_ws_mutex);

        if (!slot_authenticated) {
            char token[Session::TOKEN_LEN + 1] = {0};
            UserRole role = Session::validate(req, token, sizeof(token));
            if (role == UserRole::NONE) {
                ESP_LOGW(TAG, "WS Frame ditolak: Client tidak memiliki sesi valid (fd %d)", fd);
                if (buf != stack_buf) free(buf);
                release_ws_slot(fd);
                return ESP_FAIL;
            }
            if (!allocate_ws_slot(fd, role, token)) {
                ESP_LOGW(TAG, "WS Slot penuh untuk fd %d", fd);
                if (buf != stack_buf) free(buf);
                return ESP_FAIL;
            }
        }

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

    if (Network::NetworkManager::isApActive()) {
        Network::NetworkManager::recordApActivity();
    }

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
    cJSON_AddNumberToObject(root, "total_bytes", (double)Network::MqttService::getTotalWireBytes());
    cJSON_AddBoolToObject(root, "ap_active", Network::NetworkManager::isApActive());
    cJSON_AddNumberToObject(root, "ap_remain_sec", Network::NetworkManager::getApRemainingSeconds());

    cJSON *beacons_arr = cJSON_CreateArray();
    static LiveBeaconItem s_live_buf[50];
    size_t count = Core::BeaconPipeline::copyLiveBeacons(s_live_buf, 50);

    for (size_t i = 0; i < count; i++) {
        cJSON *b = cJSON_CreateObject();
        cJSON_AddStringToObject(b, "code", s_live_buf[i].code);
        cJSON_AddStringToObject(b, "tag", s_live_buf[i].tag);
        cJSON_AddNumberToObject(b, "rssi", s_live_buf[i].rssi);
        if (strcmp(s_live_buf[i].code, "iBeacon") == 0 || s_live_buf[i].batt == 0) {
            cJSON_AddNullToObject(b, "batt");
        } else {
            cJSON_AddNumberToObject(b, "batt", s_live_buf[i].batt);
        }
        cJSON_AddItemToArray(beacons_arr, b);
    }
    cJSON_AddItemToObject(root, "beacons", beacons_arr);

    // Ringkasan Alasan Drop
    cJSON *drop_summary = cJSON_CreateObject();
    cJSON_AddNumberToObject(drop_summary, "ttl", Core::BeaconPipeline::getDropTtlCount());
    cJSON_AddNumberToObject(drop_summary, "rssi", Core::BeaconPipeline::getDropRssiCount());
    cJSON_AddNumberToObject(drop_summary, "uuid", Core::BeaconPipeline::getDropUuidCount());
    cJSON_AddNumberToObject(drop_summary, "major", Core::BeaconPipeline::getDropMajorCount());
    cJSON_AddNumberToObject(drop_summary, "queue", Core::BeaconPipeline::getDropQueueCount());
    cJSON_AddNumberToObject(drop_summary, "mqtt", Core::BeaconPipeline::getDropMqttCount());
    cJSON_AddItemToObject(root, "drop_summary", drop_summary);

    // Snapshot Event Dropped Beacons
    cJSON *dropped_arr = cJSON_CreateArray();
    static DroppedBeaconItem s_drop_buf[50];
    size_t drop_count = Core::BeaconPipeline::copyDroppedBeacons(s_drop_buf, 50);

    for (size_t i = 0; i < drop_count; i++) {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "code", s_drop_buf[i].code);
        cJSON_AddStringToObject(d, "tag", s_drop_buf[i].tag);
        cJSON_AddNumberToObject(d, "rssi", s_drop_buf[i].rssi);
        cJSON_AddNumberToObject(d, "reason", (int)s_drop_buf[i].reason);
        cJSON_AddItemToArray(dropped_arr, d);
    }
    cJSON_AddItemToObject(root, "dropped_beacons", dropped_arr);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        broadcast_ws_text(out);
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

static void telemetry_task_func(void *arg) {
    ESP_LOGI(TAG, "Web Telemetry broadcast task aktif (250ms adaptive interval)");
    while (!s_telemetry_stop.load(std::memory_order_relaxed)) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (s_telemetry_stop.load(std::memory_order_relaxed)) break;
        Ota::check_watchdog();
        // CATATAN: broadcast_live_ws_stats() menggunakan static buffer di BSS,
        // hanya aman dipanggil dari satu task ini saja.
        broadcast_live_ws_stats();
    }
    s_telemetry_task = nullptr;
    vTaskDelete(nullptr);
}

// -------------------------------------------------------------
// Server Lifecycle
// -------------------------------------------------------------
bool initStorage() {
    if (s_fs_mounted.load(std::memory_order_seq_cst)) {
        return true;
    }

    ESP_LOGI(TAG, "Inisialisasi Filesystem LittleFS (/storage)...");

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/storage";
    conf.partition_label = "storage";
    conf.format_if_mount_failed = false; // Anti-format
    conf.read_only = 0;
    conf.dont_mount = 0;
    conf.grow_on_mount = 0;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal me-mount LittleFS: 0x%x (Mode Darurat Aktif)", ret);
        s_fs_mounted.store(false, std::memory_order_seq_cst);
        return false;
    }

    // Verifikasi integritas saat cold boot (antisipasi jika mati lampu saat OTA)
    struct stat st;
    if (stat("/storage/index.html", &st) != 0) {
        ESP_LOGW(TAG, "LittleFS ter-mount namun index.html tidak ditemukan (Inkomplit/Mati Lampu). Mode Darurat Aktif.");
        esp_vfs_littlefs_unregister("storage");
        s_fs_mounted.store(false, std::memory_order_seq_cst);
        return false;
    }

    ESP_LOGI(TAG, "LittleFS berhasil dimount dan terverifikasi di /storage");
    s_fs_mounted.store(true, std::memory_order_seq_cst);
    return true;
}

bool remountStorage() {
    if (s_fs_mounted.load(std::memory_order_seq_cst)) return true;

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = "/storage";
    conf.partition_label = "storage";
    conf.format_if_mount_failed = false; // WAJIB FALSE: Jangan auto-format!
    conf.read_only = 0;
    conf.dont_mount = 0;
    conf.grow_on_mount = 0;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gagal remount LittleFS: 0x%x", ret);
        s_fs_mounted.store(false, std::memory_order_seq_cst);
        return false;
    }

    struct stat st;
    if (stat("/storage/index.html", &st) == 0) {
        ESP_LOGI(TAG, "LittleFS berhasil di-remount kembali.");
        s_fs_mounted.store(true, std::memory_order_seq_cst);
        return true;
    }

    ESP_LOGW(TAG, "LittleFS ter-mount tetapi index.html tidak ditemukan.");
    esp_vfs_littlefs_unregister("storage");
    s_fs_mounted.store(false, std::memory_order_seq_cst);
    return false;
}

bool init() {
    initStorage();

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
    config.max_uri_handlers = 64; // Increased from 32 to support 37+ routes & wildcard assets
    config.stack_size = 8192;
    config.task_priority = 3; // Prioritas 3 (di bawah MQTT 6 & Telemetry 4) agar pembacaan web tidak mem-preempt telemetri
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
    s_telemetry_stop.store(false, std::memory_order_seq_cst);
    xTaskCreatePinnedToCore(telemetry_task_func, "web_telemetry", 8192, nullptr, 4, &s_telemetry_task, 1);

    ESP_LOGI(TAG, "Web Server native ESP-IDF siap dan aktif.");
    return true;
}

void stop() {
    if (s_telemetry_task) {
        s_telemetry_stop.store(true, std::memory_order_seq_cst);
        for (int i = 0; i < 15 && s_telemetry_task != nullptr; i++) {
            vTaskDelay(pdMS_TO_TICKS(25));
        }
        if (s_telemetry_task) {
            vTaskDelete(s_telemetry_task);
            s_telemetry_task = nullptr;
        }
    }
    if (s_server) {
        httpd_stop(s_server);
        s_server = nullptr;
    }
    if (s_fs_mounted.load(std::memory_order_seq_cst)) {
        esp_vfs_littlefs_unregister("storage");
        s_fs_mounted.store(false, std::memory_order_seq_cst);
    }
}

} // namespace Iotaru::Web::Server
