#include "ota_service.hpp"
#include "app_config.hpp"
#include "bsp_led.hpp"
#include <esp_littlefs.h>
#include <esp_timer.h>
#include <esp_app_format.h>
#include <cstring>
#include <cstdio>

namespace Iotaru::Web::Ota {

static OtaStatus s_status = {};
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target_partition = nullptr;
static constexpr uint32_t OTA_INACTIVITY_TIMEOUT_MS = 10000;

static inline uint32_t get_millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void init() {
    s_status.in_progress = false;
    s_status.success = false;
    s_status.total_expected = 0;
    s_status.written_bytes = 0;
    s_status.last_activity_ms = 0;
    s_status.error_msg[0] = '\0';
    s_ota_handle = 0;
    s_target_partition = nullptr;
}

bool is_in_progress() {
    return s_status.in_progress;
}

bool is_success() {
    return s_status.success;
}

const char* get_error_message() {
    return s_status.error_msg;
}

void mark_app_valid() {
    esp_ota_mark_app_valid_cancel_rollback();
}

void abort() {
    if (!s_status.in_progress) return;

    if (s_status.target == TargetType::Firmware && s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    s_status.in_progress = false;
    s_status.success = false;
    Bsp::LedManager::setOtaMode(false);
}

void check_watchdog() {
    if (s_status.in_progress && (get_millis() - s_status.last_activity_ms > OTA_INACTIVITY_TIMEOUT_MS)) {
        snprintf(s_status.error_msg, sizeof(s_status.error_msg),
                 "Upload timeout: koneksi terputus (10 detik inaktivitas).");
        abort();
    }
}

bool begin(TargetType target, size_t total_size, const uint8_t *first_chunk, size_t chunk_len, char *out_err, size_t err_len) {
    if (s_status.in_progress) {
        snprintf(out_err, err_len, "Update sedang berlangsung dari sesi lain (409 Conflict)!");
        return false;
    }

    s_status.in_progress = true;
    s_status.success = false;
    s_status.target = target;
    s_status.total_expected = total_size;
    s_status.written_bytes = 0;
    s_status.last_activity_ms = get_millis();
    s_status.error_msg[0] = '\0';
    s_ota_handle = 0;
    s_target_partition = nullptr;

    // 1. Magic check on byte 0..1: Partition Table rejection (0x50AA)
    if (chunk_len >= 2 && first_chunk[0] == 0xAA && first_chunk[1] == 0x50) {
        snprintf(out_err, err_len, "File terdeteksi sebagai Tabel Partisi ESP32 (partitions.bin)! Ditolak.");
        strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
        abort();
        return false;
    }

    if (target == TargetType::Firmware) {
        // Validate Model and Board Revision from esp_app_desc_t at offset 0x20 if metadata is present
        if (chunk_len >= 112) {
            bool is_app_desc = (first_chunk[32] == 0x32 && first_chunk[33] == 0x54 &&
                                first_chunk[34] == 0xCD && first_chunk[35] == 0xAB);
            if (is_app_desc) {
                const auto *incoming_app = reinterpret_cast<const esp_app_desc_t *>(&first_chunk[0x20]);
                const char *incoming_proj = incoming_app->project_name;
                const char *at_sign = strchr(incoming_proj, '@');
                if (at_sign) {
                    char incoming_model[32] = {0};
                    char incoming_board_ver[32] = {0};
                    size_t m_len = static_cast<size_t>(at_sign - incoming_proj);
                    if (m_len < sizeof(incoming_model)) {
                        memcpy(incoming_model, incoming_proj, m_len);
                        incoming_model[m_len] = '\0';
                    }
                    strncpy(incoming_board_ver, at_sign + 1, sizeof(incoming_board_ver) - 1);

                    const char *cur_model = (g_config.device_model[0] != '\0') ? g_config.device_model : "EMPTY";
                    const char *cur_board_ver = (g_config.device_version[0] != '\0') ? g_config.device_version : "v1.0.0";

                    if (strcmp(cur_model, "EMPTY") != 0 && strcmp(incoming_model, cur_model) != 0) {
                        snprintf(out_err, err_len, "Ditolak: Model firmware ('%s') tidak sesuai model perangkat ('%s')!",
                                 incoming_model, cur_model);
                        strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
                        abort();
                        return false;
                    }
                    if (strcmp(cur_board_ver, "EMPTY") != 0 && strcmp(incoming_board_ver, cur_board_ver) != 0) {
                        snprintf(out_err, err_len, "Ditolak: Versi board ('%s') tidak kompatibel dengan board fisik ('%s')!",
                                 incoming_board_ver, cur_board_ver);
                        strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
                        abort();
                        return false;
                    }
                }
            }
        }

        s_target_partition = esp_ota_get_next_update_partition(nullptr);
        if (!s_target_partition) {
            snprintf(out_err, err_len, "Gagal menemukan partisi target OTA update!");
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }

        size_t ota_size = (total_size > 0) ? total_size : OTA_SIZE_UNKNOWN;
        esp_err_t err = esp_ota_begin(s_target_partition, ota_size, &s_ota_handle);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menginisialisasi partisi OTA flash (0x%x).", err);
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }
    } else {
        // Filesystem (LittleFS)
        if (chunk_len >= 16 && memcmp(&first_chunk[8], "littlefs", 8) != 0) {
            snprintf(out_err, err_len, "Error: Berkas bukan citra partisi LittleFS valid (signature 'littlefs' tidak cocok)!");
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }

        s_target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
        if (!s_target_partition) {
            snprintf(out_err, err_len, "Gagal menemukan partisi 'storage' untuk LittleFS!");
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }

        // Unmount LittleFS sebelum melakukan raw flash erase agar VFS driver tidak korup (VULN-MEM-04)
        esp_vfs_littlefs_unregister("storage");

        // Erase range needed for filesystem
        size_t erase_len = (total_size > 0) ? total_size : s_target_partition->size;
        erase_len = (erase_len + 4095) & ~4095;
        if (erase_len > s_target_partition->size) {
            erase_len = s_target_partition->size;
        }

        esp_err_t err = esp_partition_erase_range(s_target_partition, 0, erase_len);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menghapus partisi flash filesystem (0x%x).", err);
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }
    }

    Bsp::LedManager::setOtaMode(true);
    return true;
}

bool write_chunk(const uint8_t *data, size_t len, size_t current_offset, bool is_final, char *out_err, size_t err_len) {
    if (!s_status.in_progress || !data || len == 0) return false;

    s_status.last_activity_ms = get_millis();

    if (s_status.target == TargetType::Firmware) {
        esp_err_t err = esp_ota_write(s_ota_handle, data, len);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menulis chunk OTA ke flash (0x%x).", err);
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }
    } else {
        esp_err_t err = esp_partition_write(s_target_partition, current_offset, data, len);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menulis chunk LittleFS ke flash (0x%x).", err);
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }
    }

    s_status.written_bytes = current_offset + len;
    return true;
}

bool finalize(char *out_err, size_t err_len) {
    if (!s_status.in_progress) return false;

    if (s_status.target == TargetType::Firmware) {
        esp_err_t err = esp_ota_end(s_ota_handle);
        s_ota_handle = 0;
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Validasi akhir partisi OTA gagal (0x%x).", err);
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }

        err = esp_ota_set_boot_partition(s_target_partition);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menetapkan partisi boot baru (0x%x).", err);
            strncpy(s_status.error_msg, out_err, sizeof(s_status.error_msg) - 1);
            abort();
            return false;
        }
    }

    s_status.in_progress = false;
    s_status.success = true;
    Bsp::LedManager::setOtaMode(false);
    return true;
}

} // namespace Iotaru::Web::Ota
