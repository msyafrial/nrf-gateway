#include "web_routes.hpp"
#include "web_server.hpp"
#include "session_mgr.hpp"
#include "web_template.hpp"
#include "ota_service.hpp"
#include "app_config.hpp"
#include "config_store.hpp"
#include "net_manager.hpp"
#include "mqtt_service.hpp"
#include "net_log_service.hpp"
#include "drop_log_service.hpp"
#include "IEthernetDriver.hpp"
#include "beacon_pipeline.hpp"
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <atomic>

static const char *TAG = "WebRoutes";

namespace Iotaru::Web::Routes {

static bool s_auth_reboot_required = false;

// -------------------------------------------------------------
// URL Decoding & Form Parameter Extraction Helpers
// -------------------------------------------------------------
// -------------------------------------------------------------
// URL Decoding & Form Parameter Extraction Helpers
// -------------------------------------------------------------
static void urldecode_s(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) { *dst = '\0'; return; }

    char a, b;
    size_t written = 0;
    while (*src && (written + 1 < dst_len)) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            dst[written++] = static_cast<char>(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            dst[written++] = ' ';
            src++;
        } else {
            dst[written++] = *src++;
        }
    }
    dst[written] = '\0';
}

static bool get_multipart_param(const char *body, const char *key, char *out_val, size_t max_len) {
    if (!body || !key || !out_val || max_len == 0) return false;
    out_val[0] = '\0';

    char search_pattern[80];
    snprintf(search_pattern, sizeof(search_pattern), "name=\"%s\"", key);
    const char *p = strstr(body, search_pattern);
    if (!p) {
        snprintf(search_pattern, sizeof(search_pattern), "name='%s'", key);
        p = strstr(body, search_pattern);
        if (!p) return false;
    }

    // Skip forward to header end: \r\n\r\n or \n\n
    const char *header_end = strstr(p, "\r\n\r\n");
    size_t header_skip = 4;
    if (!header_end) {
        header_end = strstr(p, "\n\n");
        header_skip = 2;
        if (!header_end) return false;
    }
    const char *val_start = header_end + header_skip;

    // Value continues until boundary marker (\r\n-- or \n--)
    const char *val_end = strstr(val_start, "\r\n--");
    if (!val_end) {
        val_end = strstr(val_start, "\n--");
    }
    if (!val_end) {
        val_end = val_start + strlen(val_start);
    }

    // Strip trailing \r or \n
    while (val_end > val_start && (*(val_end - 1) == '\r' || *(val_end - 1) == '\n')) {
        val_end--;
    }

    size_t len = static_cast<size_t>(val_end - val_start);
    if (len >= max_len) len = max_len - 1;
    memcpy(out_val, val_start, len);
    out_val[len] = '\0';
    return true;
}

static bool get_form_param(const char *body, const char *key, char *out_val, size_t max_len) {
    if (!body || !key || !out_val || max_len == 0) return false;
    out_val[0] = '\0';

    // 1. Try multipart/form-data first if boundary / field name syntax detected
    if (strstr(body, "name=\"") != nullptr || strstr(body, "name='") != nullptr) {
        if (get_multipart_param(body, key, out_val, max_len)) {
            return true;
        }
    }

    // 2. Try standard application/x-www-form-urlencoded
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
            urldecode_s(out_val, max_len, enc);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

static const char* get_content_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "text/plain";
    if (strcasecmp(ext, ".html") == 0) return "text/html";
    if (strcasecmp(ext, ".css") == 0) return "text/css";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript";
    if (strcasecmp(ext, ".json") == 0) return "application/json";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    return "text/plain";
}

// -------------------------------------------------------------
// Defense-in-Depth HTTP Security Headers (VULN-SEC-03)
// -------------------------------------------------------------
static inline void set_security_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self' 'unsafe-inline' data: ws:;");
}

// -------------------------------------------------------------
// Static File Streamer (With automatic GZIP detection)
// -------------------------------------------------------------
static esp_err_t stream_static_file(httpd_req_t *req, const char *base_path) {
    struct FileReadGuard {
        bool acquired;
        FileReadGuard() : acquired(Server::acquire_file_read()) {}
        ~FileReadGuard() {
            if (acquired) Server::release_file_read();
        }
    } file_guard;

    if (!file_guard.acquired) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, "Sistem sedang dalam proses pemeliharaan/update.", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

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
        f = fopen(gz_path, "rb");
        is_gzip = true;
    } else if (stat(base_path, &st) == 0) {
        f = fopen(base_path, "rb");
        is_gzip = false;
    }

    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    set_security_headers(req);
    httpd_resp_set_type(req, get_content_type(base_path));
    if (is_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    // Cache-Control: Aset statis (CSS, JS, font, gambar) disimpan di cache browser (1 hari)
    // Berkas dinamis (HTML/JSON) tetap no-cache agar perubahan instan terbaca
    if (strstr(base_path, "/assets/") != nullptr || strstr(base_path, ".css") != nullptr ||
        strstr(base_path, ".js") != nullptr || strstr(base_path, ".ico") != nullptr ||
        strstr(base_path, ".png") != nullptr || strstr(base_path, ".svg") != nullptr) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400, immutable");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    }

    char chunk[2048];
    size_t chunk_count = 0;
    while (true) {
        size_t r = fread(chunk, 1, sizeof(chunk), f);
        if (r == 0) break;
        if (httpd_resp_send_chunk(req, chunk, r) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
        chunk_count++;
        // Beri kesempatan bagi task lain (Cooperative Yield 1 tick tiap 8 KB)
        if ((chunk_count % 4) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
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

static esp_err_t version_json_handler(httpd_req_t *req) {
    return stream_static_file(req, "/storage/version.json");
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

static esp_err_t log_handler(httpd_req_t *req) {
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
    return Template::stream_template_file(req, "/storage/log.html");
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
    if (strstr(req->uri, "..") != nullptr || strstr(req->uri, "//") != nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }
    char filepath[600];
    snprintf(filepath, sizeof(filepath), "/storage%s", req->uri);
    char *q = strchr(filepath, '?');
    if (q) *q = '\0';
    return stream_static_file(req, filepath);
}

// -------------------------------------------------------------
// Route Handlers: Authentication REST APIs
// -------------------------------------------------------------
static esp_err_t api_login_handler(httpd_req_t *req) {
    set_security_headers(req);
    if (Session::is_rate_limited(req)) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Terlalu banyak percobaan gagal. Silakan tunggu 60 detik.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char body[512] = {0};
    if (req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Payload too large");
        return ESP_FAIL;
    }
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

    ESP_LOGI(TAG, "Login request received (%d bytes), user='%s'", r, u);

    UserRole role = UserRole::NONE;
    char token[Session::TOKEN_LEN + 1] = {0};

    Session::AuthResult auth_res = Session::authenticate(req, u, p, role, token, sizeof(token));
    if (auth_res == Session::AuthResult::SUCCESS) {
        char cookie_hdr[192];
        snprintf(cookie_hdr, sizeof(cookie_hdr), "token=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=3600", token);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
        httpd_resp_set_type(req, "application/json");

        char resp[128];
        snprintf(resp, sizeof(resp), "{\"status\":\"success\",\"role\":\"%s\"}",
                 role == UserRole::ADMIN ? "admin" : "teknisi");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Login BERHASIL untuk user '%s' (role: %s)", u, role == UserRole::ADMIN ? "admin" : "teknisi");
    } else if (auth_res == Session::AuthResult::SESSION_LIMIT_REACHED) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"error\",\"code\":\"session_limit\",\"message\":\"Batas maksimal 2 perangkat aktif tercapai. Silakan logout terlebih dahulu di salah satu perangkat yang sedang login.\"}", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "Login DITOLAK: Batas 2 perangkat aktif tercapai untuk user '%s'", u);
    } else if (auth_res == Session::AuthResult::RATE_LIMITED) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Terlalu banyak percobaan gagal. Silakan tunggu 60 detik.", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Username atau password salah.", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "Login GAGAL untuk user '%s'", u);
    }
    return ESP_OK;
}

static esp_err_t api_logout_handler(httpd_req_t *req) {
    char token[Session::TOKEN_LEN + 1] = {0};
    Session::validate(req, token, sizeof(token));
    if (token[0] != '\0') {
        Session::invalidate(token);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "token=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"success\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_auth_check_handler(httpd_req_t *req) {
    char token[Session::TOKEN_LEN + 1] = {0};
    UserRole role = Session::validate(req, token, sizeof(token));
    httpd_resp_set_type(req, "application/json");
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"authenticated\":false}", HTTPD_RESP_USE_STRLEN);
    } else {
        if (token[0] != '\0') {
            char cookie_hdr[192];
            snprintf(cookie_hdr, sizeof(cookie_hdr), "token=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=3600", token);
            httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
        }
        httpd_resp_send(req, "{\"authenticated\":true}", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: Configuration & Network REST APIs
// -------------------------------------------------------------
// Helper untuk validasi password tidak masked dan tidak kosong
static inline bool is_masked_or_empty(const char* val) {
    if (!val || val[0] == '\0') return true;
    if (strcmp(val, "••••••••") == 0) return true;
    if (strcmp(val, "********") == 0) return true;
    return false;
}

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
        if (get_form_param(body, "admin_user", val, sizeof(val))) {
            if (strlen(val) >= 4 && strchr(val, ':') == nullptr) {
                strlcpy(g_config.admin_user, val, sizeof(g_config.admin_user));
            }
        }
        if (get_form_param(body, "admin_pass", val, sizeof(val))) {
            if (!is_masked_or_empty(val)) {
                if (strlen(val) < 8 || strchr(val, ':') != nullptr) {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Password Admin minimal 8 karakter dan tidak boleh mengandung karakter ':'", HTTPD_RESP_USE_STRLEN);
                    return ESP_OK;
                }
                strlcpy(g_config.admin_pass, val, sizeof(g_config.admin_pass));
            }
        }
        if (get_form_param(body, "mqtt_host", val, sizeof(val))) strlcpy(g_config.mqtt_host, val, sizeof(g_config.mqtt_host));
        if (get_form_param(body, "mqtt_port", val, sizeof(val))) g_config.mqtt_port = atoi(val);
        if (get_form_param(body, "mqtt_user", val, sizeof(val))) strlcpy(g_config.mqtt_user, val, sizeof(g_config.mqtt_user));
        if (get_form_param(body, "mqtt_use_ssl", val, sizeof(val))) g_config.mqtt_use_ssl = atoi(val);
        if (get_form_param(body, "mqtt_password", val, sizeof(val))) {
            if (!is_masked_or_empty(val)) {
                strlcpy(g_config.mqtt_password, val, sizeof(g_config.mqtt_password));
            }
        }
        if (get_form_param(body, "ap_password", val, sizeof(val))) {
            if (!is_masked_or_empty(val)) {
                if (strlen(val) >= 8) {
                    strlcpy(g_config.ap_password, val, sizeof(g_config.ap_password));
                }
            }
        }
        if (get_form_param(body, "user_user", val, sizeof(val))) strlcpy(g_config.user_user, val, sizeof(g_config.user_user));
        if (get_form_param(body, "user_pass", val, sizeof(val))) {
            if (!is_masked_or_empty(val)) {
                if (strlen(val) < 8 || strchr(val, ':') != nullptr) {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Password Teknisi minimal 8 karakter dan tidak boleh mengandung karakter ':'", HTTPD_RESP_USE_STRLEN);
                    return ESP_OK;
                }
                strlcpy(g_config.user_pass, val, sizeof(g_config.user_pass));
            }
        }
    } else {
        // Role TEKNISI: hanya boleh mengubah user_pass diri sendiri
        char val[64];
        if (get_form_param(body, "user_pass", val, sizeof(val))) {
            if (!is_masked_or_empty(val)) {
                if (strlen(val) < 8 || strchr(val, ':') != nullptr) {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Password Teknisi minimal 8 karakter dan tidak boleh mengandung karakter ':'", HTTPD_RESP_USE_STRLEN);
                    return ESP_OK;
                }
                strlcpy(g_config.user_pass, val, sizeof(g_config.user_pass));
            }
        }
    }

    char val[64];
    if (get_form_param(body, "connection_mode", val, sizeof(val))) {
        g_config.connection_mode = atoi(val);
    }
    if (get_form_param(body, "w_ssid", val, sizeof(val))) strlcpy(g_config.w_ssid, val, sizeof(g_config.w_ssid));
    if (get_form_param(body, "w_password", val, sizeof(val))) {
        if (!is_masked_or_empty(val)) {
            strlcpy(g_config.w_password, val, sizeof(g_config.w_password));
        }
    }
    if (get_form_param(body, "w_dhcp", val, sizeof(val))) g_config.w_dhcp = atoi(val);
    if (get_form_param(body, "w_ip", val, sizeof(val))) strlcpy(g_config.w_ip, val, sizeof(g_config.w_ip));
    if (get_form_param(body, "w_gateway", val, sizeof(val))) strlcpy(g_config.w_gateway, val, sizeof(g_config.w_gateway));
    if (get_form_param(body, "w_subnet", val, sizeof(val))) strlcpy(g_config.w_subnet, val, sizeof(g_config.w_subnet));
    if (get_form_param(body, "w_dns", val, sizeof(val))) strlcpy(g_config.w_dns, val, sizeof(g_config.w_dns));
    if (get_form_param(body, "w_dns2", val, sizeof(val))) strlcpy(g_config.w_dns2, val, sizeof(g_config.w_dns2));

    if (get_form_param(body, "eth_dhcp", val, sizeof(val))) g_config.eth_dhcp = atoi(val);
    if (get_form_param(body, "eth_ip", val, sizeof(val))) strlcpy(g_config.eth_ip, val, sizeof(g_config.eth_ip));
    if (get_form_param(body, "eth_gateway", val, sizeof(val))) strlcpy(g_config.eth_gateway, val, sizeof(g_config.eth_gateway));
    if (get_form_param(body, "eth_subnet", val, sizeof(val))) strlcpy(g_config.eth_subnet, val, sizeof(g_config.eth_subnet));
    if (get_form_param(body, "eth_dns", val, sizeof(val))) strlcpy(g_config.eth_dns, val, sizeof(g_config.eth_dns));
    if (get_form_param(body, "eth_dns2", val, sizeof(val))) strlcpy(g_config.eth_dns2, val, sizeof(g_config.eth_dns2));

    Core::ConfigStore::saveConfigAuth();
    s_auth_reboot_required = true;

    char detAuth[80];
    snprintf(detAuth, sizeof(detAuth), "Kredensial Akun Diperbarui oleh %s",
             role == UserRole::ADMIN ? "Admin" : "Teknisi");
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::AUTH_UPDATE, detAuth);

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

    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::CONFIG_DISCARD,
                                "Perubahan Konfigurasi Jaringan Dibatalkan oleh Pengguna");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", "Pengaturan jaringan tertunda berhasil dibatalkan.");
    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        cJSON_free(out);
    } else {
        httpd_resp_send_500(req);
    }
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
    if (get_form_param(body, "filter_uuid", val, sizeof(val))) strlcpy(g_config.filter_uuid, val, sizeof(g_config.filter_uuid));
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
    if (get_form_param(body, "restart_schedule", val, sizeof(val))) strlcpy(g_config.restart_schedule, val, sizeof(g_config.restart_schedule));
    if (get_form_param(body, "discon_rst", val, sizeof(val))) g_config.discon_rst = atoi(val);

    Core::ConfigStore::saveConfigSettings();
    Network::MqttService::updateTopicNames();

    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::CONFIG_UPDATE,
                                "Pengaturan Sistem Disimpan oleh Admin");

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

    uint64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    cJSON_AddNumberToObject(root, "uptime_sec", (double)uptime_sec);
    cJSON_AddNumberToObject(root, "free_heap_kb", (double)(esp_get_free_heap_size() / 1024));
    cJSON_AddNumberToObject(root, "net_bandwidth", Network::MqttService::getBandwidthKBps());

    cJSON_AddBoolToObject(root, "mqtt_connected", Network::MqttService::isConnected());
    cJSON_AddStringToObject(root, "mqtt_host", g_config.mqtt_host[0] != '\0' ? g_config.mqtt_host : "—");
    cJSON_AddNumberToObject(root, "mqtt_port", g_config.mqtt_port);
    cJSON_AddBoolToObject(root, "mqtt_ssl", g_config.mqtt_use_ssl == 1);
    cJSON_AddBoolToObject(root, "net_connected", Network::NetworkManager::isConnected());

    const char *net_mode = (g_config.connection_mode == 1) ? "Ethernet" : "Wi-Fi";
    cJSON_AddStringToObject(root, "net_mode", net_mode);
    cJSON_AddStringToObject(root, "active_mode", net_mode);
    cJSON_AddStringToObject(root, "staged_mode", net_mode);
    cJSON_AddBoolToObject(root, "reboot_required", s_auth_reboot_required);

    char ip_str[32];
    Network::NetworkManager::getActiveIp(ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(root, "ip_address", ip_str);

    char mac_str[32];
    Network::NetworkManager::getActiveMac(mac_str, sizeof(mac_str));
    cJSON_AddStringToObject(root, "mac_address", mac_str);

    char wifi_ip[32];
    Network::NetworkManager::getWifiIp(wifi_ip, sizeof(wifi_ip));
    cJSON_AddStringToObject(root, "wifi_ip", wifi_ip);

    char wifi_mac[32];
    Network::NetworkManager::getWifiMac(wifi_mac, sizeof(wifi_mac));
    cJSON_AddStringToObject(root, "wifi_mac", wifi_mac);

    bool eth_hw = Network::NetworkManager::isEthernetHardwareDetected();
    cJSON_AddBoolToObject(root, "eth_hw_detected", eth_hw);

    char eth_ip[32];
    Network::NetworkManager::getEthernetIp(eth_ip, sizeof(eth_ip));
    cJSON_AddStringToObject(root, "eth_ip", eth_ip);

    char eth_mac[32];
    Network::NetworkManager::getEthernetMac(eth_mac, sizeof(eth_mac));
    cJSON_AddStringToObject(root, "eth_mac", eth_mac);

    if (g_config.connection_mode == 0 && Network::NetworkManager::isConnected()) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            cJSON_AddNumberToObject(root, "wifi_rssi", ap_info.rssi);
        }
    }

    cJSON_AddNumberToObject(root, "mqtt_published", Core::BeaconPipeline::getTotalMqttPublished());
    cJSON_AddNumberToObject(root, "mqtt_failed", Core::BeaconPipeline::getTotalMqttFailed());
    cJSON_AddStringToObject(root, "mqtt_last_error", Network::MqttService::getLastError());

    auto *drv = Network::NetworkManager::getEthernetDriver();
    if (drv) {
        cJSON_AddBoolToObject(root, "eth_link_up", drv->isConnected());
        cJSON_AddNumberToObject(root, "eth_speed", drv->linkSpeed());
        cJSON_AddStringToObject(root, "eth_duplex", drv->isFullDuplex() ? "Full" : "Half");
        cJSON_AddBoolToObject(root, "eth_direct_mode", Network::NetworkManager::isEthernetDirectPcMode());
        cJSON_AddBoolToObject(root, "eth_slave_mode", Network::NetworkManager::isEthernetPeerSlaveMode());
    }

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        cJSON_free(out);
    } else {
        httpd_resp_send_500(req);
    }
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

static esp_err_t api_touch_ap_handler(httpd_req_t *req) {
    if (Network::NetworkManager::isApActive()) {
        Network::NetworkManager::recordApActivity();
    }

    char token[Session::TOKEN_LEN + 1] = {0};
    UserRole role = Session::validate(req, token, sizeof(token));
    if (role != UserRole::NONE && token[0] != '\0') {
        char cookie_hdr[192];
        snprintf(cookie_hdr, sizeof(cookie_hdr), "token=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=3600", token);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddBoolToObject(root, "ap_active", Network::NetworkManager::isApActive());
    cJSON_AddNumberToObject(root, "ap_remain_sec", Network::NetworkManager::getApRemainingSeconds());

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        cJSON_free(out);
    } else {
        httpd_resp_send_500(req);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: Network Event Logs REST APIs
// -------------------------------------------------------------
static esp_err_t api_net_logs_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();

    // 1. Status Realtime Interfaces
    char activeIp[16] = {0};
    Network::NetworkManager::getActiveIp(activeIp, sizeof(activeIp));

    bool netConn = Network::NetworkManager::isConnected();
    bool ethActive = Network::NetworkManager::isEthernetActive();
    bool ethHw = Network::NetworkManager::isEthernetHardwareDetected();
    bool ethLink = Network::NetworkManager::isEthernetLinkUp();
    bool wifiConn = netConn && !ethActive;
    bool ethConn = netConn && ethActive;
    bool mqttConn = Network::MqttService::isConnected();

    cJSON_AddStringToObject(root, "wifi_status", wifiConn ? "connected" : "disconnected");
    cJSON_AddStringToObject(root, "wifi_ssid", g_config.w_ssid[0] ? g_config.w_ssid : "-");
    cJSON_AddStringToObject(root, "wifi_ip", wifiConn ? activeIp : "-");
    cJSON_AddNumberToObject(root, "wifi_disconnects", Network::NetLogService::getWifiDisconnectCount());

    const char* ethStatusStr = "link_down";
    if (!ethHw) {
        ethStatusStr = "uninstalled";
    } else if (ethConn) {
        ethStatusStr = "connected";
    } else if (ethLink) {
        ethStatusStr = "link_up";
    }

    cJSON_AddStringToObject(root, "eth_status", ethStatusStr);
    cJSON_AddStringToObject(root, "eth_ip", ethConn ? activeIp : "-");
    cJSON_AddNumberToObject(root, "eth_disconnects", Network::NetLogService::getEthDisconnectCount());

    cJSON_AddStringToObject(root, "mqtt_status", mqttConn ? "online" : "offline");
    cJSON_AddStringToObject(root, "mqtt_host", g_config.mqtt_host[0] ? g_config.mqtt_host : "-");
    cJSON_AddNumberToObject(root, "mqtt_port", g_config.mqtt_port);
    cJSON_AddBoolToObject(root, "mqtt_ssl", g_config.mqtt_use_ssl == 1);
    cJSON_AddNumberToObject(root, "mqtt_disconnects", Network::NetLogService::getMqttDisconnectCount());

    // Telemetri Stabilitas ESP32 & Reboot
    cJSON_AddNumberToObject(root, "boot_count", Network::NetLogService::getBootCount());
    cJSON_AddStringToObject(root, "reset_reason", Network::NetLogService::getLastResetReasonString());

    // Metadata Media Penyimpanan Log & Proteksi Flash
    cJSON_AddStringToObject(root, "storage_mode", Network::NetLogService::getStorageModeString());
    cJSON_AddBoolToObject(root, "eeprom_detected", Network::NetLogService::isEepromDetected());
    cJSON_AddBoolToObject(root, "flash_protected", true);

    // 2. Data Log (Maksimal 50)
    static Network::NetLogEntry s_tempLogs[Network::MAX_NET_LOGS];
    size_t count = Network::NetLogService::getLogs(s_tempLogs, Network::MAX_NET_LOGS);
    cJSON_AddNumberToObject(root, "total", count);

    cJSON *logsArr = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", s_tempLogs[i].id);
        cJSON_AddNumberToObject(item, "uptime_sec", s_tempLogs[i].uptime_sec);
        cJSON_AddStringToObject(item, "time", s_tempLogs[i].time_str);
        cJSON_AddStringToObject(item, "cat", Network::NetLogService::categoryToString(s_tempLogs[i].category));
        cJSON_AddStringToObject(item, "event", Network::NetLogService::eventToString(s_tempLogs[i].event));
        cJSON_AddStringToObject(item, "status_class", Network::NetLogService::eventToStatusClass(s_tempLogs[i].event));
        cJSON_AddStringToObject(item, "detail", s_tempLogs[i].detail);
        cJSON_AddItemToArray(logsArr, item);
    }
    cJSON_AddItemToObject(root, "logs", logsArr);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        cJSON_free(out);
    } else {
        httpd_resp_send_500(req);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_net_logs_clear_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    Network::NetLogService::clearLogs();
    char detClr[80];
    snprintf(detClr, sizeof(detClr), "Seluruh Riwayat Log Sistem Dibersihkan oleh %s",
             role == UserRole::ADMIN ? "Admin" : "Teknisi");
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::LOG_CLEARED, detClr);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"Riwayat net log berhasil dihapus.\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_net_logs_export_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"net_log_sg10.csv\"");

    // Header CSV
    const char *csv_head = "No,Waktu,Interface,Event,Detail\r\n";
    httpd_resp_send_chunk(req, csv_head, strlen(csv_head));

    static Network::NetLogEntry s_tempLogs[Network::MAX_NET_LOGS];
    size_t count = Network::NetLogService::getLogs(s_tempLogs, Network::MAX_NET_LOGS);

    char lineBuf[256];
    for (size_t i = 0; i < count; i++) {
        snprintf(lineBuf, sizeof(lineBuf), "%u,%s,%s,%s,\"%s\"\r\n",
                 (unsigned int)s_tempLogs[i].id,
                 s_tempLogs[i].time_str,
                 Network::NetLogService::categoryToString(s_tempLogs[i].category),
                 Network::NetLogService::eventToString(s_tempLogs[i].event),
                 s_tempLogs[i].detail);
        httpd_resp_send_chunk(req, lineBuf, strlen(lineBuf));
    }

    httpd_resp_send_chunk(req, nullptr, 0); // Selesai
    return ESP_OK;
}

static esp_err_t api_drop_logs_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // 1. Ringkasan Statistik Drop
    Core::DropSummaryStats stats = Core::DropLogService::getSummaryStats();
    cJSON *sumObj = cJSON_CreateObject();
    cJSON_AddNumberToObject(sumObj, "total_dropped", stats.total_dropped);
    cJSON_AddNumberToObject(sumObj, "ttl_expired", stats.ttl_expired);
    cJSON_AddNumberToObject(sumObj, "rssi_min", stats.rssi_min);
    cJSON_AddNumberToObject(sumObj, "uuid_mismatch", stats.uuid_mismatch);
    cJSON_AddNumberToObject(sumObj, "major_mismatch", stats.major_mismatch);
    cJSON_AddNumberToObject(sumObj, "queue_overflow", stats.queue_overflow);
    cJSON_AddNumberToObject(sumObj, "mqtt_failed", stats.mqtt_failed);
    cJSON_AddItemToObject(root, "summary", sumObj);

    // 2. Data Log Drop (Maksimal 50)
    static Core::DropLogEntry s_tempDropLogs[Core::MAX_DROP_LOGS];
    size_t count = Core::DropLogService::getLogs(s_tempDropLogs, Core::MAX_DROP_LOGS);
    cJSON_AddNumberToObject(root, "total", count);

    cJSON *logsArr = cJSON_CreateArray();
    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", s_tempDropLogs[i].id);
        cJSON_AddNumberToObject(item, "uptime_sec", s_tempDropLogs[i].uptime_sec);
        cJSON_AddStringToObject(item, "time", s_tempDropLogs[i].time_str);
        cJSON_AddStringToObject(item, "code", s_tempDropLogs[i].code);
        cJSON_AddStringToObject(item, "tag", s_tempDropLogs[i].tag);
        cJSON_AddNumberToObject(item, "rssi", s_tempDropLogs[i].rssi);
        cJSON_AddNumberToObject(item, "reason", static_cast<uint8_t>(s_tempDropLogs[i].reason));
        cJSON_AddStringToObject(item, "reason_str", Core::DropLogService::reasonToString(s_tempDropLogs[i].reason));
        cJSON_AddStringToObject(item, "status_class", Core::DropLogService::reasonToStatusClass(s_tempDropLogs[i].reason));
        cJSON_AddItemToArray(logsArr, item);
    }
    cJSON_AddItemToObject(root, "logs", logsArr);

    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        cJSON_free(out);
    } else {
        httpd_resp_send_500(req);
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t api_drop_logs_clear_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    Core::DropLogService::clearLogs();
    Core::BeaconPipeline::resetDropCounters();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"message\":\"Riwayat log drop data berhasil dihapus.\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_drop_logs_export_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"drop_log_sg10.csv\"");

    // Header CSV (6 kolom presisi tanpa keterangan)
    const char *csv_head = "No,Waktu,Tipe,Major-Minor/Tag,RSSI(dBm),Alasan\r\n";
    httpd_resp_send_chunk(req, csv_head, strlen(csv_head));

    static Core::DropLogEntry s_tempDropLogs[Core::MAX_DROP_LOGS];
    size_t count = Core::DropLogService::getLogs(s_tempDropLogs, Core::MAX_DROP_LOGS);

    char lineBuf[256];
    for (size_t i = 0; i < count; i++) {
        snprintf(lineBuf, sizeof(lineBuf), "%u,%s,%s,%s,%d,\"%s\"\r\n",
                 (unsigned int)s_tempDropLogs[i].id,
                 s_tempDropLogs[i].time_str,
                 s_tempDropLogs[i].code,
                 s_tempDropLogs[i].tag,
                 (int)s_tempDropLogs[i].rssi,
                 Core::DropLogService::reasonToString(s_tempDropLogs[i].reason));
        httpd_resp_send_chunk(req, lineBuf, strlen(lineBuf));
    }

    httpd_resp_send_chunk(req, nullptr, 0); // Selesai
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

    xTaskCreatePinnedToCore([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        vTaskDelete(NULL);
    }, "reboot_task", 2048, NULL, 5, NULL, 1);

    return ESP_OK;
}

static esp_err_t api_reset_handler(httpd_req_t *req) {
    if (Session::validate(req) != UserRole::ADMIN) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Akses ditolak: Hanya Administrator.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    Core::ConfigStore::resetConfigToFactory();
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::CONFIG_RESET,
                                "Konfigurasi Sistem Direset ke Standar Pabrik (Web Admin)");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Konfigurasi NVS berhasil di-reset ke default. Sistem me-reboot...", HTTPD_RESP_USE_STRLEN);

    xTaskCreatePinnedToCore([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
        vTaskDelete(NULL);
    }, "reset_task", 2048, NULL, 5, NULL, 1);

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
static std::atomic<bool> s_scan_in_progress{false};
static std::atomic<uint32_t> s_scan_start_millis{0};

static esp_err_t api_wifi_scan_stop_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (s_scan_in_progress.load()) {
        esp_wifi_scan_stop();
        s_scan_in_progress.store(false);
        Network::NetworkManager::resetWifiScanDone();
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

    // 1. Pastikan driver Wi-Fi diinisialisasi secara on-demand
    Network::NetworkManager::ensureWifiDriverInitialized();

    // 2. Pastikan mode Wi-Fi mendukung pemindaian saluran (STA atau APSTA)
    wifi_mode_t curMode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&curMode);
    if (curMode == WIFI_MODE_AP) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    } else if (curMode == WIFI_MODE_NULL) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    } else {
        // Mode STA atau APSTA, pastikan radio Wi-Fi aktif
        esp_wifi_start();
    }

    // 3. Cek parameter refresh dari query string
    char query[64] = {0};
    bool forceRefresh = false;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[16] = {0};
        if (httpd_query_key_value(query, "refresh", param, sizeof(param)) == ESP_OK) {
            forceRefresh = (strcmp(param, "1") == 0);
        }
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    // Watchdog: jika scan stuck lebih dari 8 detik, batalkan dan mulai baru
    if (s_scan_in_progress.load() && (now - s_scan_start_millis.load() > 8000)) {
        ESP_LOGW(TAG, "Wi-Fi scan timeout (>8 detik), mereset state scanner.");
        esp_wifi_scan_stop();
        s_scan_in_progress.store(false);
        Network::NetworkManager::resetWifiScanDone();
    }

    if (forceRefresh && s_scan_in_progress.load()) {
        esp_wifi_scan_stop();
        s_scan_in_progress.store(false);
        Network::NetworkManager::resetWifiScanDone();
    }

    // 4. Jika belum ada scan yang berjalan, mulai scan baru
    if (!s_scan_in_progress.load()) {
        // Pastikan Wi-Fi Station tidak dalam status connecting jika mode Ethernet aktif
        if (Network::NetworkManager::isEthernetActive() || 
            Network::NetworkManager::getActiveConnectionMode() != ConnectionMode::WIFI) {
            esp_wifi_disconnect();
        }

        Network::NetworkManager::resetWifiScanDone();

        wifi_scan_config_t scan_cfg = {};
        scan_cfg.show_hidden = false;
        scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scan_cfg.scan_time.active.min = 120;
        scan_cfg.scan_time.active.max = 160;

        esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
        if (err == ESP_ERR_WIFI_STATE) {
            ESP_LOGW(TAG, "Wi-Fi busy (0x3008), memaksa disconnect dan retry scan...");
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(50));
            err = esp_wifi_scan_start(&scan_cfg, false);
        }

        if (err == ESP_OK) {
            s_scan_in_progress.store(true);
            s_scan_start_millis.store(now);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"scanning\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Gagal memulai scan Wi-Fi: 0x%x (%s)", err, esp_err_to_name(err));
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Gagal memulai pemindaian Wi-Fi\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    }

    // 5. Cek apakah seluruh channel telah selesai dipindai melalui event WIFI_EVENT_SCAN_DONE
    if (!Network::NetworkManager::isWifiScanDone()) {
        // Pemindaian masih berlangsung di background ESP-IDF melintasi channel 1 s.d. 13
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"scanning\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Pemindaian seluruh channel 1-13 telah selesai 100%!
    s_scan_in_progress.store(false);
    Network::NetworkManager::resetWifiScanDone();

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count > 40) ap_count = 40;
    wifi_ap_record_t records[40];
    esp_wifi_scan_get_ap_records(&ap_count, records);

    // Urutkan berdasarkan kekuatan sinyal RSSI (terkuat di paling atas)
    for (int i = 0; i < (int)ap_count - 1; i++) {
        for (int j = i + 1; j < (int)ap_count; j++) {
            if (records[j].rssi > records[i].rssi) {
                wifi_ap_record_t tmp = records[i];
                records[i] = records[j];
                records[j] = tmp;
            }
        }
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "complete");
    cJSON *nets = cJSON_CreateArray();

    // Deduplikasi SSID (pertahankan sinyal terkuat untuk SSID yang sama)
    for (uint16_t i = 0; i < ap_count; i++) {
        if (records[i].ssid[0] == '\0') continue;

        bool duplicate = false;
        for (uint16_t k = 0; k < i; k++) {
            if (strcmp(reinterpret_cast<const char *>(records[i].ssid), 
                       reinterpret_cast<const char *>(records[k].ssid)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", reinterpret_cast<const char *>(records[i].ssid));
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(nets, item);
    }

    cJSON_AddItemToObject(root, "networks", nets);
    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
        cJSON_free(out);
    } else {
        httpd_resp_send_500(req);
    }
    cJSON_Delete(root);

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
            char *endptr = nullptr;
            unsigned long parsed = strtoul(size_val, &endptr, 10);
            if (endptr != size_val && parsed > 0) {
                total_size = static_cast<size_t>(parsed);
            }
        }
    }

    if (total_size == 0 || total_size > (4UL * 1024UL * 1024UL)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Ukuran file OTA tidak valid atau melebihi batas 4MB.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
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
    httpd_resp_set_hdr(req, "Set-Cookie", "token=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Update Berhasil! Sistem me-reboot...", HTTPD_RESP_USE_STRLEN);

    xTaskCreatePinnedToCore([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
        vTaskDelete(NULL);
    }, "ota_reboot", 2048, NULL, 5, NULL, 1);

    return ESP_OK;
}

// -------------------------------------------------------------
// Helper for Safe URI Registration (Zero-initialized struct & AP activity hook)
// -------------------------------------------------------------
static esp_err_t generic_uri_dispatcher(httpd_req_t *req) {
    if (Network::NetworkManager::isApActive()) {
        Network::NetworkManager::recordApActivity();
    }
    auto handler = reinterpret_cast<esp_err_t (*)(httpd_req_t *)>(req->user_ctx);
    if (handler) {
        return handler(req);
    }
    return ESP_FAIL;
}

static inline void register_uri(httpd_handle_t server, const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *), bool is_ws = false) {
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = method;
    u.handler = generic_uri_dispatcher;
    u.user_ctx = reinterpret_cast<void *>(handler);
    u.is_websocket = is_ws;
    esp_err_t err = httpd_register_uri_handler(server, &u);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gagal mendaftarkan URI '%s': 0x%x (handlers full?)", uri, err);
    }
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
    register_uri(server, "/log", HTTP_GET, log_handler);
    register_uri(server, "/log.html", HTTP_GET, log_handler);
    register_uri(server, "/net_log", HTTP_GET, log_handler);
    register_uri(server, "/net_log.html", HTTP_GET, log_handler);
    register_uri(server, "/auth", HTTP_GET, auth_handler);
    register_uri(server, "/auth_admin.html", HTTP_GET, auth_admin_handler);
    register_uri(server, "/auth_teknisi.html", HTTP_GET, auth_teknisi_handler);
    register_uri(server, "/settings", HTTP_GET, settings_handler);
    register_uri(server, "/settings.html", HTTP_GET, settings_handler);
    register_uri(server, "/firmware", HTTP_GET, firmware_handler);
    register_uri(server, "/firmware.html", HTTP_GET, firmware_handler);
    register_uri(server, "/version.json", HTTP_GET, version_json_handler);

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
    register_uri(server, "/api/v1/system/net-logs", HTTP_GET, api_net_logs_handler);
    register_uri(server, "/api/v1/system/net-logs/clear", HTTP_POST, api_net_logs_clear_handler);
    register_uri(server, "/api/v1/system/net-logs/export", HTTP_GET, api_net_logs_export_handler);
    register_uri(server, "/api/v1/system/drop-logs", HTTP_GET, api_drop_logs_handler);
    register_uri(server, "/api/v1/system/drop-logs/clear", HTTP_POST, api_drop_logs_clear_handler);
    register_uri(server, "/api/v1/system/drop-logs/export", HTTP_GET, api_drop_logs_export_handler);
    register_uri(server, "/api/v1/system/config-ap/stop", HTTP_POST, api_stop_ap_handler);
    register_uri(server, "/api/v1/system/config-ap/touch", HTTP_POST, api_touch_ap_handler);
    register_uri(server, "/api/v1/system/config-ap/touch", HTTP_GET, api_touch_ap_handler);
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
