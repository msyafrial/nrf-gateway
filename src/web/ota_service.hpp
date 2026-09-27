#pragma once

#include <cstdint>
#include <cstddef>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace Iotaru::Web::Ota {

enum class TargetType {
    Firmware,
    Filesystem
};

struct OtaStatus {
    bool in_progress;
    bool success;
    TargetType target;
    size_t total_expected;
    size_t written_bytes;
    uint32_t last_activity_ms;
    char error_msg[128];
};

void init();
bool is_in_progress();
bool is_success();
const char* get_error_message();
void check_watchdog();
void abort();

bool begin(TargetType target, size_t total_size, const uint8_t *first_chunk, size_t chunk_len, char *out_err, size_t err_len);
bool write_chunk(const uint8_t *data, size_t len, size_t current_offset, bool is_final, char *out_err, size_t err_len);
bool finalize(char *out_err, size_t err_len);

// Rollback validation
void mark_app_valid();

} // namespace Iotaru::Web::Ota
