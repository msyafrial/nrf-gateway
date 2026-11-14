#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace Iotaru {

// =================================================================
// 1. BEACON PROTOCOL STRUCTS & ENUMS
// =================================================================
struct __attribute__((packed)) BeaconBinaryFrame {
    uint8_t  header;    // 0xAA
    uint8_t  code;      // 0x00 = iBeacon, 0x01 = ibeacon_ext
    uint8_t  uuid[16];  // 16-byte raw Proximity UUID
    uint16_t major;     // Major (Big-Endian)
    uint16_t minor;     // Minor (Big-Endian)
    int8_t   rssi;      // RSSI (-128 .. 127 dBm)
    uint8_t  battery;   // Battery % (0 .. 100)
    uint8_t  checksum;  // XOR Checksum (bytes 1 to 23)
};
static_assert(sizeof(BeaconBinaryFrame) == 25, "BeaconBinaryFrame must be exactly 25 bytes");

// Struktur Frame Biner Universal 42-Byte dari nRF52840
struct __attribute__((packed)) RawBleBinaryFrame {
    uint8_t header;      // Byte 0      : Selalu 0xAA (Sync Byte)
    uint8_t mac[6];      // Byte 1..6   : MAC Address (MSB ke LSB)
    uint8_t adv_type;    // Byte 7      : Tipe PDU Radio (ADV_IND, etc.)
    int8_t  rssi;        // Byte 8      : Kekuatan sinyal dBm
    uint8_t payload_len; // Byte 9      : Panjang data AD asli (0..31 byte)
    uint8_t payload[31]; // Byte 10..40 : Raw AD Structures (0-padded jika len < 31)
    uint8_t checksum;    // Byte 41     : XOR byte 1..40
};
static_assert(sizeof(RawBleBinaryFrame) == 42, "RawBleBinaryFrame must be exactly 42 bytes");

// Struktur Frame Biner Telemetri MQTT 44-Byte (Solusi 3: dengan Unix Epoch Timestamp)
struct __attribute__((packed)) MqttBleFrame44 {
    uint32_t timestamp;   // Byte 0..3   : Unix Epoch Time detik (Big-Endian)
    uint8_t  mac[6];      // Byte 4..9   : MAC Address (MSB ke LSB)
    uint8_t  adv_type;    // Byte 10     : Tipe PDU Radio (ADV_IND, etc.)
    int8_t   rssi;        // Byte 11     : Kekuatan sinyal dBm
    uint8_t  payload_len; // Byte 12     : Panjang data AD asli (0..31 byte)
    uint8_t  payload[31]; // Byte 13..43: Raw AD Structures (0-padded)
};
static_assert(sizeof(MqttBleFrame44) == 44, "MqttBleFrame44 must be exactly 44 bytes");

// Protokol Perintah Downlink ESP32 -> nRF52840 (5 Byte)
struct __attribute__((packed)) ControlCmdFrame {
    uint8_t header;   // 0x55
    uint8_t cmd_id;   // 0x01 (SET_SCAN_MODE), 0x02 (GET_STATUS), 0x03 (SOFT_REBOOT)
    uint8_t param;    // 0x01 (Passive), 0x02 (Active), 0x00 (Stop)
    uint8_t reserved; // 0x00
    uint8_t crc;      // cmd_id ^ param ^ reserved
};
static_assert(sizeof(ControlCmdFrame) == 5, "ControlCmdFrame must be exactly 5 bytes");

// Protokol Balasan Uplink nRF52840 -> ESP32 (4 Byte)
struct __attribute__((packed)) ControlRspFrame {
    uint8_t header;     // 0xBB
    uint8_t rsp_type;   // 0x81 (ACK), 0x82 (STATUS), 0x8F (NACK)
    uint8_t scan_state; // 0x00 (STOPPED), 0x01 (PASSIVE), 0x02 (ACTIVE)
    uint8_t crc;        // rsp_type ^ scan_state
};
static_assert(sizeof(ControlRspFrame) == 4, "ControlRspFrame must be exactly 4 bytes");

// Bitmask Flags Dynamic Telemetry Protocol v2 (Urutan: UUID, Major, Minor, RSSI, Battery, Timestamp)
constexpr uint32_t TELEM_MASK_V2_FLAG   = 0x80; // Penanda format dinamis v2
constexpr uint32_t TELEM_MASK_UUID      = 0x01; // Proximity UUID (16B)
constexpr uint32_t TELEM_MASK_MAJOR     = 0x02; // Major (2B)
constexpr uint32_t TELEM_MASK_MINOR     = 0x04; // Minor (2B)
constexpr uint32_t TELEM_MASK_RSSI      = 0x08; // RSSI (1B)
constexpr uint32_t TELEM_MASK_BATT      = 0x10; // Battery (1B)
constexpr uint32_t TELEM_MASK_TIME      = 0x20; // Timestamp (4B)
constexpr uint32_t TELEM_MASK_DEFAULT   = 0x9E; // 0x80 | 0x02 | 0x04 | 0x08 | 0x10 (Major, Minor, RSSI, Battery)

enum class BeaconType : uint8_t {
    UNKNOWN     = 0,
    IBEACON_EXT = 1,
    MINEW_B     = 1,
    IBEACON     = 2,
    EDDYSTONE   = 3,
    MOKO_SENSOR = 4,
    MOKO_ALARM  = 5,
    RAW_BLE     = 6
};

struct BeaconPacket {
    uint32_t   timestamp; // Uptime timestamp (ms)
    BeaconType type;
    int8_t     rssi;
    uint8_t    battery;
    bool       has_major;
    uint16_t   major;
    uint16_t   minor;
    uint8_t    raw_frame[42]; // Full raw 42-byte unaltered frame
};

struct LiveBeaconItem {
    char    code[16];
    char    tag[24];
    int8_t  rssi;
    uint8_t batt;
    uint8_t mac[6];
    uint8_t adv_type;
    uint8_t raw_frame[42];
};

enum class DropReason : uint8_t {
    TTL_EXPIRED       = 0, // Kedaluwarsa di antrean (> data_ttl)
    RSSI_OUT_OF_RANGE = 1, // RSSI di bawah ambang batas minimal (rssi_min)
    UUID_MISMATCH     = 2, // UUID tidak cocok dengan konfigurasi filter
    MAJOR_MISMATCH    = 3, // Major tidak masuk dalam rentang filter
    QUEUE_OVERFLOW    = 4, // Antrean FreeRTOS penuh (drop-oldest)
    MQTT_FAIL         = 5, // Gagal publish ke MQTT broker / Outbox full
    UNKNOWN_FORMAT    = 6  // Format BLE tidak dikenali / di-reject oleh filter
};

struct DroppedBeaconItem {
    char       code[16];
    char       tag[24];
    int8_t     rssi;
    DropReason reason;
    uint8_t    mac[6];
    uint8_t    raw_frame[42];
};

constexpr size_t LIVE_FIFO_MAX = 50;
constexpr size_t DROPPED_FIFO_MAX = 50;
constexpr size_t BEACON_QUEUE_MAX = 214;
constexpr size_t MQTT_BATCH_BUFFER_SIZE = 6656;

// =================================================================
// 2. SYSTEM & HARDWARE ENUMS
// =================================================================
enum class SysLedState : uint8_t {
    NORMAL  = 0, // Hijau Solid
    BOOTING = 1, // Kuning Kedip 2 Hz
    OTA     = 2, // Oranye Fast Strobe 10 Hz
    FAULT   = 3, // Merah Solid
    CONFIG  = 4, // Biru Kedip 1 Hz (Config Mode: SoftAP / Direct PC)
    OFF     = 5  // Padam
};

enum class ComLedState : uint8_t {
    NORMAL        = 0, // Hijau Solid + Traffic Blip 90ms
    MQTT_OFFLINE  = 1, // Cyan Kedip 1 Hz
    CONNECTING    = 2, // Kuning Kedip 2 Hz
    SOFTAP        = 3, // Legacy alias (kini ditangani oleh SysLedState::CONFIG)
    DISCONNECTED  = 4, // Merah Double Blink
    OFF           = 5  // Padam
};

enum class UserRole : uint8_t {
    NONE     = 0,
    ENGINEER = 1,
    ADMIN    = 2
};

enum class ConnectionMode : uint8_t {
    WIFI     = 0,
    ETHERNET = 1
};

// =================================================================
// 3. HARDWARE PIN DEFINITIONS
// =================================================================
namespace Pins {
    // UART2 nRF52840 (1 Mbps)
    constexpr int NRF_RX = 16;
    constexpr int NRF_TX = 17;
    constexpr int NRF_BAUD = 1000000;

    // WS2812B NeoPixel (2 LED Daisy-Chain: SYS & COM)
    constexpr int LED_WS2812 = 13;
    constexpr int LED_COUNT = 2;

    // Boot Recovery Button (Active-LOW, pull-up eksternal 10k)
    constexpr int BUTTON_RESET = 34;

    // Ethernet W5500 (SPI2_HOST / VSPI)
    constexpr int W5500_CS   = 26;
    constexpr int W5500_SCK  = 18;
    constexpr int W5500_MISO = 19;
    constexpr int W5500_MOSI = 23;
    constexpr int W5500_RST  = 15;
    constexpr int W5500_INT  = 14;
    constexpr int W5500_SPI_CLOCK_MHZ = 16; // 16 MHz SPI Clock (PCB-proven sinyal bersih anti-noise)

    // I2C Master & AT24C256 EEPROM
    constexpr int I2C_SDA = 21;
    constexpr int I2C_SCL = 22;
    constexpr uint32_t I2C_FREQ_HZ = 400000;
    constexpr uint8_t AT24C256_I2C_ADDR = 0x50;
    constexpr size_t AT24C256_PAGE_SIZE = 64;
    constexpr size_t AT24C256_SLOT_SIZE = 128;
    constexpr size_t AT24C256_TOTAL_SLOTS = 200;
}

} // namespace Iotaru
