#!/usr/bin/env python3
"""
scripts/build_web_assets.py
===========================
Enterprise Web Asset Build & Synchronization Pipeline for ESP32 Gateway (AR-27)

Architecture:
- web_src/ : Master human-readable source repository (pure source, no .gz).
- data/    : Distribution build directory for LittleFS partition.
             - Assets in assets/css/ and assets/js/ are stored PURELY as .gz (no raw .css/.js).
             - Static HTML files are synced with Unix LF line endings (\n, 0 \r).

Space Savings:
- Eliminates ~403 KB of duplicate raw CSS and JS from LittleFS.
"""

import os
import sys
import shutil
import gzip

PROJ_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_SRC = os.path.join(PROJ_ROOT, "web_src")
DATA_DIR = os.path.join(PROJ_ROOT, "data")

def normalize_lf(content_bytes: bytes) -> bytes:
    """Normalize CRLF to pure Unix LF."""
    return content_bytes.replace(b"\r\n", b"\n").replace(b"\r", b"\n")

def init_web_src_if_needed():
    """Copy uncompressed files from data to web_src if web_src does not exist."""
    if not os.path.exists(WEB_SRC):
        print(f"[INIT] Creating master source directory: {WEB_SRC}")
        os.makedirs(WEB_SRC, exist_ok=True)
        # Copy everything except .gz
        for root, dirs, files in os.walk(DATA_DIR):
            rel_dir = os.path.relpath(root, DATA_DIR)
            dest_dir = os.path.join(WEB_SRC, rel_dir) if rel_dir != "." else WEB_SRC
            os.makedirs(dest_dir, exist_ok=True)
            for f in files:
                if not f.endswith(".gz"):
                    src_file = os.path.join(root, f)
                    dest_file = os.path.join(dest_dir, f)
                    shutil.copy2(src_file, dest_file)
        print("[INIT] Copied uncompressed source files from data/ to web_src/.")
    else:
        # If web_src exists, ensure any .gz files in web_src are cleaned up (web_src is source only)
        for root, dirs, files in os.walk(WEB_SRC):
            for f in files:
                if f.endswith(".gz"):
                    os.remove(os.path.join(root, f))

def build_assets():
    init_web_src_if_needed()
    print("=" * 65)
    print("  ESP32 GATEWAY ENTERPRISE WEB ASSET BUILD PIPELINE (PHASE 1)")
    print(f"  Source : {WEB_SRC}")
    print(f"  Target : {DATA_DIR} (LittleFS Distribution)")
    print("=" * 65)

    # 1. Normalize line endings in web_src
    for root, dirs, files in os.walk(WEB_SRC):
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext in [".html", ".css", ".js", ".json", ".svg", ".md"]:
                p = os.path.join(root, f)
                with open(p, "rb") as fp:
                    raw = fp.read()
                clean = normalize_lf(raw)
                if clean != raw:
                    with open(p, "wb") as fp:
                        fp.write(clean)

    # 2. Re-create clean target directory structure in data/
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(os.path.join(DATA_DIR, "assets", "css"), exist_ok=True)
    os.makedirs(os.path.join(DATA_DIR, "assets", "js"), exist_ok=True)
    os.makedirs(os.path.join(DATA_DIR, "assets", "img"), exist_ok=True)

    # Clean old raw .css and .js from data/assets
    for f in os.listdir(os.path.join(DATA_DIR, "assets", "css")):
        if f.endswith(".css"):
            os.remove(os.path.join(DATA_DIR, "assets", "css", f))
    for f in os.listdir(os.path.join(DATA_DIR, "assets", "js")):
        if f.endswith(".js"):
            os.remove(os.path.join(DATA_DIR, "assets", "js", f))

    # 3. Process HTML files (Sync with pure Unix LF)
    # Prune obsolete HTML files in data/ that no longer exist in web_src/
    for f in os.listdir(DATA_DIR):
        if f.endswith(".html") and not os.path.exists(os.path.join(WEB_SRC, f)):
            p = os.path.join(DATA_DIR, f)
            os.remove(p)
            print(f"  [PRUNED] Removed obsolete HTML from data/: {f}")

    html_count = 0
    total_html_bytes = 0
    for f in os.listdir(WEB_SRC):
        if f.endswith(".html"):
            src_p = os.path.join(WEB_SRC, f)
            dst_p = os.path.join(DATA_DIR, f)
            with open(src_p, "rb") as fp:
                b = normalize_lf(fp.read())
            with open(dst_p, "wb") as fp:
                fp.write(b)
            html_count += 1
            total_html_bytes += len(b)

    # 4. Process Images & Metadata
    logo_src = os.path.join(WEB_SRC, "assets", "img", "logo.png")
    logo_dst = os.path.join(DATA_DIR, "assets", "img", "logo.png")
    if os.path.exists(logo_src):
        shutil.copy2(logo_src, logo_dst)

    ver_src = os.path.join(WEB_SRC, "version.json")
    if os.path.exists(ver_src):
        with open(ver_src, "rb") as fp:
            ver_b = normalize_lf(fp.read())
        ver_dst = os.path.join(DATA_DIR, "version.json")
        with open(ver_dst, "wb") as fp:
            fp.write(ver_b)
        with open(ver_dst + ".gz", "wb") as fp:
            fp.write(gzip.compress(ver_b, compresslevel=9))

    # 5. Process CSS -> Pure Gzip Only in data/
    css_src_dir = os.path.join(WEB_SRC, "assets", "css")
    css_dst_dir = os.path.join(DATA_DIR, "assets", "css")
    total_css_raw = 0
    total_css_gz = 0
    for f in os.listdir(css_src_dir):
        if f.endswith(".css"):
            src_p = os.path.join(css_src_dir, f)
            with open(src_p, "rb") as fp:
                raw_b = normalize_lf(fp.read())
            total_css_raw += len(raw_b)
            gz_b = gzip.compress(raw_b, compresslevel=9)
            total_css_gz += len(gz_b)
            gz_path = os.path.join(css_dst_dir, f + ".gz")
            with open(gz_path, "wb") as fp:
                fp.write(gz_b)
            print(f"  [CSS .gz] {f} -> {f}.gz ({len(raw_b):,} B -> {len(gz_b):,} B, saved {len(raw_b)-len(gz_b):,} B)")

    # 6. Process JS -> Pure Gzip Only in data/
    js_src_dir = os.path.join(WEB_SRC, "assets", "js")
    js_dst_dir = os.path.join(DATA_DIR, "assets", "js")
    total_js_raw = 0
    total_js_gz = 0
    for f in os.listdir(js_src_dir):
        if f.endswith(".js"):
            src_p = os.path.join(js_src_dir, f)
            with open(src_p, "rb") as fp:
                raw_b = normalize_lf(fp.read())
            total_js_raw += len(raw_b)
            gz_b = gzip.compress(raw_b, compresslevel=9)
            total_js_gz += len(gz_b)
            gz_path = os.path.join(js_dst_dir, f + ".gz")
            with open(gz_path, "wb") as fp:
                fp.write(gz_b)
            print(f"  [JS  .gz] {f} -> {f}.gz ({len(raw_b):,} B -> {len(gz_b):,} B, saved {len(raw_b)-len(gz_b):,} B)")

    # 7. Summary & Verification
    total_data_bytes = 0
    file_inventory = []
    has_cr = False
    for root, dirs, files in os.walk(DATA_DIR):
        for f in files:
            p = os.path.join(root, f)
            size = os.path.getsize(p)
            total_data_bytes += size
            rel_p = os.path.relpath(p, DATA_DIR).replace("\\", "/")
            file_inventory.append((rel_p, size))
            if not f.endswith(".png") and not f.endswith(".gz"):
                with open(p, "rb") as fp:
                    if b"\r" in fp.read():
                        has_cr = True

    total_saved = (total_css_raw - total_css_gz) + (total_js_raw - total_js_gz)
    print("=" * 65)
    print("  BUILD REPORT")
    print(f"  HTML Pages Processed : {html_count} ({total_html_bytes:,} bytes)")
    print(f"  Raw CSS Saved        : {total_css_raw:,} B -> {total_css_gz:,} B (Saved {total_css_raw-total_css_gz:,} B)")
    print(f"  Raw JS Saved         : {total_js_raw:,} B -> {total_js_gz:,} B (Saved {total_js_raw-total_js_gz:,} B)")
    print(f"  TOTAL FLASH SAVED    : {total_saved:,} bytes (~{total_saved/1024:.1f} KB)")
    print(f"  TOTAL data/ SIZE     : {total_data_bytes:,} bytes (~{total_data_bytes/1024:.1f} KB)")
    print(f"  CR Check (\r count)  : {'FAILED! CR detected' if has_cr else 'PASSED (0 CR, pure Unix LF)'}")
    print(f"  LittleFS Headroom    : {1048576 - total_data_bytes:,} bytes free (out of 1,048,576 B)")
    print("=" * 65)

if __name__ == "__main__":
    build_assets()

