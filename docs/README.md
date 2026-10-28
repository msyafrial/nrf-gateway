# IOTARU SG-10 - ESP-IDF BLE Beacon Gateway Documentation

Selamat datang di dokumentasi teknis resmi **IOTARU SG-10 ESP-IDF Native Firmware**.

Dokumentasi ini disusun dengan standar arsitektur industri untuk memberikan panduan komprehensif mulai dari integrasi hardware, pemisahan tugas asimetris Dual-Core FreeRTOS, konfigurasi database NVS silikon native, referensi REST API & WebSocket, manajemen Carrier-Grade Dual OTA, hingga panduan flashing produksi massal.

---

## 🌟 Ikhtisar Gateway (Gateway Overview)

IOTARU SG-10 adalah gateway IoT berkinerja tinggi berbasis **ESP32 Dual-Core (240 MHz)** yang berjalan di atas framework **ESP-IDF v6 murni**. Gateway ini bertindak sebagai jembatan data antara modul scanner BLE frekuensi tinggi (**nRF52840**) dengan **Broker MQTT** pusat, baik melalui jalur kabel industri **Ethernet W5500 via SPI DMA** maupun koneksi nirkabel **Wi-Fi**.

```mermaid
graph TD
    subgraph BLE_Ingestion["Subsystem Ingestion (Core 0 - PRO_CPU)"]
        A["Beacon Tag (iBeacon / Extended)"] -->|BLE 2.4GHz Advertisements| B["nRF52840 Co-Processor"]
        B -->|UART2 1 Mbps (Frame 25-Byte)| C["UART ISR & Ring-Buffer (4KB)"]
        C --> D["Fast XOR CRC & Frame Evaluator"]
        D --> E["Hardware Filters (RSSI, UUID, Major)"]
        E --> F["FreeRTOS Beacon Queue (300 Slots Drop-Oldest)"]
        E --> G["Live Circular Buffer (50 Slots Atomik)"]
    end

    subgraph Network_Stack["Subsystem Network & App (Core 1 - APP_CPU)"]
        F --> H["MQTT Batch Publisher (Buffer 4096B Binary 6B/7B)"]
        H -->|SPI DMA 25 MHz| I["W5500 Ethernet MAC/PHY"]
        H -->|802.11 b/g/n| J["ESP32 Wi-Fi Station"]
        I --> K["Central MQTT Broker"]
        J --> K
        G --> L["WebSocket Broadcaster (/ws @ 100ms)"]
        M["Operator Web Client"] -->|HTTP REST API & Zero-FOUC UI| N["Native HTTP Server (Port 80)"]
    end
```

### Spesifikasi Teknis Utama:
- **Processor**: ESP32-D0WDQ6 Dual-Core Xtensa® LX6 @ 240 MHz, 320 KB SRAM, 4 MB SPI Flash.
- **Framework Core**: **ESP-IDF v6.1.0** murni (PlatformIO `espressif32@7.1.3`) dengan kompilasi `-std=gnu++17`, FreeRTOS 1000 Hz, dan lwIP TCP MSS 1460.
- **BLE Scanner Co-Processor**: nRF52840 terhubung via UART2 berkecepatan **1.000.000 bps (1 Mbps)**, diproses eksklusif di Core 0.
- **Jaringan Kabel Industri**: Ethernet 10/100 Mbps via chip **W5500** berantarmuka SPI DMA berkecepatan **25 MHz**.
- **Jaringan Nirkabel**: Wi-Fi 802.11 b/g/n (Station Mode dengan SoftAP On-Demand fallback untuk darurat/konfigurasi).
- **Indikator Visual**: Dual Addressable RGB LED (WS2812B / SK6805) Daisy-Chain pada GPIO 13 dengan bitbang timing presisi (< 60 µs).
- **Antarmuka Web**: Web UI responsif Glassmorphism tersimpan di partisi **LittleFS** (1 MB), disajikan via *Zero-FOUC Chunked Streaming Template Engine*.
- **Keamanan Kredensial**: **Zero Hardcoded Secrets** — seluruh konfigurasi tersimpan aman di partisi NVS Flash dengan mekanisme *Dual-Tier Recovery*.
- **Memori & Efisiensi**: Penggunaan internal SRAM hanya **14.8%** (~48.6 KB terpakai), menyisakan **~279 KB RAM bebas** tanpa fragmentasi heap (`String` class 100% ditiadakan).

---

## 📑 Sitemap Dokumentasi Industri

Dokumentasi ini dibagi ke dalam modul-modul spesifik sesuai standar operasional industri:

| Dokumen | Deskripsi | Target Pembaca |
| :--- | :--- | :--- |
| **[1. Hardware Pinout & Wiring](hardware_and_pinout.md)** | Skema pinout lengkap ESP32, wiring W5500 SPI DMA, nRF52840 UART2, WS2812B, tombol recovery, serta kalkulasi catu daya. | Hardware / Electronic Engineers |
| **[2. Arsitektur Firmware](firmware_architecture.md)** | Arsitektur Dual-Core FreeRTOS (Core 0 vs Core 1), zero-allocation pipeline, thread safety, dan struktur direktori C++ modular. | Firmware Engineers (C++) |
| **[3. Network Stack & MQTT Engine](network_and_mqtt.md)** | Driver W5500 SPI DMA, Wi-Fi failover, sinkronisasi SNTP GMT+7, MQTT batch engine biner 6B/7B, dan penghitung throughput egress. | Network & Cloud Engineers |
| **[4. Native Web Server & REST API](web_and_rest_api.md)** | Zero-FOUC chunked streaming template, RBAC auth session, spesifikasi 25+ endpoint REST API, dan protokol WebSocket `/ws`. | Frontend & Backend Developers |
| **[5. Database NVS & Konfigurasi](nvs_and_configuration.md)** | Skema namespace NVS, mekanisme Dual-Tier protection (`cfg_*` vs Master), skrip sinkronisasi otomatis, dan prosedur factory reset. | Teknisi Lapangan / System Admin |
| **[6. Carrier-Grade Dual OTA](carrier_grade_ota.md)** | Proteksi partisi dual OTA `app0`/`app1`, validasi pre-flight signature `MODEL@BOARD_VERSION`, LittleFS OTA, dan hardware anti-brick rollback. | DevOps & Maintenance Engineers |
| **[7. Panduan Flashing & Produksi Massal](flashing_and_production.md)** | Peta memori 6 file `.bin`, offset alamat memori, panduan GUI Espressif Flash Tool, serta perintah 1-baris CLI untuk chip kosong. | Tim Produksi & Quality Control |
| **[8. Panduan Git Trial & Restore](../PANDUAN_GIT_TRIAL_DAN_RESTORE.md)** | Alur kerja branch eksperimen (trial), isolasi kode lokal, cara beralih/merge/rollback kembali ke branch `main`, dan restore tag rilis. | Software & Firmware Engineers |

---

## 🚀 Panduan Memulai Cepat (Quick Start)

### 1. Prasyarat Lingkungan
- **Visual Studio Code** dengan ekstensi **PlatformIO IDE**.
- **Python 3.8+** (termasuk modul standard `json`, `os`, `gzip`).
- Driver USB UART (CP2102 / CH340).

### 2. Konfigurasi Target di `platformio.ini`
Metadata perangkat dikonfigurasikan terpusat pada blok `[custom]`:
```ini
[custom]
device_model = SG-10
device_board_ver = v1.0.0
firmware_version = v1.0.1
ui_version = v1.0.0
```

### 3. Kompilasi & Build Seluruh Komponen
Jalankan perintah berikut di terminal PlatformIO:

```bash
# 1. Kompilasi firmware utama ESP-IDF murni
pio run

# 2. Build image filesystem Web UI (LittleFS)
pio run -t buildfs

# 3. Flashing firmware & bootloader ke ESP32
pio run -t upload

# 4. Flashing filesystem LittleFS ke ESP32
pio run -t uploadfs
```
