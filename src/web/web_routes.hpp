#pragma once

#include <esp_http_server.h>

namespace Iotaru::Web::Routes {

esp_err_t register_all(httpd_handle_t server);

} // namespace Iotaru::Web::Routes
