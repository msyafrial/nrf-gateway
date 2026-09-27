# Native Web Server & REST API Specification - IOTARU SG-10

Dokumen ini memuat dokumentasi arsitektur **Native HTTP Web Server**, mesin rendering **Zero-FOUC Chunked Streaming Template**, kontrol sesi **RBAC (Role-Based Access Control)**, serta referensi lengkap seluruh endpoint **REST API** dan streaming **WebSocket** pada **IOTARU SG-10**.

---

## 1. Arsitektur Native Web Server & Zero-FOUC Streaming

Berbeda dengan pustaka Arduino `ESPAsyncWebServer`, gateway ini menggunakan server bawaan silikon **ESP-IDF HTTP Server** (`esp_http_server`) pada port 80.

```
Browser Client                    ESP32 (Core 1 - LittleFS VFS)
     |                                      |
     |---- GET /settings.html ------------->| Buka file di LittleFS (/storage)
     |                                      |
     |<--- HTTP 200 Chunked Transfer -------| Baca chunk 512-byte
     |     Chunk 1: "<!DOCTYPE html>..."    | In-flight replace: %model% -> "SG-10"
     |<--- Chunk 2: "...<title>SG-10..."    | In-flight replace: %device_id% -> "1a2b3c4d"
     |<--- Chunk 3: "...</html>"            | Kirim via httpd_resp_send_chunk()
     |<--- Chunk 0 (EOF) -------------------| Selesai (Nol Alokasi Heap Tambahan)
```

### Keunggulan Desain:
1. **Zero-FOUC (Flash of Unstyled Content)**: Judul halaman, model perangkat, dan identitas hardware di-injeksi langsung pada saat berkas HTML dialirkan ke browser. Browser tidak perlu menunggu respon API asynchronous tambahan untuk menampilkan identitas mesin.
2. **Nol Alokasi Dinamis String**: File di-stream dalam potongan kecil 512-byte menggunakan buffer statis, menghemat hingga 100 KB RAM dibandingkan membaca seluruh file HTML ke memori.
3. **Kompresi GZIP Transparan**: Jika browser mendukung `Accept-Encoding: gzip`, server otomatis mendeteksi keberadaan berkas `.gz` di LittleFS dan menyajikannya dengan header `Content-Encoding: gzip`.

---

## 2. Model Keamanan RBAC & Proteksi Brute-Force

Gateway menerapkan **Role-Based Access Control (RBAC)** dua tingkat:

| Peran (Role) | Hak Akses Fitur | Kredensial Default |
| :--- | :--- | :--- |
| **Administrator (`ROLE_ADMIN`)** | Akses penuh ke seluruh parameter: Konfigurasi Jaringan (IP/Wi-Fi/Ethernet/SoftAP), Pengaturan Broker MQTT, Manajemen Firmware OTA, dan Reset Pabrik. | User: `admin` / Pass: `admin` |
| **Teknisi (`ROLE_TEKNISI`)** | Akses diagnostik dan operasional: Halaman Overview, Live Test Monitor, serta pengujian konektivitas tanpa izin mengubah konfigurasi sensitif. | User: `user` / Pass: `user` |

### Mekanisme Pertahanan Siber:
- **Cookie Sesi Bertanda Waktu (`IOTARU_SESSION`)**: Token acak 32-karakter hex dihasilkan saat login berhasil, disimpan di memori RAM hingga sesi kedaluwarsa atau pengguna logout.
- **Timing Attack Defense Delay (1800 ms)**: Setiap percobaan login sengaja ditunda selama 1.800 ms secara konstan sebelum memberikan respon HTTP, menggagalkan teknik *timing analysis* peretas.
- **Brute-Force Rate Limiter**: Maksimal 5 kali kegagalan login dalam jendela waktu 60 detik per alamat IP client. Jika dilanggar, IP client diblokir sementara (*HTTP 429 Too Many Requests*).

---

## 3. Protokol Streaming WebSocket Telemetri (`/ws`)

Untuk visualisasi real-time berlatensi ultra-rendah pada halaman [test.html](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/test.html), gateway menyediakan kanal WebSocket di `/ws`:
- **Interval Broadcast**: Dijalankan oleh task independen di Core 1 setiap **100 ms** (10 Hz).
- **Struktur Payload Telemetri Real-Time**:
```json
{
  "read_speed": 185,
  "unique_beacons": 42,
  "mqtt_speed": 3,
  "queue_load": 12,
  "batch_load": 45,
  "last_batch_pkt": 95,
  "net_bandwidth": 14.8,
  "free_heap": 279,
  "packets": [
    {
      "format": "ibeacon",
      "major": 100,
      "minor": 205,
      "rssi": -65,
      "battery": 92
    }
  ]
}
```

---

## 4. Referensi Lengkap Endpoint REST API (`/api/v1/*`)

### A. Autentikasi & Sesi
| Endpoint | Method | Akses | Deskripsi & Payload |
| :--- | :---: | :---: | :--- |
| `/api/v1/auth/login` | `POST` | Publik | Autentikasi user. Body: `{"username":"admin","password":"..."}`. |
| `/api/v1/auth/logout` | `POST` | Sesi Aktif | Mengakhiri sesi aktif dan menghapus token sesi. |
| `/api/v1/auth/check` | `GET` | Publik | Memvalidasi status sesi aktif. Mengembalikan `{"authenticated":true,"role":"admin"}`. |
| `/api/v1/auth/change-password` | `POST` | Admin | Mengubah password admin/teknisi. Body: `{"target":"admin","new_pass":"..."}`. |

### B. Status & Sistem
| Endpoint | Method | Akses | Deskripsi & Payload |
| :--- | :---: | :---: | :--- |
| `/api/v1/system/status` | `GET` | Sesi Aktif | Status konektivitas, IP, MQTT broker, uptime, dan sisa RAM. |
| `/api/v1/system/version` | `GET` | Publik | Mengembalikan versioning 3-tingkat: `device_version`, `firmware_version`, dan `ui_version`. |
| `/api/v1/system/reboot` | `POST` | Admin | Memerintahkan restart bersih ESP32. |
| `/api/v1/system/factory-reset`| `POST` | Admin | Mengembalikan konfigurasi gateway ke pengaturan pabrik. |

### C. Konfigurasi Gateway & Staging
| Endpoint | Method | Akses | Deskripsi & Payload |
| :--- | :---: | :---: | :--- |
| `/api/v1/config/get` | `GET` | Admin | Mengambil seluruh konfigurasi aktif (Network, MQTT, Filters). |
| `/api/v1/config/save` | `POST` | Admin | Menyimpan konfigurasi ke NVS staging. Body: Form JSON konfigurasi. |
| `/api/v1/config/network/discard`| `POST`| Admin | Membatalkan perubahan konfigurasi jaringan staging yang tertunda. |
| `/api/v1/config/export` | `GET` | Admin | Mengunduh berkas backup konfigurasi terenkripsi. |
| `/api/v1/config/import` | `POST` | Admin | Memulihkan konfigurasi dari berkas backup. |

### D. Pengujian & Diagnostik
| Endpoint | Method | Akses | Deskripsi & Payload |
| :--- | :---: | :---: | :--- |
| `/api/v1/test/start` | `POST` | Teknisi+ | Mengaktifkan live stream ingestion ke kanal WebSocket `/ws`. |
| `/api/v1/test/stop` | `POST` | Teknisi+ | Menghentikan stream WebSocket untuk analisa statis. |
| `/api/v1/test/status` | `GET` | Teknisi+ | Mengambil metrik throughput terkini. |

### E. Firmware & Filesystem OTA
| Endpoint | Method | Akses | Deskripsi & Payload |
| :--- | :---: | :---: | :--- |
| `/api/v1/ota/upload` | `POST` | Admin | Multi-part upload biner firmware (`firmware.bin`) atau filesystem (`littlefs.bin`). Dilengkapi proteksi anti-brick. |
