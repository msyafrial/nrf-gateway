# Database NVS & Manajemen Konfigurasi - IOTARU SG-10

Dokumen ini menjelaskan struktur penyimpanan non-volatile (NVS Flash), mekanisme proteksi **Dual-Tier Recovery**, skema namespace, serta panduan sinkronisasi parameter pabrik pada **IOTARU SG-10**.

---

## 1. Arsitektur Dual-Tier NVS Recovery

Gateway menggunakan strategi dua lapisan penyimpanan pada partisi NVS (`0x9000`, 24 KB) untuk menjamin perangkat tidak akan pernah berada dalam kondisi *bricked* akibat kesalahan konfigurasi kredensial:

```
[Partisi NVS Flash (0x9000 - 24 KB)]
+---------------------------------------+---------------------------------------+
|        MASTER NAMESPACES              |        RUNTIME NAMESPACES             |
|   (Kredensial Pabrik / Hard Reference)|   (Dikelola & Dimodifikasi Pengguna)  |
+---------------------------------------+---------------------------------------+
| - "auth"    : adm_user, adm_pass      | - "cfg_auth": adm_user, adm_pass      |
| - "network" : mode, w_ssid, w_pass... | - "cfg_net" : mode, w_ssid, w_pass... |
| - "mqtt"    : host, port, user, pass  | - "cfg_mqtt": host, port, user, pass  |
| - "gateway" : dev_model, dev_ver...   | - "cfg_gate": dev_model, dev_ver...   |
+---------------------------------------+---------------------------------------+
                                   |
         [Factory Reset / Inisialisasi Pertama]
         Salin otomatis seluruh Master -> Runtime
```

### Alur Siklus Hidup Konfigurasi:
1. **Boot Perdana (Fresh Chip)**: Flag `cfg_sys/cfg_valid` bernilai `0`. Gateway secara otomatis menyalin seluruh nilai dari Master namespaces (`auth`, `network`, `mqtt`, `gateway`) ke Runtime namespaces (`cfg_*`), kemudian menetapkan `cfg_valid = 1`.
2. **Modifikasi Pengguna**: Saat pengguna mengubah password atau broker MQTT via Web UI, perubahan disimpan secara eksklusif ke namespace `cfg_*`. Master namespace tetap murni dan tidak tersentuh.
3. **Reset Pabrik (Factory Reset)**: Baik dipicu via tombol hardware (GPIO 34 > 10s) maupun REST API (`/api/v1/system/factory-reset`), gateway menghapus namespace `cfg_*` dan menyalin ulang nilai dari Master. Gateway seketika kembali ke kondisi awal pabrik tanpa perlu flashing ulang.

---

## 2. Skema Tabel Konfigurasi (`nvs_config.csv`)

Berkas [nvs_config.csv](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/nvs_config.csv) mendefinisikan parameter dasar pabrik:

```csv
key,type,encoding,value
auth,namespace,,
adm_user,data,string,admin
adm_pass,data,string,admin
tek_user,data,string,user
tek_pass,data,string,user
network,namespace,,
mode,data,u32,1
w_ssid,data,string,C43
w_pass,data,string,Eyro1234
w_dhcp,data,u32,1
w_ip,data,string,192.168.1.100
w_gw,data,string,192.168.1.1
w_sn,data,string,255.255.255.0
w_dns,data,string,8.8.8.8
w_dns2,data,string,
eth_dhcp,data,u32,1
eth_ip,data,string,192.168.1.150
eth_gw,data,string,192.168.1.1
eth_sn,data,string,255.255.255.0
eth_dns,data,string,8.8.8.8
eth_dns2,data,string,
ap_pass,data,string,12345678
mqtt,namespace,,
host,data,string,socket.cubeacon.com
port,data,u32,1884
user,data,string,eyroMQTT
pass,data,string,eyroMQTT1234
gateway,namespace,,
dev_model,data,string,SG-10
dev_ver,data,string,v1.0.0
uuid_en,data,u32,0
uuid,data,string,FDA50693A4E24FB1AFCFC6EB07647825
major_en,data,u32,0
major,data,string,
rssi_min,data,i32,-100
batch_max,data,u32,100
batch_flush,data,u32,300
data_ttl,data,u32,5000
restart_sch,data,string,03:00
discon_rst,data,u32,30
```

---

## 3. Sinkronisasi Otomatis Metadata (`embed_app_desc.py`)

Skrip [scripts/embed_app_desc.py](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/scripts/embed_app_desc.py) otomatis dieksekusi oleh PlatformIO setiap kali proses kompilasi atau build dijalankan:
1. Membaca variabel `[custom]` dari `platformio.ini`:
   - `device_model` (misal: `SG-10`)
   - `device_board_ver` (misal: `v1.0.0`)
   - `firmware_version` (misal: `v1.0.1`)
   - `ui_version` (misal: `v1.0.0`)
2. Memperbarui nilai `dev_model` dan `dev_ver` pada [nvs_config.csv](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/nvs_config.csv) secara otomatis jika ada perbedaan.
3. Memperbarui berkas [data/version.json](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/version.json) dan versi kompresi GZIP [data/version.json.gz](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/version.json.gz).

---

## 4. Pembuatan & Flashing Partisi Biner NVS (`generate_nvs.py` & `flash_nvs.py`)

Tersedia dua skrip otomatis untuk mempercepat proses pembuatan dan pengunggahan biner NVS:

### A. Generate Binary NVS (`generate_nvs.py`)
Skrip [generate_nvs.py](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/generate_nvs.py) secara otomatis:
1. Membaca metadata `[custom]` dari `platformio.ini`.
2. Menyinkronkan nilai `dev_model` dan `dev_ver` ke dalam `nvs_config.csv`.
3. Membaca ukuran partisi NVS (`0x6000` / 24 KB) dari `partitions.csv`.
4. Menghasilkan berkas `nvs.bin` berukuran tepat 24.576 byte via `nvs_partition_gen.py`.

```bash
python generate_nvs.py
```

### B. Flash NVS ke ESP32 (`flash_nvs.py`)
Skrip [flash_nvs.py](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/flash_nvs.py) menuliskan `nvs.bin` langsung ke partisi NVS (offset `0x9000`):

```bash
# Otomatis mendeteksi port COM
python flash_nvs.py

# Atau tentukan port COM secara spesifik
python flash_nvs.py COM3
```

