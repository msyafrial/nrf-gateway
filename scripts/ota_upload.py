#!/usr/bin/env python3
"""
Script Upload Filesystem / Firmware OTA ke ESP32 Gateway via HTTP REST API
Usage: python scripts/ota_upload.py [--target filesystem|firmware] [--host http://sg10-f5145c3c.local]
"""

import sys
import os
import argparse
import urllib.request
import urllib.parse
import urllib.error
import time

def upload_ota(host, bin_path, target_type="filesystem", username="admin", password="admin", token=None):
    if not os.path.exists(bin_path):
        print(f"[-] File binary tidak ditemukan: {bin_path}")
        return False

    file_size = os.path.getsize(bin_path)
    print(f"[*] Menghubungkan ke gateway: {host}")
    print(f"[*] Target Upload: {target_type} ({file_size:,} bytes) dari {bin_path}")

    cookie_token = f"token={token}" if token else None

    # 1. Login jika belum ada token
    if not cookie_token:
        login_url = f"{host.rstrip('/')}/api/v1/auth/login"
        login_data = urllib.parse.urlencode({"username": username, "password": password}).encode("utf-8")
        login_req = urllib.request.Request(login_url, data=login_data, method="POST")
        login_req.add_header("Content-Type", "application/x-www-form-urlencoded")

        try:
            with urllib.request.urlopen(login_req, timeout=8) as resp:
                cookie_header = resp.headers.get("Set-Cookie", "")
                for part in cookie_header.split(";"):
                    part = part.strip()
                    if part.startswith("token="):
                        cookie_token = part
                        break
                print(f"[+] Login berhasil sebagai {username} (Cookie: {cookie_token[:20]}...)")
        except Exception as e:
            print(f"[-] Gagal login ke {login_url}: {e}")
            return False
    else:
        print(f"[+] Menggunakan token aktif: {cookie_token[:25]}...")

    if not cookie_token:
        print("[-] Tidak menerima token sesi dari gateway.")
        return False

    # 2. Upload file binary via OTA REST API
    upload_url = f"{host.rstrip('/')}/api/v1/ota/upload?type={target_type}&size={file_size}"
    print(f"[*] Mengirim payload biner ke: {upload_url}...")

    with open(bin_path, "rb") as f:
        bin_data = f.read()

    upload_req = urllib.request.Request(upload_url, data=bin_data, method="POST")
    upload_req.add_header("Cookie", cookie_token)
    upload_req.add_header("Content-Type", "application/octet-stream")
    upload_req.add_header("Content-Length", str(file_size))

    start_time = time.time()
    try:
        with urllib.request.urlopen(upload_req, timeout=60) as resp:
            resp_body = resp.read().decode("utf-8", errors="ignore")
            elapsed = time.time() - start_time
            print(f"\n[+] Status {resp.status}: {resp_body}")
            print(f"[+] Upload {target_type} selesai dalam {elapsed:.2f} detik (Kecepatan: {(file_size/1024)/elapsed:.1f} KB/s)!")
            print("[*] Gateway ESP32 sedang me-reboot untuk memuat filesystem baru...")
            return True
    except urllib.error.HTTPError as e:
        print(f"[-] HTTP Error {e.code}: {e.read().decode('utf-8', errors='ignore')}")
        return False
    except Exception as e:
        print(f"[-] Error saat upload OTA: {e}")
        return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="OTA Upload Gateway")
    parser.add_argument("--host", default="http://sg10-f5145c3c.local", help="URL Gateway ESP32")
    parser.add_argument("--target", default="filesystem", choices=["filesystem", "firmware"], help="Target OTA")
    parser.add_argument("--file", default=".pio/build/esp32doit-devkit-v1/littlefs.bin", help="Path file .bin")
    parser.add_argument("--user", default="admin", help="Admin username")
    parser.add_argument("--password", default="admin", help="Admin password")
    parser.add_argument("--token", default=None, help="Active session token (bypass login)")
    args = parser.parse_args()

    success = upload_ota(args.host, args.file, args.target, args.user, args.password, args.token)
    sys.exit(0 if success else 1)
