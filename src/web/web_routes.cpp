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
#include "bsp_at24c256.hpp"
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
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

    char gz_path[536];
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

    char chunk[1024];
    size_t chunk_count = 0;
    while (true) {
        size_t r = fread(chunk, 1, sizeof(chunk), f);
        if (r == 0) break;
        if (httpd_resp_send_chunk(req, chunk, r) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
        chunk_count++;
        // Beri kesempatan bagi task lain (Cooperative Yield 1 tick tiap 2 KB)
        if ((chunk_count % 2) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
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

    char query[32] = {0};
    bool is_expired = false;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (strstr(query, "expired=1") != nullptr || strstr(query, "expired") != nullptr) {
            is_expired = true;
        }
    }

    if (is_expired) {
        // Jika diarahkan karena sesi expired / idle timeout, hanguskan token sesi lama dan hapus cookie
        char token[Session::TOKEN_LEN + 1] = {0};
        Session::validate(req, token, sizeof(token), false);
        if (token[0] != '\0') {
            Session::invalidate(token);
        }
        httpd_resp_set_hdr(req, "Set-Cookie", "token=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
        return Template::stream_template_file(req, "/storage/login.html");
    }

    UserRole role = Session::validate(req);
    if (role != UserRole::NONE) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/login.html");
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
    return Template::stream_template_file(req, "/storage/auth.html");
}

static esp_err_t auth_redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/auth");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
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
    return Template::stream_template_file(req, "/storage/firmware.html");
}

static esp_err_t docs_handler(httpd_req_t *req) {
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
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, nullptr, 0);
        return ESP_OK;
    }
    return Template::stream_template_file(req, "/storage/docs.html");
}

static esp_err_t assets_wildcard_handler(httpd_req_t *req) {
    if (strstr(req->uri, "..") != nullptr || strstr(req->uri, "//") != nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }
    char filepath[528];
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
        httpd_resp_send(req, "Too many failed attempts. Please wait 60 seconds.", HTTPD_RESP_USE_STRLEN);
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
                 role == UserRole::ADMIN ? "admin" : "engineer");
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "Login BERHASIL untuk user '%s' (role: %s)", u, role == UserRole::ADMIN ? "admin" : "engineer");
    } else if (auth_res == Session::AuthResult::SESSION_LIMIT_REACHED) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"error\",\"code\":\"session_limit\",\"message\":\"Batas maksimal 2 perangkat aktif tercapai. Silakan logout terlebih dahulu di salah satu perangkat yang sedang login.\"}", HTTPD_RESP_USE_STRLEN);
        ESP_LOGW(TAG, "Login DITOLAK: Batas 2 perangkat aktif tercapai untuk user '%s'", u);
    } else if (auth_res == Session::AuthResult::RATE_LIMITED) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Too many failed attempts. Please wait 60 seconds.", HTTPD_RESP_USE_STRLEN);
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
    bool update_sliding = false;
    char query[32] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (strstr(query, "active=1") != nullptr) {
            update_sliding = true;
        }
    }
    UserRole role = Session::validate(req, token, sizeof(token), update_sliding);
    httpd_resp_set_type(req, "application/json");
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"authenticated\":false}", HTTPD_RESP_USE_STRLEN);
    } else {
        if (update_sliding && token[0] != '\0') {
            char cookie_hdr[192];
            snprintf(cookie_hdr, sizeof(cookie_hdr), "token=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=3600", token);
            httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
        }
                char chk_resp[64];
        snprintf(chk_resp, sizeof(chk_resp), "{\"authenticated\":true,\"role\":\"%s\"}",
                 role == UserRole::ADMIN ? "admin" : "engineer");
        httpd_resp_send(req, chk_resp, HTTPD_RESP_USE_STRLEN);
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

    char body[1536] = {0};
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
                    httpd_resp_send(req, "Admin password must be at least 8 characters and cannot contain ':'", HTTPD_RESP_USE_STRLEN);
                    return ESP_OK;
                }
                strlcpy(g_config.admin_pass, val, sizeof(g_config.admin_pass));
            }
        }
        if (get_form_param(body, "mqtt_host", val, sizeof(val))) strlcpy(g_config.mqtt_host, val, sizeof(g_config.mqtt_host));
        if (get_form_param(body, "mqtt_port", val, sizeof(val))) g_config.mqtt_port = atoi(val);
        if (get_form_param(body, "mqtt_user", val, sizeof(val))) strlcpy(g_config.mqtt_user, val, sizeof(g_config.mqtt_user));
        if (get_form_param(body, "mqtt_use_ssl", val, sizeof(val))) g_config.mqtt_use_ssl = atoi(val);
        if (get_form_param(body, "mqtt_qos", val, sizeof(val))) {
            int q = atoi(val);
            g_config.mqtt_qos = (q == 0) ? 0 : 1;
        }
        if (get_form_param(body, "mqtt_keepalive", val, sizeof(val))) {
            int k = atoi(val);
            if (k < 5) k = 5;
            if (k > 65535) k = 65535;
            g_config.mqtt_keepalive = k;
        }
        if (get_form_param(body, "telem_mask", val, sizeof(val))) {
            g_config.telem_mask = (uint32_t)strtoul(val, nullptr, 10);
        } else if (strstr(body, "telem_present=1") != nullptr) {
            uint32_t mask = TELEM_MASK_V2_FLAG;
            char chk[16];
            if (get_form_param(body, "telem_uuid", chk, sizeof(chk)) && strcmp(chk, "1") == 0)  mask |= TELEM_MASK_UUID;
            if (get_form_param(body, "telem_major", chk, sizeof(chk)) && strcmp(chk, "1") == 0) mask |= TELEM_MASK_MAJOR;
            if (get_form_param(body, "telem_minor", chk, sizeof(chk)) && strcmp(chk, "1") == 0) mask |= TELEM_MASK_MINOR;
            if (get_form_param(body, "telem_rssi", chk, sizeof(chk)) && strcmp(chk, "1") == 0)  mask |= TELEM_MASK_RSSI;
            if (get_form_param(body, "telem_batt", chk, sizeof(chk)) && strcmp(chk, "1") == 0)  mask |= TELEM_MASK_BATT;
            if (get_form_param(body, "telem_time", chk, sizeof(chk)) && strcmp(chk, "1") == 0)  mask |= TELEM_MASK_TIME;
            g_config.telem_mask = mask;
        }
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
                    httpd_resp_send(req, "Technician password must be at least 8 characters and cannot contain ':'", HTTPD_RESP_USE_STRLEN);
                    return ESP_OK;
                }
                strlcpy(g_config.user_pass, val, sizeof(g_config.user_pass));
            }
        }
    } else {
        // Role ENGINEER: hanya boleh mengubah user_pass diri sendiri
        char val[64];
        if (get_form_param(body, "user_pass", val, sizeof(val))) {
            if (!is_masked_or_empty(val)) {
                if (strlen(val) < 8 || strchr(val, ':') != nullptr) {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Engineer password must be at least 8 characters and cannot contain ':'", HTTPD_RESP_USE_STRLEN);
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
             role == UserRole::ADMIN ? "Admin" : "Engineer");
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::AUTH_UPDATE, detAuth);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Configuration committed to NVS. Reboot required to apply changes.", HTTPD_RESP_USE_STRLEN);
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
    cJSON_AddStringToObject(root, "message", "Pending network changes discarded successfully.");
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

    char val[64];
    if (get_form_param(body, "scan_mode", val, sizeof(val))) {
        int sm = atoi(val);
        if (sm == 1 || sm == 2) {
            g_config.scan_mode = sm;
        }
    }
    if (get_form_param(body, "filter_uuid_en", val, sizeof(val))) g_config.filter_uuid_en = atoi(val);
    if (get_form_param(body, "filter_uuid", val, sizeof(val))) strlcpy(g_config.filter_uuid, val, sizeof(g_config.filter_uuid));
    if (get_form_param(body, "filter_major_en", val, sizeof(val))) g_config.filter_major_en = atoi(val);
    if (get_form_param(body, "filter_unknown_en", val, sizeof(val))) g_config.filter_unknown_en = atoi(val);
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
    if (role == UserRole::ADMIN) {
        if (get_form_param(body, "batch_max", val, sizeof(val))) {
            int bm = atoi(val);
            if (bm < 10) bm = 10;
            if (bm > 150) bm = 150;
            g_config.batch_max = bm;
        }
        if (get_form_param(body, "batch_flush", val, sizeof(val))) g_config.batch_flush = atoi(val);
        if (get_form_param(body, "data_ttl", val, sizeof(val))) g_config.data_ttl = atoi(val);
        if (get_form_param(body, "hb_interval", val, sizeof(val))) {
            int hb = atoi(val);
            if (hb < 1) hb = 1;
            if (hb > 300) hb = 300;
            g_config.hb_interval = hb;
        }
    }
    if (get_form_param(body, "restart_schedule", val, sizeof(val))) strlcpy(g_config.restart_schedule, val, sizeof(g_config.restart_schedule));
    if (get_form_param(body, "discon_rst", val, sizeof(val))) g_config.discon_rst = atoi(val);
    if (get_form_param(body, "ntp_server", val, sizeof(val))) {
        char cleanVal[sizeof(g_config.ntp_server)] = {0};
        size_t cIdx = 0;
        for (size_t i = 0; val[i] != '\0' && cIdx + 1 < sizeof(cleanVal); i++) {
            if (val[i] > 32 && val[i] <= 126) {
                cleanVal[cIdx++] = val[i];
            }
        }
        if (cleanVal[0] == '\0') {
            strlcpy(g_config.ntp_server, "pool.ntp.org", sizeof(g_config.ntp_server));
        } else {
            strlcpy(g_config.ntp_server, cleanVal, sizeof(g_config.ntp_server));
        }
        Network::NetworkManager::updateSntpServer(g_config.ntp_server);
    }

    Core::ConfigStore::saveConfigSettings();
    Network::MqttService::updateTopicNames();

    char detCfg[80];
    snprintf(detCfg, sizeof(detCfg), "Pengaturan Sistem Disimpan oleh %s",
             role == UserRole::ADMIN ? "Admin" : "Engineer");
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::CONFIG_UPDATE, detCfg);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Gateway configuration updated successfully.", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handlers: System Telemetry & Control REST APIs
// -------------------------------------------------------------
static esp_err_t api_system_version_handler(httpd_req_t *req) {
    if (Session::validate(req, nullptr, 0, false) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char activeIp[32] = {0};
    Network::NetworkManager::getActiveIp(activeIp, sizeof(activeIp));

    char mac_str[32] = {0};
    Network::NetworkManager::getActiveMac(mac_str, sizeof(mac_str));

    char wifi_ip[32] = {0};
    Network::NetworkManager::getWifiIp(wifi_ip, sizeof(wifi_ip));

    char wifi_mac[32] = {0};
    Network::NetworkManager::getWifiMac(wifi_mac, sizeof(wifi_mac));

    char eth_ip[32] = {0};
    Network::NetworkManager::getEthernetIp(eth_ip, sizeof(eth_ip));

    char eth_mac[32] = {0};
    Network::NetworkManager::getEthernetMac(eth_mac, sizeof(eth_mac));

    char mod_id[32] = {0};
    Core::ConfigStore::getModuleId(mod_id, sizeof(mod_id));

    uint64_t uptime_sec = esp_timer_get_time() / 1000000ULL;
    const char *net_mode = (g_config.connection_mode == 1) ? "Ethernet" : "Wi-Fi";
    bool eth_hw = Network::NetworkManager::isEthernetHardwareDetected();
    auto *drv = Network::NetworkManager::getEthernetDriver();

    httpd_resp_set_type(req, "application/json");
    set_security_headers(req);

    char chunk[512];
    // Chunk 1: Identitas perangkat & telemetri dasar
    int len = snprintf(chunk, sizeof(chunk),
        "{\"device_model\":\"%s\",\"device_version\":\"%s\",\"firmware_version\":\"%s\",\"ui_version\":\"%s\","
        "\"ap_active\":%s,\"ap_remain_sec\":%u,\"ap_ssid\":\"%s\",\"uptime_sec\":%llu,"
        "\"free_heap_kb\":%u,\"net_bandwidth\":%.2f,",
        g_config.device_model[0] != '\0' ? g_config.device_model : "EMPTY",
        g_config.device_version,
        FIRMWARE_VERSION,
        Template::get_ui_version(),
        Network::NetworkManager::isApActive() ? "true" : "false",
        (unsigned)Network::NetworkManager::getApRemainingSeconds(),
        mod_id,
        (unsigned long long)uptime_sec,
        (unsigned)(esp_get_free_heap_size() / 1024),
        Network::MqttService::getBandwidthKBps()
    );
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    // Chunk 2: Status MQTT & NTP & Mode Jaringan
    len = snprintf(chunk, sizeof(chunk),
        "\"mqtt_connected\":%s,\"mqtt_host\":\"%s\",\"mqtt_port\":%d,\"mqtt_ssl\":%s,\"mqtt_qos\":%d,\"mqtt_keepalive\":%d,"
        "\"net_connected\":%s,\"ntp_synced\":%s,\"ntp_server\":\"%s\",\"net_mode\":\"%s\",\"active_mode\":\"%s\",\"staged_mode\":\"%s\","
        "\"reboot_required\":%s,",
        Network::MqttService::isConnected() ? "true" : "false",
        g_config.mqtt_host[0] != '\0' ? g_config.mqtt_host : "-",
        g_config.mqtt_port,
        (g_config.mqtt_use_ssl == 1) ? "true" : "false",
        g_config.mqtt_qos,
        (g_config.mqtt_keepalive >= 5 && g_config.mqtt_keepalive <= 65535) ? g_config.mqtt_keepalive : 30,
        Network::NetworkManager::isConnected() ? "true" : "false",
        Network::NetworkManager::isNtpSynced() ? "true" : "false",
        g_config.ntp_server[0] != '\0' ? g_config.ntp_server : "pool.ntp.org",
        net_mode, net_mode, net_mode,
        s_auth_reboot_required ? "true" : "false"
    );
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    // Chunk 3: IP Address & Hardware
    len = snprintf(chunk, sizeof(chunk),
        "\"ip_address\":\"%s\",\"mac_address\":\"%s\",\"wifi_ip\":\"%s\",\"wifi_mac\":\"%s\","
        "\"eth_hw_detected\":%s,\"eth_ip\":\"%s\",\"eth_mac\":\"%s\",",
        activeIp, mac_str, wifi_ip, wifi_mac,
        eth_hw ? "true" : "false", eth_ip, eth_mac
    );
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    // Chunk 4: Metrik MQTT & Ethernet Driver detail
    int wifi_rssi = -999;
    if (g_config.connection_mode == 0 && Network::NetworkManager::isConnected()) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            wifi_rssi = ap_info.rssi;
        }
    }

    char wifi_rssi_part[32] = "";
    if (wifi_rssi != -999) {
        snprintf(wifi_rssi_part, sizeof(wifi_rssi_part), "\"wifi_rssi\":%d,", wifi_rssi);
    }

    char eth_extra[160] = "";
    if (drv) {
        snprintf(eth_extra, sizeof(eth_extra),
            ",\"eth_link_up\":%s,\"eth_speed\":%u,\"eth_duplex\":\"%s\",\"eth_direct_mode\":%s,\"eth_slave_mode\":%s",
            drv->isConnected() ? "true" : "false",
            (unsigned)drv->linkSpeed(),
            drv->isFullDuplex() ? "Full" : "Half",
            Network::NetworkManager::isEthernetDirectPcMode() ? "true" : "false",
            Network::NetworkManager::isEthernetPeerSlaveMode() ? "true" : "false"
        );
    }

    len = snprintf(chunk, sizeof(chunk),
        "%s\"mqtt_published\":%u,\"mqtt_failed\":%u,\"mqtt_last_error\":\"%s\"%s}",
        wifi_rssi_part,
        (unsigned)Core::BeaconPipeline::getTotalMqttPublished(),
        (unsigned)Core::BeaconPipeline::getTotalMqttFailed(),
        Network::MqttService::getLastError(),
        eth_extra
    );
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    return httpd_resp_send_chunk(req, nullptr, 0); // Selesai
}

static esp_err_t api_system_ap_status_handler(httpd_req_t *req) {
    char mod_id[32] = {0};
    Core::ConfigStore::getModuleId(mod_id, sizeof(mod_id));
    char buf[128];
    int len = snprintf(buf, sizeof(buf),
        "{\"ap_active\":%s,\"ap_ssid\":\"%s\",\"ap_remain_sec\":%u,\"reboot_required\":%s}",
        Network::NetworkManager::isApActive() ? "true" : "false",
        mod_id,
        static_cast<unsigned>(Network::NetworkManager::getApRemainingSeconds()),
        s_auth_reboot_required ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_send(req, buf, len);
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
    UserRole role = Session::validate(req, token, sizeof(token), false);
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
// Defense-in-Depth: Zero-OOM Heap Guard & Streaming JSON Helpers
// -------------------------------------------------------------
static inline bool check_heap_safety(httpd_req_t *req, size_t min_free_bytes = 35000) {
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < min_free_bytes) {
        ESP_LOGW(TAG, "Heap guard aktif: free heap %u B < threshold %u B. Menolak request web 503.",
                 (unsigned)free_heap, (unsigned)min_free_bytes);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_hdr(req, "Retry-After", "3");
        httpd_resp_set_type(req, "application/json");
        set_security_headers(req);
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Device busy, BLE pipeline prioritized\"}", HTTPD_RESP_USE_STRLEN);
        return false;
    }
    return true;
}

static void escape_json_string(const char *src, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t out = 0;
    while (*src && out + 1 < dst_len) {
        if (*src == '"' || *src == '\\') {
            if (out + 2 >= dst_len) break;
            dst[out++] = '\\';
            dst[out++] = *src;
        } else if (*src == '\n') {
            if (out + 2 >= dst_len) break;
            dst[out++] = '\\';
            dst[out++] = 'n';
        } else if (*src == '\r') {
            if (out + 2 >= dst_len) break;
            dst[out++] = '\\';
            dst[out++] = 'r';
        } else if (*src == '\t') {
            if (out + 2 >= dst_len) break;
            dst[out++] = '\\';
            dst[out++] = 't';
        } else if (static_cast<unsigned char>(*src) < 32) {
            // Hilangkan non-printable control characters
        } else {
            dst[out++] = *src;
        }
        src++;
    }
    dst[out] = '\0';
}

// -------------------------------------------------------------
// Route Handlers: Network Event Logs REST APIs (Zero-DRAM Streaming)
// -------------------------------------------------------------
static esp_err_t api_net_logs_handler(httpd_req_t *req) {
    if (!check_heap_safety(req, 12000)) {
        return ESP_OK;
    }

    UserRole role = Session::validate(req, nullptr, 0, false);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

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

    const char* ethStatusStr = "link_down";
    if (!ethHw) {
        ethStatusStr = "uninstalled";
    } else if (ethConn) {
        ethStatusStr = "connected";
    } else if (ethLink) {
        ethStatusStr = "link_up";
    }

    char chunk[512];
    httpd_resp_set_type(req, "application/json");
    set_security_headers(req);

    // Kirim bagian awal JSON header tanpa alokasi heap cJSON
    int len = snprintf(chunk, sizeof(chunk),
        "{\"wifi_status\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_ip\":\"%s\",\"wifi_disconnects\":%u,"
        "\"eth_status\":\"%s\",\"eth_ip\":\"%s\",\"eth_disconnects\":%u,"
        "\"mqtt_status\":\"%s\",\"mqtt_host\":\"%s\",\"mqtt_port\":%d,\"mqtt_ssl\":%s,\"mqtt_disconnects\":%u,"
        "\"boot_count\":%u,\"reset_reason\":\"%s\",\"storage_mode\":\"%s\",\"eeprom_detected\":%s,\"flash_protected\":true,\"uptime_sec\":%llu,\"lifetime_uptime_sec\":%llu,",
        wifiConn ? "connected" : "disconnected",
        g_config.w_ssid[0] ? g_config.w_ssid : "-",
        wifiConn ? activeIp : "-",
        (unsigned)Network::NetLogService::getWifiDisconnectCount(),
        ethStatusStr,
        ethConn ? activeIp : "-",
        (unsigned)Network::NetLogService::getEthDisconnectCount(),
        mqttConn ? "online" : "offline",
        g_config.mqtt_host[0] ? g_config.mqtt_host : "-",
        g_config.mqtt_port,
        (g_config.mqtt_use_ssl == 1) ? "true" : "false",
        (unsigned)Network::NetLogService::getMqttDisconnectCount(),
        (unsigned)Network::NetLogService::getBootCount(),
        Network::NetLogService::getLastResetReasonString(),
        Network::NetLogService::getStorageModeString(),
        Network::NetLogService::isEepromDetected() ? "true" : "false",
        (unsigned long long)(esp_timer_get_time() / 1000000ULL),
        (unsigned long long)Network::NetLogService::getLifetimeUptimeSec()
    );
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    // 2. Data Log (Maksimal 50, Stream Langsung dari static BSS buffer)
    static Network::NetLogEntry s_tempLogs[Network::MAX_NET_LOGS];
    size_t count = Network::NetLogService::getLogs(s_tempLogs, Network::MAX_NET_LOGS);

    len = snprintf(chunk, sizeof(chunk), "\"total\":%u,\"logs\":[", (unsigned)count);
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    char escDetail[160];
    for (size_t i = 0; i < count; i++) {
        escape_json_string(s_tempLogs[i].detail, escDetail, sizeof(escDetail));
        len = snprintf(chunk, sizeof(chunk),
            "%s{\"id\":%u,\"uptime_sec\":%llu,\"time\":\"%s\",\"cat\":\"%s\",\"event\":\"%s\",\"status_class\":\"%s\",\"detail\":\"%s\"}",
            (i > 0) ? "," : "",
            (unsigned)s_tempLogs[i].id,
            (unsigned long long)s_tempLogs[i].uptime_sec,
            s_tempLogs[i].time_str,
            Network::NetLogService::categoryToString(s_tempLogs[i].category),
            Network::NetLogService::eventToString(s_tempLogs[i].event),
            Network::NetLogService::eventToStatusClass(s_tempLogs[i].event),
            escDetail
        );
        if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;
    }

    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, nullptr, 0); // Selesai
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
             role == UserRole::ADMIN ? "Admin" : "Engineer");
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
    char clean_model[32] = {0};
    size_t mIdx = 0;
    const char* raw_model = g_config.device_model[0] ? g_config.device_model : "sg10";
    for (size_t k = 0; raw_model[k] != '\0' && mIdx < sizeof(clean_model) - 1; ++k) {
        char c = raw_model[k];
        if (c >= 'A' && c <= 'Z') clean_model[mIdx++] = static_cast<char>(c + ('a' - 'A'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) clean_model[mIdx++] = c;
    }
    if (mIdx == 0) strlcpy(clean_model, "sg10", sizeof(clean_model));
    char disp_hdr[64];
    snprintf(disp_hdr, sizeof(disp_hdr), "attachment; filename=\"net_log_%s.csv\"", clean_model);
    httpd_resp_set_hdr(req, "Content-Disposition", disp_hdr);

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
    if (!check_heap_safety(req, 12000)) {
        return ESP_OK;
    }

    if (Session::validate(req, nullptr, 0, false) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    Core::DropSummaryStats stats = Core::DropLogService::getSummaryStats();
    static Core::DropLogEntry s_tempDropLogs[Core::MAX_DROP_LOGS];
    size_t count = Core::DropLogService::getLogs(s_tempDropLogs, Core::MAX_DROP_LOGS);

    char chunk[512];
    httpd_resp_set_type(req, "application/json");
    set_security_headers(req);

    // Kirim bagian ringkasan statistik
    int len = snprintf(chunk, sizeof(chunk),
        "{\"summary\":{\"total_dropped\":%u,\"ttl_expired\":%u,\"rssi_min\":%u,\"uuid_mismatch\":%u,\"major_mismatch\":%u,\"queue_overflow\":%u,\"mqtt_failed\":%u,\"unknown_format\":%u},"
        "\"uptime_sec\":%llu,\"lifetime_uptime_sec\":%llu,\"mqtt_connected\":%s,\"total\":%u,\"logs\":[",
        (unsigned)stats.total_dropped,
        (unsigned)stats.ttl_expired,
        (unsigned)stats.rssi_min,
        (unsigned)stats.uuid_mismatch,
        (unsigned)stats.major_mismatch,
        (unsigned)stats.queue_overflow,
        (unsigned)stats.mqtt_failed,
        (unsigned)stats.unknown_format,
        (unsigned long long)(esp_timer_get_time() / 1000000ULL),
        (unsigned long long)Network::NetLogService::getLifetimeUptimeSec(),
        Network::MqttService::isConnected() ? "true" : "false",
        (unsigned)count
    );
    if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;

    // Stream 50 rekaman drop langsung tanpa alokasi cJSON AST
    for (size_t i = 0; i < count; i++) {
        len = snprintf(chunk, sizeof(chunk),
            "%s{\"id\":%u,\"uptime_sec\":%llu,\"time\":\"%s\",\"code\":\"%s\",\"tag\":\"%s\",\"mac\":\"%s\",\"raw\":\"%s\",\"rssi\":%d,\"reason\":%u,\"reason_str\":\"%s\",\"status_class\":\"%s\"}",
            (i > 0) ? "," : "",
            (unsigned)s_tempDropLogs[i].id,
            (unsigned long long)s_tempDropLogs[i].uptime_sec,
            s_tempDropLogs[i].time_str,
            s_tempDropLogs[i].code,
            s_tempDropLogs[i].tag,
            s_tempDropLogs[i].mac,
            s_tempDropLogs[i].raw,
            (int)s_tempDropLogs[i].rssi,
            (unsigned)static_cast<uint8_t>(s_tempDropLogs[i].reason),
            Core::DropLogService::reasonToString(s_tempDropLogs[i].reason),
            Core::DropLogService::reasonToStatusClass(s_tempDropLogs[i].reason)
        );
        if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) return ESP_FAIL;
    }

    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, nullptr, 0); // Selesai
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
    char clean_drop_model[32] = {0};
    size_t dmIdx = 0;
    const char* raw_drop_model = g_config.device_model[0] ? g_config.device_model : "sg10";
    for (size_t k = 0; raw_drop_model[k] != '\0' && dmIdx < sizeof(clean_drop_model) - 1; ++k) {
        char c = raw_drop_model[k];
        if (c >= 'A' && c <= 'Z') clean_drop_model[dmIdx++] = static_cast<char>(c + ('a' - 'A'));
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) clean_drop_model[dmIdx++] = c;
    }
    if (dmIdx == 0) strlcpy(clean_drop_model, "sg10", sizeof(clean_drop_model));
    char drop_disp_hdr[64];
    snprintf(drop_disp_hdr, sizeof(drop_disp_hdr), "attachment; filename=\"drop_log_%s.csv\"", clean_drop_model);
    httpd_resp_set_hdr(req, "Content-Disposition", drop_disp_hdr);

    // Header CSV (8 kolom)
    const char *csv_head = "No,Waktu,MAC Address,Tipe,Major-Minor/Tag,RSSI(dBm),Alasan,Raw 44B\r\n";
    httpd_resp_send_chunk(req, csv_head, strlen(csv_head));

    static Core::DropLogEntry s_tempDropLogs[Core::MAX_DROP_LOGS];
    size_t count = Core::DropLogService::getLogs(s_tempDropLogs, Core::MAX_DROP_LOGS);

    char lineBuf[256];
    char raw44[90];
    for (size_t i = 0; i < count; i++) {
        const char* rawOut = s_tempDropLogs[i].raw;
        size_t rawLen = strlen(s_tempDropLogs[i].raw);
        if (rawLen >= 84) {
            time_t now = 0;
            time(&now);
            uint32_t epoch = (now > 1704067200LL) ? static_cast<uint32_t>(now) : s_tempDropLogs[i].uptime_sec;
            snprintf(raw44, sizeof(raw44), "%08X%.80s", (unsigned int)epoch, s_tempDropLogs[i].raw + 2);
            rawOut = raw44;
        }

        snprintf(lineBuf, sizeof(lineBuf), "%u,%s,%s,%s,%s,%d,\"%s\",%s\r\n",
                 (unsigned int)s_tempDropLogs[i].id,
                 s_tempDropLogs[i].time_str,
                 s_tempDropLogs[i].mac,
                 s_tempDropLogs[i].code,
                 s_tempDropLogs[i].tag,
                 (int)s_tempDropLogs[i].rssi,
                 Core::DropLogService::reasonToString(s_tempDropLogs[i].reason),
                 rawOut);
        httpd_resp_send_chunk(req, lineBuf, strlen(lineBuf));
    }

    httpd_resp_send_chunk(req, nullptr, 0); // Selesai
    return ESP_OK;
}

static esp_err_t api_reboot_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char rbtDetail[80];
    snprintf(rbtDetail, sizeof(rbtDetail), "Manual gateway reboot requested by %s",
             role == UserRole::ADMIN ? "Admin" : "Engineer");
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::REBOOT, rbtDetail);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Gateway is rebooting...", HTTPD_RESP_USE_STRLEN);

    xTaskCreatePinnedToCore([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        vTaskDelete(NULL);
    }, "reboot_task", 2048, NULL, 5, NULL, 1);

    return ESP_OK;
}

static esp_err_t api_eeprom_clear_handler(httpd_req_t *req) {
    // 1. Otorisasi ketat: HANYA ADMINISTRATOR
    UserRole role = Session::validate(req);
    if (role != UserRole::ADMIN) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "failed", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // 2. Cek deteksi hardware
    if (!Bsp::At24c256::isDetected()) {
        ESP_LOGE(TAG, "Gagal clear EEPROM: Hardware AT24C256 tidak terdeteksi");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "failed", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // 3. Eksekusi penghapusan seluruh 32 KB data EEPROM
    esp_err_t err = Bsp::At24c256::wipeAll(0xFF);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Gagal wipe EEPROM AT24C256: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "failed", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // 4. Sinkronkan cache memori RAM agar riwayat log & counter telemetri langsung nol
    Network::NetLogService::onEepromWiped();

    // 5. Kirim respons sukses plaintext murni
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "success", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_reset_handler(httpd_req_t *req) {
    UserRole role = Session::validate(req);
    if (role == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized: Login required", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    Core::ConfigStore::resetConfigToFactory();
    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::CONFIG_RESET,
                                "Konfigurasi Sistem Direset ke Standar Pabrik (Web Admin/Engineer)");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "NVS configuration restored to factory defaults. Gateway restarting...", HTTPD_RESP_USE_STRLEN);

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

static void stop_wifi_if_ethernet_active() {
    // Radio Wi-Fi HANYA boleh dimatikan jika konfigurasi koneksi aktif memang murni Mode Ethernet
    // dan SoftAP konfigurasi tidak sedang aktif.
    // Pada mode Wi-Fi, radio Wi-Fi HARUS tetap menyala untuk menjaga uplink router dan MQTTS.
    if (Network::NetworkManager::getActiveConnectionMode() == ConnectionMode::ETHERNET &&
        !Network::NetworkManager::isApActive()) {
        wifi_mode_t curMode = WIFI_MODE_NULL;
        if (esp_wifi_get_mode(&curMode) == ESP_OK && curMode != WIFI_MODE_NULL) {
            ESP_LOGI(TAG, "Mode Ethernet aktif: mematikan radio Wi-Fi untuk mengembalikan DRAM...");
            esp_wifi_stop();
        }
    }
}

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
    stop_wifi_if_ethernet_active();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"stopped\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t api_wifi_scan_handler(httpd_req_t *req) {
    if (!check_heap_safety(req, 12000)) {
        return ESP_OK;
    }

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
    } else if (curMode != WIFI_MODE_STA && curMode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

    // 3. Pastikan radio Wi-Fi aktif (esp_wifi_start)
    esp_err_t startErr = esp_wifi_start();
    if (startErr != ESP_OK && startErr != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "Gagal mengaktifkan radio Wi-Fi: 0x%x (%s)", startErr, esp_err_to_name(startErr));
        stop_wifi_if_ethernet_active();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Wi-Fi radio unavailable\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Beri jeda 30ms agar background Wi-Fi driver task ESP-IDF berpindah ke state STARTED
    vTaskDelay(pdMS_TO_TICKS(30));

    // 4. Cek parameter refresh dari query string
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
        stop_wifi_if_ethernet_active();
    }

    if (forceRefresh && s_scan_in_progress.load()) {
        esp_wifi_scan_stop();
        s_scan_in_progress.store(false);
        Network::NetworkManager::resetWifiScanDone();
    }

    // 5. Jika belum ada scan yang berjalan, mulai scan baru
    if (!s_scan_in_progress.load()) {
        Network::NetworkManager::resetWifiScanDone();

        wifi_scan_config_t scan_cfg = {};
        scan_cfg.show_hidden = false;
        scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        scan_cfg.scan_time.active.min = 120;
        scan_cfg.scan_time.active.max = 160;

        esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
        if (err == ESP_ERR_WIFI_STATE) {
            ESP_LOGW(TAG, "Wi-Fi radio sedang sibuk/menghubungkan diri ke AP (0x3006), menunda pemindaian...");
            // Kirim status scanning agar frontend terus polling halus hingga koneksi Wi-Fi stabil
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"scanning\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        if (err == ESP_OK) {
            s_scan_in_progress.store(true);
            s_scan_start_millis.store(now);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"scanning\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Gagal memulai scan Wi-Fi: 0x%x (%s)", err, esp_err_to_name(err));
            stop_wifi_if_ethernet_active();
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"status\":\"error\",\"message\":\"Gagal memulai pemindaian Wi-Fi\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
    }

    // 6. Cek apakah seluruh channel telah selesai dipindai melalui event WIFI_EVENT_SCAN_DONE
    if (!Network::NetworkManager::isWifiScanDone()) {
        // Pemindaian masih berlangsung di background ESP-IDF melintasi channel 1 s.d. 13
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"scanning\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Pemindaian seluruh channel 1-13 telah selesai 100%!
    s_scan_in_progress.store(false);
    Network::NetworkManager::resetWifiScanDone();

    static wifi_ap_record_t s_scan_records[30];
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count > 30) ap_count = 30;
    esp_wifi_scan_get_ap_records(&ap_count, s_scan_records);

    // Urutkan berdasarkan kekuatan sinyal RSSI (terkuat di paling atas)
    for (int i = 0; i < (int)ap_count - 1; i++) {
        for (int j = i + 1; j < (int)ap_count; j++) {
            if (s_scan_records[j].rssi > s_scan_records[i].rssi) {
                wifi_ap_record_t tmp = s_scan_records[i];
                s_scan_records[i] = s_scan_records[j];
                s_scan_records[j] = tmp;
            }
        }
    }

    char chunk[256];
    httpd_resp_set_type(req, "application/json");
    set_security_headers(req);

    if (httpd_resp_send_chunk(req, "{\"status\":\"complete\",\"networks\":[", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        stop_wifi_if_ethernet_active();
        return ESP_FAIL;
    }

    bool first = true;
    char escSsid[64];
    for (uint16_t i = 0; i < ap_count; i++) {
        if (s_scan_records[i].ssid[0] == '\0') continue;

        bool duplicate = false;
        for (uint16_t k = 0; k < i; k++) {
            if (strcmp(reinterpret_cast<const char *>(s_scan_records[i].ssid), 
                       reinterpret_cast<const char *>(s_scan_records[k].ssid)) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        escape_json_string(reinterpret_cast<const char *>(s_scan_records[i].ssid), escSsid, sizeof(escSsid));
        int len = snprintf(chunk, sizeof(chunk),
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"secure\":%s}",
            first ? "" : ",",
            escSsid,
            s_scan_records[i].rssi,
            s_scan_records[i].primary,
            (s_scan_records[i].authmode != WIFI_AUTH_OPEN) ? "true" : "false"
        );
        first = false;
        if (httpd_resp_send_chunk(req, chunk, len) != ESP_OK) {
            stop_wifi_if_ethernet_active();
            return ESP_FAIL;
        }
    }

    if (httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        stop_wifi_if_ethernet_active();
        return ESP_FAIL;
    }
    esp_err_t finishErr = httpd_resp_send_chunk(req, nullptr, 0); // Selesai
    stop_wifi_if_ethernet_active();
    return finishErr;
}

// -------------------------------------------------------------
// Route Handlers: Dual OTA Stream Upload
// -------------------------------------------------------------
static esp_err_t api_ota_upload_handler(httpd_req_t *req) {
    if (Session::validate(req) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Sesi Anda telah berakhir.", HTTPD_RESP_USE_STRLEN);
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
        httpd_resp_send(req, "Invalid OTA binary file size or exceeds 4MB limit.", HTTPD_RESP_USE_STRLEN);
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
            httpd_resp_send(req, "Connection interrupted during upload stream.", HTTPD_RESP_USE_STRLEN);
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
    httpd_resp_send(req, "Update complete! Gateway restarting...", HTTPD_RESP_USE_STRLEN);

    Network::NetLogService::log(Network::NetCategory::SYSTEM, Network::NetEvent::REBOOT,
                                "Firmware OTA update completed, restarting gateway");

    xTaskCreatePinnedToCore([](void *) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
        vTaskDelete(NULL);
    }, "ota_reboot", 2048, NULL, 5, NULL, 1);

    return ESP_OK;
}

// -------------------------------------------------------------
// Route Handler: Real-Time NTP Server Probe API (UDP Port 123)
// -------------------------------------------------------------
static esp_err_t api_test_ntp_handler(httpd_req_t *req) {
    if (Session::validate(req, nullptr, 0, false) == UserRole::NONE) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"error\",\"success\":false,\"message\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char serverTarget[64] = {0};

    // 1. Coba baca dari query string jika ada
    char query[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        get_form_param(query, "server", serverTarget, sizeof(serverTarget));
    }

    // 2. Jika kosong dan ada request body, baca dari body
    if (serverTarget[0] == '\0' && req->content_len > 0) {
        char body[256] = {0};
        int to_read = req->content_len < (int)sizeof(body) - 1 ? req->content_len : (int)sizeof(body) - 1;
        int r = httpd_req_recv(req, body, to_read);
        if (r > 0) {
            body[r] = '\0';
            cJSON *json = cJSON_Parse(body);
            if (json) {
                cJSON *srvItem = cJSON_GetObjectItem(json, "server");
                if (srvItem && cJSON_IsString(srvItem) && srvItem->valuestring) {
                    strlcpy(serverTarget, srvItem->valuestring, sizeof(serverTarget));
                }
                cJSON_Delete(json);
            }
            if (serverTarget[0] == '\0') {
                get_form_param(body, "server", serverTarget, sizeof(serverTarget));
            }
        }
    }

    // 3. Fallback ke konfigurasi yang tersimpan
    if (serverTarget[0] == '\0') {
        strlcpy(serverTarget, g_config.ntp_server[0] ? g_config.ntp_server : "pool.ntp.org", sizeof(serverTarget));
    }

    // Sanitasi whitespace
    char cleanServer[sizeof(serverTarget)] = {0};
    size_t sIdx = 0;
    for (size_t i = 0; serverTarget[i] != '\0' && sIdx + 1 < sizeof(cleanServer); i++) {
        if (serverTarget[i] > 32 && serverTarget[i] <= 126) {
            cleanServer[sIdx++] = serverTarget[i];
        }
    }
    if (cleanServer[0] == '\0') {
        strlcpy(cleanServer, "pool.ntp.org", sizeof(cleanServer));
    }

    ESP_LOGI(TAG, "Pengujian NTP probe ke server: '%s'...", cleanServer);

    // 4. DNS Resolution
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = nullptr;

    int gai_err = getaddrinfo(cleanServer, "123", &hints, &res);
    if (gai_err != 0 || !res) {
        ESP_LOGW(TAG, "DNS lookup gagal untuk NTP '%s' (err=%d)", cleanServer, gai_err);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "server", cleanServer);
        cJSON_AddStringToObject(root, "message", "DNS lookup failed: Host unreachable");
        char *resp_str = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        free(resp_str);
        cJSON_Delete(root);
        return ESP_OK;
    }

    char ipStr[INET_ADDRSTRLEN] = "";
    inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, ipStr, sizeof(ipStr));

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "server", cleanServer);
        cJSON_AddStringToObject(root, "message", "Failed to initialize UDP socket");
        char *resp_str = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        free(resp_str);
        cJSON_Delete(root);
        return ESP_OK;
    }

    // Set timeout receive & send 3 detik
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Standar RFC 5905 NTPv4 Request: 48 bytes, LI=0, VN=4, Mode=3 -> 0x23
    uint8_t ntpPacket[48] = {0};
    ntpPacket[0] = 0x23;

    int64_t startUs = esp_timer_get_time();
    ssize_t sent = sendto(sock, ntpPacket, sizeof(ntpPacket), 0, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (sent < (ssize_t)sizeof(ntpPacket)) {
        close(sock);
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "server", cleanServer);
        cJSON_AddStringToObject(root, "ip", ipStr);
        cJSON_AddStringToObject(root, "message", "Failed to transmit UDP probe frame");
        char *resp_str = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        free(resp_str);
        cJSON_Delete(root);
        return ESP_OK;
    }

    struct sockaddr_in fromAddr = {};
    socklen_t fromLen = sizeof(fromAddr);
    ssize_t recvd = recvfrom(sock, ntpPacket, sizeof(ntpPacket), 0, (struct sockaddr *)&fromAddr, &fromLen);
    int64_t elapsedMs = (esp_timer_get_time() - startUs) / 1000;
    close(sock);

    if (recvd < 48) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "error");
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "server", cleanServer);
        cJSON_AddStringToObject(root, "ip", ipStr);
        cJSON_AddStringToObject(root, "message", "Server tidak merespons (Timeout 3s / Port UDP 123 diblokir)");
        char *resp_str = cJSON_PrintUnformatted(root);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
        free(resp_str);
        cJSON_Delete(root);
        return ESP_OK;
    }

    uint8_t stratum = ntpPacket[1];
    uint32_t sec1900 = ((uint32_t)ntpPacket[40] << 24) |
                       ((uint32_t)ntpPacket[41] << 16) |
                       ((uint32_t)ntpPacket[42] << 8)  |
                       ((uint32_t)ntpPacket[43]);

    const uint32_t NTP_OFFSET_1970 = 2208988800UL;
    char timeStr[64] = "—";
    if (sec1900 > NTP_OFFSET_1970) {
        time_t unixTime = (time_t)(sec1900 - NTP_OFFSET_1970);
        struct tm tmWib;
        localtime_r(&unixTime, &tmWib);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S WIB", &tmWib);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "server", cleanServer);
    cJSON_AddStringToObject(root, "ip", ipStr);
    cJSON_AddNumberToObject(root, "rtt_ms", (double)elapsedMs);
    cJSON_AddNumberToObject(root, "stratum", stratum);
    cJSON_AddStringToObject(root, "server_time", timeStr);

    char msg[128];
    snprintf(msg, sizeof(msg), "Server NTP aktif (Latensi: %lld ms | Stratum %d)", (long long)elapsedMs, stratum);
    cJSON_AddStringToObject(root, "message", msg);

    char *resp_str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    cJSON_Delete(root);
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
static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err) {
    if (strncmp(req->uri, "/api/", 5) == 0 || strncmp(req->uri, "/assets/", 8) == 0 ||
        strstr(req->uri, ".ico") != nullptr || strstr(req->uri, ".png") != nullptr ||
        strstr(req->uri, ".css") != nullptr || strstr(req->uri, ".js") != nullptr) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

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
    register_uri(server, "/auth_admin.html", HTTP_GET, auth_redirect_handler);
    register_uri(server, "/auth_teknisi.html", HTTP_GET, auth_redirect_handler);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_handler);
    register_uri(server, "/settings", HTTP_GET, settings_handler);
    register_uri(server, "/settings.html", HTTP_GET, settings_handler);
    register_uri(server, "/firmware", HTTP_GET, firmware_handler);
    register_uri(server, "/firmware.html", HTTP_GET, firmware_handler);
    register_uri(server, "/docs", HTTP_GET, docs_handler);
    register_uri(server, "/docs.html", HTTP_GET, docs_handler);
    register_uri(server, "/version.json", HTTP_GET, version_json_handler);
    register_uri(server, "/favicon.ico", HTTP_GET, favicon_handler);

    // Authentication REST
    register_uri(server, "/api/v1/auth/login", HTTP_POST, api_login_handler);
    register_uri(server, "/api/v1/auth/logout", HTTP_POST, api_logout_handler);
    register_uri(server, "/api/v1/auth/check", HTTP_GET, api_auth_check_handler);

    // Config REST
    register_uri(server, "/api/v1/config/auth", HTTP_POST, api_config_auth_handler);
    register_uri(server, "/api/v1/config/network/discard", HTTP_POST, api_network_discard_handler);
    register_uri(server, "/api/v1/config/settings", HTTP_POST, api_config_settings_handler);

    // System REST
    register_uri(server, "/api/v1/system/ap-status", HTTP_GET, api_system_ap_status_handler);
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

    // EEPROM Clear Endpoint (Admin Only - Plaintext Response)
    register_uri(server, "/api/v1/system/eeprom/clear", HTTP_GET, api_eeprom_clear_handler);
    register_uri(server, "/api/v1/system/eeprom/clear", HTTP_POST, api_eeprom_clear_handler);
    register_uri(server, "/eeprom/clear", HTTP_GET, api_eeprom_clear_handler);
    register_uri(server, "/eeprom/clear", HTTP_POST, api_eeprom_clear_handler);
    register_uri(server, "/api/v1/system/test-ntp", HTTP_POST, api_test_ntp_handler);
    register_uri(server, "/api/v1/system/test-ntp", HTTP_GET, api_test_ntp_handler);
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
