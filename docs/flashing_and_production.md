# Panduan Flashing & Produksi Massal (Blank Chip) - IOTARU SG-10

Dokumen ini merupakan panduan teknis bagi tim produksi, teknisi perakitan, dan quality control (QC) untuk memprogram modul ESP32 baru (*blank chip*) dari pabrik menjadi gateway **IOTARU SG-10** siap pakai.

---

## 1. Peta Memori & Tabel Berkas Biner (Flash Offset Map)

Untuk memprogram chip ESP32 4MB yang masih kosong, dibutuhkan 6 berkas biner utama yang harus dituliskan ke alamat offset memori flash berikut:

| No | Berkas Biner | Alamat Offset | Ukuran Maksimal | Keterangan / Fungsi |
| :---: | :--- | :---: | :---: | :--- |
| 1 | `bootloader.bin` | `0x1000` | 28 KB | ESP-IDF 2nd stage bootloader dengan dukungan hardware rollback. |
| 2 | `partitions.bin` | `0x8000` | 4 KB | Tabel partisi flash (mendefinisikan dual OTA, LittleFS, NVS). |
| 3 | `nvs.bin` | `0x9000` | 24 KB | Konfigurasi awal NVS (dihasilkan via `nvs_partition_gen.py`). |
| 4 | `otadata.bin` | `0xF000` | 8 KB | Pointer aktif bootloader untuk memilih slot partisi `app0`. |
| 5 | `firmware.bin` | `0x20000` | 1.43 MB | Firmware aplikasi utama gateway (ESP-IDF v6 C++ Modular). |
| 6 | `firmware.bin` (Opsional)| `0x190000` | 1.43 MB | Slot cadangan OTA `app1` (diisi firmware identik untuk redundansi). |
| 7 | `spiffs.bin` (LittleFS) | `0x300000` | 1.00 MB | Aset Web UI, CSS, JavaScript, SweetAlert, dan ikon. |

---

## 2. Lokasi Berkas Biner Hasil Kompilasi di PC

Setelah menjalankan `pio run` dan `pio run -t buildfs`, semua berkas biner tersimpan di direktori:
`c:\Users\syafr\OneDrive\Documents\PlatformIO\Projects\esp32_ibeacon_espidf\.pio\build\esp32doit-devkit-v1\`

1. **Bootloader**: `.pio/build/esp32doit-devkit-v1/bootloader.bin`
2. **Partition Table**: `.pio/build/esp32doit-devkit-v1/partitions.bin`
3. **Firmware**: `.pio/build/esp32doit-devkit-v1/firmware.bin`
4. **Filesystem**: `.pio/build/esp32doit-devkit-v1/spiffs.bin`

---

## 3. Metode A: Flashing Menggunakan Espressif Flash Download Tool (GUI)

Metode ini direkomendasikan untuk operator lini produksi di pabrik:

```
+-----------------------------------------------------------------------------------+
|                           ESPRESSIF FLASH DOWNLOAD TOOL                           |
+-----------------------------------------------------------------------------------+
| [V] ...\bootloader.bin                             @ 0x1000                       |
| [V] ...\partitions.bin                             @ 0x8000                       |
| [V] ...\nvs.bin                                    @ 0x9000                       |
| [V] ...\firmware.bin                               @ 0x20000                      |
| [V] ...\firmware.bin                               @ 0x190000                     |
| [V] ...\spiffs.bin                                 @ 0x300000                     |
+-----------------------------------------------------------------------------------+
| SPI SPEED: 40MHz         SPI MODE: DIO          FLASH SIZE: 32Mbit (4MB)          |
| COM PORT : COMx          BAUDRATE: 921600       [ START ]                         |
+-----------------------------------------------------------------------------------+
```

### Langkah Kerja:
1. Hubungkan modul ESP32 ke port USB PC.
2. Buka software resmi **Espressif Flash Download Tool**.
3. Pilih Chip: **ESP32**, WorkMode: **Factory**.
4. Masukkan baris file `.bin` dan offset sesuai tabel di atas, pastikan semua kotak centang aktif.
5. Konfigurasikan:
   - **SPI SPEED**: `40MHz`
   - **SPI MODE**: `DIO`
   - **FLASH SIZE**: `32Mbit` (atau `4MB`)
   - **BAUDRATE**: `921600`
6. Pilih nomor **COM Port**, tekan tombol **START**, dan tunggu hingga indikator berubah menjadi hijau (**FINISH**).

---

## 4. Metode B: Flashing Menggunakan Perintah 1-Baris CLI (`esptool.py`)

Untuk pengujian cepat atau integrasi ke skrip otomasi pengujian pabrik (*Automated Test Fixture*):

```bash
esptool.py --chip esp32 --port COM_PORT --baud 921600 --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000   .pio/build/esp32doit-devkit-v1/bootloader.bin \
  0x8000   .pio/build/esp32doit-devkit-v1/partitions.bin \
  0x9000   nvs.bin \
  0x20000  .pio/build/esp32doit-devkit-v1/firmware.bin \
  0x190000 .pio/build/esp32doit-devkit-v1/firmware.bin \
  0x300000 .pio/build/esp32doit-devkit-v1/spiffs.bin
```

---

## 5. Checklist Uji Fungsi Akhir (Quality Control Acceptance)

Sebelum perangkat dikemas ke dalam casing:
- [ ] **Serial Log Boot (115200 bps)**: Muncul log `[CONFIG_STORE] Device Hardware ID: ...` dan `[W5500] Initialized OK`.
- [ ] **Indikator LED WS2812B**:
  - LED 1 [SYS] menyala hijau solid (`SYS_LED_NORMAL`).
  - LED 2 [COM] berkedip kuning saat mencari IP, kemudian hijau dengan blip padam saat terhubung ke jaringan & broker MQTT.
- [ ] **Uji Web UI**: Buka browser ke IP gateway, verifikasi dialog login muncul, masukkan `admin`/`admin`.
- [ ] **Uji Live Test Ingestion**: Buka halaman [test.html](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/test.html), tekan *Start Test*, dan pastikan paket beacon nRF terbaca pada grafik throughput 60 detik.
