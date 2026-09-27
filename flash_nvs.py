import os
import subprocess
import glob
import sys

base = os.path.abspath(os.path.dirname(__file__))
bin_path = os.path.join(base, "nvs.bin")
partitions_path = os.path.join(base, "partitions.csv")

# 1. Cek keberadaan nvs.bin, jika belum ada otomatis panggil generate_nvs.py
if not os.path.exists(bin_path):
    print("[INFO] File nvs.bin belum ada, memicu 'python generate_nvs.py' terlebih dahulu...")
    gen_script = os.path.join(base, "generate_nvs.py")
    res_gen = subprocess.run([sys.executable, gen_script])
    if res_gen.returncode != 0 or not os.path.exists(bin_path):
        print("[ERROR] Gagal membuat nvs.bin!")
        sys.exit(1)

# 2. Cari tool esptool.py
esptool_matches = glob.glob(r"C:\Users\syafr\.platformio\packages\tool-esptoolpy\esptool.py")
esptool = esptool_matches[0] if esptool_matches else "esptool.py"

# Gunakan python milik PlatformIO virtualenv jika tersedia
penv_python = r"C:\Users\syafr\.platformio\penv\Scripts\python.exe"
python_exe = penv_python if os.path.exists(penv_python) else sys.executable

# 3. Tentukan offset partisi NVS dari partitions.csv (default: 0x9000)
nvs_offset = "0x9000"
if os.path.exists(partitions_path):
    with open(partitions_path, "r", encoding="utf-8") as f:
        for line in f:
            line_clean = line.strip()
            if line_clean.startswith("#") or not line_clean:
                continue
            parts = [p.strip() for p in line_clean.split(",")]
            if len(parts) >= 4 and parts[0] == "nvs":
                nvs_offset = parts[3]
                break

port_arg = ["-p", sys.argv[1]] if len(sys.argv) > 1 else []

if os.path.exists(esptool):
    cmd = [python_exe, esptool] + port_arg + ["write_flash", nvs_offset, bin_path]
else:
    cmd = ["esptool.py"] + port_arg + ["write_flash", nvs_offset, bin_path]

print(f">>> Flashing nvs.bin ke alamat partisi NVS ({nvs_offset})...")
print("Command:", " ".join(cmd))
res = subprocess.run(cmd)

if res.returncode == 0:
    print("\n[SUCCESS] Partisi NVS berhasil di-flash ke ESP32!")
    print(f"Kredensial dan konfigurasi telah tersimpan aman di alamat {nvs_offset}.")
else:
    print(f"\n[ERROR] Flashing gagal dengan kode {res.returncode}")
    print("Tips: Jika port COM tidak terdeteksi otomatis, tentukan port COM: python flash_nvs.py COMx")
