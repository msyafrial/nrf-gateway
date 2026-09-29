#include "session_mgr.hpp"
#include "app_config.hpp"
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>

namespace Iotaru::Web::Session {

static SessionEntry s_sessions[MAX_SESSIONS] = {};
static int s_failedLoginAttempts = 0;
static uint32_t s_lastFailedLoginTime = 0;

static inline uint32_t get_millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void init() {
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        s_sessions[i].active = false;
        s_sessions[i].token[0] = '\0';
        s_sessions[i].client_ip = 0;
        s_sessions[i].role = UserRole::NONE;
        s_sessions[i].last_activity_ms = 0;
    }
    s_failedLoginAttempts = 0;
    s_lastFailedLoginTime = 0;
}

static void generate_token(char *out_buf, size_t max_len) {
    if (max_len < TOKEN_LEN + 1) return;
    for (int i = 0; i < 8; i++) {
        uint32_t r = esp_random();
        snprintf(out_buf + (i * 4), 5, "%04x", static_cast<unsigned int>(r & 0xFFFF));
    }
    out_buf[TOKEN_LEN] = '\0';
}

bool is_rate_limited() {
    uint32_t now = get_millis();
    if (s_failedLoginAttempts >= 5 && (now - s_lastFailedLoginTime < 60000)) {
        return true;
    }
    return false;
}

void record_failed_attempt() {
    s_failedLoginAttempts++;
    s_lastFailedLoginTime = get_millis();
}

void reset_failed_attempts() {
    s_failedLoginAttempts = 0;
}

UserRole validate(httpd_req_t *req, char *out_token, size_t max_len) {
    if (!req) return UserRole::NONE;

    char token[TOKEN_LEN + 1] = {0};

    // 1. Try Cookie header
    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (cookie_len > 0) {
        char cookie_buf[256];
        if (cookie_len < sizeof(cookie_buf)) {
            if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) == ESP_OK) {
                const char *pos = strstr(cookie_buf, "token=");
                if (pos) {
                    pos += 6;
                    const char *end = strchr(pos, ';');
                    size_t len = end ? static_cast<size_t>(end - pos) : strlen(pos);
                    if (len == TOKEN_LEN) {
                        memcpy(token, pos, TOKEN_LEN);
                        token[TOKEN_LEN] = '\0';
                    }
                }
            }
        }
    }

    // 2. Try Authorization header (Bearer <token>)
    if (token[0] == '\0') {
        size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
        if (auth_len > 0) {
            char auth_buf[128];
            if (auth_len < sizeof(auth_buf)) {
                if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) == ESP_OK) {
                    if (strncmp(auth_buf, "Bearer ", 7) == 0) {
                        const char *bearer = auth_buf + 7;
                        if (strlen(bearer) == TOKEN_LEN) {
                            memcpy(token, bearer, TOKEN_LEN);
                            token[TOKEN_LEN] = '\0';
                        }
                    }
                }
            }
        }
    }

    // 3. Try URL query parameter (?token=<token>)
    if (token[0] == '\0') {
        size_t query_len = httpd_req_get_url_query_len(req);
        if (query_len > 0) {
            char query_buf[128];
            if (query_len < sizeof(query_buf)) {
                if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) == ESP_OK) {
                    char q_tok[TOKEN_LEN + 1] = {0};
                    if (httpd_query_key_value(query_buf, "token", q_tok, sizeof(q_tok)) == ESP_OK) {
                        if (strlen(q_tok) == TOKEN_LEN) {
                            memcpy(token, q_tok, TOKEN_LEN);
                            token[TOKEN_LEN] = '\0';
                        }
                    }
                }
            }
        }
    }

    if (token[0] == '\0') {
        return UserRole::NONE;
    }

    if (out_token && max_len > TOKEN_LEN) {
        strncpy(out_token, token, max_len);
    }

    uint32_t now = get_millis();
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && (strcmp(s_sessions[i].token, token) == 0)) {
            // Check 15-minute sliding expiration (SESSION_TIMEOUT_MS = 900000 ms)
            if (now - s_sessions[i].last_activity_ms > SESSION_TIMEOUT_MS) {
                s_sessions[i].active = false;
                s_sessions[i].token[0] = '\0';
                s_sessions[i].client_ip = 0;
                return UserRole::NONE;
            }
            // Sliding reset: perbarui waktu aktivitas
            s_sessions[i].last_activity_ms = now;
            return s_sessions[i].role;
        }
    }

    return UserRole::NONE;
}

void touch(const char *token) {
    if (!token || token[0] == '\0') return;
    uint32_t now = get_millis();
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && (strcmp(s_sessions[i].token, token) == 0)) {
            s_sessions[i].last_activity_ms = now;
            break;
        }
    }
}

size_t get_active_count() {
    uint32_t now = get_millis();
    size_t count = 0;
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active) {
            if (now - s_sessions[i].last_activity_ms > SESSION_TIMEOUT_MS) {
                s_sessions[i].active = false;
                s_sessions[i].token[0] = '\0';
                s_sessions[i].client_ip = 0;
            } else {
                count++;
            }
        }
    }
    return count;
}

static bool constant_time_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a == 0 && len_b == 0) return true;
    size_t max_len = len_a > len_b ? len_a : len_b;
    unsigned char diff = static_cast<unsigned char>(len_a ^ len_b);
    for (size_t i = 0; i < max_len; i++) {
        unsigned char ca = (i < len_a) ? static_cast<unsigned char>(a[i]) : 0;
        unsigned char cb = (i < len_b) ? static_cast<unsigned char>(b[i]) : 0;
        diff |= (ca ^ cb);
    }
    return (diff == 0);
}

AuthResult authenticate(httpd_req_t *req, const char *user, const char *pass, UserRole &out_role, char *out_token, size_t max_len) {
    if (is_rate_limited()) {
        return AuthResult::RATE_LIMITED;
    }

    if (!user || !pass || user[0] == '\0' || pass[0] == '\0') {
        return AuthResult::INVALID_CREDENTIALS;
    }

    // Fallback to factory defaults if config strings are uninitialized
    const char *expected_adm_u = (g_config.admin_user[0] != '\0') ? g_config.admin_user : "admin";
    const char *expected_adm_p = (g_config.admin_pass[0] != '\0') ? g_config.admin_pass : "admin";
    const char *expected_tek_u = (g_config.user_user[0] != '\0') ? g_config.user_user : "user";
    const char *expected_tek_p = (g_config.user_pass[0] != '\0') ? g_config.user_pass : "user";

    UserRole role = UserRole::NONE;
    if (constant_time_eq(user, expected_adm_u) && constant_time_eq(pass, expected_adm_p)) {
        role = UserRole::ADMIN;
    } else if (constant_time_eq(user, expected_tek_u) && constant_time_eq(pass, expected_tek_p)) {
        role = UserRole::TEKNISI;
    }

    if (role == UserRole::NONE) {
        record_failed_attempt();
        vTaskDelay(pdMS_TO_TICKS(1800)); // Anti-brute force timing defense
        return AuthResult::INVALID_CREDENTIALS;
    }

    reset_failed_attempts();
    out_role = role;

    uint32_t now = get_millis();

    // 1. Ekstrak IP client
    uint32_t client_ip = 0;
    if (req) {
        int fd = httpd_req_to_sockfd(req);
        if (fd >= 0) {
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
                client_ip = addr.sin_addr.s_addr;
            }
        }
    }

    // 2. Ekstrak existing token dari Cookie jika ada (Re-login dari browser yang sama)
    char existing_token[TOKEN_LEN + 1] = {0};
    if (req) {
        char cookie_buf[256];
        if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) == ESP_OK) {
            const char *pos = strstr(cookie_buf, "token=");
            if (pos) {
                pos += 6;
                const char *end = strchr(pos, ';');
                size_t len = end ? static_cast<size_t>(end - pos) : strlen(pos);
                if (len == TOKEN_LEN) {
                    memcpy(existing_token, pos, TOKEN_LEN);
                    existing_token[TOKEN_LEN] = '\0';
                }
            }
        }
    }

    // 3. Bersihkan sesi kedaluwarsa (> 15 menit)
    for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
        if (s_sessions[i].active && (now - s_sessions[i].last_activity_ms > SESSION_TIMEOUT_MS)) {
            s_sessions[i].active = false;
            s_sessions[i].token[0] = '\0';
            s_sessions[i].client_ip = 0;
        }
    }

    // 4. Cek apakah klien ini sudah memiliki sesi aktif (Re-login via token ATAU via IP yang sama)
    for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
        if (s_sessions[i].active) {
            bool matches_token = (existing_token[0] != '\0' && strcmp(s_sessions[i].token, existing_token) == 0);
            bool matches_ip = (client_ip != 0 && s_sessions[i].client_ip == client_ip);
            if (matches_token || matches_ip) {
                // Perbarui sesi yang ada tanpa memakan kuota baru
                char new_token[TOKEN_LEN + 1];
                generate_token(new_token, sizeof(new_token));
                strlcpy(s_sessions[i].token, new_token, sizeof(s_sessions[i].token));
                s_sessions[i].role = role;
                if (client_ip != 0) {
                    s_sessions[i].client_ip = client_ip;
                }
                s_sessions[i].last_activity_ms = now;

                if (out_token && max_len > TOKEN_LEN) {
                    strlcpy(out_token, new_token, max_len);
                }
                return AuthResult::SUCCESS;
            }
        }
    }

    // 4. Hitung jumlah sesi aktif saat ini
    size_t active_count = 0;
    for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
        if (s_sessions[i].active) {
            active_count++;
        }
    }

    // Jika sudah mencapai batas maksimal 2 perangkat aktif -> Tolak perangkat ke-3
    if (active_count >= MAX_CONCURRENT_SESSIONS) {
        return AuthResult::SESSION_LIMIT_REACHED;
    }

    // 5. Cari slot kosong
    size_t target_idx = MAX_CONCURRENT_SESSIONS;
    for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
        if (!s_sessions[i].active) {
            target_idx = i;
            break;
        }
    }

    if (target_idx >= MAX_CONCURRENT_SESSIONS) {
        return AuthResult::SESSION_LIMIT_REACHED;
    }

    char new_token[TOKEN_LEN + 1];
    generate_token(new_token, sizeof(new_token));

    strlcpy(s_sessions[target_idx].token, new_token, sizeof(s_sessions[target_idx].token));
    s_sessions[target_idx].client_ip = client_ip;
    s_sessions[target_idx].role = role;
    s_sessions[target_idx].last_activity_ms = now;
    s_sessions[target_idx].active = true;

    if (out_token && max_len > TOKEN_LEN) {
        strlcpy(out_token, new_token, max_len);
    }

    return AuthResult::SUCCESS;
}

void invalidate(const char *token) {
    if (!token || token[0] == '\0') return;
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && (strcmp(s_sessions[i].token, token) == 0)) {
            s_sessions[i].active = false;
            s_sessions[i].token[0] = '\0';
            s_sessions[i].client_ip = 0;
        }
    }
}

} // namespace Iotaru::Web::Session
