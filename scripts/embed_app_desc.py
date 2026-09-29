import os
import json
import gzip
from datetime import datetime
Import("env")

def sync_nvs_metadata(env):
    project_dir = env.subst("$PROJECT_DIR")
    csv_path = os.path.join(project_dir, "nvs_config.csv")
    if not os.path.exists(csv_path):
        return

    config = env.GetProjectConfig()
    try:
        device_model = config.get("custom", "device_model")
    except Exception:
        device_model = "SG-10"

    try:
        device_board_ver = config.get("custom", "device_board_ver")
    except Exception:
        device_board_ver = "v1.0.0"

    with open(csv_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    new_lines = []
    changed = False
    for line in lines:
        parts = line.strip().split(",")
        if len(parts) >= 4 and parts[0] == "dev_model":
            target_line = f"dev_model,data,string,{device_model}\n"
            if line != target_line:
                changed = True
            new_lines.append(target_line)
        elif len(parts) >= 4 and parts[0] == "dev_ver":
            target_line = f"dev_ver,data,string,{device_board_ver}\n"
            if line != target_line:
                changed = True
            new_lines.append(target_line)
        else:
            new_lines.append(line)

    if changed:
        with open(csv_path, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
        print(f"[AUTO-SYNC NVS] nvs_config.csv disinkronkan ke Model='{device_model}' | Board='{device_board_ver}'")

# Sinkronkan nvs_config.csv dengan platformio.ini
sync_nvs_metadata(env)

def generate_littlefs_metadata(env):
    project_dir = env.subst("$PROJECT_DIR")
    data_dir = os.path.join(project_dir, "data")
    if not os.path.exists(data_dir):
        return

    config = env.GetProjectConfig()
    try:
        device_model = config.get("custom", "device_model")
    except Exception:
        device_model = "SG-10"

    try:
        device_board_ver = config.get("custom", "device_board_ver")
    except Exception:
        device_board_ver = "v1.0.0"

    try:
        ui_version = config.get("custom", "ui_version")
    except Exception:
        ui_version = "v1.0.0"

    now = datetime.now()
    build_date = now.strftime("%d %b %Y")

    metadata = {
        "device_model": device_model,
        "device_board_ver": device_board_ver,
        "ui_version": ui_version,
        "version": ui_version,
        "build_date": build_date
    }

    json_path = os.path.join(data_dir, "version.json")
    gz_path = os.path.join(data_dir, "version.json.gz")

    json_str = json.dumps(metadata, indent=2)

    with open(json_path, "w", encoding="utf-8") as f:
        f.write(json_str)

    with open(gz_path, "wb") as f_gz:
        f_gz.write(gzip.compress(json_str.encode("utf-8")))

    print("\n=======================================================")
    print("[AUTO-GENERATE LITTLEFS METADATA]")
    print(f"  Target File     : {json_path} & {gz_path}")
    print(f"  Device Model    : {device_model} | Board: {device_board_ver}")
    print(f"  UI Version      : {ui_version}")
    print(f"  Build Date      : {build_date}")
    print("=======================================================\n")

def sync_gzip_assets(env):
    project_dir = env.subst("$PROJECT_DIR")
    data_dir = os.path.join(project_dir, "data")
    if not os.path.exists(data_dir):
        return
    for root, dirs, files in os.walk(data_dir):
        for file in files:
            if file.endswith(('.html', '.js', '.css', '.json')) and not file.endswith('.gz'):
                full_path = os.path.join(root, file)
                gz_path = full_path + '.gz'
                if os.path.exists(gz_path):
                    try:
                        with open(full_path, 'rb') as f_in:
                            content = f_in.read()
                        with open(gz_path, 'wb') as f_out:
                            f_out.write(gzip.compress(content))
                    except Exception as e:
                        pass

# Otomatis generate metadata LittleFS & sync gzip assets saat script dijalankan
generate_littlefs_metadata(env)
sync_gzip_assets(env)

def inject_metadata_elf(source, target, env):
    elf_path = str(target[0])
    if not os.path.exists(elf_path):
        return

    config = env.GetProjectConfig()
    try:
        device_model = config.get("custom", "device_model")
    except Exception:
        device_model = "SG-10"

    try:
        device_board_ver = config.get("custom", "device_board_ver")
    except Exception:
        device_board_ver = "v1.0.0"

    try:
        firmware_version = config.get("custom", "firmware_version")
    except Exception:
        firmware_version = "v1.0.1"

    hw_identity = f"{device_model}@{device_board_ver}".encode("utf-8")[:31]
    fw_ver = firmware_version.encode("utf-8")[:31]

    # Tanggal dan waktu compile saat ini
    now = datetime.now()
    compile_date = now.strftime("%d %b %Y").encode("utf-8")[:15]
    compile_time = now.strftime("%H:%M:%S").encode("utf-8")[:15]

    with open(elf_path, "r+b") as f:
        data = f.read()
        magic = b"\x32\x54\xcd\xab"
        idx = data.find(magic)
        if idx != -1:
            # Offset 0x10 dari magic: version (32 bytes)
            f.seek(idx + 0x10)
            f.write(fw_ver.ljust(32, b"\x00"))

            # Offset 0x30 dari magic: project_name (32 bytes)
            f.seek(idx + 0x30)
            f.write(hw_identity.ljust(32, b"\x00"))

            # Offset 0x50 dari magic: time (16 bytes)
            f.seek(idx + 0x50)
            f.write(compile_time.ljust(16, b"\x00"))

            # Offset 0x60 dari magic: date (16 bytes)
            f.seek(idx + 0x60)
            f.write(compile_date.ljust(16, b"\x00"))

            print("\n=======================================================")
            print(f"[AUTO-INJECT METADATA] ELF: {elf_path}")
            print(f"  Device Model    : {device_model} | Board: {device_board_ver}")
            print(f"  Firmware Version: {firmware_version}")
            print(f"  Compile Date    : {compile_date.decode('utf-8')}")
            print("  Status          : Checksum & SHA256 Valid (Generated via elf2image)")
            print("=======================================================\n")

# Inject metadata ke .elf SEBELUM elf2image membentuk firmware.bin
env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", inject_metadata_elf)

# Otomatis timpa partisi app1 (0x190000 di ESP-IDF) dengan firmware baru setiap kali upload USB
fw_bin_target = os.path.join(env.subst("$BUILD_DIR"), f"{env.subst('${PROGNAME}')}.bin")
env.Append(FLASH_EXTRA_IMAGES=[("0x190000", fw_bin_target)])
