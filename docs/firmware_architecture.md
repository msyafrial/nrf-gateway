# Arsitektur Firmware Modular - IOTARU SG-10 (ESP-IDF Native)

Dokumen ini memuat arsitektur internal firmware **IOTARU SG-10** yang dibangun di atas framework **ESP-IDF v6** murni menggunakan C++ modular terstruktur (`-std=gnu++17`).

---

## 1. Asymmetric Dual-Core FreeRTOS Model

Silikon ESP32 memiliki dua inti prosesor Xtensa® LX6 berkecepatan 240 MHz. Gateway membagi beban kerja secara asimetris untuk menjamin pemrosesan paket radio berkecepatan tinggi tanpa terganggu latensi soket jaringan atau rendering antarmuka web.

```
+--------------------------------------------------+--------------------------------------------------+
|               CORE 0 (PRO_CPU)                   |               CORE 1 (APP_CPU)                   |
|       Dedicated BLE Ingestion Engine             |         Application, Network & UI Stack          |
+--------------------------------------------------+--------------------------------------------------+
| [Hardware UART2 ISR] (1.000.000 bps)             | [lwIP TCP/IP Stack]                              |
|       |                                          |       |                                          |
| [FreeRTOS UART Event Queue (4KB Ring-Buffer)]    | [W5500 SPI DMA Driver & Interrupt Handler]       |
|       |                                          |       |                                          |
| [Stream Parser State Machine (Sync 0xAA 0x55)]   | [Network Manager (Wi-Fi STA / SoftAP / SNTP)]    |
|       |                                          |       |                                          |
| [Fast XOR Checksum Evaluator]                    | [MQTT Batch Publisher Task]                      |
|       |                                          |   - Buffer 4096B (Format biner 6B & 7B)          |
| [Hardware In-Line Filter Engine]                 |   - Auto-flush timer (100–1000 ms)               |
|   - RSSI Minimum Threshold (-100 dBm s/d -30 dBm)|       |                                          |
|   - Target UUID Filter (16-Byte Hex Matching)    | [Native HTTP Web Server Task (Port 80)]          |
|   - Target Major Filter (0–65535 Multi-Match)    |   - Zero-FOUC Chunked Streaming Template         |
|       |                                          |   - REST API Controller (/api/v1/*)              |
| [Thread-Safe Buffer Dispatch]:                   |       |                                          |
|   - beaconQueue (300 slots FreeRTOS Drop-Oldest) | [WebSocket Live Telemetry Broadcaster Task]      |
|   - Live CircularBuffer (50 slots Atomic Lockless)       - Streaming interval 100 ms (/ws)              |
+--------------------------------------------------+--------------------------------------------------+
```

---

## 2. Struktur Direktori C++ Modular

Semua kode sumber dikelompokkan ke dalam namespace terstruktur `Iotaru::` untuk modularitas dan keterbacaan tingkat tinggi:

```
esp32_ibeacon_espidf/
├── include/
│   ├── app_config.hpp          <- Struct POD konfigurasi global & variabel versioning
│   └── app_types.hpp           <- Definisi struct biner frame nRF 25-byte & enum class
├── src/
│   ├── main.cpp                <- Orchestrator utama, init subsystem, supervisi status
│   ├── bsp/                    <- Board Support Package (Hardware Silikon)
│   │   ├── bsp_button.hpp/.cpp <- Synchronous boot-button detection (GPIO 34)
│   │   ├── bsp_led.hpp/.cpp    <- Bitbang WS2812B controller & status blink task (GPIO 13)
│   │   └── bsp_uart.hpp/.cpp   <- UART2 1 Mbps hardware driver (GPIO 16/17, Core 0)
│   ├── core/                   <- Mesin Pemrosesan Inti
│   │   ├── beacon_pipeline.hpp/.cpp <- Ingestion parser, hardware filter, queues
│   │   └── config_store.hpp/.cpp    <- NVS Dual-Tier storage manager (Master & Cfg)
│   ├── network/                <- Network & Protocol Subsystems
│   │   ├── drivers/
│   │   │   ├── IEthernetDriver.hpp   <- Abstract HAL interface
│   │   │   └── EthernetW5500.hpp/.cpp<- Driver native W5500 SPI DMA 25 MHz
│   │   ├── net_manager.hpp/.cpp      <- Wi-Fi STA, SoftAP on-demand, SNTP time sync
│   │   └── mqtt_service.hpp/.cpp     <- Native esp-mqtt, batch publisher 4KB, LWT
│   ├── web/                    <- Web UI & Management Subsystem
│   │   ├── emergency_page.hpp/.cpp   <- Self-contained PROGMEM recovery page
│   │   ├── ota_service.hpp/.cpp      <- Carrier-Grade Dual OTA & image signature validator
│   │   ├── session_mgr.hpp/.cpp      <- RBAC session manager & brute-force defense
│   │   ├── web_routes.hpp/.cpp       <- REST API endpoints & static assets routing
│   │   ├── web_server.hpp/.cpp       <- Native HTTP server & WebSocket /ws task
│   │   └── web_template.hpp/.cpp     <- Zero-FOUC chunked streaming template engine
│   └── utils/
│       └── crc_utils.hpp             <- Fast XOR frame checksum & validator
```

---

## 3. Filosofi Desain Memori: Zero Dynamic String Allocation

Masalah klasik pada firmware berbasis framework Arduino adalah terjadinya **Heap Fragmentation** akibat penggunaan kelas `String` (`std::string` atau Arduino `String`). Setelah beroperasi beberapa hari, alokasi memori yang terpecah-pecah menyebabkan `malloc()` gagal dan ESP32 mengalami crash (*Out of Memory / Guru Meditation Error*).

### Prinsip yang Diterapkan di ESP-IDF Murni:
1. **Tidak Ada Kelas `String`**: Seluruh representasi string menggunakan buffer statis karakter bertipe array (`char buffer[N]`) atau `std::string_view` non-alokasi.
2. **Bounds Checking Ketat**: Semua operasi pemformatan teks menggunakan `snprintf()` dengan ukuran array terdefinisi untuk mencegah *buffer overflow*.
3. **Ukuran Struktur POD Terdefinisi**: Struct `AppConfig` merupakan *Plain Old Data* (POD) yang dialokasikan sekali di segmen BSS (`g_config`), bebas dari konstruktor dinamis.
4. **Alokasi Heap Nol pada Hot Path**: Jalur transmisi data dari radio nRF menuju MQTT batch buffer dan WebSocket tidak melakukan satu pun panggilan `malloc()` atau `new` selama runtime.

---

## 4. Keamanan Thread (Thread Safety) & Sinkronisasi

Komunikasi antar-Core dilakukan tanpa memblokir thread eksekusi utama:
1. **Antrean MQTT `beaconQueue`**: Menggunakan antrean FreeRTOS native `xQueueSend()` dengan timeout 0 tick. Jika antrean penuh (mencapai 300 paket), paket terlama dibuang (*drop-oldest*) agar throughput tidak macet.
2. **Circular Buffer Telemetri Live**: Menggunakan pointer ring-buffer dengan penanda atomik `std::atomic<uint32_t>` C++11, memungkinkan task WebSocket membaca paket secara non-blocking bersamaan dengan task ingestion yang sedang menulis.
3. **Penghitung Throughput Atomik**: Variabel metrik kinerja (`m_readCount`, `m_mqttCount`, `m_batchPktCount`, dll.) menggunakan `std::atomic` untuk menjamin keakuratan statistik tanpa membutuhkan mutex lock yang membebani CPU.


