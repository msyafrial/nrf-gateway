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
- **Spesifikasi Driver Hardware (Bitbang Presisi Tinggi Direct Register):**
  - Driver: Memory-mapped register IO langsung (`GPIO_OUT_W1TS_REG` & `GPIO_OUT_W1TC_REG`).
  - Bit 0: T0H = 84 siklus CPU (~350 ns), T0L = 204 siklus CPU (~850 ns).
  - Bit 1: T1H = 192 siklus CPU (~800 ns), T1L = 96 siklus CPU (~400 ns).
  - Reset Latch: Pulsa LOW > 350 µs (`esp_rom_delay_us(350)`).
  - Isolasi Core: Dijalankan di Core 1 dalam critical section interrupt `portMUX_TYPE`.


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
- **Dual-Interface Network Specs Grid Layout (Wi-Fi & Ethernet Indipenden):**
  - **IP Address (Wi-Fi) Tile (`#valWifiIp`):**
    - Warna: `#34d399` (Emerald 400), Font: Monospace.
    - Menampilkan alamat IP koneksi Wi-Fi saat terhubung (`192.168.x.x`) atau `-` saat terputus.
  - **MAC Address (Wi-Fi) Tile (`#valWifiMac`):**
    - Warna: `#38bdf8` (Sky 400), Font: Monospace.
    - Menampilkan alamat MAC fisik antarmuka nirkabel Wi-Fi dari eFuse ESP32.
  - **IP Address (Ethernet) Tile (`#valEthIp`, Container `#tileEthIp`):**
    - Warna: `#34d399` (Emerald 400) saat terhubung, Muted Gray `#94a3b8` saat kabel terputus.
    - Otomatis adaptif: Menampilkan IP Router LAN (`192.168.1.x`), IP Direct PC (`192.168.4.1`), atau status `Kabel Terputus (Link Down)`.
    - Auto-Hide: Kontainer tile otomatis disembunyikan (`display: none;`) jika hardware kontroler Ethernet W5500 tidak terdeteksi.
  - **MAC Address (Ethernet) Tile (`#valEthMac`, Container `#tileEthMac`):**
    - Warna: `#38bdf8` (Sky 400), Font: Monospace.
    - Menampilkan alamat MAC perangkat keras Ethernet W5500.
    - Auto-Hide: Kontainer tile disembunyikan bersamaan jika hardware Ethernet uninstalled.
- **Ethernet Physical Link Tile (#labelSignalStrength, #valSignalStrength):**
  - Menggantikan tampilan RSSI nirkabel dengan telemetri kabel fisik RJ-45: 100 Mbps (Full Duplex) atau Kabel Terputus (Link Down).
  - Memberikan visibilitas langsung kepada teknisi IT/OT pabrik untuk mendeteksi kabel rusak atau Duplex Mismatch switch.

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
  - **WS2812B NeoPixel Multi-LED Indicator (2-Pixel Daisy-Chain on GPIO 13)**:
    - **Timing Engine**: Optimized direct register bitbang cycle-counted (< 60 µs latch, IRAM-resident) dengan FreeRTOS task 50 FPS di Core 1.
    - **Pixel 0: LED SYS (Status Operasional Sistem)**:
      - *Identify Mode*: Biru Murni Strobe 4 Hz (125ms On / 125ms Off) — Prioritas 1
      - *OTA Firmware Flash*: Oranye Tajam Strobe 10 Hz (50ms On / 50ms Off) — Prioritas 2
      - *Config Mode (SoftAP / Direct PC LAN)*: **Biru Murni Kedip 1 Hz (500ms On / 500ms Off)** — Prioritas 3
      - *Hardware Fault*: Merah Murni Solid — Prioritas 4
      - *Booting / Init*: Kuning Hangat Kedip 2 Hz (250ms On / 250ms Off) — Prioritas 5
      - *Normal Running*: Hijau Murni Solid — Prioritas 6
    - **Pixel 1: LED NET / COM (Status Komunikasi Jaringan Murni)**:
      - *Link Down (Kabel RJ45 lepas / Wi-Fi disconnected)*: Merah Murni Double Blink (100ms On, 100ms Off, 100ms On, 700ms Off) — Prioritas 1
      - *Link Up, Menunggu IP DHCP*: Kuning Hangat Kedip 2 Hz (250ms On / 250ms Off) — Prioritas 2
      - *IP Acquired, MQTT Offline*: Cyan (Biru Muda) Kedip 1 Hz (500ms On / 500ms Off) — Prioritas 3
      - *Normal Operasional (IP & MQTT Online)*: **Hijau Murni Solid + Traffic Blip Padam 90ms** saat frame beacon terkirim — Prioritas 4
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


## 13. Authentication Security, Transport Protocol & NVS Fault Tolerance
- **Dual-Mode Form Parameter Extraction (`get_form_param` & `urldecode_s`)**:
  - **Memory Safety**: Parser URL-decode (`urldecode_s`) menerapkan bounds checking ketat (`written + 1 < dst_len`) untuk mencegah potensi buffer overflow pada buffer tujuan.
  - **Dukungan Dual-Format Otomatis**:
    1. `multipart/form-data`: Mendeteksi header MIME `name="key"` dan batas boundary `\r\n\r\n` ... `\r\n--`, membersihkan trailing CR/LF secara otomatis.
    2. `application/x-www-form-urlencoded`: Mendeteksi pasangan `key=value` dipisah `&` dengan URL decoding aman.
  - **Standardisasi Frontend**: Frontend ([login.html](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/login.html), [app.js](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/assets/js/app.js), [emergency_page.cpp](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/src/web/emergency_page.cpp)) membungkus payload menggunakan `new URLSearchParams(formData)` dengan header `Content-Type: application/x-www-form-urlencoded` untuk efisiensi transfer data.

- **Mitigasi Serangan Otentikasi (Defense-in-Depth)**:
  - **Timing Side-Channel Protection**: Menggunakan `constant_time_eq()` untuk pencocokan kredensial username dan password secara bitwise konstan, mencegah penyerang menganalisa panjang atau kecocokan karakter melalui variasi latensi respon CPU.
  - **Anti-Brute Force Delay**: Penundaan `vTaskDelay(pdMS_TO_TICKS(1800))` saat kredensial salah untuk membatasi throughput serangan kamus / brute-force.
  - **Rate Limiting**: Maksimal 5 percobaan gagal berturut-turut memicu penguncian sementara selama 60 detik (HTTP 429 Too Many Requests).

- **Arsitektur NVS Zero-Lockout**:
  - Jika partisi master NVS (`nvs.bin`) belum di-flash ke alamat `0x9000`, `ConfigStore::initNvsConfiguration()` otomatis menulis kredensial default (`admin`/`admin` & `user`/`user`) langsung ke namespace operasional `cfg_auth`.
  - Fallback memori RAM di `Session::authenticate()` dan `ConfigStore::loadConfig()` menjamin kredensial tidak pernah kosong/blank meskipun flash NVS baru saja di-format atau mengalami kegagalan baca.


## 14. MQTT Topic Hierarchy & mDNS Discovery Protocol
- **Hierarki Topik MQTT Terstandarisasi (`<dev_model>/<action>/<device_id>`)**:
  - **MQTT Client ID**: Menggunakan murni Hardware ID lowercase: `<device_id>` (contoh: `b3462cf9`).
  - **Uplink Data**: `<dev_model>/data/<device_id>` (contoh: `SG-10/data/b3462cf9`).
  - **Status / Telemetri / LWT**: `<dev_model>/info/<device_id>/status` (contoh: `SG-10/info/b3462cf9/status`).
  - **Downlink Command RPC**: `<dev_model>/cmd/<device_id>` (contoh: `SG-10/cmd/b3462cf9`).
  - **Command Response RPC**: `<dev_model>/res/<device_id>` (contoh: `SG-10/res/b3462cf9`).
  - **Konsistensi Kode**: Variabel di seluruh layer C++ terstandarisasi menggunakan pasangan nama `dev_model` dan `device_id` dengan penjaminan karakter hex lowercase.

- **Protokol Auto-Discovery mDNS (Multicast DNS)**:
  - **Sanitasi Model & Format Hostname**: Semua karakter selain huruf dan angka pada `dev_model` dibersihkan dan diubah menjadi lowercase (contoh: `"SG-10"` $\rightarrow$ `"sg10"`). Format hostname mDNS adalah `<clean_model>-<device_id>` (contoh: `sg10-b3462cf9`), sehingga dapat diakses via URL: `http://sg10-<device_id>.local`.
  - **Service Registration**: Layanan web HTTP port 80 diiklankan secara otomatis melalui protokol `_http._tcp` dengan TXT records: `model`, `firmware`, dan `device_id`.
  - **Inisialisasi**: Aktif otomatis saat stack IP jaringan (baik Ethernet W5500 maupun Wi-Fi Station) menerima konfigurasi IP (`IP_EVENT_*_GOT_IP`).


## 15. Industrial-Grade WebSocket & Live Telemetry Architecture
- **Kepatuhan Protokol RFC 6455 (Two-Step Ingestion)**:
  - **Header & Opcode Extraction**: Pembacaan frame WebSocket diawali dengan panggilan `httpd_ws_recv_frame(req, &ws_pkt, 0)` untuk membaca panjang payload dan tipe frame secara presisi tanpa risiko buffer overflow atau parser desync di level lwIP/httpd.
  - **Penanganan Otomatis Frame Kontrol**:
    - `HTTPD_WS_TYPE_PING`: Dijawab seketika dengan `HTTPD_WS_TYPE_PONG` (`final = true`) untuk menjaga integritas socket keep-alive tanpa membebani thread aplikasi.
    - `HTTPD_WS_TYPE_CLOSE`: Menutup koneksi dan membersihkan slot socket secara tertib (*clean graceful shutdown*).
  - **Integritas Frame Broadcast (`final = true`)**: Seluruh frame teks JSON yang disiarkan menyertakan flag `ws_pkt.final = true`, menjamin browser klien langsung memicu event `ws.onmessage` tanpa fragmentasi buffer internal browser yang menggantung.

- **State Management Berbasis Slot Sesi (`WsClientSlot`)**:
  - Mengeliminasi ketergantungan pada global atomic counter (`s_active_ws_subscribers`).
  - Menggunakan tabel slot sesi terproteksi mutex (`WsClientSlot s_ws_slots[4]`) yang melacak `fd`, status langganan `is_subscribed`, peran otentikasi `role`, dan timestamp aktivitas `last_activity_ms`.
  - Bebas dari *counter drift*: Refresh halaman, multiple tabs, atau koneksi terputus tiba-tiba ditangani secara otomatis pada level deskriptor socket masing-masing.

- **Adaptive Broadcaster & Proteksi DRAM Heap**:
  - **Zero-Allocation saat Idle**: Broadcaster memeriksa keberadaan klien yang aktif *subscribed* sebelum melakukan serialisasi JSON. Jika tidak ada subscriber aktif, task langsung kembali tidur tanpa menyentuh alokasi memori heap (`cJSON_CreateObject`), mencegah fragmentasi memori DRAM saat berjalan 24/7.
  - **Cadence Broadcasting Optimal**: Interval broadcast diatur pada 250 ms (4 Hz) yang adaptif, menghasilkan visualisasi data yang fluid pada UI Live Test tanpa membebani Core 1.
  - **Resiliensi TCP Backpressure**: Toleransi terhadap status socket sibuk sementara (`EAGAIN`/`EWOULDBLOCK`), hanya melepaskan slot jika socket terkonfirmasi hangus (`ESP_ERR_INVALID_STATE` atau `ESP_FAIL`).


## 16. Multi-Device Concurrent Session Control & Sliding Window Lifecycle
- **Kapasitas Koneksi & Alokasi Soket TCP (3 Klien Simultan)**:
  - **lwIP Maximum Sockets**: Dikonfigurasi menjadi 24 (`CONFIG_LWIP_MAX_SOCKETS=24` pada `sdkconfig.defaults` dan `sdkconfig.esp32doit-devkit-v1`), menyediakan kapasitas headroom untuk 3 laptop, Ethernet/WiFi stack, MQTT client, mDNS, dan background tasks.
  - **ESP-HTTPD Server Sockets**: `config.max_open_sockets = 16`, mampu melayani aset statis HTTP bersamaan dengan koneksi WebSocket pada 3 laptop.
- **Kebijakan Batas Login Klien Ganda (Maksimal 2 Perangkat)**:
  - **Maksimal Sesi Aktif**: `MAX_CONCURRENT_SESSIONS = 2` pada `session_mgr.hpp`.
  - **Penolakan Perangkat Ketiga (HTTP 409 Conflict)**: Jika slot otentikasi penuh (2 perangkat berbeda telah aktif login), percobaan login dari perangkat ke-3 ditolak dengan status HTTP 409 dan kode JSON `session_limit`.
  - **Modal Notifikasi SweetAlert2 (Frontend UX)**: Pada `login.html`, respons HTTP 409 memunculkan modal peringatan amber:
    - *Title*: "Sesi Penuh (Maks. 2 Perangkat)"
    - *Message*: "Kapasitas login telah mencapai batas maksimal (2 perangkat aktif). Silakan logout terlebih dahulu di salah satu perangkat yang sedang aktif sebelum masuk."
    - *Icon*: "warning"
- **Sliding Window Session Timeout (15 Menit / 900 Detik)**:
  - **Durasi Sesi**: `SESSION_TIMEOUT_MS = 900000` (15 menit) dengan atribut cookie `Max-Age=900`.
  - **Sliding Renewal Otomatis**: Setiap interaksi API (`validate()`, `touch()`) memperbarui timestamp aktivitas (`last_activity_ms = now`), sehingga pengguna yang aktif bekerja tidak akan terputus sesi loginnya.
  - **Keepalive Saat Uji Diagnostik ("Start Test")**:
    - Backend: Broadcaster WebSocket memanggil `Session::touch(token)` secara berkala selama klien terdaftar dalam status berlangganan uji (`is_subscribed == true`).
    - Frontend: Halaman `/test` menjalankan interval keepalive heartbeat setiap 60 detik (`/api/v1/auth/check`) selama pengetesan aktif, menjamin sesi teknisi tetap terjaga selama sesi monitoring jangka panjang.
- **Client IP Tracking & Deduplikasi Sesi**:
  - Pelacakan IP klien via socket `getpeername()` mencegah pemborosan slot otentikasi jika satu laptop melakukan re-login atau me-refresh browser.


## 17. Resilient File Upload UI/UX & Live Telemetry HUD
- **Ergonomi Dropzone Pembaruan Firmware & LittleFS**:
  - **Isolasi Event Bubbling**: Menghapus `onclick` ganda dari markup HTML dan menambahkan `e.stopPropagation()` pada file input click handler, mengeliminasi pembatalan dialog pemilihan file Chromium pada OS Windows.
  - **State Tampilan Terisolasi**: Area file terpilih (`#dropzoneFirmwareSelected` dan `#dropzoneFSSelected`) disembunyikan secara eksplisit (`display: none;`) saat inisialisasi awal hingga file binary `.bin` valid dipilih.
  - **Penghapusan Pemeriksaan Magic Byte 0xE9**:
    - Validasi kaku terhadap byte `0xE9` dihapus sepenuhnya dari `ota_service.cpp`, `emergency_page.cpp`, dan frontend `firmware.js`.
    - Pengecekan diserahkan kepada subsistem OTA bawaan ESP-IDF (`esp_ota_begin`, `esp_ota_write`, dan `esp_ota_end`) yang memverifikasi integritas checksum SHA256 serta keabsahan image binary secara native.
  - **Transmisi Biner Murni (Raw Octet-Stream Streaming)**:
    - Frontend mengirim file langsung via `xhr.send(file)` dengan header `Content-Type: application/octet-stream`, menggantikan `FormData` (multipart) yang sebelumnya menyisipkan header `WebKitFormBoundary` dan merusak offset biner firmware pada partisi flash.
  - **Validasi Binary Pre-Flight Toleran**: Pemeriksaan model perangkat keras pada header image binary disesuaikan untuk mengenali identitas proyek build PlatformIO (`SG-10`, `esp32_ibeacon_espidf`, `arduino-lib-builder`), mencegah penolakan palsu (*false rejection*) sebelum proses OTA dikirim ke mikrokontroler.
- **Minimalisme HUD Card MQTT Publish Speed**:
  - Penyelarasan tampilan kartu telemetri agar seragam dengan HUD Card lainnya (hanya menampilkan angka kecepatan utama `#stat_mqtt_speed` dan satuan `batch/s`).
  - Baris sub-metrik `Tot: X pkt` dihapus dari antarmuka visual demi estetika dashboard yang bersih dan simetris, sementara penghitung metrik internal tetap aktif di backend untuk telemetri log sistem.

## 18. Hardware Status LED (Dual WS2812B NeoPixel) & Traffic Dynamics
- **Arsitektur Hardware Visual**:
  - Dua unit LED WS2812B terhubung daisy-chain pada **GPIO 13** (`Pins::LED_WS2812`), dikelola oleh FreeRTOS task khusus di Core 1 (50 FPS / 20ms refresh).
  - **LED 1 (SYS)**:
    - *Hijau Solid*: Operasi normal dan stabil (`SysLedState::NORMAL`).
    - *Kuning Kedip (2 Hz)*: Fase booting & inisialisasi awal hardware (`SysLedState::BOOTING`).
    - *Oranye Strobo Cepat (10 Hz)*: Proses penulisan OTA Flash berlangsung (`SysLedState::OTA`).
    - *Biru Kedip (4 Hz)*: Mode Identifikasi perangkat aktif selama 3 detik (`triggerIdentify(3000)`).
    - *Merah Solid*: System Fault / Panic.
  - **LED 2 (COM) - Hirarki Status Komunikasi 5 Tahap & Penegakan Strict Priority**:
    - *Hirarki Mutlak (Status di Bawahnya DILARANG Menyala)*:
      1. **Level 1 (Tertinggi)**: `ComLedState::SOFTAP` (*Biru Kedip 1 Hz*). Aktif jika `s_configMode` atau `s_softApActive`. Memblokir 100% status Merah, Kuning, Cyan, dan Hijau terlepas dari event kabel LAN lepas atau MQTT putus.
      2. **Level 2**: `ComLedState::DISCONNECTED` (*Merah Double Blink*). Fisik kabel LAN Ethernet tercabut (Link Down) atau sinyal Wi-Fi STA terputus saat SoftAP tidak aktif.
      3. **Level 3**: `ComLedState::CONNECTING` (*Kuning Kedip 2 Hz*). Fisik kabel LAN tercolok (Link Up) dan sedang menunggu negosiasi IP DHCP dari router. Merah dilarang menyala.
      4. **Level 4**: `ComLedState::MQTT_OFFLINE` (*Cyan Kedip 1 Hz*). Alamat IP valid didapat (LAN/Wi-Fi up), namun broker MQTT belum tersambung. Merah dan Kuning dilarang menyala.
      5. **Level 5 (Terendah)**: `ComLedState::NORMAL` (*Hijau Solid + Traffic Blip 90ms*). Seluruh lapisan jaringan dan MQTT 100% normal. Padam sekejap selama 90 ms (~4-5 frame @ 50 FPS) setiap kali batch iBeacon berhasil terkirim via `triggerTrafficBlip()`.
    - *Strict Priority Guard*: Fungsi `setComState()` menolak modifikasi status jika sistem berada pada Level 1 (SoftAP/Config Mode).

## 19. Boot Recovery Button, Zero-Task Isolation, & Wi-Fi Scanning Architecture
- **Zero-Task Isolation pada Boot Recovery (`checkBootRecovery`)**:
  - Inisialisasi pin GPIO LED WS2812B (`LedManager::init`) dipisahkan secara ketat dari peluncuran task FreeRTOS (`LedManager::startTask`).
  - Selama evaluasi penekanan tombol recovery fisik (`Pins::BUTTON_RESET`, GPIO 34), Core 0 memiliki kendali eksklusif 100% tanpa adanya background task di Core 1 yang dapat menimpa bit data WS2812B:
    - **0 – 5 Detik**: Merah Solid (`0xFF0000`). Melepaskan tombol membatalkan recovery dan melanjutkan boot normal.
    - **5 – 10 Detik**: Biru Solid (`0x0000FF`). Memberikan umpan balik visual yang tegas bahwa sistem siap masuk ke Mode Hotspot Konfigurasi SoftAP saat dilepas.
    - **> 10 Detik**: Padam Total (`0x000000`). Indikasi visual bahwa perangkat siap melakukan Factory Reset (NVS wipe) saat tombol dilepas.
  - Task FreeRTOS `WS2812_Task` baru diluncurkan setelah fungsi `checkBootRecovery()` selesai, menjamin warna solid tidak pernah berkedip atau tertimpa pola animasi booting.
- **Global Lazy Initialization Wi-Fi Driver (`ensureWifiDriverInitialized`)**:
  - Pada konfigurasi default Ethernet (`ConnectionMode::ETHERNET`), inisialisasi Wi-Fi STA sengaja dilewati untuk menghemat RAM (320 KB heap optimization).
  - Ditambahkan helper terpusat `NetworkManager::ensureWifiDriverInitialized()` yang menginisialisasi kernel Wi-Fi ESP-IDF (`esp_wifi_init`) dan netif secara *on-demand*.
  - Mengeliminasi crash `ESP_ERR_WIFI_NOT_INIT` (`0x3001`) saat memanggil `startConfigAP()` maupun endpoint `/api/v1/wifi/scan` di lingkungan berorientasi Ethernet.
- **Arsitektur State Machine Wi-Fi Scanner (`/api/v1/wifi/scan`) & Passive Station Mode**:
  - **Pencegahan Auto-Connect Loop (`shouldWifiStaConnect`)**: Pada `net_manager.cpp`, event `WIFI_EVENT_STA_START` dan `WIFI_EVENT_STA_DISCONNECTED` diproteksi secara ketat. ESP32 hanya memanggil `esp_wifi_connect()` dan memperbarui status LED COM (`ComLedState::CONNECTING` / `DISCONNECTED`) jika mode aktif adalah Wi-Fi (`ConnectionMode::WIFI`), Ethernet tidak aktif, bukan mode SoftAP, dan SSID terkonfigurasi.
  - **Isolasi Mode Ethernet**: Saat gateway beroperasi di mode Ethernet (`ConnectionMode::ETHERNET`), radio Wi-Fi Station beroperasi dalam kondisi IDLE/pasif murni saat scanning. Tidak ada pemanggilan koneksi otomatis ke SSID lama yang menyebabkan banjir log `NO_AP_FOUND (201)` dan tabrakan state machine Wi-Fi (`ESP_ERR_WIFI_STATE` `0x3008`).
  - **Auto-Elevation Mode Wi-Fi**: Jika mode Wi-Fi saat ini `NULL`, otomatis diaktifkan ke `WIFI_MODE_STA`. Jika mode saat ini `AP` (SoftAP aktif), dinaikkan ke `WIFI_MODE_APSTA` sehingga pemindaian saluran nirkabel dapat berlangsung tanpa memutus koneksi hotspot pengguna.
  - **Sinkronisasi Selesai Seluruh Saluran (`WIFI_EVENT_SCAN_DONE`)**:
    - Backend TIDAK PERNAH membaca hasil prematur via `esp_wifi_scan_get_ap_num` sebelum pemindaian seluruh saluran selesai. Sinyal kesiapan scan hanya di-flag saat event resmi ESP-IDF `WIFI_EVENT_SCAN_DONE` terpicu.
    - Menghilangkan anomali pemindaian "terlalu cepat" (<1 detik) yang disebabkan oleh polling pertama membaca buffer AP parsial dari 1-2 channel awal saja.
  - **Dukungan Frekuensi Penuh Channel 1 s.d. 13 (Regulasi ID)**:
    - Ditambahkan inisialisasi `wifi_country_t` dengan kode negara `"ID"`, rentang saluran `schan=1` hingga `nchan=13`, serta kebijakan `WIFI_COUNTRY_POLICY_MANUAL`. Memastikan access point di frekuensi atas (mis. channel 11-13) tidak terlewatkan.
  - **Pencegahan Bentrok Polling & Watchdog Timeout**: Backend melacak flag `s_scan_in_progress` dan `s_scan_start_millis`. Polling periodik 700 ms dari modal UI `app.js` tidak akan memicu ulang scan baru, melainkan mengecek kesiapan hasil via `NetworkManager::isWifiScanDone()`. Jika pemindaian tertahan > 8 detik, sistem secara defensif menghentikan pemindaian lama via `esp_wifi_scan_stop()`.
  - **Defensive State Retry**: Jika driver merespons `ESP_ERR_WIFI_STATE` saat memulai scan, handler secara otomatis mengeksekusi `esp_wifi_disconnect()`, memberi jeda 50 ms untuk transisi driver ke state IDLE, lalu mengulangi pemindaian secara transparan.
  - **Pengurutan Kualitas Sinyal & Deduplikasi SSID**: Ketika pemindaian selesai, daftar Access Point diurutkan berdasarkan kekuatan sinyal RSSI (*descending* / sinyal terkuat di atas), dan SSID ganda dideduplikasi secara otomatis untuk menyajikan daftar jaringan yang bersih dan profesional.

## 20. Resiliensi WS2812B Driver & Anti-Dimming Engine (Web Concurrency)
- **Eksekusi IRAM Murni (`IRAM_ATTR writeRawPixels`)**:
  - Rutinitas bit-bang nanodetik WS2812B dipindahkan sepenuhnya dari Flash SPI ke Internal IRAM (`IRAM_ATTR`).
  - Mengeliminasi *Instruction Cache Miss* dan stall CPU ketika Web Server (`httpd`) membaca puluhan file static HTML/CSS/JS dari partisi LittleFS pada SPI Flash. Timing WS2812B kebal dari *premature latch reset*, mencegah fenomena LED meredup sekejap saat halaman web dibuka.
- **Transmisi Berbasis Dirty-Checking**:
  - Sistem melacak frame terakhir (`s_lastColorSys`, `s_lastColorCom`, `s_lastBrightness`).
  - Pulsa sinyal fisik ke GPIO 13 HANYA dipancarkan saat nilai warna atau kecerahan berubah.
  - Saat status statis (misal Hijau Solid atau fase ON 500ms), jalur data GPIO 13 idle sepenuhnya. Mengeliminasi 98% kemungkinan tabrakan bus dengan I/O web server serta menghemat beban CPU interrupt lockup.
- **Prioritas Task Adaptif di Core 1**:
  - Prioritas task `WS2812_Task` dinaikkan ke Priority 3 untuk menjamin konsistensi evaluasi frame tanpa terhambat oleh background telemetry worker.

## 21. Industrial Cyberpunk Telemetry HUD & Live Test Monitor 2.0 (`/test`)
- **Header Control Bar & Session Tracking (`.test-control-card`)**:
  - **Status & Actions Header**:
    - **Header Title Area**: Dilengkapi icon box neon cyan (`.test-header-icon`, 38x38px), judul utama *"Pengujian BLE Gateway"*, dan subjudul *"Streaming real-time inspeksi beacon"*.
    - **Status Pill Reaktif (`#testStatusBadge`)**: Menampilkan badge ringkas `STANDBY` (saat berhenti) atau pulsating cyan `STREAMING LIVE` (saat aktif).
  - **Aksi Kontrol Responsif**:
    - **Start / Stop Test (`#btnStartTest` / `#btnStopTest`)**: Tombol utama dengan warna cyan neon `#22d3ee` teks gelap `#042436` (Start) atau rose crimson `#f43f5e` (Stop). Pada mobile (<640px), tombol ini otomatis melebar (`flex: 1`, tinggi 44px) mengisi baris kontrol.
    - **Export CSV (`#btnExportCsv`)**: Tombol dark slate berbatas halus. Pada mobile (<640px), teks label disembunyikan sehingga berubah menjadi tombol ikon persegi kompak (`44x44px`) berdampingan dengan Start Test (`[ ▶ Start Test ] [ ↓ ]`).
  - **Sub-Baris Metrik Sesi Pengujian Realtime (`.test-session-stats`)**:
    - Tampil sebagai kapsul/pill bar gelap (`background: rgba(2, 6, 23, 0.65)`, border 1px subtle).
    - **Elapsed (`#sessionElapsedTime`)**: Stopwatch aktif dengan format `HH:MM:SS`.
    - **Rx (`#sessionPacketsReceived`)**: Akumulator total paket beacon yang diterima gateway secara simetris dihitung langsung dari selisih register hardware chip ESP32 (`msg.beacons_read - sessionStartBeaconsRead`). Mengeliminasi disparitas pembacaan client-side.
    - **Tx (`#sessionPacketsSent`)**: Akumulator total paket beacon yang berhasil dipublikasikan ke broker MQTT (`msg.mqtt_published - sessionStartMqttPublished`).
    - **Pembersihan Pre-Start Buffer (`flushLiveBeacons`)**: Saat tombol *Start Test* ditekan, firmware ESP32 otomatis mengosongkan buffer live FIFO (`s_liveBeaconCount = 0`), mencegah bocornya 50 paket lama dari sesi pra-start sehingga Rx dan Tx bergerak serempak dari angka 0 dengan selisih minimal (0-5 paket in-flight).
    - **Drop (`#sessionDropRate`)**: Rasio kegagalan transmisi realtime dalam format `Jumlah Drop (Persentase dari Terkirim)` (contoh: `0 (0.00%)`).
- **8-Card Cyberpunk Telemetry HUD Grid (`.hud-card-v2`)**:
  - **Tata Letak Responsif 2 Kolom Mobile**:
    - Standar mobile & tablet (`< 960px`): Menggunakan format **2 Kolom** penuh (`grid-template-columns: repeat(2, 1fr); gap: 8px s/d 10px`).
    - Desktop (`>= 960px`): Melebar menjadi **4 Kolom** (`grid-template-columns: repeat(4, 1fr); gap: 14px`).
  - **Matriks Posisi 2 Kolom Mobile (Sesuai Referensi Visual)**:
    - **Baris 1**: Kolom 1 = **Tag Read Speed** (`card-cyan`) | Kolom 2 = **Unique Beacons** (`card-lavender`)
    - **Baris 2**: Kolom 1 = **MQTT Publish Speed** (`card-emerald`) | Kolom 2 = **Queue FreeRTOS** (`card-amber`)
    - **Baris 3**: Kolom 1 = **Antrian Buffer Batch** (`card-sky`) | Kolom 2 = **Net Bandwidth** (`card-cyan`)
    - **Baris 4**: Kolom 1 = **Pkt per Batch** (`card-indigo`) | Kolom 2 = **Free Heap RAM** (`card-lavender`)
  - **Detail Kartu Telemetri**:
    1. **Tag Read Speed (`card-cyan`)**:
       - Nilai utama: `#stat_read_speed` (`pkt/s`, font cyan 1.85rem / 1.25rem mobile monospace).
       - Rolling sub-stats: Rata-rata sesi (`#stat_read_avg` `Avg: X pkt/s`) dan kecepatan puncak (`#stat_read_max` `Max: Y pkt/s`).
       - Tanpa progress bar & tanpa badge nominal demi presisi numerik murni.
    2. **Unique Beacons (`card-lavender` - Opsi 3)**:
       - Nilai utama: `#stat_unique_tags` (`tag`, font lavender `#c084fc`).
       - Badge protokol: Lavender pill `[iBeacon]` (`#stat_beacon_type`).
       - Sub-baris metrik: Rata-rata kekuatan sinyal (`#stat_beacon_rssi` `Sinyal: -XX dBm (Avg)`).
       - Tanpa progress bar demi estetika bersih.
    3. **MQTT Publish Speed (`card-emerald`)**:
       - Nilai utama: `#stat_mqtt_speed` (`pkt/s`, font emerald `#34d399`).
       - Kecepatan murni paket per detik (`pkt/s`).
       - Rolling sub-stats: Rata-rata throughput (`#stat_mqtt_avg` `Avg: X pkt/s`) dan puncak (`#stat_mqtt_max` `Max: Y pkt/s`).
       - Tanpa progress bar & tanpa badge nominal.
    4. **Queue FreeRTOS (`card-amber`)**:
       - Nilai utama: `#stat_queue_load` (`0 / 300`, font putih tebal).
       - Badge status: `#stat_queue_badge` (`0% Load Safe` hijau atau `Warning` amber).
       - Progress fill line: `#bar_queue_load` (`height: 3px`).
    5. **Antrian Buffer Batch (`card-sky`)**:
       - Nilai utama: `#stat_batch_load` (`0 / 100`, font putih tebal).
       - Badge kapasitas: `#stat_batch_badge` (`0% Buffer` cyan).
       - Progress fill line: `#bar_batch_load`.
    6. **Net Bandwidth (`card-cyan`)**:
       - Nilai utama: `#stat_net_bandwidth` (`KB/s`, font cyan).
       - Badge status: `Optimum` hijau.
       - Sub-baris status interface: `#stat_net_link_text` menampilkan indikator visual `■■■ LAN W5500 100M Full` atau `■■■ WiFi 5GHz -XXdBm`.
    7. **Pkt per Batch (`card-indigo`)**:
       - Nilai utama: `#stat_batch_pkt_size` (`/ 100 pkt`, font putih tebal).
       - Badge informasi: `#stat_batch_pkt_badge` (`Avg payload` slate).
       - Progress fill line: `#bar_batch_pkt`.
    8. **Free Heap RAM (`card-lavender`)**:
       - Nilai utama: `#stat_free_heap` (`KB`, font putih).
       - Badge memori: `#stat_heap_badge` (`XX% Left` hijau).
       - Progress fill line: `#bar_free_heap` ungu (`fill-purple`).
- **Ekspor Telemetri CSV (`exportToCsv`)**:
  - Format output `.csv` dengan UTF-8 Byte Order Mark (`\uFEFF`) untuk kompatibilitas penuh dengan Microsoft Excel dan Google Sheets.
  - Kolom data: `No, Waktu, Tipe Beacon, Tag / MAC, RSSI (dBm), Baterai (%)`.
  - Penamaan file otomatis: `ble_gateway_telemetry_YYYYMMDDHHMMSS.csv`.

## 22. Navigation Guard & Active Test Warning UX (`/test`)
- **Proteksi Status Uji Aktif (`isTesting === true`)**:
  - Mencegah terputusnya stream telemetri secara tak sengaja atau hilangnya rekaman live beacon saat teknisi sedang mengamati performa gateway.
  - Jika pengguna mengklik link menu navigasi apa pun (sidebar desktop, mobile bottom nav, header logo, atau action sheet), aksi default navigasi ditahan (`e.preventDefault()`, `e.stopPropagation()`).
- **Modal Dialog Peringatan Cyberpunk (`Swal.fire` / `swal-lite.js`)**:
  - **Tampilan Dark Glassmorphic**: Latar belakang `#0f172a` dengan linear gradient ke `#1e293b`, border subtle `rgba(255, 255, 255, 0.1)`, serta backdrop blur 8px (`rgba(2, 6, 23, 0.75)`).
  - **Ikon Status**: `warning` amber `#f59e0b` dengan lingkaran pulsing semi-transparan `rgba(245, 158, 11, 0.15)`.
  - **Judul**: `"Sesi Pengujian Sedang Berjalan"` (Font `#f8fafc` tebal).
  - **Penjelasan Eksplisit**:
    > *"Pengujian live beacon sedang aktif. Jika Anda berpindah halaman, koneksi pengujian akan dihentikan dan seluruh data sesi uji yang belum diekspor akan hilang.<br><br>Yakin ingin tetap berpindah halaman?"*
  - **Tombol Konfirmasi ("Lanjut")**:
    - Styling: Danger/Warning Rose `#ef4444` (`#ef4444` dengan teks putih).
    - Tindakan: Memanggil `stopTest()` untuk mengirimkan frame `{ action: "stop" }` ke WebSocket ESP32, menghentikan timer, membersihkan flag, lalu mengeksekusi `window.location.href = targetUrl`.
  - **Tombol Pembatalan ("Batal")**:
    - Styling: Slate Muted `rgba(51, 65, 85, 0.7)` dengan border `rgba(255, 255, 255, 0.08)`.
    - Tindakan: Menutup modal, membatalkan perpindahan halaman, membiarkan pengujian dan grafik telemetri tetap berjalan normal.
- **Proteksi Tingkat Browser (`window.onbeforeunload`)**:
  - Mencegah reload (F5 / Ctrl+R) atau penutupan tab secara tidak sengaja saat pengujian sedang aktif.
  - Memanggil `e.preventDefault()` dan menyetel pesan peringatan standar browser.

## 23. 2026 Enterprise Segmented Tab & Live Unique Beacon Monitor (`/test`)
- **Session Telemetry Sub-Stats Bar (`.test-session-stats`)**:
  - Bar statistik sesi horizontal di bawah tombol kontrol sesi pengujian:
    `Elapsed: 00:00:00 | Rx: 0 | Tx: 0 | Drop: 0 (0.00%) | Bandwidth: 0.00 KB`
  - **Total Bandwidth (`#sessionTotalBandwidth`, `.session-stat-val.sky`)**: Menghitung akumulasi byte egress nyata (Ethernet L2/WiFi + TCP/IP + MQTT header + Payload) secara presisi dari firmware (`s_statTotalWireBytes`), ditransmisikan via WebSocket telemetry (`total_bytes`), dan diformat otomatis (`KB`, `MB`, `GB`).
- **Segmented Control Rail (Apple HIG / Linear / Vercel Standards)**:
  - **Track / Rail**: Kontainer inset gelap `rgba(2, 6, 23, 0.7)` dengan border `1px solid rgba(255, 255, 255, 0.08)`, border-radius `12px`, dan padding `4px`.
  - **Floating Active Tile**: Segmen aktif berpendar cyan neon (`background: linear-gradient(...)`, border `rgba(56, 189, 248, 0.4)`, glow `rgba(56, 189, 248, 0.15)`, shadow `0 4px 14px rgba(0, 0, 0, 0.45)`).
  - **Clean Minimalist Design**:
    - **Tab Buffer FIFO (`#tabFifo`)**: Memiliki pulsing live stream dot cyan (`.segment-live-dot`) dan label teks "Buffer FIFO" (tanpa badge pil "10 Pkt").
    - **Tab Unique Beacons (`#tabUnique`)**: Dilengkapi ikon radio pulse SVG dan label "Unique Beacons" (tanpa badge pil angka), menghasilkan tampilan ultra-clean dan lapang.
- **Tab Body Toolbar (Search Field Placement & Standard UI/UX Width)**:
  - Kotak filter pencarian (`.filter-search-box.compact`) diletakkan **di bawah tab segmen dan di atas tabel**, diposisikan di sebelah **KANAN** (`.tab-body-toolbar.right-aligned`, `justify-content: flex-end`).
  - Lebar proporsional standar enterprise UI/UX: **250px** di desktop, **200px** di mobile (tidak lagi melar full-width).
- **Tabel Unique Beacons (Datadog Telemetry Style)**:
  - **Struktur 6 Kolom (Tanpa Kolom Format)**:
    `#` (40px) | `Major-Minor` | `RSSI` | `Battery` | `Terakhir Dilihat` | `Total Pkt`.
    Kolom `Format` dihilangkan agar ruang identitas Major-Minor dan metrik sinyal/waktu/baterai lebih lapang.
  - **Urutan Stabil (First-Seen Insertion Order / No Jumping Rows)**:
    Data baris tidak saling bertukar posisi (*no flickering/row jumping*) saat paket dari berbagai tag masuk silih berganti. Baris #1, #2, #3, ... tetap permanen berdasarkan urutan kedatangan pertama.
  - **In-Place Live Update**: Nilai **Waktu Terakhir**, **RSSI**, **Baterai**, dan **Total Pkt (Hits)** diperbarui secara *live in-place* pada baris masing-masing.
  - **Persistensi Baterai**: Status baterai dari paket `ibeacon_ext` tetap tersimpan dan tidak hilang meskipun paket berikutnya berupa `iBeacon` standar.
  - **Signal Strength Coding**: RSSI diberi kode warna: hijau neon (`good`, `>= -65 dBm`), kuning amber (`mid`, `-66 s/d -80 dBm`), merah rose (`weak`, `< -80 dBm`).
- **Optimasi Responsif 360x800 Mobile (Android Standard)**:
  - Segmented rail mengisi lebar kartu dengan alokasi tombol seimbang `flex: 1` dan tinggi touch-target **42px**.
  - Kotak pencarian rata kanan dengan lebar 200px di layar mobile.
  - Kontainer tabel memiliki batas ketinggian vertikal `max-height: 360px` dengan *smooth touch scroll* horizontal dan custom dark scrollbar.

## 24. Dropped Data Telemetry & Diagnostic Tab (`/test`)
- **Tujuan & Arsitektur Diagnosis Real-time**:
  - Menyediakan visibilitas penuh terhadap paket beacon yang ditolak (*drop*) atau gagal dipublikasikan oleh ESP32 gateway selama pengujian live.
  - Setiap paket drop diklasifikasikan ke dalam 6 kategori alasan mutlak:
    1. `TTL Expired` (Value 0): Paket beacon berada di antrian buffer melebihi batas waktu TTL (Time-To-Live).
    2. `RSSI < Min` (Value 1): Kekuatan sinyal beacon berada di bawah ambang batas minimal filter RSSI.
    3. `UUID Mismatch` (Value 2): Nilai Proximity UUID paket beacon tidak cocok dengan UUID yang diizinkan pada pengaturan filter.
    4. `Major Mismatch` (Value 3): Nilai Major beacon tidak memenuhi filter target Major.
    5. `Queue Full` (Value 4): Antrian FreeRTOS penuh saat beban throughput spike (drop-oldest overflow).
    6. `MQTT Fail` (Value 5): Kegagalan saat mempublikasikan payload ke broker MQTT (koneksi putus/buffer socket penuh).
- **Segmented Control Rail Tab ke-3 (`#tabDropped`)**:
  - Ditambahkan segmen ketiga dengan ikon block/circle-slash SVG dan label "Dropped Data" pada `.segmented-control`.
  - Berpindah tab secara instan tanpa reload browser, mengaktifkan tampilan visual grid kartu dan tabel dropped data.
- **Ringkasan Total Alasan Drop (Desktop Symmetric 6-Column Grid Layout)**:
  - Kontainer `.drop-summary-grid` (`#dropSummaryGrid`) diletakkan tepat di atas tabel data dan di bawah segmented tab rail.
  - **Tata Letak Desktop (`> 960px`)**:
    - Disusun simetris dan seragam dalam **1 baris 6 kolom horizontal** (`repeat(6, 1fr)`), dengan gap 10px.
    - Setiap kartu memiliki *top-accent border* berwarna khas (2px) untuk memperjelas kategori:
      - TTL Expired: Rose Crimson (`#f43f5e`, `.card-ttl`)
      - RSSI < Min: Amber Warning (`#f59e0b`, `.card-rssi`)
      - UUID Mismatch: Violet Purple (`#a855f7`, `.card-uuid`)
      - Major Mismatch: Yellow Gold (`#eab308`, `.card-major`)
      - Queue Full: Sky Blue (`#0284c7`, `.card-queue`)
      - MQTT Fail: Red Alert (`#ef4444`, `.card-mqtt`)
    - Header kartu menampilkan pulsing dot berwarna dan nama alasan.
    - Body kartu menampilkan nilai angka tebal monospace 1.25rem dengan satuan `pkt`.
  - **Tata Letak Responsif Mobile (360x800)**:
    - Ditata dalam grid 3 kolom x 2 baris (`repeat(3, 1fr)`), nilai angka disesuaikan menjadi 1.05rem, padding 8px 6px.
- **Balanced Toolbar Layout (`#tabBodyToolbar`)**:
  - Menggunakan `justify-content: space-between` saat tab Dropped Data aktif:
    - **Sisi Kiri (`#toolbarLeftInfo`)**: Menampilkan pill badge amber `.drop-total-badge` dengan pulsing dot: `Total Ditolak: X pkt`.
    - **Sisi Kanan**: Kotak filter pencarian `.filter-search-box.compact` dengan lebar standar 250px (desktop) atau 200px (mobile).
  - Saat tab Buffer FIFO atau Unique Beacons aktif, elemen sisi kiri disembunyikan (`display: none;`), dan kotak pencarian tetap rata kanan secara elegan via `margin-left: auto`.
- **Tabel Log Dropped Data Tepat 5 Kolom**:
  - Kolom tabel disederhanakan tanpa kolom redundant:
    `#` (40px) | `Tag / Identitas` | `RSSI` | `Alasan Drop` | `Waktu Drop`.
  - **Kolom #**: Penomoran indeks baris (1 s.d. 50).
  - **Kolom Tag / Identitas**: Teks Major-Minor atau identifier beacon dengan font monospace cyan `#38bdf8`.
  - **Kolom RSSI**: Nilai sinyal numerik dalam format `-XX dBm` dengan badge sinyal berwarna.
  - **Kolom Alasan Drop (`.badge-drop`)**: Badge pill berlatar transparan dengan border tipis dan warna sesuai jenis kegagalan:
    - `.drop-ttl`: Rose background (`#fb7185`)
    - `.drop-rssi`: Amber background (`#fbbf24`)
    - `.drop-uuid`: Purple background (`#c084fc`)
    - `.drop-major`: Yellow background (`#fde047`)
    - `.drop-queue`: Sky background (`#38bdf8`)
    - `.drop-mqtt`: Red background (`#f87171`)
  - **Kolom Waktu Drop**: Timestamp presisi milidetik `HH:MM:SS.mmm`.
- **Sinkronisasi Firmware & Reset Status**:
  - Telemetri drop dipancarkan via WebSocket setiap detik dalam objek `drop_summary` (6 field counter) dan array `dropped_beacons` (ring buffer 50 paket terbaru).
  - Saat pengetesan dihentikan atau dimulai ulang (`Start Test`), daftar dropped data di-reset ke 0 untuk memastikan sesi uji baru bersih.

---

## 32. Komponen Banner Hotspot SoftAP & Keepalive Lifecycle (`#apModeBanner`)
- **Tujuan Desain**:
  - Memberikan indikator visual yang jelas kepada teknisi/administrator ketika perangkat sedang beroperasi dalam mode darurat/on-demand Hotspot SoftAP (`192.168.4.1`).
  - Menyajikan informasi SSID aktif, countdown timer *auto-shutdown idle*, dan tombol aksi langsung untuk mematikan AP secara manual.
- **Spesifikasi Anatomi & Gaya (`.ap-banner-card`)**:
  - **Latar & Bingkai**: Latar transparan gelap dengan aksen peringatan amber industri `rgba(245, 158, 11, 0.08)`, border solid tipis `1px solid rgba(245, 158, 11, 0.25)`, border-radius `10px`, padding `12px 18px`.
  - **Indikator Pulsing (`.ap-pulse-ring`)**: Animasi riak cincin amber dengan titik inti solid `#f59e0b` yang berdenyut lembut (`animation: apPulse 2s infinite`).
  - **Tipografi**:
    - Judul: Teks tebal semibold 0.85rem dengan warna amber `#fbbf24`.
    - Subtitle: Teks sekunder slate `#94a3b8` 0.78rem dengan penekanan SSID `#f8fafc`.
    - Badge Timer (`.ap-timer-pill`): Pill badge gelap monospace dengan latar `rgba(15, 23, 42, 0.6)`, border `rgba(245, 158, 11, 0.3)`, warna teks kuning amber terang `#fef08a`.
  - **Tombol Aksi (`.btn-ap-stop`)**: Tombol kompak dengan ikon antena terputus, latar merah rose transparan `rgba(244, 63, 94, 0.15)`, border `rgba(244, 63, 94, 0.35)`, teks `#fda4af`. Pada hover bertransisi ke latar rose pekat dengan efek glow.
- **Siklus Hidup Keepalive & Auto-Reset Interaktif**:
  - **Backend Global Activity Hook**: Setiap request HTTP (halaman, REST API, static assets) dan frame WebSocket secara otomatis memanggil `NetworkManager::recordApActivity()` jika mode AP aktif.
  - **Dedicated Touch API**: Endpoint ringan `POST /api/v1/system/config-ap/touch` mengembalikan `{ status: "ok", ap_active: true, ap_remain_sec: 60 }`.
  - **Frontend User-Activity Throttling**:
    - Mendengarkan event interaksi pengguna (`mousedown`, `keydown`, `input`, `touchstart`, `scroll`).
    - Mereset counter tampilan secara instan kembali ke 60s untuk responsivitas UX maksimal.
    - Mengirim ping touch ke backend dengan throttle 12 detik dan heartbeat periodik 20 detik selama tab/halaman browser aktif.
  - **Safety Auto-Off**: Jika pengguna benar-benar meninggalkan browser tanpa aktivitas selama 60 detik, SoftAP akan dimatikan otomatis oleh backend untuk keamanan jaringan industri dan penghematan daya radio RF.

---

## 33. Mekanisme Sliding Expiration Session & Global Keepalive
- **Tujuan Keamanan & Pengalaman Pengguna**:
  - Mencegah teknisi atau administrator terputus sesi loginnya secara mendadak saat sedang melakukan pemantauan live test beacon dalam durasi panjang (>15-20 menit) maupun saat sedang mengisi konfigurasi formulir yang kompleks.
  - Mempertahankan batas keamanan sesi di sisi server (kedaluwarsa otomatis jika perangkat benar-benar ditinggalkan idle tanpa aktivitas selama 15 menit).
- **Spesifikasi Teknis**:
  - **Cookie Lifetime Buffer**: Cookie otentikasi `token` diterbitkan dengan atribut `Max-Age=3600` (buffer 1 jam) dan `SameSite=Lax; HttpOnly`.
  - **Sliding Renewal via HTTP Heartbeat**:
    - Setiap respons otentikasi sukses pada `/api/v1/auth/check` atau `/api/v1/system/config-ap/touch` menyertakan kembali header `Set-Cookie` yang diperbarui untuk me-reset hitungan mundur cookie di browser kembali ke 3600 detik.
    - Script global [app.js](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/assets/js/app.js) menjalankan interval heartbeat setiap 45 detik selama tab browser aktif.
    - Script [test.js](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/data/assets/js/test.js) menjalankan ping keepalive khusus setiap 30 detik saat sesi pengujian live aktif.
  - **Proteksi Navigasi Bersih**:
    - Event penutupan WebSocket `ws.onclose` hanya melakukan verifikasi status/redirect jika pengetesan sedang berjalan aktif, mencegah navigasi sukarela pengguna antar halaman terputus secara keliru.

---

## 34. Arsitektur Pusat Log Sistem & Drop Data (`/log` - Unified System & Data Logs Hub)
- **Tujuan Arsitektur**:
  - Menyediakan pusat audit terpadu bagi teknisi dan administrator terhadap riwayat konektivitas jaringan (Wi-Fi STA, Ethernet W5500, MQTT Broker, SoftAP, Boot) serta **50 riwayat paket beacon yang terbuang/gagal (Dropped Beacon Data)** pada gateway IOTARU SG-10.
  - Memudahkan audit dan diagnosa pemutusan jaringan maupun alasan filtering data beacon (RSSI terlalu rendah, UUID/Major mismatch, antrean penuh, gagal publish MQTT) dalam satu halaman fokus.
- **Dual-Engine Log Storage Architecture**:
  1. **Log Jaringan (`NetLogService`) - Persisten di LittleFS**:
     - Database dibatasi pada **maksimal 50 log terbaru** (FIFO Ring Buffer di RAM dan LittleFS `/storage/net_events.log`).
     - Aman dari wear-out karena event jaringan jarang terjadi dan dilindungi debouncing 2 detik.
     - Persisten: Data tetap utuh saat gateway restart atau mati listrik.
  2. **Log Drop Data Beacon (`DropLogService`) - In-RAM Static Rolling FIFO (Zero Flash Wear-out)**:
     - Dibatasi secara presisi pada **50 data paket drop terbaru** (`MAX_DROP_LOGS = 50`) yang disimpan **100% di memori RAM** (~1.6 KB alokasi statis).
     - **Flash Longevity Guard**: Menghindari kerusakan chip SPI flash akibat tingginya frekuensi drop paket beacon (bisa puluhan hingga ratusan per detik di lingkungan padat radio). Flash LittleFS sama sekali tidak ditulis saat drop terjadi.
     - Thread-safe: Dilindungi FreeRTOS Mutex dengan *non-blocking try-lock* (2 ms timeout), menjamin Core 0 UART ingestion dan throughput paket tetap berjalan dengan kecepatan mikrodetik tanpa latency atau packet loss.
- **Kebijakan Navigasi**:
  - URL Utama: `/log` (dengan alias `/log.html` dan backward-compatibility redirect `/net_log`).
  - Label Sidebar Desktop: `System Logs` dengan icon terminal/audit log.
  - Bebas Polusi Navigasi Mobile: Tetap menjaga kebersihan dock bottom nav mobile (`.bottom-nav`).
- **Anatomi Visual & Desain UI (`log.html` & `log.js`)**:
  - **Master Tab Switcher (Segmented Control Utama)**:
    - *Tab 1: Log Jaringan & Sistem* dengan live dot Cyan dan counter 50 data.
    - *Tab 2: Log Drop Data Beacon* dengan live dot Rose dan counter 50 data.
  - **Adaptive 4 KPI Status Cards**:
    - Mode Net Log: Wi-Fi Station, Ethernet W5500, MQTT Broker, Kapasitas LittleFS (`x / 50 Data`).
    - Mode Drop Log: Total Drop Sesi (akumulasi), Filter RSSI Min, Filter Target (UUID/Major mismatch), Status Buffer RAM (`x / 50 Data`).
  - **Control Toolbar Dinamis**:
    - Segmented Filter Kategori otomatis beralih:
      - Tab Net Log: `Semua`, `Wi-Fi`, `Ethernet`, `MQTT`, `SoftAP`, `System`.
      - Tab Drop Log: `Semua`, `RSSI Min`, `UUID`, `Major`, `Overflow`, `TTL`, `MQTT Fail`.
    - Input Pencarian Cepat real-time untuk kedua mode.
    - Toggle Auto-Refresh 5 detik dengan animasi pulse green dot.
    - Tombol **Export CSV Cerdas**: Otomatis mengunduh `net_log_sg10.csv` saat di Tab Jaringan atau `drop_log_sg10.csv` saat di Tab Drop Data.
    - Tombol **Hapus Log Kontekstual**: Modal konfirmasi SweetAlert Lite yang disesuaikan ("Hapus Log Jaringan?" vs "Hapus Log Drop Data?").
  - **Anatomi Tabel Drop Log 6 Kolom Presisi (Tanpa Kolom Keterangan)**:
    1. `#`: ID urut numerik monospace `#50`.
    2. `Waktu`: Format kalender aktual WIB `YYYY-MM-DD HH:MM:SS` atau uptime `+HH:MM:SS`.
    3. `Tipe`: Badge tipe frame (`iBeacon` Sky, `ibeacon_ext` Purple, `MQTT` Rose).
    4. `Tag / Major-Minor`: Monospace pill `10001-00042` atau `10 pkt`.
    5. `RSSI`: Indikator sinyal dBm berwarna (Hijau > -65, Kuning > -80, Merah ≤ -80).
    6. `Alasan Drop`: Semantic badge berwarna (`TTL Expired` Slate, `RSSI Min` Amber, `UUID/Major Mismatch` Orange, `Queue Overflow/MQTT Fail` Rose).
    *(Kolom Keterangan dieliminasi agar tampilan data tampil ringkas, bersih, dan berfokus pada audit cepat).*
- **REST API Endpoints**:
  - `GET /log` & `GET /log.html`: Streaming halaman `/storage/log.html`.
  - `GET /net_log`: Kompatibilitas link lama menuju `/storage/log.html`.
  - `GET /api/v1/system/net-logs`, `POST .../clear`, `GET .../export`: Endpoint audit log jaringan.
  - `GET /api/v1/system/drop-logs`: JSON ringkasan counter dan 50 data log drop.
  - `POST /api/v1/system/drop-logs/clear`: Menghapus riwayat 50 log drop data di memori RAM.
  - `GET /api/v1/system/drop-logs/export`: Mengunduh berkas CSV `drop_log_sg10.csv` (6 kolom presisi).

## 32. Simulator 2: RDR Hex Protocol & Gateway Heartbeat (`res/rdr`)
- **Tujuan Arsitektur**:
  - Mengemulasikan 4 reader gateway (`a4c13801`, `ee100101`, `c0100101`, `e1a00103`) untuk protokol biner 34-byte (68 karakter hex) pada port web `3334`.
  - Memancarkan transmisi tag pada topik `rdr/<id_gateway>` tiap 200 ms dan heartbeat periodik tiap 5000 ms (5 detik) ke topik `res/rdr`.
- **Spesifikasi Heartbeat**:
  - **Topik**: `res/rdr`
  - **Interval**: 5000 ms (5 detik).
  - **Payload**: String plain text `<id_module>` dari gateway aktif (mis. `"a4c13801"`), tanpa pembungkus JSON atau biner.
- **HUD & Visual Telemetry**:
  - **HUD Overlay Glassmorphism**:
    - `TOTAL MESSAGES`: Counter akumulasi pesan MQTT (Tag + Heartbeat).
    - `HEARTBEAT (5s)` (`#hud-heartbeats`): Counter emisi heartbeat gateway dengan token warna `#c084fc` (Purple Violet Neon).
    - `TOTAL PACKETS`: Counter paket biner deteksi tag.
    - `EGRESS BYTES`: Total throughput keluar dalam format KB.
  - **Real-time Log Box (`#log-box`)**:
    - Menampilkan paket transmisi hex 68 karakter: `📤 [rdr/<gw>] <tag>: <rssi> dBm...`
    - Menampilkan heartbeat gateway tiap 5 detik: `💓 [res/rdr] Heartbeat: "<id_module>" (<gw_name>)`
  - **Freeze Protection & Background Optimization**:
    - `Page Visibility API`: Mencegah antrian render canvas dan manipulasi DOM berlebihan saat browser berada di background/tab lain.
    - Log buffer dibatasi maksimal 25 item terbaru (`children.length > 25`).
    - Drag-and-drop mouse move di-throttle dengan interval 40 ms (`requestAnimationFrame`).

## 33. Firmware ESP32 Gateway Heartbeat Subsystem (Topik `CRHC3048`)
- **Tujuan**:
  - Mengirimkan sinyal detak jantung (*heartbeat*) periodik langsung dari gateway ESP32 ke broker MQTT pada topik independen `CRHC3048`.
- **Spesifikasi Kanal**:
  - **Topik**: `CRHC3048` (string konstan)
  - **Interval**: 5000 ms (5 detik)
  - **Payload**: `device_id` (plain text 8-karakter hex lowercase, misal: `"299cbddb"`), bersumber dari `Core::ConfigStore::getDeviceId()`.
  - **QoS**: `0` (At-most-once)
  - **Trigger Awal**: Emisi perdana dieksekusi seketika saat event `MQTT_EVENT_CONNECTED` diterima dari broker.
- **Integrasi Throughput & Bandwidth Monitoring**:
  - Setiap paket heartbeat dihitung secara akurat menggunakan `recordWireBytes(calculateWireBytes(strlen(s_topicHeartbeat), payloadLen, 0))`.
  - Transmisi heartbeat otomatis tercermin pada indikator live bandwidth dashboard Web UI (`#stat_net_bandwidth`) dan log supervisor diagnostik FreeRTOS (`[MONITOR] MQTT: ... BW: x.xx KB/s`).
- **Thread Safety**:
  - Berjalan langsung di dalam loop `mqttPublisherTask` (Core 1, Prioritas 6) untuk mencegah multithreading race condition dengan antrean batch data BLE.

## 34. AT24C256 I2C EEPROM Wear-Leveling & Zero Flash-Wear Protection
- **Latar Belakang & Filosofi Desain**:
  - Penulisan log berulang ke Flash SPI internal (LittleFS) dapat memicu keausan memori (*flash wear-out* / bad sectors).
  - Sistem beralih ke penyimpanan eksternal berbasis EEPROM I2C **AT24C256** (32 KB / 256 Kbit) dengan ketahanan tulis 1.000.000 kali per siklus.
- **Hardware Auto-Detection**:
  - I2C Bus: `I2C_NUM_0` dengan pin SDA = GPIO 21, SCL = GPIO 22, Clock = 100 kHz.
  - Alamat I2C IC: `0x50`.
  - Saat booting, driver `Bsp::At24c256::init()` melakukan *non-destructive probe* ke alamat `0x50`.
  - **Fallback RAM-Only**: Jika IC AT24C256 tidak terdeteksi, gateway beroperasi dalam mode RAM-Only (FIFO 50 entri). Seluruh penulisan ke LittleFS `/storage/net_events.log` sepenuhnya dihentikan untuk menjamin nol keausan Flash (*zero flash wear*).
  - Berkas lama di LittleFS dibiarkan tetap ada dan tidak dimodifikasi.
- **Arsitektur Rolling Circular Wear-Leveling (Zero Memory Shifting)**:
  - **Ukuran Slot**: 128 bytes (tepat 2 halaman AT24C256 @ 64 bytes).
  - **Total Kapasitas**: 200 slot fisik (menggunakan 25.600 bytes dari total 32.768 bytes).
  - **Struktur Slot Data (128B Packed)**:
    - `magic` (4B): `0xA55A1074` (Marker validitas slot).
    - `seq_id` (4B): ID monontonik terus bertambah tanpa reset.
    - `uptime_sec` (4B): Uptime saat kejadian log.
    - `time_str` (24B): Kalender nyata WIB atau uptime.
    - `category` (1B): Kategori jaringan (`NetCategory`).
    - `event` (1B): Jenis event (`NetEvent`).
    - `detail` (80B): Keterangan kontekstual (IP, SSID, Broker).
    - `checksum` (2B): XOR checksum integritas data.
    - `reserved` (8B): Padding keselarasan byte.
  - **Distribusi Keausan (Wear-Leveling)**:
    - Setiap penulisan log hanya menargetkan slot berikutnya secara melingkar: `nextSlot = (headSlot + 1) % 200`.
    - **Nol Pergeseran Fisik (Zero Shifting)**: Tidak ada pemindahan data slot lama di EEPROM (`memmove` hanya dilakukan di buffer RAM).
    - **Ketahanan Operasional**: $200 \times 1.000.000 = 200.000.000$ siklus penulisan (lebih dari 380 tahun operasi kontinu pada 1 log/menit).
  - **Headerless Slot Discovery Saat Booting**:
    - Booting membaca 8-byte header (`magic` + `seq_id`) dari 200 slot secara cepat.
    - Slot dengan `seq_id` tertinggi ditetapkan sebagai `headSlot`.
    - Menghindari penulisan berulang pada 1 blok header statis di alamat `0x0000`.
- **Integrasi REST API**:
  - Endpoint `GET /api/v1/system/net-logs` menyajikan informasi diagnostik:
    - `"storage_mode"`: `"AT24C256 EEPROM (200 Slots Rolling, 50 FIFO)"` atau `"RAM-Only (Zero Flash Wear)"`.
    - `"eeprom_detected"`: `true` / `false`.
    - `"flash_protected"`: `true`.

## 35. Security Hardening, Multi-Core Concurrency & Anti-Replay Subsystem
- **Latar Belakang & Ruang Lingkup**:
  - Peningkatan menyeluruh mencakup proteksi konkurensi dual-core Xtensa, integritas memori stack FreeRTOS, keamanan otentikasi REST & WebSocket, proteksi berkas partisi OTA, serta otentikasi dinamis anti-replay MQTT.
- **1. Multi-Core Atomic Statistics (`DropLogService`)**:
  - Anggota counter statistik `DropLogService` (`s_statTotalDropped`, `s_statTtlExpired`, dll.) diubah menjadi `std::atomic<uint32_t>` internal dengan order `std::memory_order_relaxed`.
  - Struktur publik `DropSummaryStats` tetap bertipe Plain Old Data (POD) struct untuk mempertahankan kompatibilitas penuh dengan REST API tanpa modifikasi antarmuka eksternal.
- **2. Stack Optimization & Dual-Checksum EEPROM Logging (`NetLogService`)**:
  - Alokasi pemindaian slot EEPROM `validSlots[200]` dipindahkan dari stack ke segmen static BSS (`s_scanSlots`), memangkas 1.6 KB beban stack FreeRTOS saat boot.
  - Perhitungan checksum slot disesuaikan ke tepat 118 bytes (`offsetof(Bsp::EepromSlotData, checksum)`).
  - Mekanisme pembacaan boot menerapkan *dual-checksum verification* (118B format baru atau 114B format lama), menjamin backward-compatibility penuh terhadap log yang sudah tersimpan di EEPROM fisik.
  - Pembersihan log (`clearLogs()`) melakukan reset instan pada counter RAM dan menerapkan *cooperative yielding* 1 tick (`vTaskDelay(1)`) antar slot EEPROM guna mencegah starvation task lain.
- **3. Thread-Safe Session Management & Anti-DoS (`SessionManager`)**:
  - Mutex FreeRTOS `s_sessionMutex` melindungi manipulasi tabel sesi dan tracking percobaan login dari akses konkuren multi-socket.
  - Delay pemblokiran `vTaskDelay(1800)` dihapus sepenuhnya dari thread server HTTP.
  - Rate limiting diubah menjadi pelacakan berbasis IP client (hingga 6 slot IP aktif) dengan masa penahanan 60 detik setelah 5 kegagalan berturut-turut.
  - Mekanisme re-login hanya mengenali token kriptografi valid (`matches_token`), sepenuhnya menghapus pengenalan via IP NAT (`matches_ip`) untuk mencegah pembajakan sesi di jaringan NAT bersama.
- **4. Defense-in-Depth HTTP Security & WebSocket Session Guard**:
  - Seluruh respon web dan file streaming menyertakan header keamanan:
    - `X-Content-Type-Options: nosniff`
    - `X-Frame-Options: DENY`
    - `Referrer-Policy: no-referrer`
  - Cookie otentikasi ditingkatkan menggunakan atribut `SameSite=Strict`.
  - Alokasi hak akses admin otomatis pada WebSocket (`allocate_ws_slot(fd, UserRole::ADMIN)`) dihapus total; frame WebSocket hanya diproses setelah memvalidasi session cookie atau query parameter `?token=`.
- **5. Filesystem OTA Flash Erase Protection (`OtaService`)**:
  - Pengecekan `esp_littlefs_mounted("storage")` sebelum eksekusi `esp_vfs_littlefs_unregister("storage")` dan proteksi error `0x103` (`ESP_ERR_INVALID_STATE`). Mencegah rusaknya VFS cache saat partisi LittleFS ditimpa, sekaligus menjamin proses upload LittleFS via Emergency Recovery Mode tetap berhasil meskipun LittleFS awalnya belum terpasang atau gagal mount saat cold boot.
- **6. Kredensial NVS Universal Terenkripsi AES-128 Hardware-Bound (`ConfigStore`)**:
  - Mempertahankan satu citra pabrik universal `nvs.bin` untuk seluruh unit ESP32 tanpa memerlukan pembakaran eFuse fisik.
  - Pada boot pertama atau saat penyimpanan, password dienkripsi secara software menggunakan algoritma AES-128-ECB dengan key 16-byte yang diturunkan secara deterministik dari MAC fisik unik ESP32 (`esp_read_mac`) ditambah salt konstan.
  - Password tersimpan di NVS dengan prefiks `enc:`. Data legacy tanpa prefiks tetap terbaca dan secara otomatis dimigrasikan ke bentuk terenkripsi.
  - Password didekripsi ke RAM operasional saat boot agar verifikasi hash dinamis MQTT dapat berjalan tanpa beban dekripsi berulang.
  - Fallback kredensial hardcoded pada broker MQTT dihapus sepenuhnya.
- **7. Otentikasi Dinamis MQTT & 10-Item Rolling FIFO Anti-Replay (`MqttService`)**:
  - **Pencocokan Topik Eksak**: Topik perintah diverifikasi dengan panjang string eksak (`event->topic_len == strlen(s_topicCmd)`), mencegah tabrakan subtopik.
  - **Format Payload JSON**: `{"cmd":"<perintah>", "seq":<angka>, "auth":"<sha256_hex>"}`.
  - **Rumus Otentikasi SHA-256**:
    $$\text{auth} = \text{SHA256}(\text{admin\_username} + \text{":"} + \text{admin\_password\_asli} + \text{":"} + \text{cmd} + \text{":"} + \text{seq})$$
  - **Respon Kode Numerik Terpadu**:
    - `0`: SUCCESS (`{"res":"ok", "code":0, "cmd":"...", "seq":...}`)
    - `1`: BAD_FORMAT (`{"res":"err", "code":1, "seq":...}`)
    - `2`: AUTH_FAILED (`{"res":"err", "code":2, "cmd":"...", "seq":...}`)
    - `3`: DUPLICATE_SEQ (`{"res":"err", "code":3, "cmd":"...", "seq":...}`)
    - `4`: UNKNOWN_CMD (`{"res":"err", "code":4, "cmd":"...", "seq":...}`)
  - **Anti-Replay Rolling FIFO (10 Data Terakhir)**:
    - Sequence number yang sama hanya dapat dieksekusi satu kali. Sequence yang berubah (lebih tinggi maupun lebih rendah) dapat diterima secara valid.
    - Untuk perintah berfrekuensi tinggi (`ping`), rolling FIFO 10 entri dikelola di RAM guna menghemat umur pakai EEPROM.
    - Untuk perintah kritis (`reboot`, `identify`), rolling FIFO 10 entri dipersistensikan ke halaman EEPROM AT24C256 di alamat `0x6400` (64 bytes dengan magic `0x53455131` dan checksum XOR), dengan fallback otomatis ke RAM jika IC EEPROM tidak terpasang.
  - **Delayed Reboot Task**: Eksekusi perintah `reboot` dialihkan ke task FreeRTOS terpisah dengan delay 1000 ms agar paket respon MQTT QoS 1 terkirim dan ter-flush sempurna ke broker sebelum sistem restart.

## 36. Production-Grade Hardening, Zero-Corrupt Password Masking & System Integrity
- **Latar Belakang & Ruang Lingkup**:
  - Pengerasan level produksi industri mencakup mitigasi kerentanan keamanan web, pencegahan silent password corruption pada antarmuka pengguna, penguatan kriptografi NVS ke AES-128-CBC dengan per-device salt, serta optimasi stabilitas multi-core Xtensa.
- **1. Zero-Corrupt UI Password Masking (`web_template.cpp` & `web_routes.cpp`)**:
  - Seluruh field password sensitif (`admin_pass`, `user_pass`, `w_password`, `ap_password`, `mqtt_password`) di-*masking* dengan representasi titik peluru `••••••••` saat di-*render* ke form HTML/template.
  - Endpoint REST API (`api_config_auth_handler`) menerapkan filter predikat `is_masked_or_empty(val)`: jika nilai yang dikirim browser berupa `••••••••`, `********`, atau string kosong, konfigurasi password yang ada di NVS tidak akan ditimpa atau dirusak.
  - Validasi panjang minimal 8 karakter dan larangan karakter `:` diberlakukan untuk kredensial autentikasi.
  - Pembatasan peran (Role-Based Access Control): akun teknisi dibatasi hanya dapat mengubah password teknisi dan tidak diizinkan mengubah kredensial administrator.
- **2. Emergency Recovery Mode Hardening (`emergency_page.cpp`)**:
  - Menghapus atribut default `value="admin"` pada input kata sandi administrator untuk mencegah eskalasi akses tanpa otorisasi.
  - Membersihkan timer pemulihan (`clearInterval`) setelah hitung mundur reboot selesai guna mencegah memory/timer leak di peramban klien.
  - Menghapus isian default `'admin'` pada dialog konfirmasi `prompt()` fungsi reboot darurat.
- **3. Defense-in-Depth HTTP Content-Security-Policy & Path Traversal Guard**:
  - Menambahkan header `Content-Security-Policy: default-src 'self' 'unsafe-inline' data: ws:;` pada setiap respon HTTP untuk memitigasi serangan Cross-Site Scripting (XSS).
  - Melindungi handler aset berkas (`assets_wildcard_handler`) dari eksploitasi direktori traversal (`..` dan `//`).
  - Mengamankan pemanggilan `fopen` untuk berkas statis dan gzip dengan mode biner `"rb"`.
  - Memperluas buffer Set-Cookie HTTP (`cookie_hdr`) menjadi 192 bytes untuk menampung token sesi 64-karakter (256-bit entropy).
- **4. Cryptographic Storage Hardening (`config_store.cpp`)**:
  - Meningkatkan cipher enkripsi password NVS dari AES-128-ECB menjadi **AES-128-CBC** dengan Initialization Vector (IV) 16-byte acak per-penulisan (`esp_fill_random`).
  - Menghasilkan dan memvalidasi salt unik per-unit di partisi NVS (`cfg_sys:k_salt`) sehingga dua gateway dengan MAC identik atau password identik menghasilkan ciphertext yang berbeda.
  - Mendukung panjang kata sandi hingga 63 karakter tanpa risiko buffer truncation.
  - Mempertahankan kompatibilitas penuh backward-compatibility: sistem secara otomatis mendeteksi format lama (`enc:` atau plaintext) dan memigrasikannya ke format terenkripsi CBC ber-prefiks `cbc:`.
- **5. Session Entropy & Timing Attack Prevention (`session_mgr.cpp`)**:
  - Ukuran token sesi diperluas menjadi 64 karakter heksadesimal (256-bit entropy).
  - Mengimplementasikan perbandingan *constant-time* (`constant_time_eq`) pada seluruh tahap verifikasi, pembaharuan, dan pemusnahan token sesi untuk mencegah serangan *timing attack*.
- **6. Multi-Core Concurrency & Deadlock Prevention**:
  - Mengisolasi task reboot dan reset ke Core 1 (`xTaskCreatePinnedToCore(..., 1)`).
  - Menyeragamkan penguncian buffer telemetri beacon (`s_beaconBufMutex`) untuk melindungi buffer live dan buffer drop tanpa fragmentasi mutex.
  - Mengadopsi pola *Copy-Then-Send* pada WebSocket broadcast (`broadcast_ws_text`) untuk mencegah kondisi deadlock antara HTTP socket send dan session lock.
  - Menyesuaikan prioritas `nRF_UART_Task` ke `configMAX_PRIORITIES - 4` untuk menjamin tidak terjadinya kelaparan proses (*starvation*) pada task jaringan dan sistem.
- **7. OTA Integrity & Size Verification (`ota_service.cpp` & `web_routes.cpp`)**:
  - Memverifikasi header biner OTA pada chunk pertama (minimal 112 byte) sebelum melakukan penulisan ke flash.
  - Memvalidasi batas ukuran maksimum berkas OTA (maksimal 4 MB) menggunakan konversi aman `strtoul`.
  - Melakukan verifikasi integritas sistem berkas pasca-flash dengan memeriksa keberadaan dan ukuran file krusial (`index.html` dan `app.js`).
- **8. Intranet Authentication & Browser Password Manager Suppression (`web_routes.cpp` & `login.html`)**:
  - **Cookie Policy (`SameSite=Lax`)**: Seluruh respon header `Set-Cookie` (`api_login_handler`, `api_logout_handler`, `api_auth_check_handler`, `api_touch_ap_handler`, `api_ota_upload_handler`) menggunakan atribut `SameSite=Lax`. Hal ini memastikan cookie sesi HTTP pada alamat IP intranet/lokal selalu disertakan oleh peramban berbasis Chromium (Edge, Chrome) pada navigasi top-level tanpa memicu kecurigaan atau isolasi cookie ketat.
  - **DOM Form Credential Sanitization (`login.html`)**:
    - Elemen form menggunakan `autocomplete="off"` dan `onsubmit="return false;"`, serta tombol submit diubah menjadi tombol mandiri `<button type="button" id="btnLogin">` guna mencegah penangkapan form submit standar oleh peramban.
    - Input dilengkapi atribut eksplisit `autocomplete="username"` dan `autocomplete="current-password"` dengan `spellcheck="false"`.
    - Dukungan keyboard `Enter` ditangani melalui event listener independen.
    - Sesaat setelah respon otentikasi `200 OK` diterima dari ESP32, nilai elemen `#password` dan `#username` di DOM langsung dikosongkan (`value = ""`) sebelum `window.location.href = "/"` dieksekusi. Pola ini mencegah Windows Web Account Manager (WAM) dan Microsoft Wallet memicu sinkronisasi akun saat event `beforeunload`/`unload`, sehingga secara efektif mengeliminasi pop-up error Windows `0x8007052e`.
- **9. Overview Spec Grid: Dynamic MAC Address & Realtime Signal Stability (`net_manager.cpp`, `web_routes.cpp`, `web_template.cpp`, & `index.html`)**:
  - **Tile MAC Address Dinamis**: Menambahkan tile spesifikasi dengan label dinamis `%mac_label%` (`MAC Address (LAN)` saat koneksi Ethernet aktif vs `MAC Address (Wi-Fi)` saat koneksi Wi-Fi) dengan tipografi monospace cyan neon `#38bdf8` (`#valMacAddress`).
  - **Abstraksi Hardware MAC (`NetworkManager::getActiveMac`)**: Memilih MAC interface W5500 via IOCTL driver saat Ethernet terpasang, atau eFuse Wi-Fi STA saat mode nirkabel, menjamin pembacaan konsisten tanpa overhead.
  - **Realtime RSSI Polling Stability**: Endpoint `/api/v1/system/version` menyertakan key numerik `wifi_rssi` saat Wi-Fi terhubung, mencegah nilai Signal Strength di Overview berubah menjadi strip `-` pada siklus polling berkala (5 detik).
- **10. Professional System Logging Taxonomy, Audit Trail & Hardware-Level Diagnostics (`net_log_service.*`, `EthernetW5500.cpp`, `mqtt_service.cpp`, & `web_routes.cpp`)**:
  - **Zero-Speculative Logging Taxonomy**:
    - Seluruh terminologi probabilistik (*"kemungkinan"*, *"mungkin"*, atau *"diduga"*) dieliminasi secara ketat dari seluruh subsistem logging.
    - Setiap pesan log menyajikan fakta teknis deterministik berbasis status register hardware, event handler ESP-IDF, dan errno TCP/IP socket standar POSIX/lwIP.
  - **Comprehensive Operational Audit Trail**:
    - Penambahan event audit operasional pada `NetEvent` enum (bertipe `uint8_t` sehingga ukuran `EepromSlotData` tetap tepat 128 bytes dan kompatibel penuh dengan EEPROM fisik):
      - `AUTH_UPDATE`: Pembaruan kredensial username/password administrator atau teknisi (`Kredensial Akun Diperbarui oleh Admin/Teknisi`).
      - `CONFIG_UPDATE`: Perubahan konfigurasi sistem dari antarmuka Web UI (`Pengaturan Sistem Disimpan oleh Admin`).
      - `CONFIG_DISCARD`: Pembatalan konfigurasi tertunda (*staged configuration*) oleh operator (`Perubahan Konfigurasi Jaringan Dibatalkan oleh Pengguna`).
      - `LOG_CLEARED`: Pembersihan histori log sistem pada memori EEPROM (`Seluruh Riwayat Log Sistem Dibersihkan oleh Admin/Teknisi`).
      - `CONFIG_RESET`: Eksekusi factory reset / inisialisasi ulang konfigurasi pabrik (`Konfigurasi Sistem Direset ke Standar Pabrik (Web Admin)`).
  - **Deep-Dive Ethernet Diagnostics (Wiznet W5500)**:
    - **Link Up Auto-Negotiation**: Menanyakan status register PHY via `esp_eth_ioctl(ETH_CMD_G_SPEED)` dan `esp_eth_ioctl(ETH_CMD_G_DUPLEX_MODE)`, mencatat kecepatan aktual (`10/100 Mbps`), mode transmisi (`Full/Half-Duplex`), serta alamat MAC fisik interface LAN.
    - **Physical Link Down**: Mendeteksi pencabutan kabel RJ45 atau port switch nonaktif secara deterministik (`Kabel Fisik RJ45 Terputus / Switch Port Nonaktif (Link Down)`).
    - **Hardware Interface Deactivation**: Pencatatan resmi saat mode switched ke Wi-Fi (`Interface Ethernet Dinonaktifkan (Peralihan ke Mode Wi-Fi)`).
    - **IP Allocation Mode**: Mencatat mode perolehan IP (`DHCP` vs `Static`), subnet mask, dan gateway address (`IP (DHCP/Static): X.X.X.X | GW: Y.Y.Y.Y | Mask: Z.Z.Z.Z`).
    - **Controller Init Guard**: Pencatatan deteksi hardware W5500 pada bus SPI beserta status kegagalan komunikasi jika chip tidak merespon SPI (`Inisialisasi Hardware W5500 SPI Controller Berhasil` vs `Hardware W5500 Tidak Terdeteksi / SPI Gagal`).
  - **Definitive MQTT Error & Disconnect Cause Mapping**:
    - **Errno Translator**: Menerjemahkan `sock_errno` dari transport layer lwIP ke terminologi baku:
      - `ECONNRESET (104)`: `Koneksi Direset oleh Server / Broker (RST)`
      - `ETIMEDOUT (110)`: `Koneksi TCP Timeout (Batas Waktu Terlampaui)`
      - `EHOSTUNREACH (113)`: `Host Broker Tidak Dapat Dijangkau (Unreachable)`
      - `ECONNREFUSED (111)`: `Koneksi Ditolak oleh Broker (Port Tertutup / Service Down)`
      - `EPIPE (32)`: `Jalur Komunikasi TCP Terputus (Broken Pipe)`
      - `ENOTCONN (128)`: `Socket Tidak Terhubung (Socket Not Connected)`
      - `EHOSTDOWN (118)`: `Host Broker Tidak Aktif (Host Down)`
    - **Clean Broker FIN / Keepalive Timeout**: Ketika socket ditutup tanpa errno error (`error_type == NONE`), secara deterministik dicatat sebagai `Sesi Dihentikan oleh Broker (TCP FIN / Keepalive Timeout)`.
    - **Protocol-Level Return Codes**: Menerjemahkan kode error return MQTT connect:
      - `1`: `Protokol Tidak Diterima (Unacceptable Protocol Version)`
      - `2`: `Identifier Ditolak (Identifier Rejected)`
      - `3`: `Server Broker Tidak Tersedia (Server Unavailable)`
      - `4`: `Kredensial Tidak Valid (Bad Username or Password)`
      - `5`: `Akses Ditolak / Tidak Memiliki Izin (Not Authorized)`
    - **Keepalive Optimization**: Interval heartbeat keepalive MQTT dinaikkan dari 15 detik ke 30 detik untuk meningkatkan toleransi latensi jaringan seluler/WAN serta mengurangi pemutusan agresif.
- **11. Beacon Drop Pipeline Hardening, Zero-Loss Telemetry Synchronization & Hardware Observability (`drop_log_service.*`, `beacon_pipeline.*`, `mqtt_service.cpp`, `main.cpp`, `bsp_uart.cpp`)**:
  - **Zero-Loss TTL Pruning Pipeline**:
    - Paket yang melebihi batas waktu hidup `data_ttl` saat ditarik oleh `mqttPublisherTask` dicatat ke sistem drop log sebelum dilewati (`Core::BeaconPipeline::recordDroppedBeacon(packet, DropReason::TTL_EXPIRED)`).
    - Menghidupkan fungsi pembersihan antrean `Core::BeaconPipeline::pruneExpired()` secara berkala pada `supervisorTask` (`src/main.cpp`), menjamin paket kedaluwarsa dibersihkan dan dicatat bahkan saat koneksi MQTT sedang terputus.
  - **Batch Buffer Flush Protection**:
    - Mengeliminasi *silent drop* saat koneksi jaringan atau MQTT putus sesaat. Jika `batchCount > 0` saat koneksi terputus, memanggil `Core::BeaconPipeline::recordMqttFailed(batchCount)` terlebih dahulu sebelum mereset buffer batch ke nol.
  - **Unified Packet-Count Drop Synchronization**:
    - Menambahkan parameter `uint32_t count = 1` pada `DropLogService::record()`. Saat batch berisi $N$ paket gagal, counter akumulatif atomik `s_statTotalDropped` dan `s_statMqttFailed` bertambah sebesar $N$ paket (bukan 1 batch), menyinkronkan metrik antara WebSocket Live Monitor dan kartu KPI Drop Log REST API.
  - **Consolidated Single Source of Truth (`getTotalDropped()`)**:
    - Fungsi `BeaconPipeline::getTotalDropped()` mengagregasi seluruh 6 kategori drop (`TTL + RSSI + UUID + Major + Queue + MQTT`), menjamin kesesuaian nilai total header WebSocket dengan rincian drop summary.
  - **Synchronized Log & Counter Clear**:
    - Operasi penghapusan log drop (`POST /api/v1/system/drop-logs/clear`) mengosongkan tabel riwayat 50 data di RAM dan serentak mereset seluruh variabel atomik sesi (`s_statTotalDropped`, `s_statTtlExpired`, dll.) serta counter pipeline via `BeaconPipeline::resetDropCounters()`.
  - **Edge-Case Hardening & Concurrency Protection**:
    - **TTL Zero Guard**: `BeaconPipeline::pruneExpired()` memvalidasi `g_config.data_ttl <= 0` di awal fungsi guna mencegah pembuangan paket baru secara tidak sengaja saat TTL dinonaktifkan (`data_ttl = 0`).
    - **Queue Contention & Race Prevention**: `supervisorTask` hanya memanggil `pruneExpired()` saat MQTT offline (`!MqttService::isConnected()`), mengeliminasi kontensi antrean dan *preemption race* antara `xQueuePeek` dan `xQueueReceive` dengan `mqttPublisherTask` (Prioritas 6).
    - **Unconditional Atomic Stats Clear**: Reset seluruh variabel atomik `s_stat*` pada `DropLogService::clearLogs()` diposisikan di luar penguncian mutex `s_dropMutex`, menjamin reset metrik kartu KPI 100% instan tanpa risiko terhambat timeout mutex.
- **12. Telemetri Total Reboot ESP32, Disconnect Counters Per-Interface & Persistensi AT24C256 EEPROM / RAM Fallback**:
  - **Pemisahan Domain Metrik Mutlak**:
    - **Total Reboot ESP32**: Didedikasikan 100% untuk menghitung siklus boot hardware chip ESP32 (`boot_count` kali), dilengkapi keterangan alasan reset hardware terakhir (`esp_reset_reason()`: Power-On Cold Boot, Software Reboot Manual, Task Watchdog Timer, Brownout, dll.).
    - **Disconnect Counters Per-Interface**: Metrik pemutusan koneksi dipisahkan secara independen pada kartu masing-masing antarmuka:
      - Wi-Fi Station: `IP: X.X.X.X • DC: <n> kali`
      - Ethernet W5500: `IP: Y.Y.Y.Y • DC: <n> kali`
      - MQTT Broker: `Port: 1883 • DC: <n> kali`
  - **Alokasi EEPROM Mandiri Bebas Benturan (`0x6440`)**:
    - Struktur data `SystemStatsStore` dialokasikan pada alamat `0x6440` (persis 64 byte, 1 halaman AT24C256).
    - Mempertahankan integritas slot 200 log jaringan (`0x0000 - 0x63FF`) dan `MqttSeqStore` (`0x6400 - 0x643F`).
    - Dilindungi checksum **CRC16-LE** untuk ketahanan terhadap pemadaman listrik tiba-tiba (*power-cut resilience*).
    - Mekanisme *throttled commit* (maksimal tulis setiap 3 detik) untuk menjaga ketahanan siklus tulis EEPROM.
    - Fallback otomatis ke variabel static RAM jika modul EEPROM I2C tidak terpasang (*Zero Flash Wear*).
  - **Perbaikan Bug Ethernet W5500 Link Up**:
    - Mengeliminasi kebocoran evaluasi `isLinkUp()` yang sebelumnya mengembalikan `true` saat Wi-Fi aktif.
    - Menambahkan `NetworkManager::isEthernetLinkUp()` yang murni memeriksa status fisik chip SPI W5500 (`s_ethDriver && s_ethDriver->isConnected()`).
    - Jika kabel RJ45 dicabut, kartu Ethernet menampilkan status **LINK DOWN** (badge merah) dan keterangan **Kabel Terputus** secara deterministik.
  - **Penggantian KPI Drop Tab ke-4**:
    - Mengganti kartu redundan *Buffer Riwayat / 50 Data* dengan **`GAGAL PUBLISH MQTT`** (`#kpiDropMqtt`, badge rose `Broker Fail`), terhubung langsung ke akumulasi `summary.mqtt_failed`.
- **13. Komponen Navigasi Paging 10 Data per Halaman (10-Item Table Pagination)**:
  - **Kapasitas Standar**: 10 baris per halaman (`PAGE_SIZE = 10`), diterapkan seragam pada kedua tabel di `/log` (*Log Jaringan & Sistem* dan *Log Drop Data Beacon*).
  - **Anatomi UI Footer (`.log-pagination-wrap`)**:
    - **Info Rentang**: `Menampilkan 1 - 10 dari 50 data` (dinamis mengikuti filter & pencarian).
    - **Navigasi Tombol**: `[‹ Prev] [ 1 ] [ 2 ] [ 3 ] [ 4 ] [ 5 ] [ Next ›]`.
  - **Gaya Desain Dark Cyber**:
    - Tombol `.btn-page`: Background `rgba(255,255,255,0.05)`, border `1px solid rgba(255,255,255,0.1)`, teks slate `#94a3b8`, font monospace, min-width 34px.
    - State Aktif: Background `#38bdf8`, teks gelap `#0f172a`, font-weight 700, cyan glowing shadow `0 0 10px rgba(56,189,248,0.4)`.
    - State Hover: Background `rgba(56, 189, 248, 0.15)`, border `#38bdf8`, teks cerah.
    - State Disabled: Opacity 0.35, kursor `not-allowed`.
  - **Smart UX Auto-Reset**:
    - Mengklik tab kategori segmen atau mengetik di kotak pencarian secara instan menghitung ulang total halaman dan mereset posisi navigasi ke **Halaman 1**.
- **14. Kebijakan Pencatatan Drop Log: Isolasi Kondisi Online (MQTT Connected Only)**:
  - **Prinsip Arsitektur**:
    - Log Drop Data Beacon (`/log?tab=drop`) difokuskan murni untuk memantau performa pipeline dan integritas transmisi data saat sistem dalam kondisi operasional normal (**MQTT Connected**).
    - Kegagalan transmisi selama fase inisialisasi awal (booting), proses negosiasi DHCP Wi-Fi/Ethernet, atau saat koneksi internet terputus dikategorikan sebagai anomali jaringan (*Network Event*) dan dicatat secara akurat di *Log Jaringan & Sistem* (Network Logs) serta counter *Disconnect*.
  - **Mekanisme Eksekusi (Zero False-Drop Enforcement)**:
    - `BeaconPipeline::recordDroppedBeacon()` dan `DropLogService::record()` dilindungi oleh evaluasi predikat: alasan drop pipeline normal (`TTL_EXPIRED`, `RSSI_OUT_OF_RANGE`, `UUID_MISMATCH`, `MAJOR_MISMATCH`, `QUEUE_OVERFLOW`) hanya dicatat saat MQTT Connected.
    - **Pengecualian Khusus Gagal Publish MQTT (`DropReason::MQTT_FAIL`)**: Paket batch yang gagal dipublish ke broker MQTT (akibat socket error, outbox overflow, atau koneksi Wi-Fi/MQTT terputus saat batch siap kirim) **tetap selalu dicatat** ke Log Drop dan counter `GAGAL PUBLISH MQTT` meskipun sistem dalam kondisi disconnect.
    - `BeaconPipeline::pruneExpired()` tetap melakukan pembersihan antrean saat offline agar RAM FreeRTOS tidak jenuh, namun pembersihan ini dilakukan secara *silent* tanpa mencemari Log Drop.
    - `BeaconPipeline::flushQueue()` dieksekusi secara otomatis pada event `MQTT_EVENT_CONNECTED`. Paket beacon usang yang terakumulasi selama jeda koneksi langsung dikosongkan sebelum task publisher mulai bekerja, menjamin tidak ada lonjakan drop TTL palsu sesaat setelah gateway terhubung.

## 37. Fitur Enkripsi MQTTS (MQTT over SSL/TLS) Dinamis & Factory NVS Synchronization
- **Latar Belakang & Spesifikasi**:
  - Menyediakan perlindungan privasi dan keamanan tingkat industri untuk transmisi telemetri beacon, status gateway, dan penerimaan perintah kendali melalui saluran terenkripsi MbedTLS (TLS 1.2 / 1.3).
  - Pilihan protokol bersifat dinamis: pengguna dapat beralih antara **MQTT Plain (TCP)** dan **MQTTS (TLS/SSL)** langsung dari antarmuka Web Admin (`/auth_admin.html`) tanpa perlu kompilasi ulang firmware.
- **1. Master NVS & Factory Default Synchronization (`nvs_config.csv` & `config_store.cpp`)**:
  - **Tabel Partisi Master**: Menambahkan baris `use_ssl,data,u32,0` di bawah namespace `mqtt` pada berkas `nvs_config.csv` (standar default pabrik: Plain TCP `0`).
  - **First-Boot Clone**: Fungsi `initNvsConfiguration()` menyalin `use_ssl` dari partisi master `mqtt` ke working namespace `cfg_mqtt` (`nvsCopyU32(hMaster, hDest, "use_ssl", 0);`).
  - **Factory Reset Safety**: Pada eksekusi factory reset via tombol boot recovery atau Web UI, parameter `use_ssl` dikembalikan ke default `0`.
- **2. Driver & Engine MQTTS ESP-IDF (`mqtt_service.cpp`)**:
  - **Dinamika URI Scheme**: Menggunakan `mqtts://` saat `use_ssl == 1` dan `mqtt://` saat `use_ssl == 0`.
  - **Transport & Verifikasi**: Menggunakan URI Scheme (`mqtt://` vs `mqtts://`) untuk menentukan transport tanpa mengisi `broker.address.transport` secara manual (mencegah `ESP_ERR_INVALID_ARG` akibat konflik skema URI di ESP-IDF). Saat SSL aktif, sistem menyematkan **`esp_crt_bundle_attach`** (kumpulan ~200 Mozilla Root CA resmi ESP-IDF untuk memverifikasi seluruh broker publik secara otomatis seperti PC/MQTT Explorer) serta mengirimkan ekstensi **TLS SNI (Server Name Indication)** secara presisi agar kompatibel penuh dengan arsitektur multi-tenant Cloud Serverless (misal EMQX Cloud Serverless). Pesan Last Will (LWT) tetap mempertahankan `retain = 1` sesuai kebutuhan state presensi gateway.
  - **Task Stack Safety & Reconnect Sequence**: Alokasi `mqtt_cfg.task.stack_size` dinaikkan menjadi `8192` bytes guna menjamin kecukupan memori stack selama proses negosiasi kriptografi TLS Handshake. Fungsi `forceReconnect()` menangani `esp_mqtt_client_start()` secara adaptif pada koneksi jaringan pertama.
- **3. Antarmuka Web & Smart Port Switcher (`auth_admin.html`, `web_template.cpp`, & `web_routes.cpp`)**:
  - **Komponen Switch**: Mengadopsi `.toggle-card-row` dengan switch iOS `.ios-switch` pada Bento Card 4 (*Broker Telemetri MQTT Gateway*).
  - **Smart Port Adjustment**: Script JavaScript secara otomatis mengganti nilai input port menjadi `8883` saat MQTTS diaktifkan (jika sebelumnya `1883` atau `1884`), dan mengembalikannya ke `1884` saat dinonaktifkan.
  - **Integrasi REST API**: Endpoint `POST /api/v1/config/auth` mem-parsing parameter `mqtt_use_ssl` dan menyimpannya secara persisten ke NVS Flash.
- **4. Telemetri & Visualisasi Status (`log.html` & `log.js`)**:
  - Pada Card 3 (*MQTT Broker*), jika mode MQTTS aktif (`data.mqtt_ssl === true`), judul kartu berubah menjadi **MQTTS Broker** dan port menampilkan indikator gembok enkripsi: **`🔒 8883`**.

## 38. Fitur Auto-Detection Direct LAN Peer-to-Peer ke PC & Fallback Micro-DHCP Server
- **Latar Belakang & Spesifikasi**:
  - Memungkinkan gateway terhubung langsung ke PC/laptop via kabel LAN (straight-through atau crossover) tanpa memerlukan router atau server DHCP eksternal.
  - Perangkat otomatis mendeteksi ketiadaan server DHCP (timeout probe 11.5s - 13.5s dengan jitter berbasis MAC) dan secara cerdas mengaktifkan **Embedded Micro-DHCP Server** pada subnet terpadu **`192.168.4.1`**.
  - Laptop teknisi yang berada pada setting default "Obtain an IP address automatically" otomatis menerima IP **`192.168.4.2`** dalam hitungan milidetik tanpa perlu setting IP manual di Windows/macOS/Linux.
- **1. Visual Tokens & Komponen UI (`app.css` & `index.html`)**:
  - **Badge Mode Direct LAN (`.badge-tech.direct`, `.badge-tech.mode-config`)**:
    - Background: `rgba(59, 130, 246, 0.2)`
    - Text: `#60a5fa` (Blue 400)
    - Border: `1px solid rgba(59, 130, 246, 0.4)`
    - Teks Label: `Ethernet (Direct PC 192.168.4.1)` untuk Master Node, atau `Ethernet (Peer Slave 192.168.4.X)` untuk Slave Node.
- **2. Indikasi Fisik LED COM (`bsp_led.cpp`)**:
  - Saat mode Direct LAN (Master maupun Slave) aktif, sistem memanggil `Bsp::LedManager::setConfigMode(true)`.
  - Lampu **LED COM** berkedip **BIRU 1 Hz** (500ms ON, 500ms OFF) identik dengan mode konfigurasi Hotspot, memberikan kepastian visual bahwa gateway sedang dalam mode konfigurasi.
- **3. Zero Internet Blackout Enforcement (RFC 2131)**:
  - Micro-DHCP sengaja **meniadakan Option 3 (Router/Gateway)** dan **Option 6 (DNS)** pada paket penawaran BOOTP.
  - Laptop teknisi hanya membuat rute on-link untuk `192.168.4.0/24`, sehingga koneksi internet Wi-Fi di laptop tetap aktif normal 100%.
- **4. Multi-Device Hub & ARP Anti-Collision**:
  - Mengadopsi ARP Probe (RFC 5227) sebelum menetapkan `192.168.4.1` dengan MAC-seeded timer jitter.
  - Perangkat kedua di hub dummy otomatis menjadi Slave dengan IP unik `192.168.4.X` dan mematikan DHCP server untuk mencegah konflik.
- **5. Discovery Lintas Subnet**:
  - Protokol UDP Discovery Port `30303` (skrip `scripts/scan_gateway.py`) dan mDNS (`http://sg10-<id>.local`) aktif mengumumkan status gateway baik pada mode DHCP, Direct LAN, maupun IP Statis lama.
- **6. Dukungan Dual-Interface (Wi-Fi Online + Direct LAN Maintenance Port)**:
  - **Prinsip Arsitektur**:
    - Port Ethernet W5500 selalu diinisialisasi dalam kondisi siaga (*Maintenance Ready*) baik saat gateway dikonfigurasi pada mode Ethernet maupun Mode Wi-Fi (`ConnectionMode::WIFI`).
    - Kapan pun kabel LAN dicolokkan ke laptop teknisi saat gateway sedang beroperasi di Mode Wi-Fi, port Ethernet otomatis beralih ke mode Direct LAN (`192.168.4.1`) dengan Micro-DHCP aktif.
  - **Proteksi Routing Internet & MQTT Cloud**:
    - Default route internet (`0.0.0.0/0`) tetap dipertahankan 100% pada interface Wi-Fi Station (`wlanif`), karena Ethernet Direct LAN sengaja tidak menetapkan Default Gateway (`gw = 0.0.0.0`).
    - **Proteksi DNS lwIP & Anti-Hijacking**: Mengatasi perilaku internal ESP-IDF lwIP di mana panggilan `esp_netif_set_ip_info()` pada interface Ethernet secara otomatis memanggil `dns_clear_servers(true)` yang menghapus seluruh DNS server secara global. Sistem secara proaktif mengunci `s_staNetif` sebagai default netif dan memulihkan DNS utama (`8.8.8.8`) serta backup (`1.1.1.1`) via `dns_setserver()`, sekaligus mengabaikan `IP_EVENT_ETH_GOT_IP` saat mode Wi-Fi aktif.
    - Transmisi data telemetri beacon via MQTT ke cloud terus berjalan lancar tanpa terputus sama sekali saat teknisi melakukan konfigurasi.
  - **Representasi UI Dashboard (`index.html`)**:
    - Saat dual-interface aktif (Wi-Fi Online + Direct LAN tersambung), badge status koneksi menampilkan label cerdas: **`Wi-Fi + Direct LAN (192.168.4.1)`** dengan styling `.badge-tech.direct` (biru).
    - Tile sinyal otomatis menampilkan **`Ethernet Link (Direct LAN)`** dengan kecepatan link (misal `100 Mbps (Full Duplex)`), dan label MAC beralih ke **`MAC Address (LAN)`**.
  - **Restorasi Otomatis saat Kabel Dicabut**:
    - Ketika teknisi mencabut kabel LAN, event `ETHERNET_EVENT_DISCONNECTED` seketika mematikan Micro-DHCP, mereset IP Ethernet ke `0.0.0.0`, mematikan mode konfigurasi LED COM (`setConfigMode(false)`), dan menyinkronkan kembali COM LED ke status online Wi-Fi (Solid Green) tanpa perlu reboot perangkat.
## 39. Overview Bento Grid & Dual Interface System (2026 Cybernetic Dark)
- **Latar Belakang & Spesifikasi**:
  - Merekonstruksi antarmuka halaman utama Overview (`data/index.html`) mengadopsi struktur visual Bento Grid modern 12 kolom untuk desktop dan 1 kolom bertumpuk untuk mobile.
  - Memberikan visualisasi terpisah yang jelas antara **Wi-Fi Interface** dan **Ethernet Interface** beserta parameter IP, MAC, status link, sinyal, dan telemetri MQTT.
  - Mempertahankan integritas navigasi (Sidebar, Mobile Top Navbar, Mobile Bottom Navigation) dan banner keselamatan (`#pendingRebootBanner` dan `#apModeBanner`) tanpa menyentuh bagian di luar `<section class="content">`.
  - Beroperasi 100% mandiri (*Zero CDN Dependency*) menggunakan Vanilla CSS murni dan Inline SVG, sehingga antarmuka tetap tampil sempurna di lingkungan industri tertutup tanpa akses internet.

- **1. Slim Header Card (`.sg-header-card`)**:
  - **Sisi Kiri**:
    - Ikon Microchip CPU (`#60a5fa`) dalam kontainer rounded dengan latar belakang `rgba(59, 130, 246, 0.12)`.
    - Judul: `SMART GUARD (%model%)` font tebal `700`, titik pemisah `•`, dan subtitle `Industrial Smart BLE Beacon Gateway Controller`.
  - **Sisi Kanan (Status Badges)**:
    - **Desktop (>= 640px)**: Tampil sejajar fleksibel dalam 1 baris.
    - **Mobile (< 640px)**: Tersusun simetris dalam **Grid 2x2** (`grid-template-columns: 1fr 1fr; gap: 7px;`) dengan ukuran teks `11px` (0.72rem), menjamin nol overflow dan tidak saling tindih.
    - 4 Komponen Badge:
      1. **Uptime Live**: Ikon jam cyan + waktu aktif singkat (misal `14h 08j 42m`).
      2. **Running Mode**: Dinamis (`mode-wifi`, `mode-eth`, `mode-smart`) dengan ikon dan teks status.
      3. **MQTT Telemetri**: Ikon pulse + teks status (`Connected` / `Disconnected`).
      4. **Gateway Online**: Dot hijau berdenyut 2s (`.sg-pulse-dot`) + teks `Gateway Online`.

- **2. Panel Kiri: Identitas & Sistem Gateway (`.sg-panel-left`, Desktop 5 Kolom)**:
  - Header dengan ikon Info cyan (`#22d3ee`).
  - Key-Value List vertikal:
    - `MODEL`: `%model%`
    - `MANUFACTURE`: Ikon perisai + `IoT Gateway` (`#38bdf8`)
    - `DEVICE ID (MDNS)`: Chip kode cyan `%device_id%` + tombol salin cepat (`.sg-copy-btn`)
    - `SYSTEM UPTIME`: Ticker live real-time (`#detail-uptime-live`, misal `14 Hari 08:42:19`)
  - Sub-Grid 3 Versi Sistem (Grid 3 Kolom):
    - `DEVICE VER`: `%device_version%`
    - `FIRMWARE`: `%firmware_version%` (aksen indigo)
    - `UI VERSION`: `%ui_version%` (aksen indigo)

- **3. Panel Kanan: Status & Konektivitas Jaringan (`.sg-panel-right`, Desktop 7 Kolom)**:
  - Header kartu dengan ikon network emerald (`#34d399`), deskripsi mode, dan running mode badge pill.
  - **Dual Interface Sub-Cards (`.sg-subcards-grid`)**:
    - **Desktop/Tablet (>= 640px)**: 2 kolom berdampingan (*side-by-side*).
    - **Mobile (< 640px)**: Otomatis bertumpuk 1 kolom secara vertikal untuk memberikan ruang penuh bagi alamat IP dan MAC address monospace.
    - **Sub-Card Wi-Fi (`#card-wifi`)**:
      - Header: Ikon Wi-Fi emerald, label role (`Primary Link (Connected)` / `Offline`), dan badge `Active`.
      - Data: `IP (WI-FI)` dengan tombol salin, `MAC (WI-FI)` dengan tombol salin.
      - Footer: `SIGNAL STRENGTH` dengan visualisator 4-bar sinyal (`.sg-signal-bars`) + nilai dBm tebal.
    - **Sub-Card Ethernet (`#card-ethernet`)**:
      - Header: Ikon RJ45 cable slate/blue, label role (`Kabel Terputus (Link Down)` / `Router LAN Terhubung` / `Direct LAN`), dan badge status (`Link Down` amber / `Active` emerald / `Direct PC` blue).
      - Data: `IP (ETHERNET)` dengan tombol salin dinamis (tersembunyi saat link down), `MAC (ETHERNET)` dengan tombol salin.
      - Footer: `MQTT TELEMETRI` / `LINK SPEED` dengan badge status.
      - Auto-hide dinamis via style `%eth_tiles_style%` jika modul hardware W5500 tidak terdeteksi.

- **4. Toast Notification & Clipboard API (`.sg-toast`)**:
  - Mengambang elegan di atas mobile bottom bar: `bottom: calc(var(--bottom-nav-h, 64px) + 16px)` di mobile, dan `bottom: 24px; right: 24px` di desktop.
  - Transisi slide-up halus dengan durasi tampil 2.4 detik.
  - Fungsi `copyText(text)` cerdas menggunakan `navigator.clipboard.writeText` dengan fallback `document.execCommand('copy')`.

## 40. Pencegahan FOUC (Flash of Unstyled Content) & Sinkronisasi Uptime Hardware Realtime
- **Latar Belakang & Masalah Sebelumnya**:
  - **FOUC (Flash of Unstyled Content)**: Blok tag `<style>` untuk komponen Overview SG-10 sebelumnya terletak di tengah elemen `<body>` (di dalam `<section class="content">`). Saat browser memuat halaman, browser merender elemen HTML sebelum membaca aturan CSS, sehingga memicu pergeseran tampilan mendadak (*layout shift* / FOUC). Selain itu, teks nilai default kartu (seperti `"14h 08j 42m"`, `"mode-wifi"`, `"Kabel Terputus"`) sempat ter-render sebelum ditimpa JavaScript ~100ms kemudian, menyebabkan lonjakan teks dan warna yang tampak berkedip.
  - **Uptime Tidak Sesuai**: Endpoint `/api/v1/system/version` pada `src/web/web_routes.cpp` sebelumnya tidak menyertakan data waktu aktif hardware (`uptime_sec`), sehingga JavaScript dashboard Overview jatuh ke nilai fallback statis mockup `(14 * 86400) + ...` (14 hari) dan tidak pernah mencerminkan waktu boot aktual ESP32.
- **1. Pemindahan Blok Gaya ke `<head>` (`data/index.html`)**:
  - Seluruh blok `<style>` komponen SG-10 Overview dipindahkan ke dalam `<head>` tepat sebelum penutup `</head>`.
  - Browser secara deterministik memblokir proses painting pertama hingga seluruh aturan CSS terurai, menjamin elemen HTML langsung ter-render dengan styling yang presisi sejak frame pertama.
- **2. Server-Side Template Token Pre-Rendering (`src/web/web_template.cpp`)**:
  - Server ESP-IDF C++ kini langsung mengisikan token status dan gaya ke dalam file HTML sebelum dikirimkan ke klien:
    - `%uptime_sec%`: Integer detik dari `esp_timer_get_time() / 1000000ULL`.
    - `%uptime_short%`: Teks waktu aktif format ringkas, contoh `0h 02j 15m`.
    - `%uptime_detail%`: Teks waktu aktif detail, contoh `0 Hari 00:02:15`.
    - `%header_mode_pill_class%`: Class pill status berjalan (`mode-smart`, `mode-eth`, `mode-wifi`).
    - `%wifi_role_label%`, `%wifi_role_class%`, `%wifi_status_text%`, `%wifi_status_class%`, `%wifi_card_class%`, `%wifi_signal_spec%`: Menjamin kartu Wi-Fi menampilkan status dan RSSI nyata saat pertama kali dibuka.
    - `%eth_role_label%`, `%eth_role_class%`, `%eth_status_text%`, `%eth_status_class%`, `%eth_card_class%`, `%eth_ip_display%`, `%eth_ip_class%`, `%eth_copy_style%`, `%eth_footer_label%`, `%eth_footer_value%`: Menjamin kartu Ethernet langsung menampilkan status link fisik (Direct LAN, Router LAN, atau Link Down) tanpa jeda.
- **3. Sinkronisasi Uptime Hardware Akurat (`src/web/web_routes.cpp` & `data/index.html`)**:
  - Endpoint C++ `/api/v1/system/version` menambahkan properti `"uptime_sec": (double)(esp_timer_get_time() / 1000000ULL)`.
  - Inisialisasi JavaScript menggunakan `let uptimeSeconds = parseInt("%uptime_sec%") || 0;`, memastikan ticker detik dimulai tepat dari detik saat halaman dimuat.
  - Panggilan `syncDashboardStatus()` dieksekusi seketika saat inisialisasi script dan berulang setiap interval 5 detik untuk memperbarui sinkronisasi `uptimeSeconds` dan status antarmuka jaringan.

## 41. Eliminasi Redundansi Informasi & Standardisasi Physical Link pada Ethernet Sub-Card
- **Latar Belakang & Identifikasi Redundansi**:
  - **MQTT Telemetri pada Ethernet Card**: Sebelumnya footer kartu Ethernet menampilkan status MQTT Telemetri. Hal ini tidak tepat secara arsitektur model OSI karena MQTT adalah protokol Layer 7 (Aplikasi) yang melayani seluruh gateway, bukan atribut spesifik dari interface fisik Ethernet (Layer 1/2). Pada mode Wi-Fi di mana kabel LAN dicabut, menampilkan "MQTT TELEMETRI: Disconnected" pada Ethernet card menimbulkan kebingungan seolah transmisi data gateway terputus, padahal MQTT aktif berjalan lancar melalui interface Wi-Fi.
  - **Running Mode Badge Ganda**: Di header utama (Slim Header Card) sudah terdapat badge live running mode (`Wi-Fi + Direct LAN` / `Ethernet` / `Wi-Fi`). Keberadaan badge pill kedua di header panel kanan (`#network-mode-badge`) bersifat redundan dan memakan ruang horizontal visual.
- **1. Standarisasi Footer Ethernet Card (`data/index.html` & `src/web/web_template.cpp`)**:
  - Label footer kartu Ethernet kini distandardisasi menjadi **`PHYSICAL LINK / SPEED`** secara simetris dengan footer kartu Wi-Fi (`SIGNAL STRENGTH`).
  - Nilai footer menampilkan parameter tautan fisik nyata:
    - **Direct LAN**: `<span class="sg-subcard-status-badge active">100M FD • Micro-DHCP</span>`
    - **Link Up / Router LAN**: `<span class="sg-subcard-status-badge active">100 Mbps Full-Duplex</span>` (dinamis membaca kecepatan dan duplex hardware W5500)
    - **Link Down (Kabel Terputus)**: `<span class="sg-subcard-status-badge amber">Disconnected (Link Down)</span>`
  - Seluruh referensi MQTT dihilangkan sepenuhnya dari kartu Ethernet. Status MQTT tetap terpusat dan terpantau dengan jelas pada Slim Header Card di bagian atas.
- **2. Pembersihan Header Panel Kanan (`data/index.html`)**:
  - Badge ganda `#network-mode-badge` dihapus dari header panel kanan, menyisakan tata letak yang bersih dan terfokus: Ikon Jaringan, Judul "Status & Konektivitas Jaringan", dan deskripsi mode ringkas.
  - Skrip sinkronisasi `syncDashboardStatus()` dibersihkan dari pembaruan elemen badge redundan tersebut.

## 42. Penyederhanaan Slim Header Card & Integrasi Connection Mode & MQTT pada Card Status Konektivitas
- **Latar Belakang & Rasionalisasi**:
  - Seluruh status pill pada Slim Header Card atas (`#header-uptime-short`, `#header-mode-pill`, `#header-mqtt-pill`, `.online-pill`) dihilangkan untuk menyajikan identitas visual gateway yang bersih dan elegan (*uncluttered branding bar*).
  - Informasi waktu aktif sistem (**System Uptime**) tetap terpantau akurat secara real-time pada panel kiri ("Identitas & Sistem Gateway").
  - Status koneksi spesifik interface (Wi-Fi dan Ethernet) sudah terwakilkan secara mandiri pada masing-masing sub-card interface.
  - Dua parameter tingkat jaringan gateway dialokasikan secara harmonis ke dalam header kartu **Status & Konektivitas Jaringan**:
    1. **Connection Mode** (`WIFI`, `ETHERNET`, `SMART CONNECT`).
    2. **MQTT Connection Status** (`Connected`, `Disconnected`).
- **1. Struktur Visual Slim Header Card (`.sg-header-card`)**:
  - Menyisakan ikon microchip, judul `SMART GUARD (%model%)`, bullet, dan sub-label `Industrial Smart BLE Beacon Gateway Controller`.
  - Tampil bersih dan konsisten pada desktop maupun mobile tanpa risiko wrapping pill.
- **2. Struktur Visual Badges Header Jaringan (`.sg-net-header-badges`)**:
  - **Desktop (>= 640px)**:
    - Terletak di sisi kanan header `sg-card-header` sejajar dengan judul panel.
    - Badges:
      - `MODE: WIFI` (`.mode-wifi`, latar hijau transparan, teks `#34d399`, dot indikator)
      - `MODE: ETHERNET` (`.mode-eth`, latar biru transparan, teks `#60a5fa`, dot indikator)
      - `MODE: SMART CONNECT` (`.mode-smart`, latar ungu transparan, teks `#c084fc`, dot indikator)
      - `MQTT: Connected` (`.mqtt-online`, teks `#34d399`, ikon pulse) / `MQTT: Disconnected` (`.mqtt-offline`, teks `#fbbf24`, ikon pulse)
  - **Mobile (< 640px)**:
    - Header kartu `.sg-card-header` bertumpuk vertikal (`flex-direction: column; align-items: stretch;`).
    - Badges tersusun rapi dalam **Grid 2 Kolom Simetris (50% - 50%)** (`grid-template-columns: 1fr 1fr; gap: 7px;`) dengan teks terpusat (`justify-content: center;`), menjamin nol overflow dan tidak saling bertubrukan.
- **3. Pre-Rendering Server-Side C++ (`src/web/web_template.cpp`)**:
  - Token baru untuk mencegah FOUC:
    - `%conn_mode_text%`: Menghasilkan `"SMART CONNECT"`, `"ETHERNET"`, atau `"WIFI"`.
    - `%conn_mode_class%`: Menghasilkan `"mode-smart"`, `"mode-eth"`, atau `"mode-wifi"`.
    - `%conn_mqtt_class%`: Menghasilkan `"mqtt-online"` atau `"mqtt-offline"`.
    - `%network_mode_desc%`: Deskripsi operasional real-time sesuai mode yang aktif.
## 43. Tipografi Profesional & Penyelarasan Layout Berdasarkan Mockup Referensi Pengguna
- **Latar Belakang & Filosofi Desain**:
  - Merekonstruksi hierarki tipografi dari sebelumnya menggunakan huruf kapital kaku (ALL CAPS) menjadi **Title Case** berstandar enterprise yang lebih elegan, terbaca cepat, dan profesional.
  - Memfokuskan header kartu jaringan pada **Single Connection Mode Badge** (`● MODE: WI-FI`, `● MODE: ETHERNET`, `● MODE: SMART CONNECT`), mengeliminasi elemen sekunder di header agar tampilan lebih lapang dan berfokus pada status arsitektur jaringan.

## 44. Penyempurnaan 100% Visual Parity (Font Size, Family, Spacing, Padding, Card Sizing, & Colors) Berdasarkan Referensi Mockup Pengguna
- **Latar Belakang & Evaluasi Presisi Visual**:
  - Berdasarkan perbandingan piksel langsung dengan gambar referensi pengguna (`user_clipboard_mockup.png` / `media_1792573240281.png`), dilakukan perbaikan mendalam terhadap parameter ukuran font, warna teks, jarak padding antar-elemen, ukuran kontainer kartu, dan proporsi grid.
- **1. Perluasan Kontainer & Proporsi Grid Bento (`.content` & `.sg-bento-grid`)**:
  - **Kontainer Luas (`.content`)**: Ditingkatkan batas lebarnya dari pembatas sempit `1080px` menjadi `max-width: 1440px !important; width: 100% !important; padding: 24px 28px !important;`. Hal ini memberikan napas lega bagi kartu overview untuk mengisi layar secara proporsional layaknya dashboard telemetri industrial high-end.
  - **Proporsi Grid Responsif**:
    - Desktop (`min-width: 1080px`): `grid-template-columns: minmax(340px, 36%) 1fr; gap: 24px;`.
    - Memberikan ruang horizontal ekstra (~750-800px) untuk panel kanan, sehingga kedua subcard (Wi-Fi dan Ethernet) masing-masing mendapatkan lebar ~360-400px.
    - Mengeliminasi 100% masalah pemotongan teks (ellipsis `...`) pada alamat MAC (`CC:7B:5C:BB:B6:D4`), alamat IPv4, dan judul interface (`Ethernet Interface`).
- **2. Penyelarasan Warna & Tipografi Teks (Typography & Color Tokens)**:
  - **IPv4 Address Wi-Fi**: Ditampilkan dalam warna **Putih Murni (`#ffffff`)**, bukan hijau (`#34d399`). Menggunakan font `ui-monospace` tebal (`font-weight: 700; font-size: 0.94rem;`).
  - **MAC Address (Wi-Fi & Ethernet)**: Ditampilkan dalam warna **Slate-200 / Putih Bersih (`#f1f5f9`)**, bukan cyan (`#38bdf8`). Font `ui-monospace` tebal (`font-weight: 700; font-size: 0.90rem;`).
  - **Manufacture**: Teks `IoT Gateway` menggunakan warna **Putih Murni (`#ffffff`)** dengan ikon perisai cyan `#38bdf8` (`<svg stroke="#38bdf8">`), bukan seluruh teks berwarna biru/cyan.
  - **Ethernet IPv4 Unassigned**: Teks `— (Unassigned)` dalam warna slate redup (`#64748b`, `font-size: 0.88rem; font-weight: 400;`). Tombol salin disembunyikan otomatis.
  - **Physical Link**: Teks status `Kabel Terputus` tampil tebal dalam warna **Amber (`#f59e0b` / `#fbbf24`)** (`font-weight: 700; font-size: 0.94rem;`), sejajar bersih dengan baris IPv4 dan MAC tanpa kotak badge status.
  - **Header Card & Subcard Title**:
    - Panel headers (`h3`): `font-size: 1.05rem; font-weight: 700; color: #ffffff;`.
    - Subcard titles (`h4`): `font-size: 0.98rem; font-weight: 700; color: #ffffff;`.
    - Subcard roles: `font-size: 0.80rem; font-weight: 400; color: #8191a6;`.
- **3. Bentuk Kapsul Mode Badge (`.sg-badge-pill`)**:
  - Berubah dari kotak bersudut 8px menjadi **Kapsul Penuh (`border-radius: 9999px; padding: 7px 18px;`)**.
  - Dot indikator hijau solid `●` (`#10b981`, `font-size: 0.78rem`).
  - Teks: `MODE: WI-FI` (`font-size: 0.84rem; font-weight: 700; letter-spacing: 0.04em; color: #10b981;`).
- **4. Eliminasi Garis Pembatas Header yang Kaku (Seamless Modern Cards)**:
  - Dihilangkan garis `border-bottom` pada header panel (`.sg-card-header`), memberikan estetika dark mode yang mulus (*seamless floating content*).
  - Garis `border-top` pada grid versi sistem (`.sg-version-grid`) dihilangkan, menggantinya dengan jarak spasi margin vertikal yang proporsional (`margin-top: 20px;`).
  - Baris data dalam kartu (`.sg-kv-row` dan `.sg-subcard-row`) dipisahkan oleh garis sangat halus `border-bottom: 1px solid rgba(255, 255, 255, 0.04);` dengan padding `14px - 15px 0` yang lega.
- **5. Adaptasi Responsif Mobile (< 640px)**:
  - Padding konten luar diatur ke `16px 12px` dan padding kartu internal disesuaikan menjadi `16px 14px`, mencegah pemborosan ruang horizontal pada layar 375px.
  - Title subcard (`h4`) dan status badge (`Active` / `Link Down`) memiliki ruang 50px+ ekstra, menjamin **nol tumpang-tindih (zero overlap)** dan nol pemotongan teks.
  - Tiga kartu versi sistem (`DEVICE VER`, `FIRMWARE`, `UI VERSION`) tetap tersusun 3 kolom horizontal kompak dengan font `0.94rem`.

## 45. Kartu Telemetri MQTT Broker Ramping (Slim Low-Profile Telemetry Card)
- **Latar Belakang & Keseimbangan Visual (Vertical Bento Alignment)**:
  - Pada layout Bento Overview sebelumnya, panel kiri memiliki ketinggian ~360px karena ditopang oleh 3 tile versi di bagian bawah (`DEVICE VER`, `FIRMWARE`, `UI VERSION`), sedangkan panel kanan hanya memiliki dua kartu interface (~275px), menyisakan ruang kosong vertikal (*empty delta*) sebesar ~85px.
  - Untuk mengisi ruang tersebut secara harmonis dan memenuhi kebutuhan pemantauan uplink cloud, ditambahkan kartu telemetri MQTT broker ramping tepat di bawah `.sg-subcards-grid` pada panel kanan.
- **1. Struktur Visual & Tata Letak Data (3-Column Responsive Grid)**:
  - **Header Kartu Ramping (`.sg-mqtt-header`)**:
    - Ikon Cyan Industrial (`.sg-subcard-icon.cyan`, 26x26px): Background `rgba(56, 189, 248, 0.12)`, border `rgba(56, 189, 248, 0.25)`, warna `#38bdf8`.
    - Judul: `Telemetri MQTT` (`font-size: 0.90rem; font-weight: 700; color: #ffffff;`).
    - Status Peran: `• %mqtt_role_label%` (`font-size: 0.76rem; color: #8191a6;`).
    - Badge Status Realtime: Kapsul hijau emerald `● Connected` (`.sg-subcard-status-badge.active`) atau amber `● Disconnected` (`.sg-subcard-status-badge.amber`).
  - **3 Kolom Data Sejajar Horizontal (`.sg-mqtt-data-grid`)**:
    1. **Broker Destination**: Host URL / IP broker (`g_config.mqtt_host`, misal `mqtt.iotaru.id`) dalam font `ui-monospace` putih 700 (`0.90rem`) dengan tombol salin satu-klik (`copyText`).
    2. **Port Koneksi**: Nomor port TCP (`g_config.mqtt_port`, misal `8883` atau `1883`) dalam font `ui-monospace` putih 700.
    3. **Keamanan (TLS)**: Badge status enkripsi kapsul:
       - TLS Aktif (`mqtt_use_ssl == 1`): Badge hijau dengan ikon gembok `[🔒 TLS Active]`.
       - Plain TCP (`mqtt_use_ssl == 0`): Badge slate dengan ikon terbuka `[🔓 Plain TCP]`.
- **2. Ketinggian Terkalibrasi (Slim Low-Profile Height ~80px)**:
  - Padding kartu diatur kompak `13px 18px` dengan celah antar-kolom `16px`.
  - Pada layar desktop, kartu memiliki tinggi total ~80px, membuat garis bawah panel kanan menutup sejajar rata dengan 3 tile versi di panel kiri.
- **3. Adaptasi Responsif Mobile (< 640px)**:
  - Pada layar ponsel, 3 kolom data otomatis bertransisi menjadi susunan baris fleksibel (`display: flex; align-items: center; justify-content: space-between; gap: 8px;`), label di sebelah kiri dan nilai/tombol salin di sebelah kanan.
  - Subtitle role di header disembunyikan otomatis untuk memaksimalkan ruang judul dan badge status.
- **4. Pre-Rendering C++ Server-Side (Zero FOUC)**:
  - `%mqtt_broker_host%`: Host broker (atau `—` jika belum diatur).
  - `%mqtt_broker_port%`: Nomor port string.
  - `%mqtt_tls_badge%`: Tag HTML badge keamanan (TLS Active / Plain TCP).
  - `%mqtt_conn_badge%`: Tag HTML badge koneksi (Connected / Disconnected).
  - `%mqtt_card_class%`: Class border aktif `active-mqtt` (`border-color: rgba(56, 189, 248, 0.35);`).
  - `%mqtt_role_label%`: Deskripsi status koneksi uplink.
- **5. REST API & Sinkronisasi Polling 5 Detik**:
  - Endpoint `GET /api/v1/system/version` menyertakan key `mqtt_host`, `mqtt_port`, `mqtt_ssl`, dan `mqtt_connected`.
  - Fungsi JavaScript `syncDashboardStatus()` di `data/index.html` memperbarui status badge, broker host, port, dan kapsul TLS secara live tanpa reload halaman.

## 46. Standarisasi Bahasa Inggris 100%, Huruf Kapital Konsisten, Pengisian Ruang Kosong Panel Identitas, Grid 2-Tier Mobile MQTT, dan Semantik Warna Hijau Aktif
- **1. Standarisasi Bahasa Inggris 100% (Full English UI Consistency)**:
  - Seluruh teks antarmuka diubah secara konsisten ke bahasa Inggris:
    - Navbar & Header: `Gateway Overview`, `SMART GUARD (%model%)`, `Industrial Smart BLE Beacon Gateway Controller`, `Logout`.
    - Safety Banners: `Network configuration changes saved to NVS. Reboot required to apply to hardware.`, `Reboot Now`, `Discard`, `Configuration Hotspot Active`, `Disable AP`.
    - Panel Kiri: `Gateway Identity & System`, `Unit identification parameters and live uptime`, `Device Model`, `Manufacturer`, `Device ID (mDNS)`, `System Uptime` (`%u %s %02u:%02u:%02u`, `Day` / `Days`), `System RAM`, `Bandwidth`.
    - Panel Kanan: `Network Status & Connectivity`, `Wi-Fi Interface`, `Ethernet Interface`, `MQTT Telemetry`, `Signal Strength Indicator`, `Physical Link`, `Cable Disconnected`, `Broker Host`, `Port`, `Security (TLS)`.
    - Diagnostic Bento: `Realtime Diagnostic Test`, `High-speed nRF52840 beacon packet monitoring`, `Open Live Test Monitor`.
    - Tooltips & Toasts: `Copy Device ID`, `Copy Wi-Fi IPv4`, `Copy Wi-Fi MAC`, `Copy Ethernet IPv4`, `Copy Ethernet MAC`, `Copy MQTT Broker Host`, `Copied to clipboard!`.
- **2. Aturan Kapitalisasi Text (Title Case & Technical Acronyms)**:
  - **Title Case**: Digunakan untuk seluruh Card Header, Subcard Title, Key-Value Labels, dan Tombol Aksi (`Gateway Identity & System`, `Network Status & Connectivity`, `System Uptime`, `System RAM`, `Bandwidth`, `Broker Host`, `Security (TLS)`, `Reboot Now`).
  - **UPPERCASE (Semua Huruf Kapital)**: Dibatasi secara ketat hanya untuk akronim teknis dan status hardware (`DEVICE VER`, `FIRMWARE`, `UI VERSION`, `MODE: ETHERNET`, `MODE: WI-FI`, `SMART CONNECT`, `IPv4`, `MAC`, `mDNS`, `TLS`, `RAM`, `NVS`, `FD`, `Mbps`).
- **3. Pengisian Ruang Kosong Vertikal Panel Kiri (System RAM & Bandwidth)**:
  - Untuk mengisi ruang kosong vertikal pada Panel Kiri secara proporsional dan menjaga kesetaraan ketinggian dengan Panel Kanan yang memiliki kartu MQTT:
    - **`System RAM`**: Label padat ringkas dengan bullet hijau: `<span style="color:#10b981;">●</span> 184 KB Free`. Sumber data live: `esp_get_free_heap_size() / 1024`.
    - **`Bandwidth`**: Nilai numerik throughput real-time: `14.2 KB/s`. Sumber data live: `Network::MqttService::getBandwidthKBps()`.
  - Kedua baris data ini memperbarui secara instan via pre-render template server (`%free_heap_kb%`, `%net_bandwidth%`) dan sinkronisasi real-time via `/api/v1/system/version`.
- **4. Tampilan Mobile 2-Tier Grid untuk Host Broker MQTT Ramping**:
  - Pada layar mobile (< 680px), host broker yang panjang (misalnya endpoint AWS IoT `a1b2c3d4e5f6-ats.iot.ap-southeast-1.amazonaws.com`) sebelumnya berpotensi terpotong.
  - Diperbarui menjadi **2-Tier Grid**:
    - **Tier 1 (Baris Penuh / `grid-column: 1 / -1; width: 100%;`)**: Menampilkan label `Broker Host` di bagian atas dan nilai URL broker di bawahnya dengan `word-break: break-all; white-space: normal; line-height: 1.35;` serta tombol copy di sampingnya. URL panjang secara mulus melipat ke 2 baris dan 100% terlihat jelas tanpa terpotong (*zero truncation*).
    - **Tier 2 (Grid 2 Kolom 50/50)**: Menampilkan kolom `Port` di sisi kiri dan `Security (TLS)` di sisi kanan.
  - Pada desktop (`min-width: 680px`), grid secara otomatis kembali ke 3 kolom sejajar horizontal (`1.6fr 0.9fr 1.2fr`).
- **5. Semantik Warna Outline & Ikon (Unified Green Active & Gray Inactive)**:
  - **Status Aktif / Online**:
    - Outline kartu antarmuka (`.active-online`, `.active-wifi`, `.active-eth`, `.active-mqtt`): Menggunakan outline hijau `border-color: rgba(16, 185, 129, 0.4);`.
    - Ikon antarmuka: Menggunakan warna hijau emerald (`.sg-subcard-icon.emerald`, `#10b981`, background `rgba(16, 185, 129, 0.12)`). Menghilangkan warna biru pada Ethernet dan cyan pada MQTT saat aktif.
  - **Status Tidak Aktif / Offline / Disconnected**:
    - Outline kartu: Menggunakan abu-abu gelap netral (`#1a2333`).
    - Ikon antarmuka: Menggunakan warna abu-abu netral (`.sg-subcard-icon.slate`, `#8191a6`, background `rgba(30, 41, 59, 0.6)`).
- **6. Penyesuaian Ketinggian Kartu Versi (Elevated & Balanced Version Tiles)**:
  - **Elevasi Tiga Kartu Versi**: Tiga kartu versi (`DEVICE VER`, `FIRMWARE`, `UI VERSION`) dinaikkan posisinya dari bagian bawah panel kiri untuk mengeliminasi kekosongan asimetris.
  - **Penyelarasan Spasi**:
    - Padding baris key-value (`.sg-kv-row`) disesuaikan menjadi `12px 0` yang proporsional.
    - Grid kartu versi (`.sg-version-grid`) diberikan `margin-top: auto; margin-bottom: 6px;` dengan gap `10px`.
    - Padding internal tile (`.sg-version-tile`) dibuat lebih ramping (`10px 6px`) dengan ukuran nilai `1.02rem`.
## 47. Kalibrasi Akurasi Metrik Bandwidth Jaringan (Rolling 1000ms Window & Downlink Accounting)
- **1. Masalah Sampling Asinkron (Under-reporting vs Over-reporting)**:
  - Sebelumnya, fungsi perhitungan bandwidth memanggil `exchange(0)` langsung saat dipanggil.
  - Halaman Live Test (`/test`) membaca via WebSocket setiap 250 ms (0.25 detik), sehingga angka terpecah 4x lebih kecil (~0.33 KB).
  - Halaman Overview (`/`) membaca via polling REST setiap 5 detik, sehingga angka terakumulasi multi-detik (~3.5 KB).
- **2. Arsitektur Perhitungan Independen (Rolling 1000ms Sampling Window)**:
  - Task publisher (`mqttPublisherTask`) sekarang menjalankan sampler internal independen setiap interval waktu nyata $\Delta t \ge 1000\text{ ms}$:
    $$\text{Bandwidth (KB/s)} = \frac{\Delta \text{Bytes Total}}{1024 \times (\Delta t / 1000)}$$
  - Nilai tersimpan di `std::atomic<float> s_statBandwidthKBps`.
  - Fungsi `getBandwidthKBps()` bersifat *read-only* murni tanpa me-reset counter (`load(memory_order_relaxed)`).
  - **Hasil**: Baik Halaman Test (250ms), Overview (5s), maupun Serial Log menampilkan metrik `KB/s` yang sinkron, identik, dan akurat per detik tanpa saling bentrok.
- **3. Integrasi Traffic Downlink Broker (Wire-Level Physical Accuracy)**:
  - Pencatatan byte kabel fisik (`recordWireBytes`) kini memperhitungkan **Uplink (TX)** dan **Downlink (RX)**:
    - **Downlink PUBACK**: Setiap paket QoS 1 yang di-ack oleh broker dicatat pada `MQTT_EVENT_PUBLISHED` sebesar 4 byte MQTT + 40 byte TCP/IP + Frame Layer 2 (18B Ethernet / 36B Wi-Fi).
    - **Downlink Inbound Command**: Perintah masuk dari broker pada `MQTT_EVENT_DATA` dicatat mencakup panjang topik, data payload, TCP/IP, dan header Layer 2.
    - **Downlink SUBACK**: Respon konfirmasi langganan topik pada `MQTT_EVENT_SUBSCRIBED` dicatat sebesar 5 byte MQTT + TCP/IP + Layer 2.
    - **Uplink Command Response**: Respon balik gateway terhadap perintah broker (`s_topicRes`) dicatat secara presisi di kabel jaringan.

## 48. Eliminasi Ruang Kosong Bawah (Bottom Scroll Void) & Penyelarasan Posisi Footer
- **1. Akar Masalah Penumpukan Jarak Bawah (Double/Triple Padding Stack)**:
  - Sebelumnya, `.main-panel` di `app.css` memiliki `padding-bottom: var(--bottom-nav-h);` (64px) yang berlaku tanpa batas di semua ukuran layar (termasuk desktop). Karena `<footer>` berada di dalam `.main-panel`, tercipta rongga kosong 64px persis di bawah garis batas footer.
  - Selain itu, `.content` di `index.html` menyematkan `padding-bottom: calc(var(--bottom-nav-h, 64px) + 28px) !important;` (92px) yang menyebabkan jarak raksasa antara bento card terakhir dan footer.
  - Akibatnya, saat pengguna scroll hingga footer, halaman masih dapat di-scroll turun lebih jauh ke area kosong (*scroll void*).
- **2. Penyelarasan Tata Letak Footer Bebas Rongga Kosong**:
  - **`.main-panel` (`app.css`)**: Dihapus nilai `padding-bottom`-nya (`padding-bottom: 0`) sehingga kontainer utama tidak lagi menambahkan ruang kosong di bawah footer.
  - **`footer` (`app.css`)**:
    - Desktop (`> 860px`): Menggunakan padding standar `padding: 20px;` dengan `margin-top: auto;` sehingga footer pas menempel rapi di dasar halaman dan scroll berhenti tepat di bawah teks hak cipta.
    - Mobile (`<= 860px`): Menggunakan `padding-bottom: calc(var(--bottom-nav-h) + 16px);` sehingga teks hak cipta berada 16px tepat di atas bottom navigation bar mengambang dan tidak tertutup navbar.
  - **`.content` (`index.html`)**: Diubah menjadi `padding: 24px 28px 28px 28px !important;` untuk menyeimbangkan jarak vertikal kartu dengan garis pemisah footer.

## 49. Perbaikan Buffer Template Engine & Integritas Parsing SVG Inline
- **1. Akar Masalah Browser Error `attribute d: Expected moveto path command`**:
  - Buffer lokal placeholder di fungsi `render_template()` (`src/web/web_template.cpp`) awalnya berukuran `char val[256]`.
  - Placeholder status kartu MQTT `%mqtt_tls_badge%` menghasilkan markup HTML badge dengan inline SVG lengkap sepanjang 308 karakter (mode TLS Active) atau 303 karakter (mode Plain TCP).
  - Ketika `snprintf(out_buf, max_len, ...)` dijalankan dengan `max_len = 256`, string terpotong paksa tepat pada karakter ke-255, persis setelah tag pembuka `<path `.
  - Akibatnya, browser menerima potongan HTML tak lengkap `<span ...><rect .../><path \n </div>`. Parser SVG HTML5 pada peramban (Chrome/Edge) mencoba memulihkan tag `<path>` dan menemukan `</div>`, memicu galat konsol browser:
    `Error: <path> attribute d: Expected moveto path command ('M' or 'm'), "… </div\u003E\n …"`.
- **2. Solusi & Mitigasi**:
  - **Ekspansi Buffer Template**: Buffer lokal `char val[256]` di `src/web/web_template.cpp` ditingkatkan menjadi `char val[512]`. Hal ini memberikan kapasitas hingga 511 karakter untuk semua placeholder ber-SVG inline, badge status dinamis, dan token HTML tanpa risiko pemotongan string, dengan tetap sangat aman pada stack task web server (kapasitas stack 8192 byte).
  - **Standardisasi Format SVG Path**: Atribut path SVG pada Plain TCP mode diselaraskan dengan spasi pemisah standar (`d="M7 11V7a5 5 0 0 1 9.9 -1"`) baik di backend template C++ (`web_template.cpp`) maupun frontend live synchronization (`data/index.html`) guna menjamin integritas rendering vektor di seluruh platform browser.

## 50. Perampingan Halaman Overview (Eliminasi Kartu Redundan 'Realtime Diagnostic Test')
- **1. Rasional Pembersihan UI**:
  - Kartu promosi bawah `Realtime Diagnostic Test` (beserta tombol pintas *Open Live Test Monitor*) dihilangkan dari tata letak Overview (`index.html`).
  - Fitur diagnostik transmisi dan pemantauan live packet nRF52840 telah dapat diakses langsung secara permanen melalui item menu sidebar utama (`Live Test Monitor` pada `/test`) dan bottom navigation sheet pada mobile.
  - Penghapusan kartu sekunder ini menghilangkan redundansi tombol ganda, menyatukan ritme visual Grid Bento utama (panel identitas di kiri dan panel jaringan/telemetri MQTT di kanan), serta menghasilkan tampilan dasbor yang bersih dan presisi tanpa scrolling berlebih.

## 51. Mekanisme Status Offline & Eliminasi Angka Live Uptime Saat Perangkat Padam
- **1. Akar Masalah Uptime Berjalan Terus Saat Perangkat Mati**:
  - Ticker waktu `updateUptimeDisplay()` di `data/index.html` dijalankan oleh `setInterval` lokal peramban (`uptimeSeconds++` tiap 1 detik) untuk kelancaran visual antarmuka.
  - Polling status `/api/v1/system/version` berulang tiap 5 detik untuk kalibrasi ulang `uptimeSeconds`. Namun, jika mikrokontroler mati/terputus, blok `.catch()` awalnya dibiarkan kosong tanpa penanda kegagalan koneksi (*silent fail*).
  - Akibatnya, selama tab peramban tetap terbuka, JavaScript di komputer/HP pengguna terus menghitung detik sendiri tanpa batas meskipun perangkat keras fisik sudah padam.
- **2. Solusi Desain & Standarisasi UI/UX**:
  - **Mekanisme Guard `isDeviceOnline` & AbortController**:
    - Polling HTTP dilengkapi `AbortController` dengan batas waktu *timeout* 3500ms agar permintaan jaringan yang menggantung segera diputus jika perangkat mati.
    - Variabel `offlineStreak` melacak kegagalan beruntun. Setelah 2 kali gagal berturut-turut, status beralih menjadi `isDeviceOnline = false`.
    - Di dalam `updateUptimeDisplay()`, penambahan `uptimeSeconds++` langsung dihentikan saat `!isDeviceOnline`.
  - **Komponen Visual & Token Warna (Eliminasi Angka Saat Offline)**:
    - **`#detail-uptime-dot`**: Berubah dari hijau emerald aktif (`#10b981`, `●`) menjadi merah padam (`#ef4444`, `○`) dengan transisi halus `color 0.2s ease`.
    - **`#detail-uptime-live`**: Seluruh angka durasi (seperti `0 Days 00:00:28`) dihilangkan secara tuntas dan digantikan dengan teks statis `"Offline"` berwarna merah (`#ef4444`) berbobot tebal (`font-weight: 700`) sehingga pengguna tidak dibingungkan oleh angka durasi beku (*frozen timestamp*).
    - **`#detail-uptime-badge`**: Disembunyikan (`display: none;`) untuk menghindari redundansi teks ganda ("Offline OFFLINE").
    - **`.sg-badge-pill.mode-offline`**: Badge koneksi utama di panel kanan beralih ke mode offline (`background: rgba(239, 68, 68, 0.12)`, `border: 1px solid rgba(239, 68, 68, 0.45)`, `color: #ef4444`) dengan label `DISCONNECTED` dan deskripsi `Device Unreachable — Gateway offline or connection lost`.
  - **Pemulihan Otomatis (*Auto-Recovery*)**: Begitu perangkat menyala kembali dan request `/api/v1/system/version` sukses, `offlineStreak` di-reset ke 0, dot kembali hijau (`#10b981`, `●`), teks uptime dikembalikan ke angka durasi aktif (`formatUptimeString(uptimeSeconds)`) berwarna putih terang (`#ffffff`), dan ticker live counter 1 detik kembali berjalan sinkron dengan hardware.

## 52. Siklus Sesi Global, Timeout Inaktivitas & Force Auto-Redirect ke Login
- **1. Rasional Keamanan Sesi Industri (Zero Zombie Session)**:
  - Sesi mikrokontroler ESP32 memiliki sliding timeout 15 menit (`SESSION_TIMEOUT_MS = 900000 ms`).
  - Sebelumnya, jika tab dibiarkan terbuka, pemanggilan heartbeat `/api/v1/auth/check` secara buta terus me-refresh sesi di backend dan jika status `401 Unauthorized` terjadi, kode lama hanya mencetak `console.warn` tanpa me-redirect pengguna.
  - Pengguna berpotensi melihat data lama (*stale UI*) dan baru terlempar ke halaman login jika melakukan reload halaman (F5) atau mengklik tautan antar-halaman.
- **2. Arsitektur Otomasi Pengalihan Layar (`app.js`)**:
  - **Pelacak Inaktivitas Nyata Pengguna (*User Activity Tracking*)**:
    - Browser mendengarkan event pengguna nyata: `mousedown`, `keydown`, `touchstart`, dan `scroll`.
    - Batas inaktivitas: `IDLE_TIMEOUT_MS = 15 * 60 * 1000` (15 menit).
    - Jika pengguna diam tanpa interaksi selama 15 menit, perpanjangan sliding session dihentikan dan sistem memicu `redirectToLogin("idle_timeout")`.
  - **Force Redirect Seketika (`window.location.replace`)**:
    - Menggunakan `window.location.replace("/login.html?expired=1")` (bukan `location.href`) untuk menghapus riwayat halaman saat ini sehingga tombol *Back* pada peramban terkunci dan tidak bisa mengakses halaman terproteksi.
  - **Verifikasi Instan saat Kembali ke Tab (`visibilitychange` & `focus`)**:
    - Ketika laptop baru dibuka dari mode sleep atau pengguna kembali mengklik tab setelah lama diminimize, event `visibilitychange` dan `focus` langsung memverifikasi `/api/v1/auth/check` secara instan tanpa perlu menunggu sisa delay interval timer.
  - **Global Fetch Interceptor Guard & Diferensiasi Logout Manual**:
    - Seluruh pemanggilan `window.fetch` ke REST API gateway diproteksi interceptor terpusat. Setiap respons berstatus HTTP `401 Unauthorized` (di luar endpoint auth login/logout) seketika memicu force redirect ke login.
    - **Guard `isManualLoggingOut`**: Saat pengguna mengklik logout secara sengaja (`triggerLogout`) atau memicu reboot/reset, sistem mengaktifkan flag `window.isManualLoggingOut = true`. Seluruh request background (seperti polling 5 detik) yang terputus dengan status 401 setelah logout tidak akan memicu flag `?expired=1`, sehingga pengguna mendarat bersih di `/login.html` tanpa banner sesi kedaluwarsa.
- **3. Banner Notifikasi Ramah di Halaman Login (`login.html`)**:
  - **Komponen `.session-expired-banner`**:
    - Hanya muncul saat sesi benar-benar kedaluwarsa (inactivity timeout) via parameter `?expired=1`.
    - Tidak muncul saat pengguna logout secara sadar/manual.
    - Background: `rgba(239, 68, 68, 0.12)`, Border: `1px solid rgba(239, 68, 68, 0.35)`, Teks: `#fca5a5`, Ikon: `#ef4444`.
    - Animasi halus `fadeInSlide 0.25s ease-out` (`translateY(-4px)` ke `translateY(0)`).
    - Menjelaskan dengan transparan kepada pengguna: *"Sesi Anda telah berakhir demi keamanan. Silakan login kembali."*

## 53. Server-Side Rendering (SSR) Halaman Login, Pemisahan Endpoint Hotspot AP, & Penguncian Telemetri
- **1. Rasional Pemisahan Arsitektur (Separation of Concerns & Least Privilege)**:
  - Sebelumnya, halaman login di-stream sebagai file statis (`stream_static_file`) sehingga tag `%model%` tidak diproses oleh server, memaksa browser melakukan pemanggilan JavaScript tambahan ke `/api/v1/system/version`.
  - Selain itu, status Hotspot Mode AP darurat (`ap_active`, `ap_remain_sec`) ikut "menumpang" di endpoint version, sehingga endpoint version terpaksa dibuka publik dan berisiko membocorkan IP, MAC, MQTT Host, Uptime, dan Heap ke pengguna belum login.
- **2. Standardisasi Server-Side Rendering (SSR) untuk Login**:
  - `login_handler` beralih ke `Template::stream_template_file(req, "/storage/login.html")`.
  - Placeholder `%model%` (`SG-10`), `%ap_ssid%` (`SG-10-xxxx`), dan `%ap_banner_active%` (`1` / `0`) dirender langsung di server sebelum byte pertama dikirim ke browser.
  - Menghilangkan *layout shift* (kedipan teks judul), serta mengeliminasi pemanggilan `fetch("/api/v1/system/version")` dari JavaScript login.
- **3. Endpoint Ringan Khusus Hotspot AP (`GET /api/v1/system/ap-status`)**:
  - Endpoint publik mandiri tanpa autentikasi yang hanya mengembalikan status non-sensitif hotspot darurat:
    ```json
    {"ap_active":true,"ap_ssid":"SG-10-xxxx","ap_remain_sec":55,"reboot_required":false}
    ```
  - **Efisiensi Memori (Zero Heap Allocation)**: Diformat langsung menggunakan `snprintf` pada buffer lokal stack `128 byte` tanpa overhead `cJSON` atau `malloc`.
  - Dipakai secara terpusat oleh `checkLoginApStatus()` di `login.html` dan `initApModeWatcher()` di `app.js`.
- **4. Penguncian Penuh Data Sensitif Telemetri (`GET /api/v1/system/version`)**:
  - Endpoint `/api/v1/system/version` kini dilindungi guard autentikasi ketat:
    `if (Session::validate(req) == UserRole::NONE) return 401 Unauthorized;`
  - Akses anonim tanpa cookie sesi ditolak seketika (menghemat RAM dan CPU cycle ESP32 karena parsing cJSON dilewati).
  - Data sensitif jaringan dan broker MQTT hanya dapat dibaca oleh pengguna terotentikasi (Overview & Test Monitor).

## 54. Arsitektur Validasi Sesi Pasif vs Aktif (Inactivity Timeout 15 Menit)
- **1. Masalah Polling Otomatis Mereset Timer Sesi**:
  - Pada arsitektur sebelumnya, setiap request berautentikasi otomatis mereset waktu `last_activity_ms` ke waktu terkini (sliding expiration).
  - Karena halaman Overview melakukan polling telemetri setiap 5 detik ke `/api/v1/system/version`, timer sesi di ESP32 selalu di-reset ke 0 detik setiap 5 detik, sehingga sesi tidak pernah mencapai batas waktu 15 menit jika layar dibiarkan terbuka.
- **2. Pemisahan Validasi Pasif vs Aktif (`update_sliding`)**:
  - `Session::validate(req, out_token, max_len, bool update_sliding = true)`:
    - **Validasi Pasif (`update_sliding = false`)**: Digunakan untuk request polling otomatis background (seperti telemetri `/api/v1/system/version`, polling log `/api/v1/system/net-logs`, `/api/v1/system/drop-logs`, dan heartbeat `/api/v1/auth/check` tanpa flag aktif). Sesi tetap dicek keabsahannya, namun `last_activity_ms` TIDAK diperbarui.
    - **Validasi Aktif (`update_sliding = true`)**: Digunakan untuk aksi nyata pengguna (navigasi halaman, pengubahan pengaturan/POST konfigurasi, reboot/reset, dan heartbeat saat terdeteksi aktivitas via `/api/v1/auth/check?active=1`).
- **3. Sinkronisasi Deteksi Diam Pengguna (User Inactivity)**:
  - Frontend (`app.js`) mencatat aktivitas nyata: `mousedown`, `keydown`, `touchstart`, `scroll`, dan `mousemove` (dibatasi/throttled tiap 5 detik).
  - Jika pengguna aktif dalam 15 menit, verifikasi background mengirimkan parameter `?active=1` untuk memperbarui sliding session di ESP32.
  - Jika pengguna diam tanpa aktivitas selama >15 menit:
    - Frontend langsung mengarahkan pengguna ke `/login.html?expired=1`.
    - Di sisi backend ESP32, timer `now - last_activity_ms` juga melampaui `SESSION_TIMEOUT_MS = 900000 ms`, sehingga request polling pasif berikutnya langsung menerima respons HTTP `401 Unauthorized`.

## 55. Konsistensi Zona Waktu Booting (WIB) & Pencatatan Event Pre-Reboot
- **1. Inisialisasi Zona Waktu Dini (Zero-Boot Timezone Lock)**:
  - Variabel lingkungan `setenv("TZ", "WIB-7", 1)` dan `tzset()` kini dieksekusi di baris paling awal `app_main()` serta pada awal inisialisasi `NetLogService::init()`.
  - Mencegah *time-warp* / pergeseran zona waktu palsu (dari UTC ke WIB) pada saat gateway me-reboot.
  - Log `SYSTEM BOOT` dan log driver awal kini langsung menggunakan waktu lokal WIB (`YYYY-MM-DD HH:MM:SS`) tanpa jeda transisi UTC.
- **2. Pencatatan Event Pre-Reboot Sistem (`NetEvent::REBOOT`)**:
  - Seluruh jalur eksekusi `esp_restart()` (Jadwal Restart Harian 03:00, Watchdog Diskoneksi Jaringan, Reboot Manual Dashboard, Remote Command MQTT, dan Selesai Update OTA) kini mencatat event eksplisit `SYSTEM REBOOT` sebelum sistem padam.
  - Memastikan rekaman audit sistem memiliki pasangan kronologis yang lengkap:
    - Sebelum padam: `SYSTEM REBOOT` (Badge warning kuning/amber) dengan alasan spesifik.
    - Setelah menyala: `SYSTEM BOOT` (Badge primary biru) dengan verifikasi reset reason.
- **3. Ketahanan Terhadap Ketiadaan Internet (Offline Graceful Fallback)**:
  - Operasi POSIX `setenv/tzset` murni kalkulasi CPU register lokal tanpa request jaringan.
  - Jika internet offline total, sistem log secara mulus beralih ke format waktu relatif (`+HH:MM:SS`), dan `checkDailyRestart()` secara aman standby tanpa memicu restart di jam yang salah.

## 56. Konfigurasi Server Waktu (NTP) & Sistem Probe Validasi Real-Time
- **1. Skema Master NVS & Nilai Default Pabrik**:
  - Tabel master partisi (`nvs_config.csv`) kini menyertakan entri `ntp_server,data,string,pool.ntp.org` di bawah namespace `gateway`.
  - Inisialisasi NVS operasional (`initNvsConfiguration` di `config_store.cpp`) menyalin parameter `ntp_server` dari partisi master ke namespace operasional `cfg_gate`.
  - Fallback aman: Jika partisi master belum di-flash, sistem otomatis menetapkan default string `"pool.ntp.org"`.
  - Struktur data POD firmware (`AppConfig` di `app_config.hpp`) memuat field `char ntp_server[64];` (Zero Dynamic Allocation, hemat RAM ESP32).
  - Pemuatan (`loadConfig`) dan penyimpanan (`saveConfigSettings`) membaca serta menulis key `"ntp_server"` ke NVS.
- **2. Hot-Reload Sinkronisasi SNTP (`updateSntpServer`)**:
  - `NetworkManager::updateSntpServer(const char* server)` memperbarui server NTP primer pada slot 0 stack `esp_sntp` secara *runtime* tanpa mewajibkan gateway reboot.
  - Slot 1 (`time.nist.gov`) dan Slot 2 (`time.google.com`) tetap dipertahankan sebagai lapisan redundansi cadangan global.
- **3. Arsitektur Probe NTP Mandiri (UDP Port 123 Real-Time REST API)**:
  - Endpoint baru: `POST /api/v1/system/test-ntp` (juga mendukung `GET /api/v1/system/test-ntp?server=...`).
  - Mengirim paket SNTP standar RFC 5905 (48-byte, `LI=0, VN=4, Mode=3`) melalui socket UDP ke port 123 target.
  - Dilindungi *socket timeout* 3000 ms (`SO_RCVTIMEO`) untuk menjamin web server tidak pernah terblokir jika host tidak dapat dihubungi.
  - **Karakteristik Diagnostik**:
    - **Aman**: Hanya membaca balasan jam dari server tanpa mengubah jam sistem internal ESP32 secara prematur sebelum disimpan.
    - **Pengukuran Latensi (RTT)**: Menghitung durasi bolak-balik dalam milidetik ($$\Delta t = t_{\text{rx}} - t_{\text{tx}}$$).
    - **Parsing Waktu Nyata**: Menghitung delta epoch NTP (1900) ke Unix (1970) dan memformat jam server ke zona waktu lokal `WIB`.
- **4. Komponen Antarmuka Bento Grid & Status Feedback**:
  - **Bento Card `Server Waktu (NTP)`**: Berada di `/settings.html` dengan ikon jam amber industrial.
  - **Input Host/IP**: Mendukung nama domain (`pool.ntp.org`, `time.google.com`) maupun alamat IP lokal private (`192.168.1.1`).
  - **Tombol Aksi Terintegrasi (`.btn-input-action`)**: Tombol *"Uji Server"* dengan ikon petir di sisi kanan kolom input.
  - **Badge Status (`#ntpTestBadge`)**:
    - Status netral: `.uuid-badge.neutral` (*"Siap diuji"*).
    - Status menguji: `.uuid-badge.testing` (*"Menguji..."*) dengan animasi spinner inline.
    - Status sukses: `.uuid-badge.valid` (*"<RTT> ms"*).
    - Status gagal: `.uuid-badge.invalid` (*"Gagal"*).
  - **Kotak Umpan Balik (`.ntp-result-feedback`)**:
    - Sukses (`.ntp-result-feedback.success`): Latar hijau emerald transparan dengan IP server, RTT ms, dan jam aktual server.
    - Gagal (`.ntp-result-feedback.error`): Latar merah transparan dengan rincian kendala (DNS lookup gagal, port UDP 123 diblokir, atau timeout 3 detik).

## 57. Siklus Hidup Sesi Sliding & Pencegahan Infinite Loop / 302 Bounce
- **1. Pemisahan Heartbeat Pasif dan Perpanjangan Aktif**:
  - Heartbeat berkala (setiap 30 detik di `app.js`) hanya memanggil endpoint `/api/v1/auth/check` secara pasif (tanpa parameter `?active=1`).
  - Server ESP32 memvalidasi sesi tanpa memperbarui timer aktivitas (`update_sliding = false`), sehingga sesi pengguna yang benar-benar ditinggalkan (idle) dapat kedaluwarsa tepat pada 15 menit.
  - Sinyal perpanjangan sliding (`/api/v1/auth/check?active=1`) HANYA dikirimkan jika pengguna melakukan interaksi fisik baru (`mousedown`, `keydown`, `touchstart`, `scroll`, `mousemove`) sejak pemanggilan sebelumnya.
- **2. Pencegahan 302 Bounce Loop pada Halaman Login**:
  - Saat sesi habis (15 menit idle), JavaScript frontend membersihkan cookie `token` secara lokal dan mengarahkan browser ke `/login.html?expired=1`.
  - Di backend ESP32 (`login_handler`), deteksi query parameter `expired=1` memastikan server tidak memantulkan request kembali ke `/` (`302 Found`), melainkan langsung menghanguskan sesi lama di memori ESP32, membersihkan header Set-Cookie, dan menyajikan halaman login.
  - Di frontend `login.html`, IIFE `checkActiveSession()` mengabaikan pengecekan jika URL memiliki `expired=1` dan langsung menampilkan `#sessionExpiredBanner` berwarna amber/danger.
- **3. Standardisasi Cache-Busting Script**:
  - Seluruh halaman web gateway (`index.html`, `settings.html`, `log.html`, `test.html`, `auth_admin.html`, `auth_teknisi.html`, `firmware.html`) kini memanggil script dengan token versi dinamis `app.js?v=%firmware_version%` untuk mencegah browser menahan script lama dalam cache disk.

## 58. Konfigurasi Tingkat Pengiriman Data MQTT (QoS) & Interval Keep Alive
- **1. Pengaturan Tingkat Pengiriman Data (Quality of Service - QoS)**:
  - **Elemen Antarmuka**: Dropdown `<select id="mqtt_qos" name="mqtt_qos">` pada Bento Card 4 (*Broker Telemetri MQTT Gateway*) di `data/auth_admin.html`.
  - **Opsi Seleksi**:
    - `QoS 1 — Delivery Guaranteed (Default)`: Nilai `1`. Broker wajib mengirimkan paket konfirmasi `PUBACK` untuk setiap batch data telemetri. Sangat disarankan untuk integritas data pada jaringan kabel/LAN atau Wi-Fi berkecepatan tinggi.
    - `QoS 0 — Fire & Forget (Hemat Kuota)`: Nilai `0`. Transmisi instan tanpa menunggu konfirmasi `PUBACK`. Mengurangi overhead paket bolak-balik, menghilangkan bottleneck outbox buffer, dan menghemat kuota transmisi (sangat ideal untuk koneksi seluler berkuota / kepadatan >500 beacon/detik).
  - **Proteksi Pesan Kritis**: Mode QoS 0 hanya berlaku untuk aliran paket data beacon pada topik `s_topicData`. Pesan operasional vital gateway (*Birth Message* `status:online`, *Last Will & Testament* `status:offline`, dan *Command Subscription*) tetap terkunci pada `QoS 1` & `Retain 1` demi keandalan monitoring sistem.
- **2. Pengaturan Interval Detak Soket TCP (MQTT Keep Alive)**:
  - **Elemen Antarmuka**: Input numerik terstandardisasi dengan unit visual `.unit-box` dan label satuan `.unit-text` (*Detik*):
    `<input type="number" id="mqtt_keepalive" name="mqtt_keepalive" value="%mqtt_keepalive%" min="5" max="300" placeholder="30" required>`.
  - **Batas Nilai**: Rentang valid `5` s/d `300` detik (Default bawaan pabrik: `30` detik).
  - **Fungsi**: Mengatur frekuensi pengiriman frame `PINGREQ` ke broker saat tidak ada transmisi data agar koneksi soket TCP tidak ditutup sepihak oleh router atau operator seluler (*NAT timeout*).
- **3. Tata Letak Grid & Preservasi NVS**:
  - Disusun berdampingan dalam form grid 2-kolom (`.bento-grid` dengan elemen `.item`) tepat di bawah kredensial password broker.
  - Tersimpan secara persisten pada partisi NVS `cfg_mqtt` (kunci `qos` bertipe `u32` dan `keepalive` bertipe `u32`), terdaftar pada master provisioning `nvs_config.csv`, dan di-*backup* secara otomatis oleh logika inisialisasi pabrik `initNvsConfiguration()`.

## 59. Visualisasi QoS dan Keep Alive pada Kartu Telemetri MQTT Dashboard Overview
- **1. Struktur Grid Responsif 5 Kolom (`.sg-mqtt-data-grid`)**:
  - **Desktop / Tablet Widescreen (`min-width: 768px`)**:
    - Disusun dalam 1 baris grid 5 kolom presisi: `grid-template-columns: 1.8fr 0.75fr 1.15fr 0.85fr 0.8fr; gap: 16px;`.
    - Kolom 1: *Broker Host* (dengan tombol salin clipboard).
    - Kolom 2: *Port* (`%mqtt_broker_port%`).
    - Kolom 3: *Security (TLS)* (`%mqtt_tls_badge%`).
    - Kolom 4: *Delivery QoS* (`#mqtt-qos-val` / `%mqtt_qos_badge%`).
    - Kolom 5: *Keep Alive* (`#mqtt-keepalive-val` / `%mqtt_keepalive%s`).
  - **Mobile Viewport (`< 768px`)**:
    - Bertransformasi menjadi grid 2-kolom 3-tingkat yang simetris dan rapi:
      - Baris 1: Broker Host (membentang penuh: `grid-column: 1 / -1`).
      - Baris 2: Port (kiri) | Security TLS (kanan).
      - Baris 3: Delivery QoS (kiri) | Keep Alive (kanan).
- **2. Status Visual Badge QoS (`#mqtt-qos-val`)**:
  - **QoS 1**: `<span class="sg-subcard-status-badge active">QoS 1</span>` (Aksen hijau emerald transparan `rgba(16, 185, 129, 0.12)` dengan teks `#10b981`), menandakan jaminan pengiriman dengan `PUBACK`.
  - **QoS 0**: `<span class="sg-subcard-status-badge amber">QoS 0</span>` (Aksen amber transparan `rgba(245, 158, 11, 0.12)` dengan teks `#f59e0b`), menandakan transmisi fire-and-forget tanpa konfirmasi outbox.
- **3. Nilai Monospace Keep Alive (`#mqtt-keepalive-val`)**:
  - Ditampilkan dengan tipografi monospace industri: `font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 0.90rem; font-weight: 700; color: #ffffff;`.
  - Dilengkapi akhiran satuan detik (`30s`, `60s`).
- **4. Sinkronisasi Real-Time Dua Arah (SSR & Live Polling)**:
  - **Server-Side Render (SSR)**: Menggunakan template placeholder `%mqtt_qos_badge%` dan `%mqtt_keepalive%` di `src/web/web_template.cpp` sehingga saat HTML pertama kali dimuat oleh browser, nilai sudah langsung terisi tanpa layout shift (*zero CLS*).
  - **Live API Endpoint (`/api/v1/system/version`)**: Mengembalikan field numerik `"mqtt_qos"` dan `"mqtt_keepalive"`.
  - **Client-Side Polling Loop**: Fungsi `syncDashboardStatus()` di `index.html` menyegarkan elemen DOM `#mqtt-qos-val` dan `#mqtt-keepalive-val` secara mulus setiap 5 detik.

## 60. Halaman Dokumentasi Integrasi & Developer API (`data/docs.html`)
- **1. Otorisasi Akses Eksklusif Administrator (RBAC Guard)**:
  - Rute `/docs` dan `/docs.html` dilindungi oleh `docs_handler` di `src/web/web_routes.cpp`.
  - Pengguna tanpa sesi (`UserRole::NONE`) dialihkan seketika via `302 Found` ke `/login.html`.
  - Pengguna role Teknisi (`UserRole::TEKNISI`) dialihkan ke `/auth_teknisi.html`.
  - Hanya peran Administrator (`UserRole::ADMIN`) yang diizinkan mengakses konten dokumentasi integrasi.
- **2. Tata Letak Terstruktur 2-Kolom (Stripe/Supabase API Style)**:
  - **Kolom Kiri (~55%)**:
    - **Header Hero**: Banner judul `Integration & Developer API` dengan badge versi firmware.
    - **Ringkasan Koneksi Broker**: Tile mini parameter aktif (`%mqtt_broker_host%`, `%mqtt_broker_port%`, `%mqtt_tls_badge%`, `%mqtt_qos_badge%`).
    - **Tabel Spesifikasi Payload iBeacon**: Rincian field `mac`, `uuid`, `major`, `minor`, `rssi`, `tx_power`, `distance`, `batt_pct`, `timestamp` beserta tipe data monospace `.docs-type-badge`.
    - **Komparasi QoS**: Callout perbandingan QoS 1 (Guaranteed Delivery) vs QoS 0 (Fire & Forget).
    - **Tabel Local REST API**: Endpoint `/api/v1/system/version`, `/api/v1/system/ap-status`, `/api/v1/system/reboot` dengan method badges (`.docs-method-badge.get`, `.docs-method-badge.post`).
  - **Kolom Kanan (~45%)**:
    - **Sticky Code Terminal**: Kotak konsol kode gelap bernomor baris dengan syntax highlighting (`.ck-kw`, `.ck-fn`, `.ck-str`, `.ck-num`, `.ck-prop`, `.ck-cmt`).
    - **Language Switcher Pills**: Tab minimalis interaktif untuk 6 bahasa/platform: **`Python`**, **`Node.js`**, **`Go`**, **`Rust`**, **`Node-RED`**, dan **`cURL`**.
    - **Aksi Salin Kode (`.docs-copy-action-btn`)**: Tombol salin satu klik dengan teks berubah menjadi "Tersalin!" dan feedback toast.
- **3. Kebijakan Navigasi & Tampilan Mobile**:
  - **Sidebar Desktop**: Item navigasi `Integration Docs` dengan ikon `< / >` (`<polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/>`) ditambahkan secara konsisten pada seluruh halaman admin (`index.html`, `auth_admin.html`, `settings.html`, `firmware.html`, `log.html`, `test.html`).
  - **Top Header Mobile (`.navbar`)**: Konsisten dengan seluruh halaman administratif: Logo IOTARU di kiri, judul halaman (`Integration Docs`), dan tombol logout mobile (`#mobileLogoutBtn`) dengan ikon dan teks bahasa Indonesia (`Keluar`).
  - **Bottom Navbar Mobile (`.bottom-nav`)**: **TIDAK MENAMPILKAN** item Docs demi menjaga kebersihan, kerapian, dan ergonomi jempol pada layar smartphone berlebar 360–414px. Tetap mempertahankan 5 tombol inti standar yang simetris: `Overview`, `Config` (sheet trigger), `Test` (highlight center pill), `Firmware`, dan `Reboot` (power sheet trigger).

## 61. Kustomisasi Field Telemetri MQTT Dinamis (Dynamic Bitmask Protocol v2)
- **1. Deskripsi Komponen UI & Lokasi**:
  - Ditempatkan di dalam Bento Card 4 (*Broker Telemetri MQTT Gateway*) pada halaman `data/auth_admin.html`.
  - Menggunakan grid checkbox responsif (`.telem-checkbox-grid`) dengan tile interaktif (`.telem-chk-card`).
  - Dilengkapi indikator badge interaktif (`#telemByteBadge`) yang menghitung panjang total biner secara real-time via fungsi `recalcTelemMask()`.
- **2. Spesifikasi Token CSS Komponen**:
  - Grid Layout: `display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px;`.
  - Tile Card (`.telem-chk-card`):
    - Background normal: `#090e18`, Border: `1px solid #1a2333`, Border Radius: `9px`, Padding: `10px 14px`.
    - Hover state: Background `#0d1424`, Border `#334155`.
    - Active / Checked state: Background `rgba(14, 165, 233, 0.07)`, Border `rgba(56, 189, 248, 0.45)`.
  - Bobot Byte Badge (`.telem-chk-badge`):
    - Tipografi: `ui-monospace, monospace; font-size: 0.70rem; font-weight: 700;`.
    - Badge state checked: Background `rgba(56, 189, 248, 0.2)`, Text `#38bdf8`.
- **3. Arsitektur Bitmask Biner Protocol v2**:
  - Setiap beacon diawali 1 Byte Header Mask (`uint8_t`):
    - `0x80`: Identifier Protokol Dinamis v2 (selalu 1).
    - `0x01`: Proximity UUID (16 Byte).
    - `0x02`: Major ID (2 Byte, Big-Endian).
    - `0x04`: Minor ID (2 Byte, Big-Endian).
    - `0x08`: RSSI (1 Byte, Signed int8).
    - `0x10`: Battery % (1 Byte, Unsigned uint8).
    - `0x20`: Timestamp (4 Byte, Unsigned uint32 Big-Endian).
  - **Urutan Pengemasan Biner**: `UUID` &rarr; `Major` &rarr; `Minor` &rarr; `RSSI` &rarr; `Battery` &rarr; `Timestamp`.
  - **Universal Client Parser Compatibility**: Subscriber klien (Node.js, Python, Go, Rust) hanya menguji bitmask flag, sehingga perubahan centang di Web UI tidak pernah merusak atau membutuhkan modifikasi script klien.

## 62. Standarisasi Terminal Kode Dokumentasi Integrasi (Integration Docs Code Terminal)
- **1. Sinkronisasi Snippet dengan `test.js`**:
  - Tab `Node.js` ditetapkan sebagai default aktif (`switchSnippet('nodejs')`) pada konsol kode sticky terminal di `data/docs.html`.
  - Logika dekoder biner di dalam snippet mengadopsi secara persis arsitektur fungsi `parseBinaryPayload(buf)` dari `test.js`, meliputi penanganan dinamis Header Bitmask v2 (`0x80 | (mask & 0x3F)`) dan backward compatibility untuk Protokol Legacy v1 (`0x00` Standard 6B dan `0x01` Extended 7B).
- **2. Standarisasi Parameter Koneksi & Topik Subkripsi**:
  - **Topik Subkripsi**: Menggunakan format spesifik per gateway: `SG-10/data/<id-device>`.
  - **Kredensial Autentikasi**: Placeholder kredensial standar pengembang:
    - Username: `"MQTT_USERNAME"`
    - Password: `"MQTT_PASSWORD"`
  - Menghilangkan kredensial hardcoded pada dokumentasi publik untuk menjaga keamanan operasional.
- **3. Urutan Tab Bahasa Pengembang**:
  - `Node.js` (Aktif secara default) &rarr; `Python` &rarr; `Go` &rarr; `Rust` &rarr; `Node-RED` &rarr; `cURL`.
  - Seluruh contoh kode bahasa pemrograman pendukung diperbarui untuk membaca struktur paket biner telemetri iBeacon secara akurat.






