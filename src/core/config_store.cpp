#include "config_store.hpp"
#include "device_id.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

static const char *TAG = "CONFIG_STORE";

#ifndef CONFIG_FIRMWARE_VERSION
#define CONFIG_FIRMWARE_VERSION "v1.0.1"
#endif
#ifndef CONFIG_UI_VERSION
#define CONFIG_UI_VERSION "v1.0.0"
#endif
#ifndef CONFIG_DEVICE_MODEL
#define CONFIG_DEVICE_MODEL "SG-10"
#endif
#ifndef CONFIG_DEVICE_BOARD_VER
#define CONFIG_DEVICE_BOARD_VER "v1.0.0"
#endif

namespace Iotaru {

const char* FIRMWARE_VERSION = CONFIG_FIRMWARE_VERSION;
char g_deviceId[16] = "1a2b3c4d";
char g_uiVersion[16] = CONFIG_UI_VERSION;
AppConfig g_config = {};

void parseHexUUID(const char* hexStr, uint8_t* out16) {
    if (!hexStr || !out16) return;
    char clean[33] = {0};
    size_t cleanLen = 0;

    for (size_t i = 0; hexStr[i] != '\0' && cleanLen < 32; ++i) {
        char c = hexStr[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            clean[cleanLen++] = c;
        }
    }

    std::memset(out16, 0, 16);
    for (size_t i = 0; i < 16 && (i * 2 + 1) < cleanLen; ++i) {
        char byteStr[3] = { clean[i * 2], clean[i * 2 + 1], '\0' };
        out16[i] = (uint8_t)strtoul(byteStr, nullptr, 16);
    }
}

bool matchMajorFilter(uint16_t beaconMajor, std::string_view filterStr) {
    if (filterStr.empty()) return true;

    size_t start = 0;
    size_t len = filterStr.length();

    while (start < len) {
        while (start < len && (filterStr[start] == ',' || filterStr[start] == ' ' || filterStr[start] == ';')) {
            start++;
        }
        if (start >= len) break;

        size_t end = start;
        while (end < len && filterStr[end] != ',' && filterStr[end] != ' ' && filterStr[end] != ';') {
            end++;
        }

        std::string_view token = filterStr.substr(start, end - start);
        if (!token.empty()) {
            bool isNum = true;
            for (char c : token) {
                if (c < '0' || c > '9') {
                    isNum = false;
                    break;
                }
            }
            if (isNum) {
                char tmp[16] = {0};
                size_t cpyLen = std::min(token.length(), sizeof(tmp) - 1);
                std::memcpy(tmp, token.data(), cpyLen);
                long val = std::strtol(tmp, nullptr, 10);
                if (val >= 0 && val <= 65535 && (uint16_t)val == beaconMajor) {
                    return true;
                }
            }
        }
        start = end;
    }
    return false;
}

} // namespace Iotaru

namespace Iotaru::Core {

static void nvsGetStr(nvs_handle_t handle, const char* key, char* outBuf, size_t maxLen, const char* defVal) {
    size_t reqLen = maxLen;
    if (nvs_get_str(handle, key, outBuf, &reqLen) != ESP_OK || reqLen == 0) {
        strlcpy(outBuf, defVal, maxLen);
    }
}

static uint32_t nvsGetU32(nvs_handle_t handle, const char* key, uint32_t defVal) {
    uint32_t val = defVal;
    if (nvs_get_u32(handle, key, &val) != ESP_OK) {
        val = defVal;
    }
    return val;
}

static int32_t nvsGetI32(nvs_handle_t handle, const char* key, int32_t defVal) {
    int32_t val = defVal;
    if (nvs_get_i32(handle, key, &val) != ESP_OK) {
        val = defVal;
    }
    return val;
}

static void nvsCopyStr(nvs_handle_t src, nvs_handle_t dst, const char* key, const char* defVal) {
    char buf[128];
    nvsGetStr(src, key, buf, sizeof(buf), defVal);
    nvs_set_str(dst, key, buf);
}

static void nvsCopyU32(nvs_handle_t src, nvs_handle_t dst, const char* key, uint32_t defVal) {
    uint32_t val = nvsGetU32(src, key, defVal);
    nvs_set_u32(dst, key, val);
}

static void nvsCopyI32(nvs_handle_t src, nvs_handle_t dst, const char* key, int32_t defVal) {
    int32_t val = nvsGetI32(src, key, defVal);
    nvs_set_i32(dst, key, val);
}

esp_err_t ConfigStore::initNvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Partisi NVS korup/berubah versi, melakukan erase dan inisialisasi ulang...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void ConfigStore::initMetadata() {
    Iotaru::Utils::DeviceIdManager::generate(g_deviceId, sizeof(g_deviceId));
    ESP_LOGI(TAG, "Device Hardware ID (Anti-Clone): %s", g_deviceId);
}

void ConfigStore::initNvsConfiguration() {
    nvs_handle_t hSys;
    uint8_t cfgValid = 0;
    if (nvs_open("cfg_sys", NVS_READWRITE, &hSys) == ESP_OK) {
        nvs_get_u8(hSys, "cfg_valid", &cfgValid);
    }

    if (cfgValid == 1) {
        nvs_close(hSys);
        ESP_LOGI(TAG, "Konfigurasi operasional (cfg_*) valid & terverifikasi.");
        return;
    }

    ESP_LOGW(TAG, "=======================================================");
    ESP_LOGW(TAG, "Inisialisasi NVS Operasional (cfg_*) dari Master nvs.bin...");
    ESP_LOGW(TAG, "Status: Konfigurasi Baru / Pasca Reset / Proteksi Brownout");
    ESP_LOGW(TAG, "=======================================================");

    nvs_handle_t hMaster, hDest;

    // 1. Salin Master 'auth' -> 'cfg_auth' (atau inisialisasi default jika master tidak ada)
    bool hasMasterAuth = (nvs_open("auth", NVS_READONLY, &hMaster) == ESP_OK);
    if (nvs_open("cfg_auth", NVS_READWRITE, &hDest) == ESP_OK) {
        nvs_erase_all(hDest);
        if (hasMasterAuth) {
            nvsCopyStr(hMaster, hDest, "adm_user", "admin");
            nvsCopyStr(hMaster, hDest, "adm_pass", "admin");
            nvsCopyStr(hMaster, hDest, "tek_user", "user");
            nvsCopyStr(hMaster, hDest, "tek_pass", "user");
            nvs_close(hMaster);
        } else {
            nvs_set_str(hDest, "adm_user", "admin");
            nvs_set_str(hDest, "adm_pass", "admin");
            nvs_set_str(hDest, "tek_user", "user");
            nvs_set_str(hDest, "tek_pass", "user");
        }
        nvs_commit(hDest);
        nvs_close(hDest);
        ESP_LOGI(TAG, "[NVS 1/4] 'cfg_auth' OK (Master: %s).", hasMasterAuth ? "ADA" : "TIDAK ADA -> Default");
    }

    // 2. Salin Master 'network' -> 'cfg_net'
    bool hasMasterNet = (nvs_open("network", NVS_READONLY, &hMaster) == ESP_OK);
    if (nvs_open("cfg_net", NVS_READWRITE, &hDest) == ESP_OK) {
        nvs_erase_all(hDest);
        if (hasMasterNet) {
            nvsCopyU32(hMaster, hDest, "mode", 1); // Default: 1 (Ethernet)
            nvsCopyStr(hMaster, hDest, "w_ssid", "");
            nvsCopyStr(hMaster, hDest, "w_pass", "");
            nvsCopyU32(hMaster, hDest, "w_dhcp", 1);
            nvsCopyStr(hMaster, hDest, "w_ip", "192.168.1.100");
            nvsCopyStr(hMaster, hDest, "w_gw", "192.168.1.1");
            nvsCopyStr(hMaster, hDest, "w_sn", "255.255.255.0");
            nvsCopyStr(hMaster, hDest, "w_dns", "8.8.8.8");
            nvsCopyStr(hMaster, hDest, "w_dns2", "");
            nvsCopyU32(hMaster, hDest, "eth_dhcp", 1);
            nvsCopyStr(hMaster, hDest, "eth_ip", "192.168.1.150");
            nvsCopyStr(hMaster, hDest, "eth_gw", "192.168.1.1");
            nvsCopyStr(hMaster, hDest, "eth_sn", "255.255.255.0");
            nvsCopyStr(hMaster, hDest, "eth_dns", "8.8.8.8");
            nvsCopyStr(hMaster, hDest, "eth_dns2", "");
            nvsCopyStr(hMaster, hDest, "ap_pass", "12345678");
            nvs_close(hMaster);
        } else {
            nvs_set_u32(hDest, "mode", 1);
            nvs_set_str(hDest, "w_ssid", "");
            nvs_set_str(hDest, "w_pass", "");
            nvs_set_u32(hDest, "w_dhcp", 1);
            nvs_set_str(hDest, "w_ip", "192.168.1.100");
            nvs_set_str(hDest, "w_gw", "192.168.1.1");
            nvs_set_str(hDest, "w_sn", "255.255.255.0");
            nvs_set_str(hDest, "w_dns", "8.8.8.8");
            nvs_set_str(hDest, "w_dns2", "");
            nvs_set_u32(hDest, "eth_dhcp", 1);
            nvs_set_str(hDest, "eth_ip", "192.168.1.150");
            nvs_set_str(hDest, "eth_gw", "192.168.1.1");
            nvs_set_str(hDest, "eth_sn", "255.255.255.0");
            nvs_set_str(hDest, "eth_dns", "8.8.8.8");
            nvs_set_str(hDest, "eth_dns2", "");
            nvs_set_str(hDest, "ap_pass", "12345678");
        }
        nvs_commit(hDest);
        nvs_close(hDest);
        ESP_LOGI(TAG, "[NVS 2/4] 'cfg_net' OK (Master: %s).", hasMasterNet ? "ADA" : "TIDAK ADA -> Default");
    }

    // 3. Salin Master 'mqtt' -> 'cfg_mqtt'
    bool hasMasterMqtt = (nvs_open("mqtt", NVS_READONLY, &hMaster) == ESP_OK);
    if (nvs_open("cfg_mqtt", NVS_READWRITE, &hDest) == ESP_OK) {
        nvs_erase_all(hDest);
        if (hasMasterMqtt) {
            nvsCopyStr(hMaster, hDest, "host", "socket.cubeacon.com");
            nvsCopyU32(hMaster, hDest, "port", 1884);
            nvsCopyStr(hMaster, hDest, "user", "eyroMQTT");
            nvsCopyStr(hMaster, hDest, "pass", "eyroMQTT1234");
            nvs_close(hMaster);
        } else {
            nvs_set_str(hDest, "host", "socket.cubeacon.com");
            nvs_set_u32(hDest, "port", 1884);
            nvs_set_str(hDest, "user", "eyroMQTT");
            nvs_set_str(hDest, "pass", "eyroMQTT1234");
        }
        nvs_commit(hDest);
        nvs_close(hDest);
        ESP_LOGI(TAG, "[NVS 3/4] 'cfg_mqtt' OK (Master: %s).", hasMasterMqtt ? "ADA" : "TIDAK ADA -> Default");
    }

    // 4. Salin Master 'gateway' -> 'cfg_gate'
    bool hasMasterGate = (nvs_open("gateway", NVS_READONLY, &hMaster) == ESP_OK);
    if (nvs_open("cfg_gate", NVS_READWRITE, &hDest) == ESP_OK) {
        nvs_erase_all(hDest);
        if (hasMasterGate) {
            nvsCopyStr(hMaster, hDest, "dev_model", CONFIG_DEVICE_MODEL);
            nvsCopyStr(hMaster, hDest, "dev_ver", CONFIG_DEVICE_BOARD_VER);
            nvsCopyU32(hMaster, hDest, "uuid_en", 0);
            nvsCopyStr(hMaster, hDest, "uuid", "FDA50693A4E24FB1AFCFC6EB07647825");
            nvsCopyU32(hMaster, hDest, "major_en", 0);
            nvsCopyStr(hMaster, hDest, "major", "");
            nvsCopyI32(hMaster, hDest, "rssi_min", -100);
            nvsCopyU32(hMaster, hDest, "batch_max", 100);
            nvsCopyU32(hMaster, hDest, "batch_flush", 300);
            nvsCopyU32(hMaster, hDest, "data_ttl", 5000);
            nvsCopyStr(hMaster, hDest, "restart_sch", "03:00");
            nvsCopyU32(hMaster, hDest, "discon_rst", 30);
            nvs_close(hMaster);
        } else {
            nvs_set_str(hDest, "dev_model", CONFIG_DEVICE_MODEL);
            nvs_set_str(hDest, "dev_ver", CONFIG_DEVICE_BOARD_VER);
            nvs_set_u32(hDest, "uuid_en", 0);
            nvs_set_str(hDest, "uuid", "FDA50693A4E24FB1AFCFC6EB07647825");
            nvs_set_u32(hDest, "major_en", 0);
            nvs_set_str(hDest, "major", "");
            nvs_set_i32(hDest, "rssi_min", -100);
            nvs_set_u32(hDest, "batch_max", 100);
            nvs_set_u32(hDest, "batch_flush", 300);
            nvs_set_u32(hDest, "data_ttl", 5000);
            nvs_set_str(hDest, "restart_sch", "03:00");
            nvs_set_u32(hDest, "discon_rst", 30);
        }
        nvs_commit(hDest);
        nvs_close(hDest);
        ESP_LOGI(TAG, "[NVS 4/4] 'cfg_gate' OK (Master: %s).", hasMasterGate ? "ADA" : "TIDAK ADA -> Default");
    }

    // 5. Tandai cfg_valid = 1
    if (hSys != 0) {
        nvs_set_u8(hSys, "cfg_valid", 1);
        nvs_commit(hSys);
        nvs_close(hSys);
    }
    ESP_LOGI(TAG, "Seluruh NVS operasional siap & cfg_valid=true dikomit.");
}

void ConfigStore::loadConfig() {
    nvs_handle_t h;

    // cfg_auth
    if (nvs_open("cfg_auth", NVS_READONLY, &h) == ESP_OK) {
        nvsGetStr(h, "adm_user", g_config.admin_user, sizeof(g_config.admin_user), "admin");
        nvsGetStr(h, "adm_pass", g_config.admin_pass, sizeof(g_config.admin_pass), "admin");
        nvsGetStr(h, "tek_user", g_config.user_user, sizeof(g_config.user_user), "user");
        nvsGetStr(h, "tek_pass", g_config.user_pass, sizeof(g_config.user_pass), "user");
        nvs_close(h);
    } else {
        strlcpy(g_config.admin_user, "admin", sizeof(g_config.admin_user));
        strlcpy(g_config.admin_pass, "admin", sizeof(g_config.admin_pass));
        strlcpy(g_config.user_user, "user", sizeof(g_config.user_user));
        strlcpy(g_config.user_pass, "user", sizeof(g_config.user_pass));
    }
    ESP_LOGI(TAG, "Auth Config Aktif: Admin='%s', Teknisi='%s'", g_config.admin_user, g_config.user_user);

    // cfg_net
    if (nvs_open("cfg_net", NVS_READONLY, &h) == ESP_OK) {
        g_config.connection_mode = (int)nvsGetU32(h, "mode", 1);
        nvsGetStr(h, "w_ssid", g_config.w_ssid, sizeof(g_config.w_ssid), "");
        nvsGetStr(h, "w_pass", g_config.w_password, sizeof(g_config.w_password), "");
        g_config.w_dhcp = (int)nvsGetU32(h, "w_dhcp", 1);
        nvsGetStr(h, "w_ip", g_config.w_ip, sizeof(g_config.w_ip), "192.168.1.100");
        nvsGetStr(h, "w_gw", g_config.w_gateway, sizeof(g_config.w_gateway), "192.168.1.1");
        nvsGetStr(h, "w_sn", g_config.w_subnet, sizeof(g_config.w_subnet), "255.255.255.0");
        nvsGetStr(h, "w_dns", g_config.w_dns, sizeof(g_config.w_dns), "8.8.8.8");
        nvsGetStr(h, "w_dns2", g_config.w_dns2, sizeof(g_config.w_dns2), "");
        g_config.eth_dhcp = (int)nvsGetU32(h, "eth_dhcp", 1);
        nvsGetStr(h, "eth_ip", g_config.eth_ip, sizeof(g_config.eth_ip), "192.168.1.150");
        nvsGetStr(h, "eth_gw", g_config.eth_gateway, sizeof(g_config.eth_gateway), "192.168.1.1");
        nvsGetStr(h, "eth_sn", g_config.eth_subnet, sizeof(g_config.eth_subnet), "255.255.255.0");
        nvsGetStr(h, "eth_dns", g_config.eth_dns, sizeof(g_config.eth_dns), "8.8.8.8");
        nvsGetStr(h, "eth_dns2", g_config.eth_dns2, sizeof(g_config.eth_dns2), "");
        nvsGetStr(h, "ap_pass", g_config.ap_password, sizeof(g_config.ap_password), "12345678");
        nvs_close(h);
    }

    // cfg_mqtt
    if (nvs_open("cfg_mqtt", NVS_READONLY, &h) == ESP_OK) {
        nvsGetStr(h, "host", g_config.mqtt_host, sizeof(g_config.mqtt_host), "socket.cubeacon.com");
        g_config.mqtt_port = (int)nvsGetU32(h, "port", 1884);
        nvsGetStr(h, "user", g_config.mqtt_user, sizeof(g_config.mqtt_user), "eyroMQTT");
        nvsGetStr(h, "pass", g_config.mqtt_password, sizeof(g_config.mqtt_password), "eyroMQTT1234");
        nvs_close(h);
    }

    // cfg_gate
    if (nvs_open("cfg_gate", NVS_READONLY, &h) == ESP_OK) {
        nvsGetStr(h, "dev_model", g_config.device_model, sizeof(g_config.device_model), CONFIG_DEVICE_MODEL);
        nvsGetStr(h, "dev_ver", g_config.device_version, sizeof(g_config.device_version), CONFIG_DEVICE_BOARD_VER);
        g_config.filter_uuid_en = (int)nvsGetU32(h, "uuid_en", 0);
        nvsGetStr(h, "uuid", g_config.filter_uuid, sizeof(g_config.filter_uuid), "FDA50693A4E24FB1AFCFC6EB07647825");
        parseHexUUID(g_config.filter_uuid, g_config.filter_uuid_bytes);
        g_config.filter_major_en = (int)nvsGetU32(h, "major_en", 0);
        nvsGetStr(h, "major", g_config.filter_major, sizeof(g_config.filter_major), "");
        g_config.rssi_min = (int)nvsGetI32(h, "rssi_min", -100);
        g_config.batch_max = (int)nvsGetU32(h, "batch_max", 100);
        g_config.batch_flush = (int)nvsGetU32(h, "batch_flush", 300);
        g_config.data_ttl = (int)nvsGetU32(h, "data_ttl", 5000);
        nvsGetStr(h, "restart_sch", g_config.restart_schedule, sizeof(g_config.restart_schedule), "03:00");
        g_config.discon_rst = (int)nvsGetU32(h, "discon_rst", 30);
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Konfigurasi berhasil dimuat: Model=%s, Mode=%s, Broker=%s:%d",
             g_config.device_model,
             (g_config.connection_mode == 1) ? "Ethernet" : "Wi-Fi",
             g_config.mqtt_host, g_config.mqtt_port);
}

void ConfigStore::saveConfigAuth() {
    nvs_handle_t h;
    if (nvs_open("cfg_auth", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "adm_user", g_config.admin_user);
        nvs_set_str(h, "adm_pass", g_config.admin_pass);
        nvs_set_str(h, "tek_user", g_config.user_user);
        nvs_set_str(h, "tek_pass", g_config.user_pass);
        nvs_commit(h);
        nvs_close(h);
    }
    if (nvs_open("cfg_net", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "mode", g_config.connection_mode);
        nvs_set_str(h, "w_ssid", g_config.w_ssid);
        nvs_set_str(h, "w_pass", g_config.w_password);
        nvs_set_u32(h, "w_dhcp", g_config.w_dhcp);
        nvs_set_str(h, "w_ip", g_config.w_ip);
        nvs_set_str(h, "w_gw", g_config.w_gateway);
        nvs_set_str(h, "w_sn", g_config.w_subnet);
        nvs_set_str(h, "w_dns", g_config.w_dns);
        nvs_set_str(h, "w_dns2", g_config.w_dns2);
        nvs_set_u32(h, "eth_dhcp", g_config.eth_dhcp);
        nvs_set_str(h, "eth_ip", g_config.eth_ip);
        nvs_set_str(h, "eth_gw", g_config.eth_gateway);
        nvs_set_str(h, "eth_sn", g_config.eth_subnet);
        nvs_set_str(h, "eth_dns", g_config.eth_dns);
        nvs_set_str(h, "eth_dns2", g_config.eth_dns2);
        nvs_set_str(h, "ap_pass", g_config.ap_password);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Konfigurasi Auth & Network berhasil disimpan ke NVS.");
}

void ConfigStore::saveConfigSettings() {
    nvs_handle_t h;
    if (nvs_open("cfg_gate", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "uuid_en", g_config.filter_uuid_en);
        nvs_set_str(h, "uuid", g_config.filter_uuid);
        nvs_set_u32(h, "major_en", g_config.filter_major_en);
        nvs_set_str(h, "major", g_config.filter_major);
        nvs_set_i32(h, "rssi_min", g_config.rssi_min);
        nvs_set_u32(h, "batch_max", g_config.batch_max);
        nvs_set_u32(h, "batch_flush", g_config.batch_flush);
        nvs_set_u32(h, "data_ttl", g_config.data_ttl);
        nvs_set_str(h, "restart_sch", g_config.restart_schedule);
        nvs_set_u32(h, "discon_rst", g_config.discon_rst);
        nvs_commit(h);
        nvs_close(h);
    }
    parseHexUUID(g_config.filter_uuid, g_config.filter_uuid_bytes);
    ESP_LOGI(TAG, "Pengaturan Gateway berhasil disimpan ke NVS.");
}

void ConfigStore::resetConfigToFactory() {
    ESP_LOGW(TAG, "Mereset NVS Operasional (cfg_*) ke setelan awal pabrik...");
    const char* namespaces[] = { "cfg_auth", "cfg_net", "cfg_mqtt", "cfg_gate", "cfg_sys" };
    for (const char* ns : namespaces) {
        nvs_handle_t h;
        if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
            nvs_erase_all(h);
            nvs_commit(h);
            nvs_close(h);
        }
    }
    ESP_LOGW(TAG, "Reset NVS selesai. Sistem siap reboot.");
}

void ConfigStore::getDeviceId(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    strlcpy(outBuf, g_deviceId, maxLen);
    for (size_t i = 0; outBuf[i] != '\0'; i++) {
        if (outBuf[i] >= 'A' && outBuf[i] <= 'Z') {
            outBuf[i] = static_cast<char>(outBuf[i] + ('a' - 'A'));
        }
    }
}

void ConfigStore::getMqttClientId(char* outBuf, size_t maxLen) {
    // Client ID MQTT murni menggunakan device_id lowercase
    getDeviceId(outBuf, maxLen);
}

void ConfigStore::getModuleId(char* outBuf, size_t maxLen) {
    char dev_id[16];
    getDeviceId(dev_id, sizeof(dev_id));
    const char* model = (strlen(g_config.device_model) > 0) ? g_config.device_model : "EMPTY";
    snprintf(outBuf, maxLen, "%s-%s", model, dev_id);
}

} // namespace Iotaru::Core
