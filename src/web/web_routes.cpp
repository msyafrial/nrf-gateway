#include "web_routes.hpp"
#include "web_server.hpp"
#include "session_mgr.hpp"
#include "web_template.hpp"
#include "ota_service.hpp"
#include "app_config.hpp"
#include "config_store.hpp"
#include "net_manager.hpp"
#include "mqtt_service.hpp"
#include "IEthernetDriver.hpp"
#include "beacon_pipeline.hpp"
#include <esp_wifi.h>
#include <esp_log.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char *TAG = "WebRoutes";

namespace Iotaru::Web::Routes {

static bool s_auth_reboot_required = false;

// -------------------------------------------------------------
// URL Decoding & Form Parameter Extraction Helpers
// -------------------------------------------------------------
static void urldecode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool get_form_param(const char *body, const char *key, char *out_val, size_t max_len) {
    if (!body || !key || !out_val || max_len == 0) return false;
    out_val[0] = '\0';

    size_t klen = strlen(key);
    const char *p = body;
    while (p) {
        if ((p == body || *(p - 1) == '&') && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val_start = p + klen + 1;
            const char *val_end = strchr(val_start, '&');
            size_t vlen = val_end ? static_cast<size_t>(val_end - val_start) : strlen(val_start);
            char enc[256];
            if (vlen >= sizeof(enc)) vlen = sizeof(enc) - 1;
            memcpy(enc, val_start, vlen);
            enc[vlen] = '\0';
            urldecode(out_val, enc);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

static const char* get_content_type(const char *filename) {
    if (strstr(filename, ".html")) return "text/html";
    if (strstr(filename, ".css")) return "text/css";
    if (strstr(filename, ".js")) return "application/javascript";
    if (strstr(filename, ".json")) return "application/json";
    if (strstr(filename, ".png")) return "image/png";
    if (strstr(filename, ".ico")) return "image/x-icon";
    return "text/plain";
}

// -------------------------------------------------------------
// Static File Streamer (With automatic GZIP detection)
// -------------------------------------------------------------
static esp_err_t stream_static_file(httpd_req_t *req, const char *base_path) {
    char gz_path[600];
    snprintf(gz_path, sizeof(gz_path), "%s.gz", base_path);

    bool client_accepts_gzip = false;
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
    if (hdr_len > 0) {
        char enc_hdr[64];
        if (hdr_len < sizeof(enc_hdr) && httpd_req_get_hdr_value_str(req, "Accept-Encoding", enc_hdr, sizeof(enc_hdr)) == ESP_OK) {
            if (strstr(enc_hdr, "gzip")) client_accepts_gzip = true;
        }
    }

    FILE *f = nullptr;
    bool is_gzip = false;
    struct stat st;

    if (client_accepts_gzip && stat(gz_path, &st) == 0) {
        f = fopen(gz_path, "r");
        is_gzip = true;
    } else if (stat(base_path, &st) == 0) {
        f = fopen(base_path, "r");
        is_gzip = false;
    }

    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, get_content_type(base_path));
    if (is_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    char chunk[2048];
    while (true) {
        size_t r = fread(chunk, 1, sizeof(chunk), f);
        if (r == 0) break;
        if (httpd_resp_send_chunk(req, chunk, r) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

// -------------------------------------------------------------
// Route Handlers: Pages
// -------------------------------------------------------------
static esp_err_t index_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/index.html");
}

static esp_err_t login_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    UserRole role = Session::validate(req);
    if (role != UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return stream_static_file(req, "/storage/login.html");
}

static esp_err_t test_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/test.html");
}

static esp_err_t auth_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    if (role == UserRole::ADMIN) {
        return Template::stream_template_file(req, "/storage/auth_admin.html");
    } else {
        return Template::stream_template_file(req, "/storage/auth_teknisi.html");
    }
}

static esp_err_t auth_admin_handler(httpd_req_t *req) {
    if (Session::validate(req) != UserRole::ADMIN) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth_teknisi.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/auth_admin.html");
}

static esp_err_t auth_teknisi_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/auth_teknisi.html");
}

static esp_err_t settings_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    if (role != UserRole::ADMIN) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth_teknisi.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/settings.html");
}

static esp_err_t firmware_handler(httpd_req_t *req) {
    if (!Server::is_filesystem_ready()) {
        return Template::stream_emergency_page(req);
    }
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    if (role != UserRole::ADMIN) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/auth_teknisi.html");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/firmware.html");
}

static esp_err_t assets_wildcard_handler(httpd_req_t *req) {
    char filepath[600];
    snprintf(filepath, sizeof(filepath), "/storage%s", req->uri);
    return stream_static_file(req, filepath);
}

// -------------------------------------------------------------
// Route Handlers: Authentication REST APIs
// -------------------------------------------------------------
static esp_err_t api_login_handler(httpd_req_t *req) {
    if (Session::is_rate_limited()) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Terlalu banyak percobaan gagal. Silakan tunggu 60 detik.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char body[512] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    if (r <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    body[r] = '\0';

    char u[64] = {0};
    char p[64] = {0};
    get_form_param(body, "username", u, sizeof(u));
    get_form_param(body, "password", p, sizeof(p));

    UserRole role = UserRole::NONE;
    char token[Session::TOKEN_LEN + 1] = {0};

    if (Session::authenticate(u, p, role, token, sizeof(token))) {
        char cookie_hdr[128];
        snprintf(cookie_hdr, sizeof(cookie_hdr), "token=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600", token);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
        httpd_resp_set_type(req, "application/json");

        char resp[96];
        snprintf(resp, sizeof(resp), "{\"status\":\"success\",\"role\":\"%s\"}",
                 role == UserRole::ADMIN ? "admin" : "teknisi");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Username atau password salah.", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static esp_err_t api_logout_handler(httpd_req_t *req) {
    char token[Session::TOKEN_LEN + 1] = {0};
    Session::validate(req, token, sizeof(token));
    if (token[0] != '\0') {
        Session::invalidate(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_auth_check_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    httpd_resp_set_type(req, "application/json");
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"authenticated\":false}", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send(req, "{\"authenticated\":true}", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: Configuration & Network REST APIs
// -------------------------------------------------------------
static esp_err_t api_config_auth_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char body[1024] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    if (r <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    body[r] = '\0';

    if (role == UserRole::ADMIN) {
        char val[64];
        if (get_form_param(body, "admin_user", val, sizeof(val))) strncpy(g_config.admin_user, val, sizeof(g_config.admin_user) - 1);
        if (get_form_param(body, "admin_pass", val, sizeof(val))) strncpy(g_config.admin_pass, val, sizeof(g_config.admin_pass) - 1);
        if (get_form_param(body, "mqtt_host", val, sizeof(val))) strncpy(g_config.mqtt_host, val, sizeof(g_config.mqtt_host) - 1);
        if (get_form_param(body, "mqtt_port", val, sizeof(val))) g_config.mqtt_port = atoi(val);
        if (get_form_param(body, "mqtt_user", val, sizeof(val))) strncpy(g_config.mqtt_user, val, sizeof(g_config.mqtt_user) - 1);
        if (get_form_param(body, "mqtt_password", val, sizeof(val))) strncpy(g_config.mqtt_password, val, sizeof(g_config.mqtt_password) - 1);
        if (get_form_param(body, "ap_password", val, sizeof(val)) && strlen(val) >= 8) {
            strncpy(g_config.ap_password, val, sizeof(g_config.ap_password) - 1);
        }
    }

    char val[64];
    if (get_form_param(body, "user_user", val, sizeof(val))) strncpy(g_config.user_user, val, sizeof(g_config.user_user) - 1);
    if (get_form_param(body, "user_pass", val, sizeof(val))) strncpy(g_config.user_pass, val, sizeof(g_config.user_pass) - 1);
    if (get_form_param(body, "connection_mode", val, sizeof(val))) {
        g_config.connection_mode = atoi(val);
    }
    if (get_form_param(body, "w_ssid", val, sizeof(val))) strncpy(g_config.w_ssid, val, sizeof(g_config.w_ssid) - 1);
    if (get_form_param(body, "w_password", val, sizeof(val))) strncpy(g_config.w_password, val, sizeof(g_config.w_password) - 1);
    if (get_form_param(body, "w_dhcp", val, sizeof(val))) g_config.w_dhcp = atoi(val);
    if (get_form_param(body, "w_ip", val, sizeof(val))) strncpy(g_config.w_ip, val, sizeof(g_config.w_ip) - 1);
    if (get_form_param(body, "w_gateway", val, sizeof(val))) strncpy(g_config.w_gateway, val, sizeof(g_config.w_gateway) - 1);
    if (get_form_param(body, "w_subnet", val, sizeof(val))) strncpy(g_config.w_subnet, val, sizeof(g_config.w_subnet) - 1);
    if (get_form_param(body, "w_dns", val, sizeof(val))) strncpy(g_config.w_dns, val, sizeof(g_config.w_dns) - 1);
    if (get_form_param(body, "w_dns2", val, sizeof(val))) strncpy(g_config.w_dns2, val, sizeof(g_config.w_dns2) - 1);

    if (get_form_param(body, "eth_dhcp", val, sizeof(val))) g_config.eth_dhcp = atoi(val);
    if (get_form_param(body, "eth_ip", val, sizeof(val))) strncpy(g_config.eth_ip, val, sizeof(g_config.eth_ip) - 1);
    if (get_form_param(body, "eth_gateway", val, sizeof(val))) strncpy(g_config.eth_gateway, val, sizeof(g_config.eth_gateway) - 1);
    if (get_form_param(body, "eth_subnet", val, sizeof(val))) strncpy(g_config.eth_subnet, val, sizeof(g_config.eth_subnet) - 1);
    if (get_form_param(body, "eth_dns", val, sizeof(val))) strncpy(g_config.eth_dns, val, sizeof(g_config.eth_dns) - 1);
    if (get_form_param(body, "eth_dns2", val, sizeof(val))) strncpy(g_config.eth_dns2, val, sizeof(g_config.eth_dns2) - 1);

    Core::ConfigStore::saveConfigAuth();
    s_auth_reboot_required = true;

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Konfigurasi disimpan ke NVS. Reboot diperlukan untuk mengaktifkan perubahan.", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_network_discard_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    Network::NetworkManager::rollbackPendingNetworkConfig();
    s_auth_reboot_required = false;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", "Pengaturan jaringan tertunda berhasil dibatalkan.");
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_config_settings_handler(httpd_req_t *req) {
    if (Session::validate(req) != UserRole::ADMIN) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Akses ditolak: Hanya Administrator.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char body[1024] = {0};
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    if (r <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Bad Request", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    body[r] = '\0';

    char val[64];
    if (get_form_param(body, "filter_uuid_en", val, sizeof(val))) g_config.filter_uuid_en = atoi(val);
    if (get_form_param(body, "filter_uuid", val, sizeof(val))) strncpy(g_config.filter_uuid, val, sizeof(g_config.filter_uuid) - 1);
    if (get_form_param(body, "filter_major_en", val, sizeof(val))) g_config.filter_major_en = atoi(val);
    if (get_form_param(body, "filter_major", val, sizeof(val))) {
        long v = strtol(val, nullptr, 10);
        if (v > 65535) v = 65535;
        if (v < 0) v = 0;
        if (strlen(val) > 0) {
            snprintf(g_config.filter_major, sizeof(g_config.filter_major), "%ld", v);
        } else {
            g_config.filter_major[0] = '\0';
        }
    }
    if (get_form_param(body, "rssi_min", val, sizeof(val))) g_config.rssi_min = atoi(val);
    if (get_form_param(body, "batch_max", val, sizeof(val))) g_config.batch_max = atoi(val);
    if (get_form_param(body, "batch_flush", val, sizeof(val))) g_config.batch_flush = atoi(val);
    if (get_form_param(body, "data_ttl", val, sizeof(val))) g_config.data_ttl = atoi(val);
    if (get_form_param(body, "restart_schedule", val, sizeof(val))) strncpy(g_config.restart_schedule, val, sizeof(g_config.restart_schedule) - 1);
    if (get_form_param(body, "discon_rst", val, sizeof(val))) g_config.discon_rst = atoi(val);

    Core::ConfigStore::saveConfigSettings();
    Network::MqttService::updateTopicNames();

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Pengaturan gateway berhasil diperbarui.", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: System Telemetry & Control REST APIs
// -------------------------------------------------------------
static esp_err_t api_system_version_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_model", g_config.device_model[0] != '\0' ? g_config.device_model : "EMPTY");
    cJSON_AddStringToObject(root, "device_version", g_config.device_version);
    cJSON_AddStringToObject(root, "firmware_version", FIRMWARE_VERSION);
    cJSON_AddStringToObject(root, "ui_version", Template::get_ui_version());
    cJSON_AddBoolToObject(root, "ap_active", Network::NetworkManager::isApActive());
    cJSON_AddNumberToObject(root, "ap_remain_sec", Network::NetworkManager::getApRemainingSeconds());

    char mod_id[32];
    Core::ConfigStore::getModuleId(mod_id, sizeof(mod_id));
    cJSON_AddStringToObject(root, "ap_ssid", mod_id);

    cJSON_AddBoolToObject(root, "mqtt_connected", Network::MqttService::isConnected());
    cJSON_AddBoolToObject(root, "net_connected", Network::NetworkManager::isConnected());

    const char *net_mode = (g_config.connection_mode == 1) ? "Ethernet" : "Wi-Fi";
    cJSON_AddStringToObject(root, "net_mode", net_mode);
    cJSON_AddStringToObject(root, "active_mode", net_mode);
    cJSON_AddStringToObject(root, "staged_mode", net_mode);
    cJSON_AddBoolToObject(root, "reboot_required", s_auth_reboot_required);

    char ip_str[32];
    Network::NetworkManager::getActiveIp(ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(root, "ip_address", ip_str);

    cJSON_AddNumberToObject(root, "mqtt_published", Core::BeaconPipeline::getTotalMqttPublished());
    cJSON_AddNumberToObject(root, "mqtt_failed", Core::BeaconPipeline::getTotalMqttFailed());
    cJSON_AddStringToObject(root, "mqtt_last_error", Network::MqttService::getLastError());

    if (g_config.connection_mode == 1) {
        auto *drv = Network::NetworkManager::getEthernetDriver();
        if (drv) {
            cJSON_AddBoolToObject(root, "eth_link_up", drv->isConnected());
            cJSON_AddNumberToObject(root, "eth_speed", drv->linkSpeed());
            cJSON_AddStringToObject(root, "eth_duplex", drv->isFullDuplex() ? "Full" : "Half");
        }
    }

    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_stop_ap_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"Mode Hotspot AP berhasil dinonaktifkan.\"}", HTTPD_RESP_USE_STRLEN);
    Network::NetworkManager::scheduleStopConfigAP(1000);
    return ESP_OK;
}

static esp_err_t api_reboot_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Perangkat sedang me-reboot...", HTTPD_RESP_USE_STRLEN);

    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        vTaskDelete(NULL);
    }, "reboot_task", 2048, NULL, 5, NULL);

    return ESP_OK;
}

static esp_err_t api_reset_handler(httpd_req_t *req) {
    if (Session::validate(req) != UserRole::ADMIN) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Akses ditolak: Hanya Administrator.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    Core::ConfigStore::resetConfigToFactory();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Konfigurasi NVS berhasil di-reset ke default. Sistem me-reboot...", HTTPD_RESP_USE_STRLEN);

    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
        vTaskDelete(NULL);
    }, "reset_task", 2048, NULL, 5, NULL);

    return ESP_OK;
}

static esp_err_t api_debug_nrf_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char query[32];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "enable", val, sizeof(val)) == ESP_OK) {
            bool en = (strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
            ESP_LOGI(TAG, "Debug NRF Serial set to %d", en);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"debug_nrf_serial\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: Wi-Fi Scanner REST APIs
// -------------------------------------------------------------
static bool s_scan_in_progress = false;

static esp_err_t api_wifi_scan_stop_handler(httpd_req_t *req) {
    if (s_scan_in_progress) {
        esp_wifi_scan_stop();
        s_scan_in_progress = false;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"stopped\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_wifi_scan_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 120;
    scan_cfg.scan_time.active.max = 150;

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err == ESP_OK) {
        s_scan_in_progress = true;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"scanning\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (ap_count > 30) ap_count = 30;
    wifi_ap_record_t records[30];
    esp_wifi_scan_get_ap_records(&ap_count, records);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "complete");
    cJSON *nets = cJSON_CreateArray();

    for (uint16_t i = 0; i < ap_count; i++) {
        if (records[i].ssid[0] == '\0') continue;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", reinterpret_cast<const char *>(records[i].ssid));
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(nets, item);
    }

    cJSON_AddItemToObject(root, "networks", nets);
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    cJSON_free(out);
    cJSON_Delete(root);

    s_scan_in_progress = false;
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: Dual OTA Stream Upload
// -------------------------------------------------------------
static esp_err_t api_ota_upload_handler(httpd_req_t *req) {
    if (Session::validate(req) != UserRole::ADMIN) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Akses ditolak: Hanya Administrator.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (Ota::is_in_progress()) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_send(req, "Update firmware sedang berlangsung dari sesi lain (409 Conflict)!", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char query[64] = {0};
    Ota::TargetType target = Ota::TargetType::Firmware;
    size_t total_size = req->content_len;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char type_val[32];
        if (httpd_query_key_value(query, "type", type_val, sizeof(type_val)) == ESP_OK) {
            if (strcmp(type_val, "filesystem") == 0) {
                target = Ota::TargetType::Filesystem;
            }
        }
        char size_val[32];
        if (httpd_query_key_value(query, "size", size_val, sizeof(size_val)) == ESP_OK) {
            total_size = static_cast<size_t>(atoi(size_val));
        }
    }

    char err_buf[128] = {0};
    uint8_t chunk[2048];
    size_t received_bytes = 0;
    bool is_first_chunk = true;

    while (received_bytes < req->content_len) {
        size_t to_read = req->content_len - received_bytes;
        if (to_read > sizeof(chunk)) to_read = sizeof(chunk);

        int r = httpd_req_recv(req, reinterpret_cast<char *>(chunk), to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            Ota::abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Koneksi terputus saat stream upload.", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        if (is_first_chunk) {
            if (!Ota::begin(target, total_size, chunk, r, err_buf, sizeof(err_buf))) {
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, err_buf, HTTPD_RESP_USE_STRLEN);
                return ESP_OK;
            }
            is_first_chunk = false;
        }

        bool is_final = (received_bytes + r >= req->content_len);
        if (!Ota::write_chunk(chunk, r, received_bytes, is_final, err_buf, sizeof(err_buf))) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, err_buf, HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        received_bytes += r;

        // Broadcast progress over WebSocket
        int pct = (total_size > 0) ? static_cast<int>((received_bytes * 100) / total_size) : 0;
        char json[128];
        snprintf(json, sizeof(json), "{\"type\":\"ota_progress\",\"written\":%u,\"total\":%u,\"percent\":%d}",
                 static_cast<unsigned>(received_bytes), static_cast<unsigned>(total_size), pct);
        Server::broadcast_ws_text(json);
    }

    if (!Ota::finalize(err_buf, sizeof(err_buf))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, err_buf, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    Server::broadcast_ws_text("{\"type\":\"ota_progress\",\"percent\":100,\"status\":\"done\"}");
    httpd_resp_set_hdr(req, "Set-Cookie", "token=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Update Berhasil! Sistem me-reboot...", HTTPD_RESP_USE_STRLEN);

    xTaskCreate([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
        vTaskDelete(NULL);
    }, "ota_reboot", 2048, NULL, 5, NULL);

    return ESP_OK;
}

// -------------------------------------------------------------
// Helper for Safe URI Registration (Zero-initialized struct)
// -------------------------------------------------------------
static inline void register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *), bool is_ws = false) {
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = method;
    u.handler = handler;
    u.is_websocket = is_ws;
    httpd_register_uri_handler(server, &u);
}

// -------------------------------------------------------------
// Route Registration
// -------------------------------------------------------------
esp_err_t register_all(httpd_handle_t server) {
    if (!server) return ESP_ERR_INVALID_ARG;

    // Pages
    register_uri(server, "/", HTTP_GET, index_handler);
    register_uri(server, "/index.html", HTTP_GET, index_handler);
    register_uri(server, "/login", HTTP_GET, login_handler);
    register_uri(server, "/login.html", HTTP_GET, login_handler);
    register_uri(server, "/test", HTTP_GET, test_handler);
    register_uri(server, "/test.html", HTTP_GET, test_handler);
    register_uri(server, "/auth", HTTP_GET, auth_handler);
    register_uri(server, "/auth_admin.html", HTTP_GET, auth_admin_handler);
    register_uri(server, "/auth_teknisi.html", HTTP_GET, auth_teknisi_handler);
    register_uri(server, "/settings", HTTP_GET, settings_handler);
    register_uri(server, "/settings.html", HTTP_GET, settings_handler);
    register_uri(server, "/firmware", HTTP_GET, firmware_handler);
    register_uri(server, "/firmware.html", HTTP_GET, firmware_handler);

    // Authentication REST
    register_uri(server, "/api/v1/auth/login", HTTP_POST, api_login_handler);
    register_uri(server, "/api/v1/auth/logout", HTTP_POST, api_logout_handler);
    register_uri(server, "/api/v1/auth/check", HTTP_GET, api_auth_check_handler);

    // Config REST
    register_uri(server, "/api/v1/config/auth", HTTP_POST, api_config_auth_handler);
    register_uri(server, "/api/v1/config/network/discard", HTTP_POST, api_network_discard_handler);
    register_uri(server, "/api/v1/config/settings", HTTP_POST, api_config_settings_handler);

    // System REST
    register_uri(server, "/api/v1/system/version", HTTP_GET, api_system_version_handler);
    register_uri(server, "/api/v1/system/config-ap/stop", HTTP_POST, api_stop_ap_handler);
    register_uri(server, "/api/v1/system/reboot", HTTP_POST, api_reboot_handler);
    register_uri(server, "/api/v1/system/reset", HTTP_POST, api_reset_handler);
    register_uri(server, "/api/v1/debug/nrf", HTTP_GET, api_debug_nrf_handler);
    register_uri(server, "/api/v1/wifi/scan", HTTP_GET, api_wifi_scan_handler);
    register_uri(server, "/api/v1/wifi/scan-stop", HTTP_POST, api_wifi_scan_stop_handler);

    // OTA REST
    register_uri(server, "/api/v1/ota/upload", HTTP_POST, api_ota_upload_handler);

    // Static Assets Wildcard Handler
    register_uri(server, "/assets/*", HTTP_GET, assets_wildcard_handler);

    return ESP_OK;
}

} // namespace Iotaru::Web::Routes
