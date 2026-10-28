# PANDUAN WORKFLOW GIT: TRIAL EXPERIMENT, ROLLBACK, & RESTORE BRANCH

Dokumen ini memuat panduan lengkap mengenai alur kerja pengembangan firmware menggunakan Git, pemisahan antara branch produksi (**`main`**) dan branch eksperimen (**`trial`**), serta langkah-langkah praktis untuk kembali/restore kode ke versi stabil atau rilis sebelumnya.

---

## DAFTAR ISI
1. [Latar Belakang: Apa yang Baru Saja Diperbaiki?](#1-latar-belakang-apa-yang-baru-saja-diperbaiki)
2. [Konsep Branch: Lokal vs GitHub Remote](#2-konsep-branch-lokal-vs-github-remote)
3. [Status Branch Saat Ini](#3-status-branch-saat-ini)
4. [Alur Kerja Selama Berada di Branch Trial](#4-alur-kerja-selama-berada-di-branch-trial)
5. [Panduan Langkah Demi Langkah: Kembali ke Branch `main`](#5-panduan-langkah-demi-langkah-kembali-ke-branch-main)
   - [Skenario A: Kembali ke `main` & Simpan Hasil Trial](#skenario-a-kembali-ke-main-dan-tetap-simpan-hasil-trial)
   - [Skenario B: Hasil Trial Sukses dan Ingin Digabung ke `main` (Merge)](#skenario-b-hasil-trial-sukses-dan-ingin-digabung-ke-main)
   - [Skenario C: Hasil Trial Gagal dan Ingin Dihapus Bersih](#skenario-c-hasil-trial-gagal-dan-ingin-dihapus-total)
   - [Skenario D: Rollback ke Versi Rilis Lama (Tag v1.2.0, v1.1.6, dll)](#skenario-d-rollback-ke-versi-rilis-lama-tag-sebelumnya)
6. [Trik Penting: Menggunakan `git stash` untuk Perubahan Sementara](#6-trik-penting-menggunakan-git-stash)
7. [Tabel Ringkasan Perintah Cepat (Cheat Sheet)](#7-tabel-ringkasan-perintah-cepat)

---

## 1. Latar Belakang: Apa yang Baru Saja Diperbaiki?

### A. Masalah Error LittleFS `0x103`
- **Penyebab:** Saat ESP32 pertama kali dinyalakan tanpa berkas `littlefs.bin`, sistem masuk ke *Recovery Mode*. Saat Anda mencoba mengunggah `littlefs.bin` via web recovery, fungsi `Ota::begin()` memaksakan eksekusi `esp_vfs_littlefs_unregister("storage")`. Karena partisi memang belum pernah ter-mount saat boot, LittleFS mengembalikan error `ESP_ERR_INVALID_STATE` (`0x103`) dan membatalkan upload (*Catch-22*).
- **Perbaikan di [`src/web/ota_service.cpp`](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/src/web/ota_service.cpp):**
  1. Menambahkan fungsi pengecekan `esp_littlefs_mounted("storage")`.
  2. Menoleransi kode `0x103` agar proses flashing storage tetap dapat berjalan saat mode darurat.
  3. Menyimpan flag `s_fs_was_mounted` agar pemulihan `remountStorage()` pada fungsi `abort()` hanya dijalankan jika LittleFS sebelumnya memang sempat terpasang.

### B. Portable Standalone Flasher (`esptool.exe`)
- Disediakan file mandiri [`tools/esptool/esptool.exe`](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/tools/esptool/esptool.exe) resmi dari Espressif (v5.6.0).
- Setiap kali menjalankan [`scripts/export_factory_bins.py`](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/scripts/export_factory_bins.py), `esptool.exe` akan otomatis disalin ke folder rilis produksi.
- Folder `factory_bins\flash_v1.2.1\board_v1.0.0\` kini **100% portable**: dapat dipindahkan ke flashdisk dan dijalankan di komputer teknisi mana pun tanpa perlu instalasi Python.
- Kecepatan baudrate diatur ke **460800** agar stabil dan kompatibel dengan chip USB serial CH340 maupun CP2102.

---

## 2. Konsep Branch: Lokal vs GitHub Remote

```
[Komputer Lokal Anda]                            [GitHub Server di Internet]
---------------------                            ---------------------------
branch: main           =======================>  branch: main (Produksi)
branch: trial-v1.2.1   (HANYA ADA DI LOKAL!)     (Tidak ada / tidak tahu)
```

- **Branch `trial-v1.2.1` hanya ada di hard drive lokal Anda.**
- Sebelum Anda menjalankan perintah eksplisit `git push origin trial-v1.2.1`, GitHub di internet **tidak akan pernah mengetahui** keberadaan branch ini.
- Hal ini menjamin kebebasan bereksperimen: Anda bisa mencoba coding apapun, compile berkali-kali, atau mengubah setting tanpa takut repositori publik/tim Anda menjadi kotor.

---

## 3. Status Branch Saat Ini

Untuk memastikan posisi branch Anda saat ini, jalankan:
```bash
git branch --show-current
```
Jika muncul `trial-v1.2.1`, berarti saat ini Anda sedang berada di ruang kerja branch trial.

Untuk melihat seluruh branch yang ada di komputer Anda:
```bash
git branch
```
*Tanda bintang (`*`) menandakan branch yang aktif saat ini.*

---

## 4. Alur Kerja Selama Berada di Branch Trial

Di dalam branch `trial-v1.2.1`, Anda dapat bekerja normal:
1. Mengubah kode C++, konfigurasi NVS ([`nvs_config.csv`](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/nvs_config.csv)), atau HTML.
2. Mengompilasi program:
   ```bash
   pio run
   ```
3. Membuat citra filesystem:
   ```bash
   pio run -t buildfs
   ```
4. Melakukan flashing ke ESP32:
   ```bash
   pio run -t upload --upload-port COM13
   ```
5. Setiap kali Anda mencapai progres yang bagus, simpan titik tersebut dengan commit di branch trial:
   ```bash
   git add .
   git commit -m "trial: uji konfigurasi wifi baru dan stabilitas ota"
   ```

---

## 5. Panduan Langkah Demi Langkah: Kembali ke Branch `main`

> [!IMPORTANT]
> Ketika Anda berpindah branch (`git checkout main`), **seluruh berkas di folder proyek Anda akan otomatis disesuaikan oleh Git** agar sama persis seperti keadaan branch tujuan.

Pilih salah satu skenario berikut sesuai kebutuhan Anda:

---

### Skenario A: Kembali ke `main` dan Tetap Simpan Hasil Trial
*Gunakan opsi ini jika Anda ingin kembali ke kode produksi yang bersih, tetapi hasil eksperimen trial tetap tersimpan aman di branch `trial-v1.2.1` agar bisa dilanjutkan nanti.*

1. Simpan semua perubahan di branch trial:
   ```bash
   git add .
   git commit -m "simpan progres trial"
   ```
2. Pindah kembali ke branch `main`:
   ```bash
   git checkout main
   ```
3. **Hasil:** Folder proyek Anda sekarang kembali persis ke versi `main`.
4. **Jika nanti ingin lanjut trial lagi:**
   ```bash
   git checkout trial-v1.2.1
   ```
   *(Semua berkas eksperimen Anda akan otomatis muncul kembali seperti saat ditinggalkan).*

---

### Skenario B: Hasil Trial Sukses dan Ingin Digabung ke `main`
*Gunakan opsi ini jika setelah dicoba di ESP32, hasil eksperimen di branch trial terbukti stabil dan ingin Anda jadikan kode standar resmi di branch `main`.*

1. Pastikan seluruh perubahan di trial sudah di-commit:
   ```bash
   git add .
   git commit -m "trial selesai: fitur dan konfigurasi terverifikasi stabil"
   ```
2. Pindah ke branch `main`:
   ```bash
   git checkout main
   ```
3. Gabungkan (*merge*) perubahan dari branch trial ke `main`:
   ```bash
   git merge trial-v1.2.1
   ```
4. Jalankan script ekspor paket pabrik (jika ingin membuat folder biner baru):
   ```bash
   python scripts/export_factory_bins.py
   ```
5. (Opsional) Hapus branch trial karena sudah menyatu dengan `main`:
   ```bash
   git branch -d trial-v1.2.1
   ```

---

### Skenario C: Hasil Trial Gagal dan Ingin Dihapus Total
*Gunakan opsi ini jika eksperimen ternyata error, macet, atau tidak ingin dipakai lagi, dan Anda ingin membuang semua percobaan tersebut tanpa sisa.*

1. Pindah kembali ke branch `main`:
   ```bash
   git checkout -f main
   ```
   *(Opsi `-f` memaksa pindah dan mengabaikan perubahan coba-coba yang belum di-commit).*
2. Hapus branch trial dari komputer lokal Anda:
   ```bash
   git branch -D trial-v1.2.1
   ```
3. **Hasil:** Branch trial terhapus bersih 100%, repositori Anda kembali murni ke `main`.

---

### Skenario D: Rollback ke Versi Rilis Lama (Tag Sebelumnya)
*Gunakan opsi ini jika Anda ingin mengembalikan proyek ke versi tag lama (misal `v1.2.0`, `v1.1.6`, atau `v1.1.2`).*

#### Opsi 1 — Buat Branch Baru dari Tag Lama (Paling Direkomendasikan & Aman):
```bash
git checkout -b branch-v1.2.0 v1.2.0
```
*Anda akan memiliki branch baru yang bermula tepat dari tag `v1.2.0` tanpa merusak branch `main`.*

#### Opsi 2 — Hanya Melihat/Kompilasi Sementara:
```bash
git checkout v1.2.0
```
*Jika sudah selesai mengecek dan ingin kembali ke `main`:*
```bash
git checkout main
```

#### Opsi 3 — Reset Permanen Branch `main` ke Tag Lama:
```bash
git checkout main
git reset --hard v1.2.0
```
> [!WARNING]
> Perintah `reset --hard` akan menghapus commit setelah tag `v1.2.0` dari branch `main`. Gunakan hanya jika Anda benar-benar yakin ingin membatalkan semua perubahan setelah rilis tersebut.

---

## 6. Trik Penting: Menggunakan `git stash`

Jika Anda sedang mengubah-ubah file di branch trial, namun tiba-tiba harus segera pindah ke `main` tanpa ingin membuat commit:

1. **Bungkus dan simpan sementara perubahan Anda:**
   ```bash
   git stash
   ```
   *Folder kerja Anda akan bersih kembali ke status commit terakhir.*

2. **Pindah ke `main`:**
   ```bash
   git checkout main
   ```

3. **Ketika kembali lagi ke `trial-v1.2.1`:**
   ```bash
   git checkout trial-v1.2.1
   ```

4. **Buka kembali bungkusan perubahan tadi:**
   ```bash
   git stash pop
   ```
   *Seluruh editan Anda yang belum selesai akan kembali persis seperti semula.*

---

## 7. Tabel Ringkasan Perintah Cepat

| Kebutuhan | Perintah |
|---|---|
| Cek branch yang sedang aktif | `git branch --show-current` |
| Lihat daftar semua branch | `git branch` |
| Pindah ke branch `main` | `git checkout main` |
| Pindah ke branch `trial-v1.2.1` | `git checkout trial-v1.2.1` |
| Simpan perubahan trial ke commit | `git add . && git commit -m "catatan"` |
| Simpan sementara tanpa commit | `git stash` |
| Ambil kembali simpanan sementara | `git stash pop` |
| Gabungkan trial ke `main` | *(di main)* `git merge trial-v1.2.1` |
| Hapus branch trial setelah selesai | `git branch -d trial-v1.2.1` |
| Hapus paksa branch trial yang gagal | `git branch -D trial-v1.2.1` |
| Cek daftar tag rilis yang ada | `git tag -l` |
| Buat branch dari tag rilis lama | `git checkout -b restore-v1.2.0 v1.2.0` |
