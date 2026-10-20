#!/usr/bin/env python3
"""
Script Otomatis Build & Export Paket Flash Produksi ESP32
IOTARU Gateway (SG-10)

Alur Kerja:
1. Membaca firmware_version dan device_board_ver dari platformio.ini.
2. Build Firmware C++ (pio run). [FAIL-FAST: Batal jika gagal]
3. Build Filesystem Web LittleFS (pio run -t buildfs). [FAIL-FAST: Batal jika gagal]
4. Build NVS Master Binary dari nvs_config.csv. [FAIL-FAST: Batal jika gagal]
5. Buat / bersihkan folder target: factory_bins/flash_{firmware_version}/board_{device_board_ver}/
6. Salin 6 file .bin ke folder target (littlefs.bin di-rename jadi filesystem.bin).
7. Buat file PANDUAN_OFFSET.txt, flash_windows.bat, dan flash_linux.sh.
"""

import os
import sys
import shutil
import subprocess
import configparser

def log_banner(title):
    print("\n" + "=" * 65)
    print(f"  {title}")
    print("=" * 65)

def log_step(step_num, title):
    print(f"\n[LANGKAH {step_num}] {title}...")

def log_success(msg):
    print(f"  [OK] {msg}")

def log_error(msg):
    print(f"\n[ERROR] {msg}")
    print("!" * 65)
    print("  PROSES DIBATALKAN (ABORT) - Folder tujuan tidak diubah.")
    print("!" * 65 + "\n")
    sys.exit(1)

def run_command_strict(cmd, step_name, cwd=None):
    print(f"  -> Menjalankan: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    res = subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str))
    if res.returncode != 0:
        log_error(f"Tahap '{step_name}' gagal dengan exit code {res.returncode}!")
    log_success(f"Tahap '{step_name}' selesai dengan sukses.")

def read_platformio_metadata(project_dir):
    ini_path = os.path.join(project_dir, "platformio.ini")
    if not os.path.exists(ini_path):
        log_error(f"File platformio.ini tidak ditemukan di {ini_path}")

    config = configparser.ConfigParser()
    config.read(ini_path)

    try:
        custom_sec = config["custom"]
        dev_model = custom_sec.get("device_model", "SG-10").strip()
        dev_board_ver = custom_sec.get("device_board_ver", "v1.0.0").strip()
        fw_ver = custom_sec.get("firmware_version", "v1.0.0").strip()
    except Exception as e:
        log_error(f"Gagal membaca section [custom] di platformio.ini: {e}")

    return dev_model, dev_board_ver, fw_ver

def generate_nvs_binary(project_dir):
    csv_path = os.path.join(project_dir, "nvs_config.csv")
    out_bin_path = os.path.join(project_dir, "nvs.bin")

    if not os.path.exists(csv_path):
        log_error(f"File nvs_config.csv tidak ditemukan di {csv_path}")

    print("  -> Mengompilasi nvs_config.csv menjadi nvs.bin (ukuran 0x6000 / 24KB)...")
    
    # Coba via module esp_idf_nvs_partition_gen terlebih dahulu
    cmd_module = [
        sys.executable, "-m", "esp_idf_nvs_partition_gen",
        "generate", csv_path, out_bin_path, "0x6000"
    ]
    res = subprocess.run(cmd_module)
    if res.returncode == 0 and os.path.exists(out_bin_path) and os.path.getsize(out_bin_path) == 24576:
        log_success("nvs.bin berhasil dibentuk via modul esp_idf_nvs_partition_gen (24576 bytes).")
        return out_bin_path

    # Fallback ke nvs_partition_gen.py di platformio packages
    possible_paths = [
        os.path.expanduser(r"~/.platformio/packages/framework-espidf/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"),
        r"C:\Users\syafr\.platformio\packages\framework-espidf\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py"
    ]
    gen_script = None
    for p in possible_paths:
        if os.path.exists(p):
            gen_script = p
            break

    if gen_script:
        cmd_script = [sys.executable, gen_script, "generate", csv_path, out_bin_path, "0x6000"]
        res = subprocess.run(cmd_script)
        if res.returncode == 0 and os.path.exists(out_bin_path):
            log_success(f"nvs.bin berhasil dibentuk via {gen_script}.")
            return out_bin_path

    log_error("Gagal membentuk nvs.bin dari nvs_config.csv!")

def create_guide_file(target_dir, dev_model, dev_board_ver, fw_ver):
    guide_path = os.path.join(target_dir, "PANDUAN_OFFSET.txt")
    content = f"""================================================================================
PANDUAN FLASHING PRODUKSI ESP32 (ESPRESSIF FLASH DOWNLOAD TOOL)
Perangkat : {dev_model}
Board Ver : {dev_board_ver}
Firmware  : {fw_ver}
Flash Map : ESP32 4MB (32Mbit) Dual-OTA + LittleFS (Filesystem)
================================================================================

1. PENGATURAN AWAL FLASH DOWNLOAD TOOL:
   - Chip Type   : ESP32
   - WorkMode    : Factory (Disarankan untuk mass-production) atau Develop
   - SPI SPEED   : 80MHz
   - SPI MODE    : DIO
   - FLASH SIZE  : 32Mbit (atau 4MB)
   - BAUD        : 921600 (Flashing ~10 detik per unit)

2. DAFTAR FILE BINARY DAN ALAMAT OFFSET (HEX):
   Pastikan beri tanda centang [v] pada setiap baris berikut:

   [v]  bootloader.bin          ----->  0x1000
   [v]  partitions.bin          ----->  0x8000
   [v]  nvs.bin                 ----->  0x9000
   [v]  ota_data_initial.bin    ----->  0xf000
   [v]  firmware.bin            ----->  0x20000
   [v]  firmware.bin            ----->  0x190000
   [v]  filesystem.bin          ----->  0x300000

   *CATATAN PENTING:
   - File 'firmware.bin' dicentang DUA KALI (pada offset 0x20000 dan 0x190000)
     agar kedua partisi OTA (app0 & app1) terisi firmware yang sama secara redundan.
   - File 'filesystem.bin' pada 0x300000 WAJIB di-flash agar Web Dashboard berfungsi.
   - Partisi 'phy_init' dibiarkan kosong, chip ESP32 akan auto-kalibrasi mandiri.

3. PROSEDUR FLASHING:
   a. Colokkan kabel USB ESP32 ke komputer.
   b. Pilih COM port yang terdeteksi.
   c. Klik tombol [ ERASE ] untuk membersihkan flash (1-2 detik).
   d. Klik tombol [ START ].
   e. Tunggu hingga bilah progres mencapai 100% dan muncul kotak hijau [ FINISH ].
   f. Tekan tombol EN / RST pada modul ESP32 untuk me-reboot unit baru.

4. QC / PENGUJIAN CEPAT UNIT BARU:
   - LED Indikator menyala normal.
   - Cek Wi-Fi HP/Laptop: muncul hotspot '{dev_model}_XXXXXX' (Default Pass: 12345678).
   - Buka browser ke http://192.168.4.1 untuk memastikan tampilan web muncul sempurna.
================================================================================
"""
    with open(guide_path, "w", encoding="utf-8") as f:
        f.write(content)
    log_success("File 'PANDUAN_OFFSET.txt' berhasil dibuat.")

def create_windows_batch(target_dir, dev_model, fw_ver):
    bat_path = os.path.join(target_dir, "flash_windows.bat")
    content = f"""@echo off
setlocal enabledelayedexpansion
title Flashing ESP32 - {dev_model} {fw_ver}

echo ================================================================================
echo   TOOL FLASHING OTOMATIS PRODUKSI - {dev_model} ({fw_ver})
echo ================================================================================
echo.

set PORT=%1
if "%PORT%"=="" (
    set /p PORT="Masukkan Port COM ESP32 (contoh: COM3 atau COM4): "
)

if "%PORT%"=="" (
    echo [ERROR] Port COM tidak boleh kosong!
    pause
    exit /b 1
)

echo.
echo Memulai proses flashing ke %PORT% pada baudrate 921600...
echo Tekan tombol BOOT pada ESP32 jika proses koneksi menunggu (Connecting...)...
echo.

python -m esptool --chip esp32 --port %PORT% --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0x9000 nvs.bin ^
  0xf000 ota_data_initial.bin ^
  0x20000 firmware.bin ^
  0x190000 firmware.bin ^
  0x300000 filesystem.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ================================================================================
    echo   [SUKSES] FLASHING SELESAI 100%%! UNIT SIAP DIGUNAKAN.
    echo ================================================================================
) else (
    echo.
    echo ================================================================================
    echo   [GAGAL] TERJADI KESALAHAN SAAT FLASHING. CEK KABEL USB / PORT COM!
    echo ================================================================================
)

echo.
pause
"""
    with open(bat_path, "w", encoding="utf-8") as f:
        f.write(content)
    log_success("File 'flash_windows.bat' berhasil dibuat.")

def create_linux_script(target_dir, dev_model, fw_ver):
    sh_path = os.path.join(target_dir, "flash_linux.sh")
    content = f"""#!/bin/bash
set -e

PORT="${{1:-/dev/ttyUSB0}}"

echo "================================================================================"
echo "  TOOL FLASHING OTOMATIS PRODUKSI - {dev_model} ({fw_ver})"
echo "  Target Port: $PORT"
echo "================================================================================"

python3 -m esptool --chip esp32 --port "$PORT" --baud 921600 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB \\
  0x1000 bootloader.bin \\
  0x8000 partitions.bin \\
  0x9000 nvs.bin \\
  0xf000 ota_data_initial.bin \\
  0x20000 firmware.bin \\
  0x190000 firmware.bin \\
  0x300000 filesystem.bin

echo ""
echo "[SUKSES] FLASHING SELESAI 100%! UNIT SIAP DIGUNAKAN."
"""
    with open(sh_path, "w", encoding="utf-8") as f:
        f.write(content)
    log_success("File 'flash_linux.sh' berhasil dibuat.")

def main():
    project_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    os.chdir(project_dir)

    log_banner("ESP32 FACTORY BINS EXPORT PIPELINE (BUILD FIRST & FAIL-SAFE)")

    # 0. Baca konfigurasi platformio.ini
    dev_model, dev_board_ver, fw_ver = read_platformio_metadata(project_dir)
    print(f"  Target Model      : {dev_model}")
    print(f"  Firmware Version  : {fw_ver} -> Folder: flash_{fw_ver}")
    print(f"  Board Version     : {dev_board_ver} -> Subfolder: board_{dev_board_ver}")

    # 1. Build Firmware C++
    log_step(1, "Kompilasi Firmware C++ (pio run)")
    run_command_strict(["pio", "run"], "Build Firmware", cwd=project_dir)

    # 2. Build Filesystem Web (data/ -> littlefs.bin)
    log_step(2, "Kompilasi Filesystem Web (pio run -t buildfs)")
    run_command_strict(["pio", "run", "-t", "buildfs"], "Build LittleFS", cwd=project_dir)

    # 3. Build NVS Master Binary (nvs_config.csv -> nvs.bin)
    log_step(3, "Kompilasi NVS Master Binary (nvs_config.csv -> nvs.bin)")
    generate_nvs_binary(project_dir)

    # 4. Tentukan folder target dan siapkan (Safe Overwrite)
    log_step(4, "Menyiapkan Folder Target Distribusi")
    target_dir = os.path.join(project_dir, "factory_bins", f"flash_{fw_ver}", f"board_{dev_board_ver}")
    print(f"  Target Path: {target_dir}")
    os.makedirs(target_dir, exist_ok=True)

    # Hapus littlefs.bin lama jika ada di target
    old_littlefs = os.path.join(target_dir, "littlefs.bin")
    if os.path.exists(old_littlefs):
        try:
            os.remove(old_littlefs)
        except Exception:
            pass

    log_success(f"Folder target siap: {target_dir}")

    # 5. Salin 6 File Binary ke Folder Target (littlefs.bin -> filesystem.bin)
    log_step(5, "Menyalin 6 File Binary ke Folder Target (littlefs.bin -> filesystem.bin)")
    pio_build_dir = os.path.join(project_dir, ".pio", "build", "esp32doit-devkit-v1")

    files_to_copy = [
        ("bootloader.bin", os.path.join(pio_build_dir, "bootloader.bin")),
        ("partitions.bin", os.path.join(pio_build_dir, "partitions.bin")),
        ("ota_data_initial.bin", os.path.join(pio_build_dir, "ota_data_initial.bin")),
        ("firmware.bin", os.path.join(pio_build_dir, "firmware.bin")),
        ("filesystem.bin", os.path.join(pio_build_dir, "littlefs.bin")),
        ("nvs.bin", os.path.join(project_dir, "nvs.bin")),
    ]

    for dst_fname, src_path in files_to_copy:
        if not os.path.exists(src_path):
            log_error(f"File binary sumber '{os.path.basename(src_path)}' tidak ditemukan di {src_path}!")
        sz = os.path.getsize(src_path)
        if sz == 0:
            log_error(f"File binary sumber '{os.path.basename(src_path)}' kosong (0 bytes)!")

        dst_path = os.path.join(target_dir, dst_fname)
        shutil.copy2(src_path, dst_path)
        src_label = f"({os.path.basename(src_path)} -> {dst_fname})" if dst_fname != os.path.basename(src_path) else dst_fname
        print(f"  -> Disalin: {src_label.ljust(35)} ({sz:>10,} bytes) ===> {dst_path}")

    log_success("Semua 6 file binary berhasil disalin ke folder target.")

    # 6. Buat File Panduan Offset dan Script Flashing 1-Klik
    log_step(6, "Membuat File Panduan Offset dan Script Flash Otomatis")
    create_guide_file(target_dir, dev_model, dev_board_ver, fw_ver)
    create_windows_batch(target_dir, dev_model, fw_ver)
    create_linux_script(target_dir, dev_model, fw_ver)

    # 7. Selesai
    log_banner("BUILD & EXPORT PAKET FLASH PRODUKSI BERHASIL 100%!")
    print(f"Lokasi Hasil Export:\n  {target_dir}\n")
    print("Isi Folder:")
    for item in sorted(os.listdir(target_dir)):
        item_path = os.path.join(target_dir, item)
        if os.path.isfile(item_path):
            print(f"  - {item.ljust(25)} ({os.path.getsize(item_path):>10,} bytes)")
    print("\n" + "=" * 65 + "\n")

if __name__ == "__main__":
    main()
