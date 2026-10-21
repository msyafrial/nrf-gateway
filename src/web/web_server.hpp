#pragma once

#include <esp_http_server.h>
#include <cstdint>

namespace Iotaru::Web::Server {

bool initStorage();
bool init();
void stop();
bool is_filesystem_ready();
httpd_handle_t get_handle();

// Filesystem OTA Synchronization & Concurrency Guards
void mark_filesystem_unmounted();
bool remountStorage();
bool acquire_file_read();
void release_file_read();
int get_active_file_reads();
void set_maintenance_mode(bool enable);
bool is_in_maintenance();

// Broadcast a message to all active authenticated WebSocket clients
void broadcast_ws_text(const char *payload);

// Broadcast telemetry and recent beacon frames to WebSocket test subscribers
void broadcast_live_ws_stats();

int get_active_ws_subscribers();
void set_active_ws_subscribers(int count);

} // namespace Iotaru::Web::Server
