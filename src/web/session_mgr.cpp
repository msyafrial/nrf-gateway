#include "session_mgr.hpp"
#include "app_config.hpp"
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cstring>
#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>

namespace Iotaru::Web::Session {

static SemaphoreHandle_t s_sessionMutex = nullptr;
static SessionEntry s_sessions[MAX_SESSIONS] = {};

struct FailedIpEntry {
    uint32_t ip;
    uint8_t count;
    uint32_t last_attempt_ms;
};
constexpr size_t MAX_FAILED_IPS = 6;
static FailedIpEntry s_failedIps[MAX_FAILED_IPS] = {};

static inline uint32_t get_millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static uint32_t extract_client_ip(httpd_req_t *req) {
    if (!req) return 0;
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return 0;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) == 0) {
        return addr.sin_addr.s_addr;
    }
    return 0;
}

void init() {
    if (s_sessionMutex == nullptr) {
        s_sessionMutex = xSemaphoreCreateMutex();
    }
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        s_sessions[i].active = false;
        s_sessions[i].token[0] = '\0';
        s_sessions[i].client_ip = 0;
        s_sessions[i].role = UserRole::NONE;
        s_sessions[i].last_activity_ms = 0;
    }
    for (size_t i = 0; i < MAX_FAILED_IPS; i++) {
        s_failedIps[i].ip = 0;
        s_failedIps[i].count = 0;
        s_failedIps[i].last_attempt_ms = 0;
    }
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

static void generate_token(char *out_buf, size_t max_len) {
    if (max_len < TOKEN_LEN + 1) return;
    for (int i = 0; i < 8; i++) {
        uint32_t r = esp_random();
        snprintf(out_buf + (i * 8), 9, "%08x", static_cast<unsigned int>(r));
    }
    out_buf[TOKEN_LEN] = '\0';
}

bool is_rate_limited(httpd_req_t *req) {
    uint32_t ip = extract_client_ip(req);
    if (ip == 0) {
        return false;
    }
    uint32_t now = get_millis();

    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool limited = false;
        for (size_t i = 0; i < MAX_FAILED_IPS; i++) {
            if (s_failedIps[i].ip == ip) {
                if (s_failedIps[i].count >= 5 && (now - s_failedIps[i].last_attempt_ms < 60000)) {
                    limited = true;
                }
                break;
            }
        }
        xSemaphoreGive(s_sessionMutex);
        return limited;
    }
    return false;
}

void record_failed_attempt(httpd_req_t *req) {
    uint32_t ip = extract_client_ip(req);
    if (ip == 0) return;
    uint32_t now = get_millis();

    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        int targetSlot = -1;
        int oldestSlot = 0;
        uint32_t oldestTime = 0xFFFFFFFF;

        for (size_t i = 0; i < MAX_FAILED_IPS; i++) {
            if (s_failedIps[i].ip == ip && ip != 0) {
                targetSlot = static_cast<int>(i);
                break;
            }
            if (s_failedIps[i].ip == 0 && targetSlot == -1) {
                targetSlot = static_cast<int>(i);
            }
            if (s_failedIps[i].last_attempt_ms < oldestTime) {
                oldestTime = s_failedIps[i].last_attempt_ms;
                oldestSlot = static_cast<int>(i);
            }
        }

        if (targetSlot == -1) {
            targetSlot = oldestSlot; // Evict entri tertua
            s_failedIps[targetSlot].count = 0;
        }

        s_failedIps[targetSlot].ip = ip;
        s_failedIps[targetSlot].count++;
        s_failedIps[targetSlot].last_attempt_ms = now;

        xSemaphoreGive(s_sessionMutex);
    }
}

void reset_failed_attempts(httpd_req_t *req) {
    uint32_t ip = extract_client_ip(req);
    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (ip != 0) {
            for (size_t i = 0; i < MAX_FAILED_IPS; i++) {
                if (s_failedIps[i].ip == ip) {
                    s_failedIps[i].ip = 0;
                    s_failedIps[i].count = 0;
                    s_failedIps[i].last_attempt_ms = 0;
                    break;
                }
            }
        }
        xSemaphoreGive(s_sessionMutex);
    }
}

UserRole validate(httpd_req_t *req, char *out_token, size_t max_len, bool update_sliding) {
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

    // 3. Catatan: URL query parameter (?token=) sengaja tidak didukung untuk mencegah
    // kebocoran token sesi melalui browser history, server/proxy access log, dan Referer header.
    if (token[0] == '\0') {
        return UserRole::NONE;
    }

    if (out_token && max_len > TOKEN_LEN) {
        strlcpy(out_token, token, max_len);
    }

    UserRole result = UserRole::NONE;
    uint32_t now = get_millis();

    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (size_t i = 0; i < MAX_SESSIONS; i++) {
            if (s_sessions[i].active && constant_time_eq(s_sessions[i].token, token)) {
                // Check 15-minute sliding expiration (SESSION_TIMEOUT_MS = 900000 ms)
                if (now - s_sessions[i].last_activity_ms > SESSION_TIMEOUT_MS) {
                    s_sessions[i].active = false;
                    s_sessions[i].token[0] = '\0';
                    s_sessions[i].client_ip = 0;
                    result = UserRole::NONE;
                } else {
                    if (update_sliding) {
                        // Sliding reset: perbarui waktu aktivitas
                        s_sessions[i].last_activity_ms = now;
                    }
                    result = s_sessions[i].role;
                }
                break;
            }
        }
        xSemaphoreGive(s_sessionMutex);
    }

    return result;
}

void touch(const char *token) {
    if (!token || token[0] == '\0') return;
    uint32_t now = get_millis();
    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (size_t i = 0; i < MAX_SESSIONS; i++) {
            if (s_sessions[i].active && constant_time_eq(s_sessions[i].token, token)) {
                s_sessions[i].last_activity_ms = now;
                break;
            }
        }
        xSemaphoreGive(s_sessionMutex);
    }
}

size_t get_active_count() {
    uint32_t now = get_millis();
    size_t count = 0;
    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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
        xSemaphoreGive(s_sessionMutex);
    }
    return count;
}

AuthResult authenticate(httpd_req_t *req, const char *user, const char *pass, UserRole &out_role, char *out_token, size_t max_len) {
    if (is_rate_limited(req)) {
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
        record_failed_attempt(req);
        // vTaskDelay(1800) DIHAPUS untuk mencegah DoS blocking pada single-threaded web server (VULN-SEC-05)
        return AuthResult::INVALID_CREDENTIALS;
    }

    reset_failed_attempts(req);
    out_role = role;

    uint32_t now = get_millis();
    uint32_t client_ip = extract_client_ip(req);

    // Ekstrak existing token dari Cookie jika ada (Re-login dari browser yang sama)
    char existing_token[TOKEN_LEN + 1] = {0};
    if (req) {
        size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");
        if (cookie_len > 0 && cookie_len < 256) {
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
    }

    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Bersihkan sesi kedaluwarsa (> 15 menit)
        for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
            if (s_sessions[i].active && (now - s_sessions[i].last_activity_ms > SESSION_TIMEOUT_MS)) {
                s_sessions[i].active = false;
                s_sessions[i].token[0] = '\0';
                s_sessions[i].client_ip = 0;
            }
        }

        // Cek apakah klien ini sudah memiliki sesi aktif (HANYA via token valid, BUKAN via IP NAT) (VULN-SEC-06)
        for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
            if (s_sessions[i].active) {
                bool matches_token = (existing_token[0] != '\0' && constant_time_eq(s_sessions[i].token, existing_token));
                if (matches_token) {
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
                    xSemaphoreGive(s_sessionMutex);
                    return AuthResult::SUCCESS;
                }
            }
        }

        // Hitung jumlah sesi aktif saat ini
        size_t active_count = 0;
        for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
            if (s_sessions[i].active) {
                active_count++;
            }
        }

        // Jika sudah mencapai batas maksimal 2 perangkat aktif -> Tolak perangkat ke-3
        if (active_count >= MAX_CONCURRENT_SESSIONS) {
            xSemaphoreGive(s_sessionMutex);
            return AuthResult::SESSION_LIMIT_REACHED;
        }

        // Cari slot kosong
        size_t target_idx = MAX_CONCURRENT_SESSIONS;
        for (size_t i = 0; i < MAX_CONCURRENT_SESSIONS; i++) {
            if (!s_sessions[i].active) {
                target_idx = i;
                break;
            }
        }

        if (target_idx >= MAX_CONCURRENT_SESSIONS) {
            xSemaphoreGive(s_sessionMutex);
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

        xSemaphoreGive(s_sessionMutex);
        return AuthResult::SUCCESS;
    }

    return AuthResult::SESSION_LIMIT_REACHED;
}

void invalidate(const char *token) {
    if (!token || token[0] == '\0') return;
    if (s_sessionMutex && xSemaphoreTake(s_sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (size_t i = 0; i < MAX_SESSIONS; i++) {
            if (s_sessions[i].active && constant_time_eq(s_sessions[i].token, token)) {
                s_sessions[i].active = false;
                s_sessions[i].token[0] = '\0';
                s_sessions[i].client_ip = 0;
            }
        }
        xSemaphoreGive(s_sessionMutex);
    }
}

} // namespace Iotaru::Web::Session
