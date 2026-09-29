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

