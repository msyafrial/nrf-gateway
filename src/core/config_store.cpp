#include "config_store.hpp"
#include "device_id.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "aes/esp_aes.h"
#include "psa/crypto.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

static const char *TAG = "CONFIG_STORE";

static void deriveHardwareKey(uint8_t key[16]) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    static char s_devSalt[33] = {0};
    if (s_devSalt[0] == '\0') {
        nvs_handle_t hSys;
        if (nvs_open("cfg_sys", NVS_READWRITE, &hSys) == ESP_OK) {
            size_t len = sizeof(s_devSalt);
            if (nvs_get_str(hSys, "k_salt", s_devSalt, &len) != ESP_OK || strlen(s_devSalt) < 16) {
                for (int i = 0; i < 4; i++) {
                    snprintf(s_devSalt + (i * 8), 9, "%08x", (unsigned int)esp_random());
                }
                nvs_set_str(hSys, "k_salt", s_devSalt);
                nvs_commit(hSys);
            }
            nvs_close(hSys);
        } else {
            strlcpy(s_devSalt, "SG10_IOTARU_ENC_SALT_2026", sizeof(s_devSalt));
        }
    }

    uint8_t inputBuf[64];
    size_t saltLen = strlen(s_devSalt);
    memcpy(inputBuf, mac, 6);
    memcpy(inputBuf + 6, s_devSalt, saltLen);

    psa_crypto_init();
    uint8_t hash[32];
    size_t hashLen = 0;
    psa_hash_compute(PSA_ALG_SHA_256, inputBuf, 6 + saltLen, hash, sizeof(hash), &hashLen);
    memcpy(key, hash, 16);
}

// Fallback kunci statis lama untuk membuka cipher "enc:" legacy
static void deriveLegacyHardwareKey(uint8_t key[16]) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    const char* salt = "SG10_IOTARU_ENC_SALT_2026";
    uint8_t inputBuf[38];
    memcpy(inputBuf, mac, 6);
    memcpy(inputBuf + 6, salt, strlen(salt));

    psa_crypto_init();
    uint8_t hash[32];
    size_t hashLen = 0;
    psa_hash_compute(PSA_ALG_SHA_256, inputBuf, 6 + strlen(salt), hash, sizeof(hash), &hashLen);
    memcpy(key, hash, 16);
}

static void encryptSecret(const char* plaintext, char* outEncBuf, size_t maxLen) {
    if (!plaintext || !outEncBuf || maxLen < 40) return;
    size_t inLen = strlen(plaintext);
    if (inLen == 0) {
        strlcpy(outEncBuf, "", maxLen);
        return;
    }

    uint8_t key[16];
    deriveHardwareKey(key);

    if (inLen > 63) inLen = 63;
    size_t padLen = 16 - (inLen % 16);
    size_t paddedLen = inLen + padLen;

    uint8_t inBuf[64];
    memcpy(inBuf, plaintext, inLen);
    memset(inBuf + inLen, (uint8_t)padLen, padLen);

    uint8_t iv[16];
    esp_fill_random(iv, sizeof(iv));
    uint8_t ivCopy[16];
    memcpy(ivCopy, iv, sizeof(iv));

    uint8_t cipherBuf[64];
    esp_aes_context aes;
    esp_aes_init(&aes);
    esp_aes_setkey(&aes, key, 128);
    esp_aes_crypt_cbc(&aes, ESP_AES_ENCRYPT, paddedLen, ivCopy, inBuf, cipherBuf);
    esp_aes_free(&aes);

    // Format output: "cbc:" + 32 hex IV + ":" + hex cipher
    strlcpy(outEncBuf, "cbc:", maxLen);
    size_t curLen = 4;
    for (size_t i = 0; i < 16 && (curLen + 2) < maxLen; i++) {
        snprintf(outEncBuf + curLen, maxLen - curLen, "%02x", iv[i]);
        curLen += 2;
    }
    if (curLen + 1 < maxLen) {
        outEncBuf[curLen++] = ':';
        outEncBuf[curLen] = '\0';
    }
    for (size_t i = 0; i < paddedLen && (curLen + 2) < maxLen; i++) {
        snprintf(outEncBuf + curLen, maxLen - curLen, "%02x", cipherBuf[i]);
        curLen += 2;
    }
}

static void decryptSecret(const char* storedStr, char* outPlainBuf, size_t maxLen) {
    if (!storedStr || !outPlainBuf || maxLen == 0) return;

    // 1. Format Baru: AES-CBC ("cbc:<32_hex_iv>:<hex_cipher>")
    if (strncmp(storedStr, "cbc:", 4) == 0) {
        const char* ivHex = storedStr + 4;
        const char* colon = strchr(ivHex, ':');
        if (!colon || (colon - ivHex) != 32) {
            strlcpy(outPlainBuf, "", maxLen);
            return;
        }
        uint8_t iv[16];
        for (size_t i = 0; i < 16; i++) {
            char bStr[3] = { ivHex[i * 2], ivHex[i * 2 + 1], '\0' };
            iv[i] = static_cast<uint8_t>(strtoul(bStr, nullptr, 16));
        }
        const char* cipherHex = colon + 1;
        size_t hexLen = strlen(cipherHex);
        if ((hexLen % 32) != 0 || hexLen == 0 || (hexLen / 2) > 64) {
            strlcpy(outPlainBuf, "", maxLen);
            return;
        }
        size_t cipherLen = hexLen / 2;
        uint8_t cipherBuf[64] = {0};
        for (size_t i = 0; i < cipherLen; i++) {
            char bStr[3] = { cipherHex[i * 2], cipherHex[i * 2 + 1], '\0' };
            cipherBuf[i] = static_cast<uint8_t>(strtoul(bStr, nullptr, 16));
        }

        uint8_t key[16];
        deriveHardwareKey(key);
        uint8_t plainBuf[64] = {0};

        esp_aes_context aes;
        esp_aes_init(&aes);
        esp_aes_setkey(&aes, key, 128);
        esp_aes_crypt_cbc(&aes, ESP_AES_DECRYPT, cipherLen, iv, cipherBuf, plainBuf);
        esp_aes_free(&aes);

        uint8_t pad = plainBuf[cipherLen - 1];
        if (pad > 0 && pad <= 16 && pad <= cipherLen) {
            size_t realLen = cipherLen - pad;
            if (realLen >= maxLen) realLen = maxLen - 1;
            memcpy(outPlainBuf, plainBuf, realLen);
            outPlainBuf[realLen] = '\0';
        } else {
            strlcpy(outPlainBuf, "", maxLen);
        }
        return;
    }

    // 2. Format Lama: AES-ECB ("enc:<hex_cipher>")
    if (strncmp(storedStr, "enc:", 4) == 0) {
        const char* hexStr = storedStr + 4;
        size_t hexLen = strlen(hexStr);
        if ((hexLen % 32) != 0 || hexLen == 0 || (hexLen / 2) > 80) {
            strlcpy(outPlainBuf, "", maxLen);
            return;
        }
        size_t cipherLen = hexLen / 2;
        uint8_t cipherBuf[80] = {0};
        for (size_t i = 0; i < cipherLen; i++) {
            char byteStr[3] = { hexStr[i * 2], hexStr[i * 2 + 1], '\0' };
            cipherBuf[i] = static_cast<uint8_t>(strtoul(byteStr, nullptr, 16));
        }
        uint8_t key[16];
        deriveLegacyHardwareKey(key);
        uint8_t plainBuf[80] = {0};
        esp_aes_context aes;
        esp_aes_init(&aes);
        esp_aes_setkey(&aes, key, 128);
        for (size_t offset = 0; offset < cipherLen; offset += 16) {
            esp_aes_crypt_ecb(&aes, ESP_AES_DECRYPT, cipherBuf + offset, plainBuf + offset);
        }
        esp_aes_free(&aes);

        uint8_t pad = plainBuf[cipherLen - 1];
        if (pad > 0 && pad <= 16 && pad <= cipherLen) {
            size_t realLen = cipherLen - pad;
            if (realLen >= maxLen) realLen = maxLen - 1;
            memcpy(outPlainBuf, plainBuf, realLen);
            outPlainBuf[realLen] = '\0';
        } else {
            strlcpy(outPlainBuf, "", maxLen);
        }
        return;
    }

    // 3. Plaintext Legacy (dari nvs.bin universal pabrik)
    strlcpy(outPlainBuf, storedStr, maxLen);
}

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
            nvsCopyStr(hMaster, hDest, "user", "");
            nvsCopyStr(hMaster, hDest, "pass", "");
            nvsCopyU32(hMaster, hDest, "use_ssl", 0);
            nvsCopyU32(hMaster, hDest, "qos", 1);
            nvsCopyU32(hMaster, hDest, "keepalive", 30);
            nvsCopyU32(hMaster, hDest, "telem_mask", TELEM_MASK_DEFAULT);
            nvs_close(hMaster);
        } else {
            nvs_set_str(hDest, "host", "socket.cubeacon.com");
            nvs_set_u32(hDest, "port", 1884);
            nvs_set_str(hDest, "user", "");
            nvs_set_str(hDest, "pass", "");
            nvs_set_u32(hDest, "use_ssl", 0);
            nvs_set_u32(hDest, "qos", 1);
            nvs_set_u32(hDest, "keepalive", 30);
            nvs_set_u32(hDest, "telem_mask", TELEM_MASK_DEFAULT);
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
            nvsCopyU32(hMaster, hDest, "data_ttl", 1000);
            nvsCopyStr(hMaster, hDest, "restart_sch", "03:00");
            nvsCopyU32(hMaster, hDest, "discon_rst", 30);
            nvsCopyStr(hMaster, hDest, "ntp_server", "pool.ntp.org");
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
            nvs_set_u32(hDest, "data_ttl", 1000);
            nvs_set_str(hDest, "restart_sch", "03:00");
            nvs_set_u32(hDest, "discon_rst", 30);
            nvs_set_str(hDest, "ntp_server", "pool.ntp.org");
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

    // cfg_auth (Otomatis migrasi plaintext ke AES-128 berbasis hardware key MAC)
    bool needAuthMigration = false;
    if (nvs_open("cfg_auth", NVS_READWRITE, &h) == ESP_OK) {
        nvsGetStr(h, "adm_user", g_config.admin_user, sizeof(g_config.admin_user), "admin");

        char rawAdmPass[192] = {0};
        nvsGetStr(h, "adm_pass", rawAdmPass, sizeof(rawAdmPass), "admin");
        decryptSecret(rawAdmPass, g_config.admin_pass, sizeof(g_config.admin_pass));
        if (strncmp(rawAdmPass, "cbc:", 4) != 0 && strlen(g_config.admin_pass) > 0) {
            char encBuf[192];
            encryptSecret(g_config.admin_pass, encBuf, sizeof(encBuf));
            nvs_set_str(h, "adm_pass", encBuf);
            needAuthMigration = true;
        }

        nvsGetStr(h, "tek_user", g_config.user_user, sizeof(g_config.user_user), "user");

        char rawTekPass[192] = {0};
        nvsGetStr(h, "tek_pass", rawTekPass, sizeof(rawTekPass), "user");
        decryptSecret(rawTekPass, g_config.user_pass, sizeof(g_config.user_pass));
        if (strncmp(rawTekPass, "cbc:", 4) != 0 && strlen(g_config.user_pass) > 0) {
            char encBuf[192];
            encryptSecret(g_config.user_pass, encBuf, sizeof(encBuf));
            nvs_set_str(h, "tek_pass", encBuf);
            needAuthMigration = true;
        }

        if (needAuthMigration) {
            nvs_commit(h);
            ESP_LOGI(TAG, "Kredensial auth berhasil dimigrasi ke enkripsi hardware AES-CBC (256-bit entropy).");
        }
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

    // cfg_mqtt (NVS_READWRITE untuk auto-migrasi enkripsi mqtt_password)
    if (nvs_open("cfg_mqtt", NVS_READWRITE, &h) == ESP_OK) {
        nvsGetStr(h, "host", g_config.mqtt_host, sizeof(g_config.mqtt_host), "socket.cubeacon.com");
        g_config.mqtt_port = (int)nvsGetU32(h, "port", 1884);
        nvsGetStr(h, "user", g_config.mqtt_user, sizeof(g_config.mqtt_user), "");
        g_config.mqtt_use_ssl = (int)nvsGetU32(h, "use_ssl", 0);
        g_config.mqtt_qos = (int)nvsGetU32(h, "qos", 1);
        g_config.mqtt_keepalive = (int)nvsGetU32(h, "keepalive", 30);
        g_config.telem_mask = nvsGetU32(h, "telem_mask", TELEM_MASK_DEFAULT);

        char rawMqttPass[192] = {0};
        nvsGetStr(h, "pass", rawMqttPass, sizeof(rawMqttPass), "");
        decryptSecret(rawMqttPass, g_config.mqtt_password, sizeof(g_config.mqtt_password));
        if (strncmp(rawMqttPass, "cbc:", 4) != 0 && strlen(g_config.mqtt_password) > 0) {
            char encMqtt[192];
            encryptSecret(g_config.mqtt_password, encMqtt, sizeof(encMqtt));
            nvs_set_str(h, "pass", encMqtt);
            nvs_commit(h);
            ESP_LOGI(TAG, "Password MQTT berhasil dimigrasi ke format enkripsi AES-CBC.");
        }
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
        g_config.data_ttl = (int)nvsGetU32(h, "data_ttl", 1000);
        nvsGetStr(h, "restart_sch", g_config.restart_schedule, sizeof(g_config.restart_schedule), "03:00");
        g_config.discon_rst = (int)nvsGetU32(h, "discon_rst", 30);
        nvsGetStr(h, "ntp_server", g_config.ntp_server, sizeof(g_config.ntp_server), "pool.ntp.org");
        nvs_close(h);
    }

    ESP_LOGI(TAG, "Konfigurasi berhasil dimuat: Model=%s, Mode=%s, Broker=%s:%d (SSL: %s, QoS: %d, KeepAlive: %ds)",
             g_config.device_model,
             (g_config.connection_mode == 1) ? "Ethernet" : "Wi-Fi",
             g_config.mqtt_host, g_config.mqtt_port,
             g_config.mqtt_use_ssl ? "YA" : "TIDAK",
             g_config.mqtt_qos, g_config.mqtt_keepalive);
}

void ConfigStore::saveConfigAuth() {
    nvs_handle_t h;
    if (nvs_open("cfg_auth", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "adm_user", g_config.admin_user);

        char encAdmPass[192];
        encryptSecret(g_config.admin_pass, encAdmPass, sizeof(encAdmPass));
        nvs_set_str(h, "adm_pass", encAdmPass);

        nvs_set_str(h, "tek_user", g_config.user_user);

        char encTekPass[192];
        encryptSecret(g_config.user_pass, encTekPass, sizeof(encTekPass));
        nvs_set_str(h, "tek_pass", encTekPass);

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
    if (nvs_open("cfg_mqtt", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "host", g_config.mqtt_host);
        nvs_set_u32(h, "port", (uint32_t)g_config.mqtt_port);
        nvs_set_str(h, "user", g_config.mqtt_user);
        nvs_set_u32(h, "use_ssl", (uint32_t)g_config.mqtt_use_ssl);
        nvs_set_u32(h, "qos", (uint32_t)g_config.mqtt_qos);
        nvs_set_u32(h, "keepalive", (uint32_t)g_config.mqtt_keepalive);
        nvs_set_u32(h, "telem_mask", g_config.telem_mask);

        char encMqttPass[192];
        encryptSecret(g_config.mqtt_password, encMqttPass, sizeof(encMqttPass));
        nvs_set_str(h, "pass", encMqttPass);

        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Konfigurasi Auth, Network & MQTT berhasil disimpan ke NVS.");
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
        nvs_set_str(h, "ntp_server", g_config.ntp_server);
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
