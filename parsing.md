# Pedoman Spesifikasi & Standar Acuan Parsing BLE Gateway (`parsing.md`)

Dokumen ini merupakan acuan resmi tunggal (*Single Source of Truth*) untuk seluruh logika ekstraksi, decoding, klasifikasi, dan penyajian data BLE (*Bluetooth Low Energy*) pada gateway ESP32 / nRF52840 dan antarmuka web dashboard (`test.js` & C firmware `main.c`).

---

## 1. Prinsip Utama Sistem (*Core Principles*)

1. **Kebijakan Nol-Mock (*Zero-Mock Policy*)**:
   - Seluruh nilai yang ditampilkan pada antarmuka pengguna (Web UI) harus bersumber langsung dari payload mentah (*raw payload*) paket BLE atau metrik gateway aktual.
   - Variabel yang tidak ada di dalam payload paket wajib menampilkan tanda `-` dengan warna redup (*muted/dim*). Dilarang keras membuat atau mengasumsikan data fiktif.

2. **Kebijakan Alamat MAC (*MAC Address Policy*)**:
   - Atribut `data.mac` (dan primary key tabel perangkat) **selalu** menggunakan alamat MAC pengirim paket BLE dari header frame radio (`mac`), **BUKAN** alamat MAC yang tersemat di dalam payload.
   - Parsing payload hanya digunakan untuk menampilkan data sensor/informasi ke dalam list visual per frame.

3. **Integritas Variabel Resmi Pabrikan**:
   - Tidak menambahkan variabel baru yang tidak tercantum dalam spesifikasi resmi pabrikan (seperti baris *Tx Power* tambahan pada frame yang hanya mendefinisikan *Ranging/Calibrated Power* seperti iBeacon dan Eddystone-URL).

4. **Ekstraksi AD Structures Dinamis**:
   - Scanning struktur *Advertising Data* (AD Type `0x09`/`0x08` untuk nama lokal, `0x0A` untuk Tx Power Level) dipindai secara dinamis dari AD Structures standar Bluetooth Core Specification.

---

## 2. Struktur Frame Biner Gateway (42-Byte Binary Frame)

Setiap paket yang diterima melalui UART/Serial Gateway dibungkus dalam frame 42-byte biner:

| Byte Offset | Nama Field | Panjang | Deskripsi |
|---|---|---|---|
| `00..01` | Preamble / Sync | 2 Byte | `0xAA 0xC3` atau `0xAA 0x48` |
| `02..07` | Sender MAC | 6 Byte | Alamat MAC pengirim BLE (Big Endian / MSB first) |
| `08` | RSSI | 1 Byte | Signed 8-bit integer (dBm, contoh: `0xC8` = -56 dBm) |
| `09` | Payload Length | 1 Byte | Panjang data BLE payload (maksimal 31 byte) |
| `10..40` | BLE ADV Payload | 31 Byte | Rangkaian TLV AD Structures (`[Length][AD_Type][AD_Data...]`) |
| `41` | Checksum / Tail | 1 Byte | Validasi frame checksum atau CRC |

---

## 3. Spesifikasi Lengkap 13 Protokol BLE

### Protokol 1: Apple iBeacon (`ibeacon`)
- **Identifikasi**: AD Type `0xFF` (Manufacturer Specific), Company ID `0x004C` (Apple Inc), Sub-type `0x02`, Sub-length `0x15` (21 byte).
- **Pola Hex**: `... FF 4C 00 02 15 [16B UUID] [2B Major] [2B Minor] [1B Calibrated RSSI@1m]`
- **Struktur Byte**:

| Offset (dari 0x4C000215) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 04..19 | UUID | 16 Byte | Hex String | Format: `8-4-4-4-12` kapital |
| Byte 20..21 | Major | 2 Byte | Big Endian UInt16 | Range: `0 .. 65535` |
| Byte 22..23 | Minor | 2 Byte | Big Endian UInt16 | Range: `0 .. 65535` |
| Byte 24 | Calibrated RSSI@1m | 1 Byte | Signed Int8 | Dipetakan ke field **Ranging** (`raw > 127 ? raw - 256 : raw`) dBm |

- **UI Schema**:
  ```javascript
  ibeacon: { label: 'iBeacon', color: '--ibc', fields: [['uuid','UUID'],['major','Major'],['minor','Minor'],['ranging','Ranging']] }
  ```
  *(Catatan: Tx Power dihapus sesuai spesifikasi Apple iBeacon & acuan `main.c`)*

---

### Protokol 2: Google Eddystone-UID (`eddy_uid`)
- **Identifikasi**: AD Type `0x16` (Service Data), Service UUID `0xFEAA`, Frame Type `0x00`.
- **Pola Hex**: `... 16 AA FE 00 [1B Ranging] [10B Namespace] [6B Instance] [2B RFU]`
- **Struktur Byte**:

| Offset (dari AAFE00) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | Calibrated RSSI@0m | 1 Byte | Signed Int8 | Dipetakan ke field **Ranging** (dBm) |
| Byte 04..13 | Namespace ID | 10 Byte | Hex String | 20 karakter hex kapital |
| Byte 14..19 | Instance ID | 6 Byte | Hex String | 12 karakter hex kapital |
| Byte 20..21 | Reserved (RFU) | 2 Byte | UInt16 | `0x0000` |

- **UI Schema**:
  ```javascript
  eddy_uid: { label: 'Eddystone-UID', color: '--eddy', fields: [['type','Type'],['namespace','Namespace'],['instance','Instance'],['ranging','Ranging']] }
  ```

---

### Protokol 3: Google Eddystone-URL (`eddy_url`)
- **Identifikasi**: AD Type `0x16` (Service Data), Service UUID `0xFEAA`, Frame Type `0x10`.
- **Pola Hex**: `... 16 AA FE 10 [1B Ranging] [1B URL Scheme] [Encoded URL Bytes...]`
- **Struktur Byte**:

| Offset (dari AAFE10) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | Calibrated RSSI@0m | 1 Byte | Signed Int8 | Dipetakan ke field **Ranging** (dBm) |
| Byte 04 | URL Scheme Prefix | 1 Byte | UInt8 | `0x00`: `http://www.`<br/>`0x01`: `https://www.`<br/>`0x02`: `http://`<br/>`0x03`: `https://` |
| Byte 05..N | Encoded URL Body | 1..17 B | ASCII / Code | Byte ASCII biasa atau kode ekspansi domain |

- **Tabel Kode Domain Eddystone-URL**:
  - `0x00`: `.com/` | `0x01`: `.org/` | `0x02`: `.edu/` | `0x03`: `.net/` | `0x04`: `.info/` | `0x05`: `.biz/` | `0x06`: `.gov/`
  - `0x07`: `.com` | `0x08`: `.org` | `0x09`: `.edu` | `0x0A`: `.net` | `0x0B`: `.info` | `0x0C`: `.biz` | `0x0D`: `.gov`
- **UI Schema**:
  ```javascript
  eddy_url: { label: 'Eddystone-URL', color: '--eddy', fields: [['type','Type'],['url','URL'],['ranging','Ranging']] }
  ```
  *(Catatan: Tx Power dihapus sesuai spesifikasi Eddystone-URL & acuan `main.c`)*

---

### Protokol 4: Google Eddystone-TLM (`eddy_tlm`)
- **Identifikasi**: AD Type `0x16`, Service UUID `0xFEAA`, Frame Type `0x20` (Unencrypted TLM).
- **Pola Hex**: `... 16 AA FE 20 [1B Version] [2B Batt mV] [2B Beacon Temp] [4B Adv PDU Count] [4B SEC Count]`
- **Struktur Byte**:

| Offset (dari AAFE20) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | TLM Version | 1 Byte | UInt8 | `0x00` |
| Byte 04..05 | Battery Voltage | 2 Byte | Big Endian UInt16 | Tegangan baterai dalam mV (contoh: `3000 mV`) |
| Byte 06..07 | Beacon Temp | 2 Byte | Signed Fixed 8.8 | Suhu dalam °C: `(int8_t)Byte06 + (Byte07 / 256.0)` |
| Byte 08..11 | Adv PDU Count | 4 Byte | Big Endian UInt32 | Total paket iklan yang telah dipancarkan |
| Byte 12..15 | Time SEC Count | 4 Byte | Big Endian UInt32 | Waktu menyala dalam unit 0.1 detik (`uptime = count * 0.1 s`) |

- **UI Schema**:
  ```javascript
  eddy_tlm: { label: 'Eddystone-TLM', color: '--eddy', fields: [['type','Type'],['battery','Battery'],['temp','Temp'],['advCount','Adv count'],['uptime','Uptime']] }
  ```

---

### Protokol 5: MOKO BXP Alarm (`moko_alarm`)
- **Acuan Resmi**: `BXP-B series.xls` (Row 3-20) & `esp32_nrf_parsing/src/main.c` (Row 650-695).
- **Identifikasi**: AD Type `0x16`, Service UUID `0xFEE0` (di payload: `E0 FE`).
- **Pola Hex**: `... 16 E0 FE [1B FrameType] [1B StatusFlag] [2B TriggerCount] [3B..6B DeviceID] [1B Firmware] [AD 0x09 Name]`
- **Struktur Byte**:

| Offset (dari E0FE) | Field | Panjang | Tipe Data | Keterangan & Nilai |
|---|---|---|---|---|
| Byte 02 | Frame Type | 1 Byte | UInt8 | `0x20`: Single Press<br/>`0x21`: Double Press<br/>`0x22`: Long Press<br/>`0x23`: Inactivity |
| Byte 03 | Status Flag | 1 Byte | UInt8 Bitfield | Bit 0: Password (0: OFF, 1: ON)<br/>Bit 1: Main Button Alarm (1: TRIGGERED)<br/>Bit 2: Sub Button Alarm (1: SUB_TRIGGERED) |
| Byte 04..05 | Trigger Count | 2 Byte | Big Endian UInt16 | Jumlah kali alarm terpicu |
| Byte 06..08 (atau ..11) | Device ID | 3..6 Byte | Hex String | Identitas unik perangkat (default: 3-6 byte hex) |
| Byte 09 (atau 12) | Firmware Type | 1 Byte | Hex UInt8 | Tipe firmware internal (format: `0x%02X`) |
| AD Type 0x09 | Complete Name | 1..N Byte | ASCII | Nama lokal perangkat (contoh: `BXP-B-D_E5`) |

- **UI Schema**:
  ```javascript
  moko_alarm: { label: 'MOKO BXP Alarm', color: '--moko', fields: [['mode','Mode'],['status','Status'],['count','Count'],['deviceId','Device ID'],['firmware','Firmware'],['password','Password'],['name','Name']] }
  ```

---

### Protokol 6: MOKO BXP Sensor Info (`moko_sensor`)
- **Acuan Resmi**: `BXP-B series.xls` (Row 21-38) & `esp32_nrf_parsing/src/main.c` (Row 715-745).
- **Identifikasi**: AD Type `0x16`, Service UUID `0xEA00` (di payload: `00 EA`), Frame Type `0x00`.
- **Pola Hex**: `... 16 00 EA 00 [1B Scale] [2B MotionThresh] [2B AccX] [2B AccY] [2B AccZ] [2B Temp] [1B Ranging] [2B Batt] [6B MAC]`
- **Struktur Byte**:

| Offset (dari 00EA) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 02 | Frame Type | 1 Byte | UInt8 | `0x00` (General device information) |
| Byte 03 | Full-scale | 1 Byte | UInt8 | `0`: ±2g, `1`: ±4g, `2`: ±8g, `3`: ±16g |
| Byte 04..05 | Motion Threshold | 2 Byte | Big Endian UInt16 | Nilai ambang gerak dalam `mg` |
| Byte 06..07 | X-axis Acceleration | 2 Byte | Signed Int16 | Nilai percepatan sumbu X langsung dalam `mg` |
| Byte 08..09 | Y-axis Acceleration | 2 Byte | Signed Int16 | Nilai percepatan sumbu Y langsung dalam `mg` |
| Byte 10..11 | Z-axis Acceleration | 2 Byte | Signed Int16 | Nilai percepatan sumbu Z langsung dalam `mg` |
| Byte 12..13 | Temperature | 2 Byte | Signed Int16 | Suhu dalam °C: `raw * 0.1` (V1.0.8+) |
| Byte 14 | Ranging Data | 1 Byte | Signed Int8 | RSSI@0m kalibrasi (dBm) |
| Byte 15..16 | Battery Voltage/Pct | 2 Byte | Big Endian UInt16 | Jika `> 100`: tegangan baterai dalam mV (misal `3276 mV`)<br/>Jika `<= 100`: persentase baterai (`100%`) |
| Byte 17..22 | Embedded MAC | 6 Byte | Mac Address | MAC tersemat perangkat |
| AD Type 0x0A | Tx Power Level | 1 Byte | Signed Int8 | Tx Power opsional dari TLV AD structure |

- **UI Schema**:
  ```javascript
  moko_sensor: { label: 'MOKO BXP Sensor Info', color: '--moko', fields: [['battery','Battery'],['temp','Temp'],['accel','Accel'],['scale','Scale'],['threshold','Threshold'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power']] }
  ```

---

### Protokol 7: MOKO Device Info (`moko_device`)
- **Acuan Resmi**: `MOKO_BeaconX_Pro_Parsing_Section.pdf` (Section 3.1) & `main.c` (Row 910-955).
- **Identifikasi**: AD Type `0x16`, Service UUID `0xFEAB` (di payload: `AB FE`), Frame Type `0x40`.
- **Pola Hex**: `... 16 AB FE 40 [1B Ranging] [1B AdvIntv] [2B Batt] [1B DevProp] [1B SwitchStat] [6B MAC] [1B FWType] [1B FWVer]`
- **Struktur Byte**:

| Offset (dari ABFE40) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | Ranging Data | 1 Byte | Signed Int8 | RSSI@0m (dBm) |
| Byte 04 | Adv Interval | 1 Byte | UInt8 | Interval iklan: `raw * 100 ms` |
| Byte 05..06 | Battery Voltage | 2 Byte | Big Endian UInt16 | Tegangan baterai (mV) |
| Byte 07 | Device Properties | 1 Byte | UInt8 Bitfield | Bit 2: Light Sensor (`(prop & 0x04) ? "Equipped" : "None"`) |
| Byte 08 | Switch Status | 1 Byte | UInt8 Bitfield | Bit 0: Connectable (`(stat & 0x01) ? "Yes" : "No"`)<br/>Bit 1: Light Status (`(stat & 0x02) ? "Detected" : "None"`) |
| Byte 09..14 | Embedded MAC | 6 Byte | MAC Address | Alamat MAC tersemat |
| Byte 15 | Firmware Type | 1 Byte | UInt8 | `1`: BXP-D, `2`: BXP-TLA, lainnya: BXP-C |
| Byte 16 | Firmware Version | 1 Byte | UInt8 Packed | Versi firmware: `v${(ver >> 4) & 0x0F}.${ver & 0x0F}` |

- **UI Schema**:
  ```javascript
  moko_device: { label: 'MOKO Device Info', color: '--moko', fields: [['battery','Battery'],['firmware','Firmware'],['lightSensor','Light sensor'],['light','Light'],['connectable','Connectable'],['interval','Interval'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power'],['name','Name']] }
  ```

---

### Protokol 8: MOKO 3-Axis Acc (`moko_acc`)
- **Acuan Resmi**: `MOKO_BeaconX_Pro_Parsing_Section.pdf` (Section 3.3 & FAQ Section 5) & `main.c` (Row 201-207, 960-990).
- **Identifikasi**: AD Type `0x16`, Service UUID `0xFEAB` (di payload: `AB FE`), Frame Type `0x60`.
- **Pola Hex**: `... 16 AB FE 60 [1B Ranging] [1B AdvIntv] [1B Rate] [1B Scale] [1B Thresh] [2B RawX] [2B RawY] [2B RawZ] [2B Batt] [6B MAC]`
- **Struktur Byte**:

| Offset (dari ABFE60) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | Ranging Data | 1 Byte | Signed Int8 | RSSI@0m (dBm) |
| Byte 04 | Adv Interval | 1 Byte | UInt8 | `raw * 100 ms` |
| Byte 05 | Sampling Rate | 1 Byte | UInt8 | `0`: 1Hz, `1`: 10Hz, `2`: 25Hz, `3`: 50Hz, `4`: 100Hz |
| Byte 06 | Scale Code | 1 Byte | UInt8 | `0`: ±2g (`s=1`), `1`: ±4g (`s=2`), `2`: ±8g (`s=4`), `3`: ±16g (`s=12`) |
| Byte 07 | Motion Threshold | 1 Byte | UInt8 | `raw * 0.1 g` |
| Byte 08..09 | Raw X Accel | 2 Byte | Big Endian UInt16 | 12-bit signed: `calcMokoAcc(raw, s)` |
| Byte 10..11 | Raw Y Accel | 2 Byte | Big Endian UInt16 | 12-bit signed: `calcMokoAcc(raw, s)` |
| Byte 12..13 | Raw Z Accel | 2 Byte | Big Endian UInt16 | 12-bit signed: `calcMokoAcc(raw, s)` |
| Byte 14..15 | Battery Voltage | 2 Byte | Big Endian UInt16 | Tegangan baterai (mV) |
| Byte 16..21 | Embedded MAC | 6 Byte | MAC Address | MAC tersemat |

- **Formula 12-Bit Accelerometer MOKO (FAQ Section 5)**:
  ```javascript
  function calcMokoAcc(raw, s) {
    if (raw < 0x8000) {
      return Math.floor(raw >> 4) * s;
    } else {
      return (Math.floor(raw >> 4) - 0x1000) * s;
    }
  }
  ```
- **UI Schema**:
  ```javascript
  moko_acc: { label: 'MOKO 3-Axis Acc', color: '--moko', fields: [['accel','Accel'],['scale','Scale'],['rate','Rate'],['threshold','Threshold'],['battery','Battery'],['interval','Interval'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power']] }
  ```

---

### Protokol 9: MOKO Temp & Humidity (`moko_th`)
- **Acuan Resmi**: `MOKO_BeaconX_Pro_Parsing_Section.pdf` (Section 3.4) & `main.c` (Row 1000-1030).
- **Identifikasi**: AD Type `0x16`, Service UUID `0xFEAB` (di payload: `AB FE`), Frame Type `0x70`.
- **Pola Hex**: `... 16 AB FE 70 [1B Ranging] [1B AdvIntv] [2B RawTemp] [2B RawHumidity] [2B Batt] [6B MAC]`
- **Struktur Byte**:

| Offset (dari ABFE70) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | Ranging Data | 1 Byte | Signed Int8 | RSSI@0m (dBm) |
| Byte 04 | Adv Interval | 1 Byte | UInt8 | `raw * 100 ms` |
| Byte 05..06 | Temperature | 2 Byte | Signed Int16 | Suhu dalam °C: `raw * 0.1` |
| Byte 07..08 | Humidity | 2 Byte | Unsigned UInt16 | Kelembaban dalam %: `raw * 0.1` |
| Byte 09..10 | Battery Voltage | 2 Byte | Big Endian UInt16 | Tegangan baterai (mV) |
| Byte 11..16 | Embedded MAC | 6 Byte | MAC Address | MAC tersemat |

- **UI Schema**:
  ```javascript
  moko_th: { label: 'MOKO Temp & Humidity', color: '--moko', fields: [['temp','Temp'],['humidity','Humidity'],['battery','Battery'],['interval','Interval'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power']] }
  ```

---

### Protokol 10: MOKO iBeacon Info (`moko_ibeacon`)
- **Acuan Resmi**: `BXP-B series.xls` (Row 60-68) & `main.c` (Row 1035-1065).
- **Identifikasi**: AD Type `0x16`, Service UUID `0xFEAB` (di payload: `AB FE`), Frame Type `0x50`.
- **Pola Hex**: `... 16 AB FE 50 [1B Ranging] [1B AdvIntv] [16B UUID] [2B Major] [2B Minor]`
- **Struktur Byte**:

| Offset (dari ABFE50) | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 03 | Calibrated RSSI@1m | 1 Byte | Signed Int8 | Ranging (dBm) |
| Byte 04 | Adv Interval | 1 Byte | UInt8 | `raw * 100 ms` |
| Byte 05..20 | iBeacon UUID | 16 Byte | Hex String | Format UUID: `8-4-4-4-12` |
| Byte 21..22 | Major | 2 Byte | Big Endian UInt16 | ID Mayor |
| Byte 23..24 | Minor | 2 Byte | Big Endian UInt16 | ID Minor |

- **UI Schema**:
  ```javascript
  moko_ibeacon: { label: 'MOKO iBeacon Info', color: '--moko', fields: [['interval','Interval'],['major','Major'],['minor','Minor'],['uuid','UUID'],['ranging','Ranging'],['txPower','Tx Power']] }
  ```

---

### Protokol 11: Minew iBeacon_ext (`minew_ext`)
- **Identifikasi**: AD Type `0x16` dengan panjang data >= 22 byte (Format B Minew Extended iBeacon).
- **Pola Hex**: `... 16 [16B UUID] [2B Major] [2B Minor] [1B Calibrated RSSI@1m] [1B Battery %]`
- **Struktur Byte**:

| Offset | Field | Panjang | Tipe Data | Keterangan & Rumus |
|---|---|---|---|---|
| Byte 00..15 | UUID | 16 Byte | Hex String | Format UUID `8-4-4-4-12` |
| Byte 16..17 | Major | 2 Byte | Big Endian UInt16 | ID Mayor |
| Byte 18..19 | Minor | 2 Byte | Big Endian UInt16 | ID Minor |
| Byte 20 | Calibrated RSSI@1m | 1 Byte | Signed Int8 | Dipetakan ke field **Ranging** (dBm) |
| Byte 21 | Battery | 1 Byte | UInt8 | Persentase sisa baterai (`%`) |

- **UI Schema**:
  ```javascript
  minew_ext: { label: 'Minew iBeacon_ext', color: '--minew', fields: [['uuid','UUID'],['major','Major'],['minor','Minor'],['battery','Battery'],['ranging','Ranging']] }
  ```

---

### Protokol 12: Minew Device Info (`minew_info`)
- **Identifikasi**: Paket Scan Response atau iklan perangkat yang memuat nama perangkat (AD Type `0x09` Complete Local Name / `0x08` Shortened Name).
- **UI Schema**:
  ```javascript
  minew_info: { label: 'Minew Device Info', color: '--minew', fields: [['name','Name']] }
  ```

---

### Protokol 13: Raw BLE Data (`raw`)
- **Identifikasi**: Fallback parser untuk setiap paket BLE yang tidak cocok dengan 12 signature protokol di atas (termasuk paket Apple Continuity / Neighbor discovery).
- **Struktur Byte**:
  - `advType`: Nilai byte 7 dari raw packet (`0x%02X`)
  - `length`: Panjang payload (Byte 9)
  - `adType`: AD type pertama yang ditemukan dalam payload
- **UI Schema**:
  ```javascript
  raw: { label: 'Raw BLE Data', color: '--raw', fields: [['advType','AdvType'],['length','Length'],['adType','AD Type']] }
  ```

---

## 4. Matriks Pemetaan UI Schema & Token Warna

| Protocol Key | Label Tampilan | CSS Color Token | Daftar Field Terdaftar |
|---|---|---|---|
| `moko_alarm` | **MOKO BXP Alarm** | `var(--moko)` (#38BDF8) | `mode`, `status`, `count`, `deviceId`, `firmware`, `password`, `name` |
| `moko_sensor` | **MOKO BXP Sensor Info** | `var(--moko)` (#38BDF8) | `battery`, `temp`, `accel`, `scale`, `threshold`, `ranging`, `mac`, `txPower` |
| `moko_device` | **MOKO Device Info** | `var(--moko)` (#38BDF8) | `battery`, `firmware`, `lightSensor`, `light`, `connectable`, `interval`, `ranging`, `mac`, `txPower`, `name` |
| `moko_acc` | **MOKO 3-Axis Acc** | `var(--moko)` (#38BDF8) | `accel`, `scale`, `rate`, `threshold`, `battery`, `interval`, `ranging`, `mac`, `txPower` |
| `moko_th` | **MOKO Temp & Humidity** | `var(--moko)` (#38BDF8) | `temp`, `humidity`, `battery`, `interval`, `ranging`, `mac`, `txPower` |
| `moko_ibeacon` | **MOKO iBeacon Info** | `var(--moko)` (#38BDF8) | `interval`, `major`, `minor`, `uuid`, `ranging`, `txPower` |
| `ibeacon` | **iBeacon** | `var(--ibc)` (#818CF8) | `uuid`, `major`, `minor`, `ranging` |
| `eddy_uid` | **Eddystone-UID** | `var(--eddy)` (#2DD4BF) | `type`, `namespace`, `instance`, `ranging` |
| `eddy_url` | **Eddystone-URL** | `var(--eddy)` (#2DD4BF) | `type`, `url`, `ranging` |
| `eddy_tlm` | **Eddystone-TLM** | `var(--eddy)` (#2DD4BF) | `type`, `battery`, `temp`, `advCount`, `uptime` |
| `minew_ext` | **Minew iBeacon_ext** | `var(--minew)` (#FB7185) | `uuid`, `major`, `minor`, `battery`, `ranging` |
| `minew_info` | **Minew Device Info** | `var(--minew)` (#FB7185) | `name` |
| `raw` | **Raw BLE Data** | `var(--raw)` (#94A3B8) | `advType`, `length`, `adType` |

---

## 5. Ringkasan Vektor Uji Regresi (*Regression Test Vectors*)

| Protokol | Contoh Cuplikan Raw Payload (Hex) | Hasil Ekstraksi Utama yang Diharapkan |
|---|---|---|
| **iBeacon** | `... 1A FF 4C 00 02 15 CB 10 02 3F A3 18 33 94 41 99 A8 73 0C 7C 1A EC 03 20 04 FD C5 ...` | UUID: `CB10023F-A318-3394-4199-A8730C7C1AEC`<br/>Major: `800`, Minor: `1277`<br/>Ranging: `-59 dBm` (Tanpa Tx Power) |
| **Eddystone-URL** | `... 11 16 AA FE 10 E8 02 6E 61 73 69 70 61 64 61 6E 67 07 ...` | Type: `URL`<br/>URL: `http://nasipadang.com`<br/>Ranging: `-24 dBm` (Tanpa Tx Power) |
| **MOKO Alarm** | `... 0C 16 E0 FE 20 02 00 0D 00 01 01 00 00 0B 09 42 58 50 2D 42 2D 44 5F 45 35 ...` | Mode: `Single Press`, Status: `TRIGGERED (ALARM!)`<br/>Count: `13`, Device ID: `000101`<br/>Firmware: `0x00`, Password: `OFF`, Name: `BXP-B-D_E5` |
| **MOKO Sensor Info** | `... 18 16 00 EA 00 00 00 10 00 28 FF 84 03 D8 00 6C C5 0C CC D3 A4 5F 30 32 1F ...` | Battery: `3276 mV (100%)`, Temp: `10.8 °C`<br/>Accel: `X:40 Y:-124 Z:984 mg`<br/>Scale: `±2g`, Threshold: `16 mg`, Ranging: `-59 dBm`<br/>MAC: Menggunakan MAC paket radio pengirim |
| **MOKO 3-Axis Acc** | `... 15 16 AB FE 60 C5 0A 01 00 0A 02 80 F8 40 3D 80 0C CC D3 A4 5F 30 32 1F ...` | Accel (12-bit): `X:40 Y:-124 Z:984 mg`<br/>Scale: `±2g`, Rate: `10Hz`, Threshold: `1.0g`<br/>Battery: `3276 mV`, Interval: `1000 ms`, Ranging: `-59 dBm` |

