# IOTARU ESP32 Gateway Design System

## 1. Color Palette & Semantic Tokens
- **Background Deep**: `#060b13` / `#0b1322`
- **Surface Bento Card**: `rgba(15, 23, 42, 0.75)` with `backdrop-filter: blur(12px)`
- **Border**: `rgba(255, 255, 255, 0.08)` / Focused: `#38bdf8` (`rgba(56, 189, 248, 0.35)`)
- **Primary Accent**: `#38bdf8` (Cyan Neon), `#0284c7` (Sky Deep)
- **Text Primary**: `#f8fafc` (White)
- **Text Muted**: `#94a3b8` (Slate 400)
- **Success**: `#22c55e` (Emerald)
- **Warning**: `#fbbf24` (Amber)
- **Danger**: `#ef4444` (Rose)

## 2. Typography
- **UI Font**: system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", sans-serif
- **Mono / Numeric Font**: `Consolas`, `Fira Code`, `SF Mono`, `monospace` (Tabular figures)
- **Section Headers**: `1.05rem`, font-weight `700`, letter-spacing `0.3px`
- **Labels**: `0.85rem` - `0.88rem`, font-weight `600`, `#94a3b8`

## 3. Bento Card Layout
- `display: flex; flex-direction: column; gap: 20px;` on `#settingsValidation`.
- Mobile padding: `padding: 16px 14px calc(var(--bottom-nav-h) + 85px) 14px;` ensuring sticky dock never obscures content.
- Clean hierarchy: No redundant `.bento-sub` sub-headers or repetitive `<small>` field hints.

## 4. Subtopic Architecture (Lock to Device ID)
- Replaced manual `id_module` text input with an immutable, glowing subtopic pill: `beacon/%device_id%`.
- Subtopic Prefix: `#94a3b8` (`beacon/`), Hardware Device ID: `#38bdf8` (`1a2b3c4d`).

## 5. Daily Reboot Schedule Toggle
- iOS tactile toggle switch (`#restartScheduleToggle`) coupled with container `#restartScheduleContainer`.
- When toggled OFF: container gains `.dimmed` (`opacity: 0.35; filter: grayscale(0.7); pointer-events: none;`) and submits empty string `""` so ESP32 clears scheduled reboot.

## 6. RSSI Minimum Threshold & Slider Control (2026 Modern Ergonomics)
- **Centered Attached-Minus Pill (`.rssi-merged-pill`)**:
  - Centered horizontally in the card via `.rssi-center-wrap` (`justify-content: center`).
  - Minus sign (`.rssi-minus`) styled in `#38bdf8`, attached directly to the digits with `0` padding and `0` margin gap (`[-80 dBm]`).
  - Monospace numeric input with dynamic character width (`style.width = len + 'ch'`) ensuring zero whitespace.
  - Clicking anywhere on the pill focuses the input.
- **Range Slider Bounds**:
  - `min="30" max="100" step="1"`.
  - Native track content-box override: `padding: 0 !important; margin: 0 !important; border: none !important; height: 8px !important;` to eliminate Chromium padding bleed.
  - Thumb: `22px` diameter circular pill with cyan gradient and white border, perfectly centered vertically (`margin-top: -7px`).
  - At `val = 30`: Thumb touches the exact left boundary directly over `-30 dBm`.
  - At `val = 100`: Thumb touches the exact right boundary directly over `-100 dBm` with 0px background bleed.
- **Clean Ticks**:
  - Display strictly `-30 dBm`, `-65 dBm`, `-100 dBm`. Removed redundant `(Dekat)` / `(default)` and `(Maksimal)` text.
  - `#rssiCatBadge` completely removed.
- **Strict Auto-Clamping**:
  - If user types `< 30` (e.g. `20`, `15`): instantly clamped to `30`. Single digits or `10` clamped to `30` on blur/debounce.
  - If user types `> 100` (e.g. `120`, `150`): instantly clamped back to `100`.
  - Valid range `30..100`: synced real-time to slider, hidden input (`-val` for ESP32 backend), and active fill gradient.

## 7. Floating Save Dock & Elimination of Static Save Button
- **Floating Save Dock (`#floatingSaveDock`)**:
  - Posisi: Sticky/fixed dock melayang di atas bottom navigation bar (`bottom: 74px`, z-index 990).
  - Dirty tracking reaktif: Muncul secara otomatis (transform translateY) hanya saat input/form mengalami perubahan dibandingkan nilai awal (`isDirty = true`).
  - Elemen: Indikator pulsing amber dot (*Ada perubahan*), tombol *Batal* (`#btnCancelChanges`) untuk rollback form, dan tombol *Simpan* (`#btnDockSave` dengan atribut HTML5 `form="settingsValidation"`).
- **Penghapusan Static Save Button**:
  - Tombol simpan statis lama di bagian paling bawah card (`.card-footer #btnStaticSubmit`) telah dihapus secara permanen untuk menjaga tampilan mobile yang bersih, modern, dan bebas redundansi.
  - Form submission sepenuhnya dialihkan ke `#btnDockSave` yang memicu submit event ke form `#settingsValidation`.

## 8. Reboot Workflow & Session Lifecycle Security
- **Reboot Feedback Modal**:
  - Saat perintah reboot dikirim ke ESP32 (`/api/v1/system/reboot`), antarmuka memunculkan SweetAlert:
    - **Title**: `Me-reboot...`
    - **Text**: `Perangkat sedang memulai ulang`
    - **Icon**: `info`
    - **Action Button**: `OK` (`#0284c7`)
  - Ketika pengguna menekan **OK**, browser langsung dialihkan (`window.location.href`) ke halaman login (`/login.html`).
- **Active Session Route Guard (Proteksi Halaman Login)**:
  - **Server-Side Protection (`src/main.cpp`)**: Rute `/login` dan `/login.html` memvalidasi cookie sesi (`validateSession(request)`). Jika sesi pengguna masih aktif (`role != ROLE_NONE`), server otomatis me-redirect (HTTP 302) pengguna ke halaman Overview (`/`).
  - **Client-Side Protection (`data/login.html`)**: Ditambahkan pre-check asynchronous (`/api/v1/auth/check`). Jika sesi terdeteksi valid (HTTP 200), halaman seketika melakukan `window.location.replace("/")` sebelum form login sempat digunakan.

## 9. Filter Major Tag Component & Numeric Bounds Validation
- **Komponen Kontrol**:
  - **Saklar Taktil ON/OFF (`#filterMajorToggle`)**:
    - Default state: **OFF** (tidak aktif).
    - Ketika switch OFF: Kontainer input `#majorContainer` diberi class `.dimmed` (opasitas 0.5, pointer-events none), input Target Major secara fisik dinonaktifkan (`disabled = true`, styling `cursor: not-allowed; opacity: 0.55`), tidak bersifat wajib (`required = false`), dan gateway menerima semua paket tanpa filter Major. Ketika saklar dinyalakan (ON), input otomatis di-enable kembali (`disabled = false`).
    - Ketika switch ON: Kontainer input aktif normal (`required = true`).
  - **Input Target Major (`#filter_major`)**:
    - **Placeholder**: `"65535"`.
    - **Max Length**: Dibatasi maksimal 5 digit (`maxlength="5"`, `inputmode="numeric"`).
    - **Rentang Angka & Auto-Clamp**: Nilai valid `0` s/d `65535`. Jika pengguna memasukkan nilai di atas `65535` (misal `70000`), sistem langsung melakukan auto-clamp kembali ke `65535`.
    - **Bebas Angka Nol di Depan**: Pengguna dapat langsung mengetik `1` untuk mencocokkan tag Major `00001` atau `1`.
  - **Badge Validasi Dinamis (`#majorBadge`)**:
    - Status kosong/default: `"uuid-badge neutral"` (*"0 - 65535"*).
    - Status valid: `"uuid-badge valid"` (*"Major [nilai] Valid"*).
    - Status melebihi batas: `"uuid-badge invalid"` (*"Maks 65535"*).
- **Backend NVS Persistence**:
  - Namespace: `"gateway"`.
  - Kunci: `major_en` (u32, default 0), `major` (string, default "").

## 10. Three-Tier System Versioning Architecture
Pemisahan arsitektur versioning gateway agar setiap layer dapat di-update secara independen:
1. **Device Version (Hardware / Model Profile)**:
   - **Penyimpanan**: Tertanam di **NVS** (Preferences namespace `"gateway"`, kunci `dev_ver`).
   - **Provisioning**: Didefinisikan pada `nvs_config.csv` (`dev_ver,data,string,v1.0.0`) dan dimuat saat boot (`config.device_version = prefs.getString("dev_ver", "v1.0.0");`).
2. **Firmware Version (Core Engine)**:
   - **Penyimpanan**: Variabel di kode **C++** (`const char *FIRMWARE_VERSION = "v1.0.0";` pada `src/main.cpp`).
   - **Kompilasi**: Terikat langsung dengan build binary firmware `.bin`.
3. **UI Version (Frontend Asset)**:
   - **Penyimpanan**: File JSON di filesystem **LittleFS** (`data/version.json` dengan struktur `{"version": "v1.0.0"}`).
   - **Pemuatan**: Dibaca secara dinamis saat boot via `loadUiVersion()` menggunakan `ArduinoJson`.
   - **Independensi OTA**: Update antarmuka web melalui OTA LittleFS filesystem seketika memperbarui nomor versi UI tanpa memerlukan flashing ulang firmware C++.
4. **REST API Endpoint**:
   - `GET /api/v1/system/version` mengembalikan payload JSON lengkap:
     ```json
     {
       "device_version": "v1.0.0",
       "firmware_version": "v1.0.0",
       "ui_version": "v1.0.0"
     }
     ```

## 6. Hardware LED Indicator Design System (WS2812B)
- **LED 1 [SYS] (Status Sistem & Firmware):**
  - `SYS_LED_NORMAL`: `#22c55e` (Hijau Solid) — Sistem dan RTOS berjalan stabil.
  - `SYS_LED_BOOTING`: `#eab308` (Kuning Kedip 2 Hz) — Inisialisasi hardware dan NVS.
  - `SYS_LED_OTA`: `#f97316` (Oranye Fast Strobe 10 Hz) — Flashing partisi firmware OTA.
  - `SYS_LED_FAULT`: `#ef4444` (Merah Solid) — Kerusakan NVS/LittleFS atau proses reset.
- **LED 2 [COM] (Status Jaringan & Komunikasi):**
  - `COM_LED_NORMAL`: `#22c55e` (Hijau Solid + 35ms Blip Padam) — Jaringan & MQTT terhubung normal.
  - `COM_LED_SOFTAP`: `#3b82f6` (Biru Kedip 1 Hz) — **Mode Hotspot Setup / Config AP Aktif** (dipicu via Tombol GPIO 14 > 5s atau SSID kosong).
  - `COM_LED_MQTT_OFFLINE`: `#06b6d4` (Cyan Kedip 1 Hz) — Jaringan lokal terhubung, tetapi broker MQTT offline.
  - `COM_LED_CONNECTING`: `#eab308` (Kuning Kedip 2 Hz) — Mencari WiFi atau DHCP Ethernet.
  - `COM_LED_DISCONNECTED`: `#ef4444` (Merah Double Blink) — Sambungan jaringan terputus (Watchdog aktif).

## 7. Hotspot AP Config Mode Banner & Controls
- **Card Container (`.ap-banner-card`):**
  - Background: `linear-gradient(135deg, rgba(15, 23, 42, 0.9) 0%, rgba(30, 41, 59, 0.8) 100%)`
  - Border: `1px solid rgba(59, 130, 246, 0.35)`
  - Backdrop Blur: `blur(12px)`
  - Glow Shadow: `0 8px 24px rgba(2, 132, 199, 0.12)`
  - Border Radius: `12px`
- **Pulsing Status Indicator (`.ap-pulse-ring`, `.ap-pulse-dot`):**
  - Dot: `#0284c7` dengan outer glow `#38bdf8`
  - Pulse Wave: `#38bdf8` (skala 0.95 -> 2.4, opacity 0.8 -> 0) irama 1.6s
- **Countdown Badge (`.ap-timer-pill`):**
  - Font: `monospace` 11.5px semi-bold
  - Background: `rgba(56, 189, 248, 0.15)`
  - Color: `#38bdf8`
  - Border: `1px solid rgba(56, 189, 248, 0.3)`
- **Action Button (`.btn-ap-stop`):**
  - Warning Accent: `rgba(249, 115, 22, 0.15)`
  - Border: `rgba(249, 115, 22, 0.4)`
  - Text: `#fb923c`
  - Hover: `rgba(249, 115, 22, 0.28)`, glow `0 4px 12px rgba(249, 115, 22, 0.25)`

### Live Test Monitor Beacon Badges
- Standard iBeacon Badge: `.badge-beacon.ibeacon` (Cyan `#38bdf8`, background `rgba(6, 182, 212, 0.15)`)
- Extended iBeacon Badge: `.badge-beacon.ibeacon_ext` (Purple `#c084fc`, background `rgba(168, 85, 247, 0.15)`)
- Legacy Fallback: `.badge-beacon.minew` (Mirrors `.badge-beacon.ibeacon_ext`)

### Network & MQTT Status Indicators (Overview Dashboard)
- Status Online / Connected: `.badge-tech.conn`, `.badge-tech.mqtt-online`
  - Background: `rgba(16, 185, 129, 0.15)`
  - Text Color: `#34d399` (Emerald 400)
  - Border: `1px solid rgba(16, 185, 129, 0.3)`
- Status Offline / Disconnected: `.badge-tech.offline`, `.badge-tech.mqtt-offline`
  - Background: `rgba(239, 68, 68, 0.15)`
  - Text Color: `#f87171` (Rose 400)
  - Border: `1px solid rgba(239, 68, 68, 0.3)`

## 7. Dynamic Device Model Branding (%model% & %ap_ssid%)
- **Template Processor Tokens**:
  - `%model%`: Dynamically injected from NVS `dev_model` (e.g. `SG-10`), with fallback to `EMPTY`. Used in `<title>`, `.machine-title`, `.login-title`, and footer copyright.
  - `%ap_ssid%`: Dynamically composed as `<device_model>-<device_id>` (e.g. `SG-10-bb5c7bcc`).
  - Web UI pages (`index.html`, `login.html`, `settings.html`, `test.html`, `firmware.html`, `auth_admin.html`, `auth_teknisi.html`) are 100% decoupling from hardcoded model strings.

## 8. Industrial Network Security & DNS Configuration Components
- **Primary & Secondary DNS Controls:**
  - Token IDs: `#w_dns`, `#w_dns2` (Wi-Fi), `#eth_dns`, `#eth_dns2` (Ethernet).
  - Primary DNS Fallback: Jika pengguna mengosongkan DNS pada mode statis, sistem menetapkan IP Gateway secara transparan.
  - Secondary DNS Optional: Placeholder `Kosongkan jika tanpa DNS sekunder` / `Kosongkan jika pakai DNS jaringan`.
- **Hotspot Mode Konfigurasi (SoftAP) Bento Card (Admin Only):**
  - Card Icon Accent: Indigo (`.bento-icon.indigo`, `rgba(99, 102, 241, 0.15)`, `#818cf8`)
  - SSID Display (`#ap_ssid_display`): Read-only styled with 75% opacity and disabled cursor.
  - WPA2 Password Control (`#ap_password`): Password input with toggle show/hide eye button, enforce `minlength="8"` for standard WPA2 PSK security compliance.
  - Access Control: Field hanya dapat dikonfigurasi melalui sesi Administrator (`auth_admin.html`) dan diverifikasi di sisi backend C++ (`ROLE_ADMIN`).

## 9. Industrial Ethernet Diagnostics & Telemetry Indicators
- **Running Mode Badge (#badgeConnMode):**
  - Pada mode Ethernet aktif dan terhubung, badge bertransformasi menampilkan format: \Ethernet (100M Full)\ atau \Ethernet (10M Half)\.
  - Status kelas: \.badge-tech.conn\ (Hijau / Emerald) saat IP diperoleh, \.badge-tech.offline\ (Merah / Rose) saat kabel terputus.
- **Ethernet Physical Link Tile (#labelSignalStrength, #valSignalStrength):**
  - Menggantikan tampilan RSSI nirkabel dengan telemetri kabel fisik RJ-45: @ Mbps (Full Duplex)\ atau \Kabel Terputus (Link Down)\.
  - Memberikan visibilitas langsung kepada teknisi IT/OT pabrik untuk mendeteksi kabel rusak atau *Duplex Mismatch* switch.
- **Automated PHY Self-Healing Architecture:**
  - Pemanfaatan gate transistor KO3401 P-MOSFET pada GPIO 15 untuk memutus daya 3.3V ke LAN8720 selama 200 ms jika kabel tercolok tapi IP gagal didapatkan selama 5 menit (300 detik).
  - Berfungsi memulihkan chip dari kondisi *latch-up* akibat induksi motor / lonjakan listrik industri tanpa me-reset timer \discon_rst\.

## 10. Industrial Staging & Pending Reboot Architecture
- **Floating Pending Reboot Banner (.pending-reboot-banner):**
  - Subtle Amber/Gold alert banner with dark glassmorphism:
    - Background: `linear-gradient(135deg, rgba(245, 158, 11, 0.15) 0%, rgba(217, 119, 6, 0.1) 100%)`
    - Border: `1px solid rgba(245, 158, 11, 0.35)`
    - Box Shadow: `0 8px 24px rgba(217, 119, 6, 0.15)`
    - Text: `#fde68a` (Amber 200) dengan icon peringatan `#fbbf24` (Amber 400).
  - Tombol Tindakan Cepat:
    - `.btn-banner-reboot`: Emerald green `#10b981` (hover `#059669`) untuk eksekusi restart bersih (`POST /api/v1/system/reboot`).
    - `.btn-banner-discard`: Rose tint `rgba(239, 68, 68, 0.15)` (hover `rgba(239, 68, 68, 0.28)`) untuk rollback pembatalan (`POST /api/v1/config/network/discard`).
- **Transparansi Pembatalan (Rollback Modal Dialog):**
  - Modal konfirmasi SweetAlert menampilkan rincian: Mode Jaringan dan SSID yang dikembalikan ke konfigurasi aktif.
  - Penegasan visual eksplisit: Kredensial akun master/teknisi dan MQTT tetap tersimpan aman tanpa ikut terhapus.

## 11. Emergency Recovery UI (PROGMEM Self-Contained System)
- **Zero-Dependency Architectural Constraint:**
  - Antarmuka darurat mandiri (*100% self-contained*) tersimpan di C++ Flash `PROGMEM` (`src/web/EmergencyRecoveryPage.h`).
  - Tidak memuat pustaka eksternal (CDN/Google Fonts/SweetAlert/FontAwesome) maupun aset LittleFS (`/assets/...`) karena partisi LittleFS diasumsikan rusak, korup, atau belum terpasang.
  - Ikon menggunakan SVG inline murni, font mengandalkan *native system UI font stack*, dan komunikasi HTTP menggunakan Vanilla JavaScript `XMLHttpRequest`.

- **Color Tokens & Aesthetic Hierarchy:**
  - **Background Gradient:** `radial-gradient(ellipse at 50% 0%, #172554 0%, #070c14 75%)` (Nuansa industri darurat/dark cybernetic).
  - **Emergency Banner Card:**
    - Background: `rgba(239, 68, 68, 0.08)`
    - Border: `1px solid rgba(239, 68, 68, 0.35)`
    - Box Shadow: `0 4px 20px rgba(239, 68, 68, 0.15)`
    - Accent: Danger Rose `#ef4444` & Amber Warning `#fbbf24`.
  - **Bento Card Surface:**
    - Background: `rgba(15, 23, 42, 0.85)` dengan `backdrop-filter: blur(16px)`
    - Border: `1px solid rgba(255, 255, 255, 0.08)`
    - Radius: `16px`
  - **Telemetri Badges (`.tele-badge`):**
    - Background: `rgba(255, 255, 255, 0.06)`
    - Label Muted: `#94a3b8`, Value Bright: `#f8fafc` (Monospace untuk IP & ID perangkat).

- **Form & Interactive Components:**
  - **Input File Upload Container (`.file-dropzone`):**
    - Border: `2px dashed rgba(56, 189, 248, 0.3)`
    - Hover / Active: `rgba(56, 189, 248, 0.6)` dengan glow halus.
  - **Primary Action Button (Restore LittleFS):**
    - Background: `linear-gradient(135deg, #06b6d4 0%, #0284c7 100%)`
    - Text: `#ffffff` bold, Shadow: `0 4px 14px rgba(6, 182, 212, 0.3)`
  - **Secondary Action Button (Flash Firmware Bin):**
    - Background: `linear-gradient(135deg, #475569 0%, #334155 100%)`
    - Border: `1px solid rgba(255, 255, 255, 0.1)`
  - **Real-Time Progress Bar (`.progress-bar`):**
    - Track: `rgba(255, 255, 255, 0.08)`
    - Fill Gradient: `linear-gradient(90deg, #06b6d4 0%, #10b981 100%)` dengan transisi halus `width 0.2s ease`.
  - **Emergency Log Console (`#recoveryLog`):**
    - Background: `#030712` (OLED Pure Black)
    - Font: `Consolas, Menlo, Monaco, monospace`
    - Teks: Emerald `#4ade80` untuk log sukses, Amber `#fde047` untuk progres, Rose `#f87171` untuk kegagalan.

- **Security & Authentication Bridge:**
  - Form darurat dilengkapi verifikasi password Administrator (`admin_password`).
  - Sebelum transfer binary diinisiasi via endpoint OTA `/api/v1/ota/upload`, JS melakukan *pre-authentication handshake* ke `/api/v1/auth/login` untuk mendapatkan cookie sesi admin resmi (`IOTARU_SESSION`), mencegah pengunggahan binary oleh pihak tidak berwenang saat perangkat dalam mode pemulihan.

## 12. Industrial Bento Drag-and-Drop File Upload Components (.bento-dropzone)
- **Aesthetic Consistency & Unified Blue Theme Accent:**
  - Kedua kotak upload berkas biner (Firmware program dan Filesystem LittleFS) diselaraskan menggunakan aksen warna **Biru Tema IOTARU** yang identik (`#38bdf8` / `#0284c7`), selaras dengan identitas visual bento dark-glassmorphism.
  - Border normal: `2px dashed rgba(56, 189, 248, 0.35)` dengan background `rgba(15, 23, 42, 0.55)`.
  - Border radius: `14px`, padding: `24px 18px`.

- **Interactive Hover & Dragover States:**
  - **Hover State:** Border bertransformasi menjadi solid glowing `#38bdf8`, background `rgba(56, 189, 248, 0.08)`, elevasi `translateY(-2px)`, dan bayangan `box-shadow: 0 0 20px rgba(56, 189, 248, 0.15)`.
  - **Dragover State (`.dragover`):** Dipicu saat kursor menyeret file ke atas zona drop. Background menguat menjadi `rgba(56, 189, 248, 0.14)`, border menyala terang, dan skala membesar halus `scale(1.01)`.
  - **Dropzone Icon (.dropzone-icon):** Kotak ikon 44x44px berlatar `rgba(56, 189, 248, 0.12)` dengan warna `#38bdf8`, membesar secara dinamis saat hover/dragover.

- **Dynamic File Selection Chip (.dropzone-selected-box):**
  - Menggantikan tampilan teks idle dengan kotak status berkas terpilih:
    - Background: `rgba(56, 189, 248, 0.1)`
    - Border: `1px solid rgba(56, 189, 248, 0.4)`
    - Radius: `10px`, animasi masuk `dropzoneFadeIn 0.3s ease`.
    - Menampilkan nama file (`.dropzone-filename`, teks putih tebal, ellipsis untuk nama panjang) dan ukuran berkas (`.dropzone-filesize`, monospace font).
    - Tombol Hapus/Ganti (`.dropzone-remove-btn`): Ikon silang yang berubah menjadi merah (`#ef4444`) saat di-hover untuk membatalkan berkas terpilih secara instan.

- **Unified Pre-Flight Binary Signature & Hardware Identity Verification (Firmware & LittleFS):**
  - **Dual Hardware Identity Validation (MODEL@BOARD_VERSION):**
    - Field `project_name` pada offset `0x50` (32 bytes) memuat format gabungan `MODEL@BOARD_VERSION` (misal: `SG-10@v1.0.0`).
    - **Model Mismatch Rejection:** Berkas ditolak seketika jika model pada biner tidak sama dengan model perangkat aktif (`CURRENT_DEVICE_MODEL`).
    - **Board Revision Mismatch Rejection:** Berkas ditolak seketika jika revisi board/PCB pada biner tidak kompatibel dengan revisi board fisik perangkat (`CURRENT_DEVICE_VERSION`).
  - **Pre-Flash Metadata Confirmation Dialog (SweetAlert Industrial Bento):**
    - Saat tombol "Unggah & Update Firmware" diklik, sistem menampilkan dialog konfirmasi yang merinci:
      - Device Model (misal: `SG-10`)
      - Board Version (misal: `v1.0.0`)
      - Firmware Version (misal: `v1.0.0-rc2`)
      - Tanggal Firmware (misal: `Mar 23 2026`, diekstrak dari offset `0x80`, tanpa jam).
  - **Partition Table Rejection (partitions.bin):** Offset 0x00..0x01 diperiksa terhadap magic number tabel partisi ESP32 `0x50AA` (`0xAA 0x50`) dan ditolak seketika.
  - **Firmware App Image Validation (firmware.bin vs bootloader.bin):**
    - Offset 0x00 mutlak `0xE9`.
    - Offset 0x20..0x23 mutlak `0xABCD5432` (`esp_app_desc_t`).
    - Berkas `bootloader.bin` ditolak seketika.
  - **LittleFS Filesystem Positive Validation (littlefs.bin):**
    - Menolak jika diawali `0xE9` atau `0x50AA`.
    - Offset 0x08..0x0F mutlak bernilai teks ASCII `"littlefs"`.
  - **Backend C++ Hard-Rejection Guard (`WebManager.cpp`):** Verifikasi identik di level C++ pada byte paket pertama (`index == 0`) untuk memblokir stream paket yang salah model, salah revisi board, atau corrupt.

## 11. Test Page Live Beacon Filter & Unique Beacon Table Pool
- **Input Filter Realtime (`#tagFilterInput`)**:
  - **Placeholder**: `Filter Major, Minor, atau Format (mis: 1-2, 100)...` (menghilangkan janji pencarian MAC address yang tidak didukung frame radio nRF).
  - **Smart Separator Matching**:
    - Mendukung format pasangan Major-Minor dengan pemisah `-`, `:`, atau `/` (contoh: `1-2`, `10-20`).
    - Mengonversi masing-masing bagian ke integer sehingga bebas dari distorsi *zero-padding* 5 digit (`00001-00002`).
    - Mendukung filter separuh pasangan (contoh: `1-` untuk Major 1 saja, atau `-2` untuk Minor 2 saja).
    - Mendukung angka tunggal (contoh: `5` mencocokkan Major 5 ATAU Minor 5).
    - Mendukung pencarian teks format/protokol (`ibeacon`, `ext`, `ibeacon_ext`).
- **Unique Beacon Pool Architecture (`test.js`)**:
  - Menggantikan raw stream array overwrite (`maxHistory = 200 raw packets`) dengan **Unique Beacon Pool** (`Map<tag, item>` dan `Array<item>`).
  - Setiap paket baru memperbarui metrik RSSI, Battery, dan `lastSeen` milik beacon bersangkutan secara in-place, lalu memindahkannya ke posisi teratas (most recently active).
  - Mencegah fenomena beacon hilang seketika dalam 1–2 detik saat frekuensi transmisi nRF tinggi (100–300 pkt/s).
  - Saat filter aktif, tabel menampilkan hingga 10 beacon unik paling mutakhir yang lolos kriteria filter.
  - **Kolom Waktu / Timestamp (Client-Side JS)**:
    - Ditambahkan kolom `Waktu` (`<th>Waktu</th>`) di posisi paling kanan tabel.
    - Diformat sebagai `HH:mm:ss` secara eksklusif di JavaScript browser (`formatTime(b.lastSeen)` menggunakan `new Date()`).
    - **Nol beban pada ESP32**: Tidak membutuhkan sinkronisasi NTP, modul RTC, maupun payload tambahan pada WebSocket gateway. Setiap paket yang diterima langsung dicatat waktu kedatangannya di browser pengguna.
  - **Arsitektur Continuous Raw Packet Stream (FIFO Maks. 30 Paket)**:
    - Mode deduplikasi unik dinonaktifkan atas kebutuhan inspeksi raw streaming beruntun.
    - Setiap paket yang masuk dari modul nRF dicatat sebagai entri independen baru lengkap dengan timestamp presisi milidetik (`HH:mm:ss.SSS`).
    - Kapasitas buffer browser dibatasi tepat **maksimal 30 paket**.
    - **Filter Bidireksional**:
      - Menyaring paket riwayat yang sudah ada di buffer (maks. 30).
      - Menyaring paket baru yang sedang mengalir masuk: paket yang cocok langsung disisipkan di baris teratas (#1), sedangkan paket yang tidak cocok disimpan di buffer latar belakang tanpa menggeser tampilan filter pengguna.
      - Menghapus input filter seketika menampilkan 10 paket terbaru dari buffer 30 riwayat.
  - **Pruning & Discard Data Non-Matching pada Riwayat Buffer**:
    - Ketika input filter diaktifkan (`filterVal !== ""`), seluruh data riwayat lama di `packetStreamHistory` yang tidak cocok seketika dibuang (`filter()`), mengosongkan slot riwayat hanya untuk paket yang dicari pengguna.
    - Setiap paket baru yang mengalir masuk via WebSocket saat filter aktif akan dicek: jika **tidak cocok**, paket tersebut **langsung dibuang (discard) dan tidak disimpan ke riwayat**.
    - Hasil: Seluruh kuota 30 slot riwayat buffer dialokasikan 100% khusus untuk paket yang lolos filter tanpa risiko terdorong keluar oleh paket lain.
## 12. Throughput HUD 8-Card Grid & 60-Second Realtime Performance Chart (6-Series)
- **8-Card Responsive Throughput HUD (`.throughput-hud`)**:
  - Menyajikan 8 metrik performa kritis gateway secara terpadu:
    1. **Tag Read Speed (`#stat_read_speed`)**: Laju penerimaan paket radio nRF (`pkt/s`).
    2. **Unique Beacons (`#stat_unique_tags`)**: Akumulasi jumlah beacon fisik unik (`Set<tagId>`) yang terbaca sejak sesi pengujian dimulai. Otomatis di-reset ke 0 saat 'Start Test' ditekan.
    3. **MQTT Publish Speed (`#stat_mqtt_speed`)**: Laju pengiriman payload JSON/Binary batch (`batch/s`).
    4. **Queue RTOS Load (`#stat_queue_load`)**: Utilisasi buffer antrian FreeRTOS (`load / 300`).
    5. **Antrian Buffer Batch (`#stat_batch_load`)**: Utilisasi penampung buffer pengiriman batch MQTT (`batch_load / batch_max`, misal `18 / 100`).
    6. **Pkt per Batch (`#stat_batch_pkt_size`)**: Jumlah paket beacon aktual yang berhasil dibundel dan dikirim dalam tiap batch terakhir (`statLastBatchPktCount`, misal `85 pkt` atau `100 pkt`).
    7. **Net Bandwidth (`#stat_net_bandwidth`)**: Kecepatan throughput trafik keluar (egress) ke broker MQTT dalam kilobyte per detik (`KB/s`). Dihitung murni untuk seluruh frame data keluar (Data Batch + Heartbeat/Status + RPC Command + PINGREQ Keepalive + TCP ACK) lengkap dengan enkapsulasi Layer 2 Ethernet (18B) atau Wi-Fi (36B) dan Layer 3/4 TCP/IP (40B).
    8. **Free Heap RAM (`#stat_free_heap`)**: Sisa memori internal SRAM ESP32 (`KB`).
  - **Tata Letak Adaptif / Responsive Grid**:
    - Layar Ultra-Wide (`>= 1320px`): 8 kartu sejajar dalam 1 baris (`grid-template-columns: repeat(8, 1fr)`).
    - Laptop / Desktop Standar (`>= 1024px`): 4 kolom x 2 baris proporsional (`repeat(4, 1fr)`).
    - Tablet (`>= 640px`): 4 kolom x 2 baris (`repeat(4, 1fr)`).
    - Mobile (`< 640px`): 2 kolom x 4 baris (`repeat(2, 1fr)`).
    - Desain kartu: Glassmorphism gelap (`background: rgba(15, 23, 42, 0.7)`), aksen border bercahaya, badge icon svg neon, dan tabular numerals semibold.

- **60-Second Realtime Analysis Chart (Windows Task Manager Style - 6 Multi-Series)**:
  - **Sumbu Waktu Windows Task Manager**:
    - Menampilkan label teks elegan di sumbu horizontal bawah: `'60 seconds'` di ujung kiri dan `'0'` di ujung kanan.
    - Menghilangkan label detik berulang yang padat (seperti `-50s`, `-40s`, dst.), digantikan oleh 6 grid vertikal halus (`rgba(255, 255, 255, 0.05)`) dan 4 grid horizontal penanda ketinggian (0%, 25%, 50%, 75%, 100%).
  - **Visualisasi 6 Metrik Multi-Series**:
    - **Tag Read Speed**: Neon Cyan (`#22d3ee`, glow blur 4px), auto-scale kecepatan (`pkt/s`).
    - **MQTT Publish Speed**: Neon Emerald (`#34d399`), laju transmisi (`batch/s`).
    - **RTOS Queue Load**: Neon Amber (`#fbbf24`), utilisasi buffer antrian (0 - 300).
    - **Batch Buffer Load**: Neon Rose (`#f43f5e`), utilisasi buffer batch (0 - 100).
    - **Pkt / Batch**: Neon Violet (`#818cf8`, fill `rgba(129, 140, 248, 0.10)`), volume paket yang dikirim per batch (0 - `batch_max`).
    - **Net Bandwidth**: Electric Sky (`#38bdf8`, fill `rgba(56, 189, 248, 0.08)`), laju transfer jaringan ke broker MQTT (`KB/s`).
  - **High-DPI Retina Rendering**:
    - Canvas diukur berdasarkan bounding rect CSS dan diskalakan dengan `window.devicePixelRatio` (2x / 3x) untuk ketajaman visual maksimal di layar Retina dan AMOLED tanpa blur.
  - **Interaktivitas Crosshair & Floating Tooltip 6 Baris**:
    - Event `mousemove` memetakan koordinat pointer ke indeks riwayat 60 detik (`-60s` s/d `0s`).
    - Garis crosshair vertikal putih transparan (`rgba(255, 255, 255, 0.4)`) dan titik penanda (bullet dot) di setiap kurva aktif.
    - Floating Tooltip glassmorphism gelap melayang mengikuti kursor, menampilkan detail nilai 6 metrik pada detik bersangkutan secara presisi (termasuk baris `Net Bandwidth: ... KB/s`).
  - **Siklus Hidup (Lifecycle Engine)**:
    - Di-reset dan mulai melakukan *sampling* 1 Hz saat tombol 'Mulai Pengujian' (Start Test) ditekan.
    - Pembekuan visual (*freeze state*) saat 'Hentikan Pengujian' (Stop Test) ditekan untuk memudahkan teknisi melakukan analisa statis.

## 13. Carrier-Grade OTA Lifecycle & Visual Feedback System
- **Status LED SYS Saat Flash OTA**:
  - **SYS_LED_OTA (Oranye Berkedip / Orange Pulse)**: Aktif seketika saat chunk pertama diterima (index == 0) dan partisi flash diinisialisasi via Update.begin().
  - **Auto-Recovery ke SYS_LED_NORMAL (Hijau)**: Jika terjadi kegagalan transmisi, koneksi putus, atau *inactivity watchdog* (10 detik) terpicu, Update.abort() dipanggil dan status LED otomatis dipulihkan kembali ke hijau tanpa restart.
- **OTA Concurrency Lock (HTTP 409 Conflict)**:
  - Mencegah request upload simultan dari beberapa tab atau user berbeda (otaInProgress guard). Request kedua ditolak seketika sehingga stream flash tidak mengalami korupsi.
- **Total Data Silencing Selama OTA (Zero Contention / Zero OOM)**:
  - **UART nRF Ingestion Ditangguhkan**: Alokasi paket beacon ke antrean FreeRTOS distop, buffer FIFO hardware di-drain cepat tanpa memory leak.
  - **MQTT Batch Publisher Ditangguhkan**: Task MQTT di Core 1 di-pause (TaskDelay(200)), mencegah benturan soket TCP dan alokasi buffer.
  - **WebSocket Live Telemetry Ditangguhkan**: Pengiriman array beacon JSON dihentikan sementara, menyisakan jalur transmisi murni untuk progress bar firmware OTA (ota_progress).
- **ESP-IDF Native Hardware Rollback**:
  - Partisi baru berstatus `ESP_OTA_IMG_PENDING_VERIFY` pasca-flash.
  - Hanya jika boot `app_main()` berhasil tuntas hingga akhir, `esp_ota_mark_app_valid_cancel_rollback()` dipanggil.
  - Mencegah *hard-brick* atau *bootloop*: jika firmware baru mengalami crash/panic saat boot, bootloader otomatis kembali ke partisi firmware lama pada reboot berikutnya.

## 14. Native ESP-IDF v6 Architecture & Hardware Pinout Specification
- **Hardware Bus & Pinout Assignment**:
  - **Ethernet W5500 (SPI DMA Hardware Acceleration)**:
    - Host: SPI2 (VSPI) Controller dengan DMA Channel Auto (`SPI_DMA_CH_AUTO`).
    - Bus Speed: 25 MHz (`SPI_MASTER_FREQ_25M`).
    - Pinout: `CS: GPIO 26, SCK: GPIO 18, MISO: GPIO 19, MOSI: GPIO 23, RST: GPIO 15, INT: GPIO 14`.
    - Stack: Native `esp_eth` MAC/PHY driver terintegrasi dengan `esp_netif` lwIP.
  - **BLE Ingestion UART (nRF52840 Co-Processor)**:
    - Port: UART2 pada `1,000,000 bps` (1 Mbps), 8N1.
    - Buffer: 4096-byte Hardware FIFO ring-buffer dengan FreeRTOS Event Queue.
    - Pinout: `RX: GPIO 16, TX: GPIO 17`.
    - Affinity: Terkunci khusus ke **Core 0** (PRO_CPU) untuk latensi sub-mikrodetik tanpa interferensi stack TCP/IP.
  - **WS2812B NeoPixel Multi-LED Indicator**:
    - Pin: `GPIO 13` (Daisy-chain LED 1 [SYS] dan LED 2 [COM]).
    - Timing Engine: Optimized bitbang cycle-counted (< 60 µs total latch) dengan FreeRTOS task di Core 1.
  - **Recovery / Factory Reset Button**:
    - Pin: `GPIO 34` (Active Low, 10k external pull-up).
    - Polling: Zero-boot synchronous evaluation (`< 5s` Batal, `5-10s` Mode AP Hotspot, `> 10s` Reset Pabrik).
  - **Partition Layout (ESP32 4MB Flash)**:
    - NVS (`nvs`): `0x9000` (24 KB)
    - OTA Data (`otadata`): `0xF000` (8 KB)
    - PHY Init (`phy_init`): `0x11000` (4 KB)
    - App Slot 0 (`app0`): `0x20000` (1.43 MB / 1464 KB)
    - App Slot 1 (`app1`): `0x190000` (1.43 MB / 1464 KB)
    - Filesystem LittleFS (`storage`): `0x300000` (1 MB / 1024 KB)

- **Asymmetric Multi-Core Affinity (Core 0 vs Core 1)**:
  - **Core 0 (PRO_CPU) — Dedicated Ingestion Engine**:
    - Melayani UART2 ISR dan ring-buffer ingestion stream secara eksklusif.
    - Eksekusi evaluasi XOR CRC 25-byte biner frame nRF.
    - Hardware filter in-line: Filter RSSI Minimum (`filter_rssi`), Target UUID 16-byte, dan Target Major (0–65535).
    - Penyaluran data ke antrean FreeRTOS `beaconQueue` (300 slot *drop-oldest*) dan `CircularBuffer` (50 slot live monitor) menggunakan lock-free C++11 `std::atomic`.
  - **Core 1 (APP_CPU) — Application, Network & UI Stack**:
    - lwIP TCP/IP stack execution & Ethernet W5500 SPI interrupt handler.
    - MQTT Publisher Task: Batch buffering hingga 4096B (format biner 6B & 7B), timer interval flush (100–1000ms), LWT, dan downlink RPC.
    - Native HTTP Server (port 80): REST API `/api/v1/*`, Static Web Assets dengan kompresi GZIP transparan.
    - WebSocket Live Telemetry Broadcast: Broadcaster task interval 100ms via `httpd_ws_send_frame_async()`.

- **Zero-FOUC Native Chunked Streaming Template Engine**:
  - Mengeliminasi alokasi dinamis string Arduino (`String`) yang menyebabkan fragmentasi heap.
  - Streaming berkas langsung dari LittleFS VFS (`/storage`) menggunakan chunk HTTP 512-byte (`httpd_resp_send_chunk()`).
  - *In-flight dynamic token replacement* untuk tag `%model%`, `%ap_ssid%`, `%device_id%`, `%firmware_version%`, `%ui_version%`, dan `%device_version%` tanpa me-load seluruh file HTML ke RAM.

- **Memori & Efisiensi Silikon**:
  - Total RAM SRAM terpakai: **14.8%** (~48.6 KB dari 327.6 KB), menyisakan **~279 KB RAM bebas** untuk stabilitas operasional industri 24/7.
  - Flash ROM terpakai: **78.9%** (~1.18 MB dari 1.43 MB alokasi partisi OTA).

