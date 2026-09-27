#pragma once

#include "app_types.hpp"
#include <esp_http_server.h>
#include <cstdint>
#include <cstddef>

namespace Iotaru::Web::Session {

constexpr size_t MAX_SESSIONS = 8;
constexpr size_t TOKEN_LEN = 32;

struct SessionEntry {
    char token[TOKEN_LEN + 1];
    UserRole role;
    uint32_t last_activity_ms;
    bool active;
};

void init();
UserRole validate(httpd_req_t *req, char *out_token = nullptr, size_t max_len = 0);
bool authenticate(const char *user, const char *pass, UserRole &out_role, char *out_token, size_t max_len);
void invalidate(const char *token);
bool is_rate_limited();
void record_failed_attempt();
void reset_failed_attempts();

} // namespace Iotaru::Web::Session
