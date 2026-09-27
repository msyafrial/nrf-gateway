import os
import sys
import subprocess
import configparser

base = os.path.abspath(os.path.dirname(__file__))
ini_path = os.path.join(base, "platformio.ini")
csv_path = os.path.join(base, "nvs_config.csv")
bin_path = os.path.join(base, "nvs.bin")
gen_py = os.path.join(base, "nvs_partition_gen.py")
partitions_path = os.path.join(base, "partitions.csv")

# 1. Ambil device_model dan device_board_ver dari platformio.ini section [custom]
device_model = "SG-10"
device_board_ver = "v1.0.0"

if os.path.exists(ini_path):
    config = configparser.ConfigParser()
    try:
        config.read(ini_path, encoding="utf-8")
        if config.has_section("custom"):
            device_model = config.get("custom", "device_model", fallback=device_model)
            device_board_ver = config.get("custom", "device_board_ver", fallback=device_board_ver)
    except Exception as e:
        print(f"[WARN] Gagal membaca platformio.ini: {e}")

# 2. Sinkronkan dev_model dan dev_ver ke dalam nvs_config.csv
if os.path.exists(csv_path):
    with open(csv_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    new_lines = []
    for line in lines:
        parts = line.strip().split(",")
        if len(parts) >= 4 and parts[0] == "dev_model":
            new_lines.append(f"dev_model,data,string,{device_model}\n")
        elif len(parts) >= 4 and parts[0] == "dev_ver":
            new_lines.append(f"dev_ver,data,string,{device_board_ver}\n")
        else:
            new_lines.append(line)

    with open(csv_path, "w", encoding="utf-8") as f:
        f.writelines(new_lines)

    print("=======================================================")
    print("[SYNC NVS CONFIG] Sinkronisasi metadata dari platformio.ini:")
    print(f"  Device Model    : {device_model}")
    print(f"  Board Version   : {device_board_ver}")
    print(f"  File Target     : {csv_path}")
    print("=======================================================\n")

# 3. Tentukan ukuran partisi NVS dari partitions.csv (default: 0x6000 / 24576 bytes)
nvs_size_hex = "0x6000"
nvs_size_bytes = 24576

if os.path.exists(partitions_path):
    with open(partitions_path, "r", encoding="utf-8") as f:
        for line in f:
            line_clean = line.strip()
            if line_clean.startswith("#") or not line_clean:
                continue
            parts = [p.strip() for p in line_clean.split(",")]
            if len(parts) >= 5 and parts[0] == "nvs":
                size_str = parts[4]
                try:
                    if size_str.lower().startswith("0x"):
                        nvs_size_bytes = int(size_str, 16)
                    else:
                        nvs_size_bytes = int(size_str)
                    nvs_size_hex = hex(nvs_size_bytes)
                except Exception:
                    pass
                break

# 4. Generate file nvs.bin dari nvs_config.csv
print(f">>> Generating nvs.bin dari nvs_config.csv (Ukuran: {nvs_size_hex} / {nvs_size_bytes} bytes)...")
cmd = [sys.executable, gen_py, "generate", csv_path, bin_path, str(nvs_size_bytes)]
res = subprocess.run(cmd)

if res.returncode == 0 and os.path.exists(bin_path):
    size = os.path.getsize(bin_path)
    print(f"\n[SUCCESS] File binary 'nvs.bin' ({size} bytes) berhasil dibuat!")
    print(f"  Tersinkronisasi : Model='{device_model}' | Board='{device_board_ver}'")
    print(f"  Ukuran Partisi  : {nvs_size_hex} ({nvs_size_bytes} bytes)")
    print("  Siap di-flash langsung ke alamat NVS 0x9000 dengan: python flash_nvs.py\n")
else:
    print(f"\n[ERROR] Pembuatan nvs.bin gagal (kode: {res.returncode})!")
    sys.exit(1)
