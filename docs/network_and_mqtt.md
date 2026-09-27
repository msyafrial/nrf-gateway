# Network Stack & MQTT Engine Specification - IOTARU SG-10

Dokumen ini menjelaskan implementasi stack jaringan (Ethernet W5500 SPI DMA, Wi-Fi Station & SoftAP, SNTP) serta arsitektur pengiriman data berkecepatan tinggi **MQTT Batch Publisher** pada **IOTARU SG-10**.

---

## 1. Arsitektur Jaringan Dual-Mode (Ethernet & Wi-Fi)

Gateway mendukung dua mode operasi jaringan fisik yang dapat dipilih melalui Web UI:

```
[Mode Jaringan]
      |
      +---> Mode 1: Ethernet W5500 (Prioritas Utama Industri)
      |         - Driver SPI DMA 25 MHz via VSPI
      |         - Terintegrasi langsung ke stack lwIP via esp_netif
      |         - Dukungan DHCP Client & Static IP
      |
      +---> Mode 0: Wi-Fi 802.11 b/g/n (Koneksi Nirkabel)
      |         - Station Mode (WPA/WPA2 Personal)
      |         - SoftAP Fallback On-Demand (Bila SSID kosong atau tombol ditekan)
```

### Fitur Kunci Network Manager:
1. **Dukungan Static IP & DNS Ganda**:
   - Baik Ethernet maupun Wi-Fi mendukung konfigurasi IP Manual (IP, Netmask, Gateway, Primary DNS, dan Secondary DNS).
   - Jika Primary DNS dikosongkan, sistem secara otomatis menetapkan alamat IP Gateway sebagai DNS Resolver transparan.
2. **Sinkronisasi Waktu SNTP (GMT+7 WIB)**:
   - Server NTP: `pool.ntp.org`.
   - Mengatur zona waktu perangkat ke `WIB-7` untuk logging dan scheduled reboot yang akurat.
3. **Watchdog Koneksi Terputus (`discon_rst`)**:
   - Jika koneksi jaringan terputus selama `discon_rst` menit (default: 30 menit), ESP32 secara otomatis melakukan restart bersih untuk memulihkan periferal jaringan.
4. **Jadwal Restart Otomatis Harian (`restart_schedule`)**:
   - Eksekusi restart terjadwal pada jam tertentu (misalnya pukul `03:00` dini hari) untuk memelihara stabilitas memori gateway.
5. **Pending Network Configuration Staging**:
   - Pengubahan parameter IP/SSID tidak langsung memutus sesi aktif pengguna. Konfigurasi disimpan dalam status *Staging (Pending Reboot)* dan baru aktif setelah pengguna melakukan restart atau dapat dibatalkan melalui antarmuka web.

---

## 2. Arsitektur MQTT Client & Protokol Komunikasi

Gateway menggunakan **Native ESP-IDF MQTT Client** (`esp-mqtt`) yang berjalan di Core 1:

### Konfigurasi Topik MQTT:
- **Status Perangkat / LWT (Last Will & Testament)**:
  - Topik: `status/<device_id>`
  - Online Payload: `{"status":"online","ip":"192.168.1.150","mode":"Ethernet","ver":"v1.0.1"}`
  - Offline / LWT Payload: `{"status":"offline"}` (QoS 1, Retain True)
- **Topik Data Uplink (Batch Beacon Stream)**:
  - Topik: `beacon/<device_id>` (Subtopic terikat langsung pada Hardware Device ID)
- **Topik Perintah Downlink (RPC Remote Command)**:
  - Topik: `cmd/<device_id>`
  - Payload yang didukung:
    - `{"cmd":"reboot"}` -> Memicu restart gateway
    - `{"cmd":"status"}` -> Mengirimkan status gateway segera

---

## 3. Protokol Streaming Biner: Format 6-Byte & 7-Byte

Untuk memaksimalkan efisiensi bandwidth transmisi radio/Ethernet dan mengurangi beban parsing JSON pada broker, data beacon dikirim dalam bentuk **payload biner terkompresi**:

### A. Format Standar 6-Byte (Legacy / Fixed iBeacon)
Setiap record beacon berukuran tepat 6 byte:
```
Offset  Ukuran  Tipe      Deskripsi
0..1    2 Byte  uint16_t  Major Tag (Big-Endian Network Byte Order)
2..3    2 Byte  uint16_t  Minor Tag (Big-Endian Network Byte Order)
4       1 Byte  int8_t    RSSI (Signed dBm, rentang -128 s/d 0)
5       1 Byte  uint8_t   Battery Percentage (0 s/d 100%)
```

### B. Format Diperluas 7-Byte (Dynamic Multi-Protocol)
Mendukung identifikasi otomatis antara format iBeacon standar dan frame iBeacon Extended (sensor telemetri):
```
Offset  Ukuran  Tipe      Deskripsi
0       1 Byte  uint8_t   Format Frame Type:
                          - 0x01: Standard iBeacon
                          - 0x02: Extended iBeacon (Sensor / Accelerometer)
1..2    2 Byte  uint16_t  Major Tag (Big-Endian)
3..4    2 Byte  uint16_t  Minor Tag (Big-Endian)
5       1 Byte  int8_t    RSSI (Signed dBm)
6       1 Byte  uint8_t   Battery Percentage (0 s/d 100%)
```

---

## 4. Mesin Batch Aggregator & Algoritma Flush

Alih-alih mengirimkan 1 paket MQTT untuk setiap 1 tag yang terbaca (yang membebani TCP socket dan overhead header IP), gateway mengumpulkan record ke dalam **Batch Buffer 4096 Byte**:

```
[nRF Frame] ---> [Queue (300)] ---> [Batch Buffer (Maks. 4096 Byte)]
                                              |
                   +--------------------------+--------------------------+
                   | Kondisi 1: Volume        | Kondisi 2: Waktu (Timer)
                   | Jumlah paket == batch_max| Timer batch_flush tercapai
                   | (misal: 100 paket)       | (misal: 300 ms)
                   +--------------------------+--------------------------+
                                              |
                                              v
                                   [MQTT PUBLISH (Core 1)]
```

### Kalkulasi Bandwidth Egress (Net Bandwidth HUD):
Metrik `Net Bandwidth` pada dashboard Web UI menghitung seluruh byte aktual yang keluar melalui antarmuka fisik:
$$\text{Total Egress} = \text{Payload Data} + \text{MQTT Header} + \text{TCP/IP Header (40B)} + \text{L2 Ethernet Header (18B)}$$
Hasil disajikan dalam satuan kilobyte per detik (`KB/s`) pada grafik telemetri 60 detik.
