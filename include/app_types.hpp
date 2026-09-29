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

enum class BeaconType : uint8_t {
    UNKNOWN     = 0,
    IBEACON_EXT = 1,
    MINEW_B     = 1,
    IBEACON     = 2
};

struct BeaconPacket {
    uint32_t   timestamp; // Uptime timestamp (ms)
    BeaconType type;
    int8_t     rssi;
    uint8_t    battery;
    uint16_t   major;
    uint16_t   minor;
    uint8_t    uuid[16];
};

struct LiveBeaconItem {
    char    code[16];
    char    tag[24];
    int8_t  rssi;
    uint8_t batt;
};

enum class DropReason : uint8_t {
    TTL_EXPIRED       = 0, // Kedaluwarsa di antrean (> data_ttl)
    RSSI_OUT_OF_RANGE = 1, // RSSI di bawah ambang batas minimal (rssi_min)
    UUID_MISMATCH     = 2, // UUID tidak cocok dengan konfigurasi filter
    MAJOR_MISMATCH    = 3, // Major tidak masuk dalam rentang filter
    QUEUE_OVERFLOW    = 4, // Antrean FreeRTOS penuh 300/300 (drop-oldest)
    MQTT_FAIL         = 5  // Gagal publish ke MQTT broker
};

struct DroppedBeaconItem {
    char       code[16];
    char       tag[24];
    int8_t     rssi;
    DropReason reason;
};

constexpr size_t LIVE_FIFO_MAX = 50;
constexpr size_t DROPPED_FIFO_MAX = 50;
constexpr size_t BEACON_QUEUE_MAX = 300;
constexpr size_t MQTT_BATCH_BUFFER_SIZE = 4096;

// =================================================================
// 2. SYSTEM & HARDWARE ENUMS
// =================================================================
enum class SysLedState : uint8_t {
    NORMAL  = 0, // Hijau Solid
    BOOTING = 1, // Kuning Kedip 2 Hz
    OTA     = 2, // Oranye Fast Strobe 10 Hz
    FAULT   = 3, // Merah Solid
    OFF     = 4  // Padam
};

enum class ComLedState : uint8_t {
    NORMAL        = 0, // Hijau Solid + Traffic Blip 35ms
    MQTT_OFFLINE  = 1, // Cyan Kedip 1 Hz
    CONNECTING    = 2, // Kuning Kedip 2 Hz
    SOFTAP        = 3, // Biru Kedip 1 Hz
    DISCONNECTED  = 4, // Merah Double Blink
    OFF           = 5  // Padam
};

enum class UserRole : uint8_t {
    NONE    = 0,
    TEKNISI = 1,
    ADMIN   = 2
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
    constexpr int W5500_SPI_CLOCK_MHZ = 25; // 25-33 MHz DMA
}

} // namespace Iotaru
