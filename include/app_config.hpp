#pragma once

#include "app_types.hpp"
#include <cstdint>
#include <string_view>

namespace Iotaru {

// =================================================================
// POD CONFIGURATION STRUCT (Zero Dynamic Allocation)
// =================================================================
struct AppConfig {
    // Auth Namespace ("auth" / "cfg_auth")
    char admin_user[32];
    char admin_pass[32];
    char user_user[32];
    char user_pass[32];

    // Network Namespace ("network" / "cfg_net")
    int  connection_mode; // 0: WiFi, 1: Ethernet
    char w_ssid[33];
    char w_password[65];
    int  w_dhcp;          // 1: DHCP, 0: Static
    char w_ip[16];
    char w_gateway[16];
    char w_subnet[16];
    char w_dns[16];
    char w_dns2[16];
    int  eth_dhcp;        // 1: DHCP, 0: Static
    char eth_ip[16];
    char eth_gateway[16];
    char eth_subnet[16];
    char eth_dns[16];
    char eth_dns2[16];
    char ap_password[33];

    // MQTT Namespace ("mqtt" / "cfg_mqtt")
    char mqtt_host[64];
    int  mqtt_port;
    char mqtt_user[32];
    char mqtt_password[32];
    int  mqtt_use_ssl; // 0: TCP Plain (mqtt://), 1: TLS/SSL (mqtts://)
    int  mqtt_qos;     // 0: QoS 0 (Fire & Forget), 1: QoS 1 (Delivery Guaranteed)
    int  mqtt_keepalive; // 5 .. 300 detik (Default: 30)
    uint32_t telem_mask; // Bitmask flags seleksi field telemetri dinamis (Default: 0x9E)

    // Gateway Namespace ("gateway" / "cfg_gate")
    char    device_model[16];
    char    device_version[16];
    int     filter_uuid_en;   // 0 or 1
    char    filter_uuid[33];
    uint8_t filter_uuid_bytes[16];
    int     filter_major_en;  // 0 or 1
    char    filter_major[32]; // Comma-separated list e.g. "1, 65535"
    int     rssi_min;         // e.g. -100 dBm
    int     batch_max;        // e.g. 100 packets
    int     batch_flush;      // e.g. 300 ms
    int     data_ttl;         // e.g. 5000 ms
    char    restart_schedule[8]; // "HH:MM"
    int     discon_rst;       // Minutes before auto-reboot, 0 = disabled
    char    ntp_server[64];   // e.g. "pool.ntp.org"
};

// Global metadata string view
extern const char* FIRMWARE_VERSION;
extern char g_deviceId[16];
extern char g_uiVersion[16];
extern AppConfig g_config;

// Helper functions (inline / constexpr)
void parseHexUUID(const char* hexStr, uint8_t* out16);
bool matchMajorFilter(uint16_t beaconMajor, std::string_view filterStr);

} // namespace Iotaru
