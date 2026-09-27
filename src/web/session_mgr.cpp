#include "session_mgr.hpp"
#include "app_config.hpp"
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>

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

    if (token[0] == '\0') {
        return UserRole::NONE;
    }

    if (out_token && max_len > TOKEN_LEN) {
        strncpy(out_token, token, max_len);
    }

    uint32_t now = get_millis();
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && (strcmp(s_sessions[i].token, token) == 0)) {
            // Check 1-hour expiration (3600000 ms)
            if (now - s_sessions[i].last_activity_ms > 3600000) {
                s_sessions[i].active = false;
                return UserRole::NONE;
            }
            s_sessions[i].last_activity_ms = now;
            return s_sessions[i].role;
        }
    }

    return UserRole::NONE;
}

bool authenticate(const char *user, const char *pass, UserRole &out_role, char *out_token, size_t max_len) {
    if (!user || !pass) return false;

    UserRole role = UserRole::NONE;
    if (strcmp(user, g_config.admin_user) == 0 && strcmp(pass, g_config.admin_pass) == 0) {
        role = UserRole::ADMIN;
    } else if (strcmp(user, g_config.user_user) == 0 && strcmp(pass, g_config.user_pass) == 0) {
        role = UserRole::TEKNISI;
    }

    if (role == UserRole::NONE) {
        record_failed_attempt();
        vTaskDelay(pdMS_TO_TICKS(1800)); // Anti-brute force timing defense
        return false;
    }

    reset_failed_attempts();
    out_role = role;

    char new_token[TOKEN_LEN + 1];
    generate_token(new_token, sizeof(new_token));

    // Register session into oldest or inactive slot
    uint32_t oldest_time = 0xFFFFFFFF;
    size_t replace_idx = 0;

    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) {
            replace_idx = i;
            break;
        }
        if (s_sessions[i].last_activity_ms < oldest_time) {
            oldest_time = s_sessions[i].last_activity_ms;
            replace_idx = i;
        }
    }

    strncpy(s_sessions[replace_idx].token, new_token, sizeof(s_sessions[replace_idx].token) - 1);
    s_sessions[replace_idx].role = role;
    s_sessions[replace_idx].last_activity_ms = get_millis();
    s_sessions[replace_idx].active = true;

    if (out_token && max_len > TOKEN_LEN) {
        strncpy(out_token, new_token, max_len);
    }

    return true;
}

void invalidate(const char *token) {
    if (!token || token[0] == '\0') return;
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && (strcmp(s_sessions[i].token, token) == 0)) {
            s_sessions[i].active = false;
            s_sessions[i].token[0] = '\0';
        }
    }
}

} // namespace Iotaru::Web::Session
