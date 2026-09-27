# Hardware Pinout & Wiring Specification - IOTARU SG-10

Dokumen ini memuat spesifikasi hardware, konfigurasi pinout silikon ESP32, antarmuka periferal (Ethernet W5500, UART2 nRF52840, WS2812B, Tombol Recovery), serta panduan catu daya untuk gateway **IOTARU SG-10**.

---

## 1. Tabel Alokasi Pinout ESP32 (Pin Assignment)

| GPIO | Fungsi Sinyal | Periferal Terhubung | Arah I/O | Keterangan & Catatan Hardware |
| :---: | :--- | :--- | :---: | :--- |
| **GPIO 26** | `SPI2_CS` | W5500 SPI Chip Select | Output | Active-Low. Dikelola oleh SPI Master Driver. |
| **GPIO 18** | `SPI2_SCK` | W5500 SPI Clock | Output | Frekuensi bus **25 MHz** (`SPI_MASTER_FREQ_25M`). |
| **GPIO 19** | `SPI2_MISO` | W5500 SPI Master In | Input | Jalur transfer RX dari W5500 ke ESP32. |
| **GPIO 23** | `SPI2_MOSI` | W5500 SPI Master Out | Output | Jalur transfer TX dari ESP32 ke W5500. |
| **GPIO 15** | `ETH_RST` | W5500 Hardware Reset | Output | Active-Low reset pulse (durasi 100 ms). |
| **GPIO 14** | `ETH_INT` | W5500 Hardware Interrupt | Input | Active-Low Falling Edge ISR (`GPIO_INTR_NEGEDGE`). |
| **GPIO 16** | `UART2_RX` | nRF52840 TX Out | Input | Baudrate **1.000.000 bps (1 Mbps)**, 8N1. Terkunci ke Core 0. |
| **GPIO 17** | `UART2_TX` | nRF52840 RX In | Output | Baudrate **1.000.000 bps (1 Mbps)**. Kontrol uplink/command. |
| **GPIO 13** | `NEOPIXEL_DATA`| WS2812B / SK6805 Data | Output | Daisy-chain 2 RGB LED (LED 1 [SYS], LED 2 [COM]). |
| **GPIO 34** | `BTN_RECOVERY` | Push Button Reset | Input | Input-only (GPI). Active-Low, wajib resistor pull-up 10kΩ ke 3.3V. |

---

## 2. Antarmuka Ethernet W5500 (SPI DMA Acceleration)

Modul W5500 berkomunikasi dengan ESP32 menggunakan kontroler **SPI2 (VSPI)** dengan akselerasi **DMA Hardware Channel Auto**.

```
ESP32 DevKit                  W5500 Ethernet Module
+--------------------+        +--------------------+
|            GPIO 26 |------->| CS / NSS           |
|            GPIO 18 |------->| SCK / SCLK         |
|            GPIO 19 |<-------| MISO / SO          |
|            GPIO 23 |------->| MOSI / SI          |
|            GPIO 15 |------->| RST (Active-Low)   |
|            GPIO 14 |<-------| INT (Active-Low)   |
|               3.3V |------->| 3V3 (Power Rail)   |
|                GND |------->| GND (Common Ground)|
+--------------------+        +--------------------+
```

### Karakteristik Teknis Bus SPI:
- **Bus Speed**: 25 MHz (`SPI_MASTER_FREQ_25M`).
- **SPI Mode**: Mode 0 (`CPOL=0, CPHA=0`).
- **DMA Buffer**: Menggunakan internal DMA buffer yang dialokasikan di SRAM untuk memastikan throughput jaringan lwIP mencapai saturasi maksimal tanpa membebani siklus CPU.
- **Handling Interrupt**: Interrupt W5500 dipicu saat frame data RX masuk ke buffer internal W5500, membangkitkan ISR yang langsung mengalirkan paket ke stack `esp_netif`.

---

## 3. Antarmuka Ingestion BLE Co-Processor (nRF52840)

Co-processor nRF52840 bertindak sebagai dedicated high-speed radio scanner yang menangkap seluruh paket BLE advertisement di kanal 37, 38, dan 39, kemudian mengalirkan paket yang telah di-parsing ke ESP32 dalam format frame biner 25-byte.

```
ESP32 DevKit                  nRF52840 Co-Processor
+--------------------+        +--------------------+
|     GPIO 16 (RX2)  |<-------| TXD (UART)         |
|     GPIO 17 (TX2)  |------->| RXD (UART)         |
|                GND |------->| GND (Common Ground)|
+--------------------+        +--------------------+
```

### Parameter UART2:
- **Baudrate**: `1.000.000 bps` (1 Mbps).
- **Format Data**: 8 Data bits, No Parity, 1 Stop bit (8N1).
- **Hardware FIFO Ring-Buffer**: `4096 bytes` internal ESP-IDF UART buffer.
- **Core Affinity**: Driver UART2 dan parsing state machine dipasang secara eksklusif pada **Core 0 (PRO_CPU)** untuk mengisolasi latensi dari proses jaringan dan web server di Core 1.

---

## 4. Indikator Visual Addressable RGB LED (WS2812B)

Dua buah LED RGB tipe WS2812B atau SK6805 dipasang secara serial (*daisy-chain*) pada **GPIO 13**:
- **LED Indeks 0 [SYS]**: Menunjukkan kondisi internal, RTOS, inisialisasi boot, dan status flashing OTA.
- **LED Indeks 1 [COM]**: Menunjukkan status komunikasi jaringan (Ethernet / Wi-Fi) dan koneksi ke Broker MQTT.

```
ESP32 (GPIO 13) ----> [DIN] LED 1 (SYS) [DOUT] ----> [DIN] LED 2 (COM)
```

### Timing Driver Silikon:
- Menggunakan driver bitbang siklus presisi berbasis assembly (`nop`) berkecepatan tinggi:
  - T0H: ~350 ns, T0L: ~800 ns
  - T1H: ~700 ns, T1L: ~600 ns
  - Latch / Reset: > 60 µs
- Durasi bitbang untuk 2 LED hanya membutuhkan waktu **~60 mikrodetik**, dijalankan dalam task FreeRTOS independen di Core 1 dengan prioritas sedang (`tskIDLE_PRIORITY + 2`).

---

## 5. Tombol Recovery / Multi-Function Button (GPIO 34)

GPIO 34 merupakan pin *Input-Only* (GPI) tanpa internal pull-up/pull-down resistor pada silikon ESP32.

```
3.3V Rail
   |
  [R 10kΩ] (Pull-up Eksternal)
   |
   +--------> ESP32 GPIO 34
   |
 [Button]
   |
  GND
```

### Logika Durasi Penekanan Saat Boot (Zero-Boot Detection):
1. **< 5 Detik (Batal)**: Tombol dilepas sebelum 5 detik; sistem mengabaikan penekanan dan melanjutkan boot normal.
2. **5 – 10 Detik (Mode Hotspot AP Config)**: Gateway membatalkan koneksi Ethernet/Wi-Fi STA dan seketika menyalakan Wi-Fi SoftAP darurat (`SG-10-XXXXXXXX`) untuk konfigurasi IP/kredensial via browser di `192.168.4.1`.
3. **> 10 Detik (Factory Reset)**: Gateway menghapus partisi NVS `cfg_*`, mengembalikan semua kredensial ke nilai Master pabrik, dan me-reboot gateway secara otomatis.

---

## 6. Persyaratan Catu Daya & Desain Daya (Power Supply Guidelines)

Konsumsi arus puncak gateway saat seluruh modul aktif:
- ESP32 (Wi-Fi TX burst): ~240 mA
- W5500 (Ethernet 100M TX): ~160 mA
- nRF52840 (BLE Active Scan): ~15 mA
- WS2812B (2 LED Full White 100%): ~40 mA
- **Total Beban Puncak**: **~455 mA**

### Rekomendasi Desain Regulator:
- Gunakan LDO atau Buck Converter 3.3V dengan kapasitas arus kontinu minimal **800 mA hingga 1.000 mA (1 A)** (misalnya regulator tipe *AMS1117-3.3*, *ME6211*, atau buck converter *MP2307*).
- Pasang kapasitor de-coupling minimum `10 µF` (Tantalum/Ceramic) dan `100 nF` sedekat mungkin dengan pin VDD modul W5500 dan nRF52840 untuk mencegah fluktuasi tegangan saat transmisi SPI atau RF.
