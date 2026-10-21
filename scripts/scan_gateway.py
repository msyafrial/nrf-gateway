#!/usr/bin/env python3
"""
IOTARU SG-10 Gateway Scanner (Cross-Platform)
Scans for SG-10 Gateways on direct LAN or local subnet via UDP Broadcast Port 30303.
Supports multi-homed systems (Wi-Fi + Ethernet concurrent), APIPA (169.254.x.x),
and strict Windows Firewall environments.
"""

import socket
import select
import json
import time
import sys

UDP_PORT = 30303
TIMEOUT = 2.0

def get_local_interfaces():
    """Mengumpulkan seluruh IP lokal IPv4 yang aktif pada PC."""
    ips = set(["0.0.0.0"])
    try:
        _, _, host_ips = socket.gethostbyname_ex(socket.gethostname())
        for ip in host_ips:
            if ":" not in ip and not ip.startswith("127."):
                ips.add(ip)
    except Exception:
        pass

    # Fallback probing via dummy UDP connection
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.add(s.getsockname()[0])
        s.close()
    except Exception:
        pass

    return list(ips)

def scan():
    print("=" * 65)
    print(" >>> IOTARU SG-10 GATEWAY SCANNER (Multi-NIC UDP Discovery) <<<")
    print("=" * 65)

    local_ips = get_local_interfaces()
    print(f"[*] Interface terdeteksi di PC: {', '.join(sorted(local_ips))}")

    # Susun daftar target broadcast dan direct unicast
    targets = set(["255.255.255.255", "192.168.4.1", "192.168.4.255"])
    for ip in local_ips:
        if ip != "0.0.0.0":
            parts = ip.split(".")
            if len(parts) == 4:
                targets.add(f"{parts[0]}.{parts[1]}.{parts[2]}.255")
                targets.add(f"{parts[0]}.{parts[1]}.{parts[2]}.1")

    # Buka socket untuk setiap interface agar paket broadcast keluar lewat semua NIC (Wi-Fi & LAN)
    sockets = []
    for ip in local_ips:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.bind((ip, 0))
            sock.setblocking(False)
            sockets.append(sock)
        except Exception:
            pass

    # Pastikan minimal ada 1 socket umum jika bind per-IP gagal
    if not sockets:
        fallback_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        fallback_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        fallback_sock.setblocking(False)
        sockets.append(fallback_sock)

    payload = json.dumps({"cmd": "DISCOVER"}).encode("utf-8")
    print(f"[*] Mengirim paket discovery broadcast ke port {UDP_PORT} di seluruh interface...")

    for sock in sockets:
        for target in targets:
            try:
                sock.sendto(payload, (target, UDP_PORT))
            except Exception:
                pass

    found = {}
    start_time = time.time()

    try:
        while (time.time() - start_time) < TIMEOUT:
            readable, _, _ = select.select(sockets, [], [], 0.2)
            for sock in readable:
                try:
                    data, addr = sock.recvfrom(1024)
                    try:
                        info = json.loads(data.decode("utf-8").strip())
                        device_key = info.get("device_id") or info.get("mac") or addr[0]
                        info["sender_ip"] = addr[0]
                        info["sender_port"] = addr[1]
                        found[device_key] = info
                    except Exception:
                        pass
                except Exception:
                    pass
    finally:
        for sock in sockets:
            try:
                sock.close()
            except Exception:
                pass

    devices = list(found.values())
    print(f"[+] Selesai scanning. Total gateway ditemukan: {len(devices)}\n")

    if not devices:
        print("[-] Tidak ada SG-10 Gateway yang merespons.")
        print("    -> Pastikan kabel RJ45 tercolok kencang dan LED LAN menyala.")
        print("    -> Jika baru dicolok langsung ke PC, tunggu ~12 detik hingga Direct PC aktif.")
        print("    -> Coba ping ke 192.168.4.1 untuk memastikan adapter PC sudah mendapatkan IP.")
        print("=" * 65)
        return

    for idx, dev in enumerate(devices, 1):
        print(f"--- [Device #{idx}] ---")
        print(f"  Model       : {dev.get('device', 'SG-10')}")
        print(f"  Device ID   : {dev.get('device_id', '-')}")
        print(f"  IP Address  : {dev.get('ip', '-')}")
        print(f"  MAC Address : {dev.get('mac', '-')}")
        print(f"  Link Mode   : {dev.get('mode', '-')}")
        print(f"  Firmware    : {dev.get('firmware', '-')} | UI: {dev.get('ui_version', '-')}")
        print(f"  Web Console : {dev.get('web_url', '-')}")
        print(f"  mDNS Host   : http://sg10-{dev.get('device_id', '')}.local")
        print()

    print("=" * 65)
    print("[*] Anda dapat langsung membuka Web Console di browser melalui tautan di atas.")

if __name__ == "__main__":
    scan()
