#include "ota_service.hpp"
#include "app_config.hpp"
#include "bsp_led.hpp"
#include "web_server.hpp"
#include <esp_littlefs.h>
#include <esp_timer.h>
#include <esp_app_format.h>
#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <sys/stat.h>

static const char *TAG = "OTA";

namespace Iotaru::Web::Ota {

static OtaStatus s_status = {};
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target_partition = nullptr;
static constexpr uint32_t OTA_INACTIVITY_TIMEOUT_MS = 10000;

static std::atomic<bool> s_ota_in_progress{false};
static bool s_flash_dirty = false;
static size_t s_erased_bytes = 0;

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
    s_ota_in_progress.store(false, std::memory_order_seq_cst);
    s_flash_dirty = false;
    s_erased_bytes = 0;
}

bool is_in_progress() {
    return s_ota_in_progress.load(std::memory_order_seq_cst);
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
    if (!s_status.in_progress && !s_ota_in_progress.load(std::memory_order_seq_cst)) return;

    if (s_status.target == TargetType::Firmware && s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    } else if (s_status.target == TargetType::Filesystem) {
        if (!s_flash_dirty) {
            ESP_LOGI(TAG, "OTA Filesystem dibatalkan sebelum flash disentuh. Memulihkan LittleFS...");
            Server::remountStorage();
        } else {
            ESP_LOGW(TAG, "OTA Filesystem dibatalkan saat flash sudah terisi parsial. Menahan Emergency Mode.");
            Server::mark_filesystem_unmounted();
        }
        Server::set_maintenance_mode(false);
    }

    s_status.in_progress = false;
    s_status.success = false;
    s_ota_in_progress.store(false, std::memory_order_seq_cst);
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
    // Atomic check-and-set untuk pencegahan race condition sesi bersamaan
    if (s_ota_in_progress.exchange(true, std::memory_order_seq_cst)) {
        snprintf(out_err, err_len, "Update sedang berlangsung dari sesi lain (409 Conflict)!");
        return false;
    }

    // Reset pelacak flash di baris paling atas
    s_flash_dirty = false;
    s_erased_bytes = 0;

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
        snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
        s_status.in_progress = false;
        s_ota_in_progress.store(false, std::memory_order_seq_cst);
        return false;
    }

    if (target == TargetType::Firmware) {
        if (chunk_len < 112) {
            snprintf(out_err, err_len, "Chunk pertama terlalu kecil (%zu B) untuk validasi header firmware!", chunk_len);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        // Validasi Model dan Versi Board dari esp_app_desc_t pada offset 0x20 jika ada metadata
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
                    snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
                    s_status.in_progress = false;
                    s_ota_in_progress.store(false, std::memory_order_seq_cst);
                    return false;
                }
                if (strcmp(cur_board_ver, "EMPTY") != 0 && strcmp(incoming_board_ver, cur_board_ver) != 0) {
                    snprintf(out_err, err_len, "Ditolak: Versi board ('%s') tidak kompatibel dengan board fisik ('%s')!",
                             incoming_board_ver, cur_board_ver);
                    snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
                    s_status.in_progress = false;
                    s_ota_in_progress.store(false, std::memory_order_seq_cst);
                    return false;
                }
            }
        }

        s_target_partition = esp_ota_get_next_update_partition(nullptr);
        if (!s_target_partition) {
            snprintf(out_err, err_len, "Gagal menemukan partisi target OTA update!");
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        if (total_size > s_target_partition->size) {
            snprintf(out_err, err_len, "Ukuran firmware (%u B) melebihi kapasitas partisi OTA (%u B)!",
                     (unsigned int)total_size, (unsigned int)s_target_partition->size);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        size_t ota_size = (total_size > 0) ? total_size : OTA_SIZE_UNKNOWN;
        esp_err_t err = esp_ota_begin(s_target_partition, ota_size, &s_ota_handle);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menginisialisasi partisi OTA flash (0x%x).", err);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }
    } else {
        // TARGET FILESYSTEM:
        s_target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
        if (!s_target_partition) {
            snprintf(out_err, err_len, "Gagal menemukan partisi 'storage' untuk LittleFS!");
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        // Cek kapasitas: Tidak boleh melampaui ukuran partisi (1 MB)
        if (total_size > s_target_partition->size) {
            snprintf(out_err, err_len, "Ukuran LittleFS (%u B) melebihi kapasitas partisi storage (%u B)!",
                     (unsigned int)total_size, (unsigned int)s_target_partition->size);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        // Cek signature LittleFS: Wajib minimal 16 byte dan teks "littlefs" pada offset 0x08
        if (chunk_len < 16 || memcmp(&first_chunk[8], "littlefs", 8) != 0) {
            snprintf(out_err, err_len, "Error: Berkas bukan citra partisi LittleFS valid (signature 'littlefs' tidak cocok)!");
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        // Masuk Maintenance Mode untuk drain active file reads
        Server::set_maintenance_mode(true);

        int wait_ms = 0;
        while (Server::get_active_file_reads() > 0 && wait_ms < 500) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_ms += 10;
        }

        if (Server::get_active_file_reads() > 0) {
            // BATALKAN SECARA BERSIH: Jangan panggil abort() karena VFS belum di-unmount!
            Server::set_maintenance_mode(false);
            snprintf(out_err, err_len, "Filesystem sedang sibuk melayani unduhan web. Coba beberapa saat lagi.");
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        // Lakukan unregister VFS
        esp_err_t unreg_err = esp_vfs_littlefs_unregister("storage");
        if (unreg_err != ESP_OK) {
            // BATALKAN SECARA BERSIH: Jangan remountStorage() karena VFS belum ter-unmount!
            ESP_LOGE(TAG, "esp_vfs_littlefs_unregister gagal: 0x%x", unreg_err);
            Server::set_maintenance_mode(false);
            snprintf(out_err, err_len, "Gagal unmount VFS storage (0x%x).", unreg_err);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            s_status.in_progress = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            return false;
        }

        // Unmount sukses
        Server::mark_filesystem_unmounted();
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
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            abort();
            return false;
        }
    } else {
        size_t target_end = current_offset + len;
        if (target_end > s_target_partition->size) {
            snprintf(out_err, err_len, "Data LittleFS melampaui kapasitas partisi storage 1MB!");
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            abort();
            return false;
        }

        size_t aligned_end = (target_end + 4095) & ~4095;
        if (aligned_end > s_erased_bytes) {
            size_t erase_start = s_erased_bytes;
            size_t erase_len = aligned_end - s_erased_bytes;
            s_flash_dirty = true; // Ditandai dirty tepat sebelum erase pertama dieksekusi
            esp_err_t err = esp_partition_erase_range(s_target_partition, erase_start, erase_len);
            if (err != ESP_OK) {
                snprintf(out_err, err_len, "Gagal erase sektor flash LittleFS (0x%x).", err);
                snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
                abort();
                return false;
            }
            s_erased_bytes = aligned_end;
        }

        esp_err_t err = esp_partition_write(s_target_partition, current_offset, data, len);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menulis chunk LittleFS ke flash (0x%x).", err);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            abort();
            return false;
        }
    }

    s_status.written_bytes = current_offset + len;
    return true;
}

bool finalize(char *out_err, size_t err_len) {
    if (!s_status.in_progress) return false;

    // Micro-Hardening: Truncated stream validation
    if (s_status.total_expected > 0 && s_status.written_bytes != s_status.total_expected) {
        snprintf(out_err, err_len, "Data LittleFS tidak lengkap (hanya %u dari %u byte)!",
                 (unsigned int)s_status.written_bytes, (unsigned int)s_status.total_expected);
        snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
        abort();
        return false;
    }

    if (s_status.target == TargetType::Firmware) {
        esp_err_t err = esp_ota_end(s_ota_handle);
        s_ota_handle = 0;
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Validasi akhir partisi OTA gagal (0x%x).", err);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            abort();
            return false;
        }

        err = esp_ota_set_boot_partition(s_target_partition);
        if (err != ESP_OK) {
            snprintf(out_err, err_len, "Gagal menetapkan partisi boot baru (0x%x).", err);
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);
            abort();
            return false;
        }
    } else {
        // Verifikasi Integritas Filesystem Baru
        esp_vfs_littlefs_conf_t conf = {};
        conf.base_path = "/storage_verify";
        conf.partition_label = "storage";
        conf.format_if_mount_failed = false; // WAJIB FALSE: Jangan auto-format jika citra rusak!
        conf.read_only = 1;                  // Read-only saat verifikasi
        conf.dont_mount = 0;
        conf.grow_on_mount = 0;

        esp_err_t m_ret = esp_vfs_littlefs_register(&conf);
        bool verify_ok = false;
        if (m_ret == ESP_OK) {
            struct stat st_idx = {}, st_js = {};
            if (stat("/storage_verify/index.html", &st_idx) == 0 && st_idx.st_size > 512 &&
                stat("/storage_verify/assets/js/app.js", &st_js) == 0 && st_js.st_size > 512) {
                verify_ok = true;
            }
            esp_vfs_littlefs_unregister("storage");
        }

        if (!verify_ok) {
            ESP_LOGE(TAG, "Verifikasi integritas LittleFS GAGAL! Citra rusak atau index.html tidak ditemukan.");
            snprintf(out_err, err_len, "Verifikasi citra LittleFS gagal: struktur berkas rusak atau index.html tidak ditemukan!");
            snprintf(s_status.error_msg, sizeof(s_status.error_msg), "%s", out_err);

            // Reset komprehensif state agar Emergency Recovery siap menerima upload ulang
            Server::mark_filesystem_unmounted();
            Server::set_maintenance_mode(false);
            s_status.in_progress = false;
            s_status.success = false;
            s_ota_in_progress.store(false, std::memory_order_seq_cst);
            Bsp::LedManager::setOtaMode(false);
            return false; // TOLAK & JANGAN REBOOT
        }

        ESP_LOGI(TAG, "Verifikasi LittleFS SUKSES! Partisi siap digunakan.");
        Server::set_maintenance_mode(false);
    }

    s_status.in_progress = false;
    s_status.success = true;
    s_ota_in_progress.store(false, std::memory_order_seq_cst);
    Bsp::LedManager::setOtaMode(false);
    return true;
}

} // namespace Iotaru::Web::Ota
