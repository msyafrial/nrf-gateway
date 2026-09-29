#pragma once

#include "app_types.hpp"
#include <esp_http_server.h>
#include <cstdint>
#include <cstddef>

namespace Iotaru::Web::Session {

constexpr size_t MAX_CONCURRENT_SESSIONS = 2; // Batas maksimal 2 perangkat login
constexpr size_t MAX_SESSIONS = MAX_CONCURRENT_SESSIONS;
constexpr size_t TOKEN_LEN = 32;
constexpr uint32_t SESSION_TIMEOUT_MS = 900000; // Tepat 15 Menit (900 detik)

enum class AuthResult {
    SUCCESS,
    INVALID_CREDENTIALS,
    SESSION_LIMIT_REACHED,
    RATE_LIMITED
};

struct SessionEntry {
    char token[TOKEN_LEN + 1];
    uint32_t client_ip;
    UserRole role;
    uint32_t last_activity_ms;
    bool active;
};

void init();
UserRole validate(httpd_req_t *req, char *out_token = nullptr, size_t max_len = 0);
AuthResult authenticate(httpd_req_t *req, const char *user, const char *pass, UserRole &out_role, char *out_token, size_t max_len);
void invalidate(const char *token);
void touch(const char *token);
size_t get_active_count();
bool is_rate_limited();
void record_failed_attempt();
void reset_failed_attempts();

} // namespace Iotaru::Web::Session
