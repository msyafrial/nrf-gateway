#pragma once

#include <esp_http_server.h>
#include <string_view>

namespace Iotaru::Web::Template {

void init();
const char* get_ui_version();

// Stream a file from LittleFS with Zero-FOUC template placeholder hydration
esp_err_t stream_template_file(httpd_req_t *req, const char *filepath);

// Stream emergency recovery page from flash PROGMEM
esp_err_t stream_emergency_page(httpd_req_t *req);

// Get placeholder value by key
bool get_placeholder_value(std::string_view key, char *out_buf, size_t max_len);

} // namespace Iotaru::Web::Template
