#include "emergency_page.hpp"

namespace Iotaru::Web {

const char EMERGENCY_RECOVERY_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>IOTARU %DEVICE_MODEL% - Emergency Recovery</title>
  <style>
    :root {
      --bg: #0b0f17;
      --card-bg: rgba(17, 24, 39, 0.85);
      --card-border: rgba(255, 255, 255, 0.08);
      --text: #f1f5f9;
      --text-muted: #94a3b8;
      --amber: #f59e0b;
      --amber-bg: rgba(245, 158, 11, 0.12);
      --amber-border: rgba(245, 158, 11, 0.35);
      --emerald: #10b981;
      --emerald-bg: rgba(16, 185, 129, 0.15);
      --rose: #ef4444;
      --rose-bg: rgba(239, 68, 68, 0.15);
      --blue: #3b82f6;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 24px 16px;
    }
    .container {
      width: 100%;
      max-width: 580px;
    }
    .header {
      text-align: center;
      margin-bottom: 24px;
    }
    .badge-emergency {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      background: var(--amber-bg);
      border: 1px solid var(--amber-border);
      color: var(--amber);
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 1px;
      text-transform: uppercase;
      padding: 6px 14px;
      border-radius: 9999px;
      margin-bottom: 12px;
    }
    .dot-pulse {
      width: 8px;
      height: 8px;
      background: var(--amber);
      border-radius: 50%;
      animation: pulse 1.6s infinite;
    }
    @keyframes pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(0.85); }
    }
    h1 {
      font-size: 24px;
      font-weight: 700;
      color: #fff;
      margin-bottom: 6px;
    }
    .subtitle {
      font-size: 13px;
      color: var(--text-muted);
    }
    .card {
      background: var(--card-bg);
      backdrop-filter: blur(16px);
      border: 1px solid var(--card-border);
      border-radius: 16px;
      padding: 22px;
      margin-bottom: 18px;
      box-shadow: 0 8px 32px rgba(0, 0, 0, 0.35);
    }
    .alert-box {
      background: var(--amber-bg);
      border-left: 4px solid var(--amber);
      padding: 14px 16px;
      border-radius: 8px;
      font-size: 13px;
      line-height: 1.5;
      color: #fef3c7;
      margin-bottom: 18px;
    }
    .grid-info {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 12px;
      margin-bottom: 6px;
    }
    @media (max-width: 480px) {
      .grid-info { grid-template-columns: 1fr; }
    }
    .info-item {
      background: rgba(255, 255, 255, 0.03);
      border: 1px solid rgba(255, 255, 255, 0.05);
      border-radius: 10px;
      padding: 12px;
    }
    .info-label {
      font-size: 11px;
      text-transform: uppercase;
      letter-spacing: 0.5px;
      color: var(--text-muted);
      margin-bottom: 4px;
    }
    .info-val {
      font-size: 14px;
      font-weight: 600;
      color: #fff;
    }
    .tag-online {
      color: var(--emerald);
      display: inline-flex;
      align-items: center;
      gap: 5px;
    }
    .tag-online::before {
      content: "";
      width: 6px;
      height: 6px;
      background: var(--emerald);
      border-radius: 50%;
    }
    .form-group {
      margin-bottom: 16px;
    }
    label {
      display: block;
      font-size: 13px;
      font-weight: 500;
      color: var(--text-muted);
      margin-bottom: 6px;
    }
    select, input[type="password"], input[type="text"] {
      width: 100%;
      background: rgba(0, 0, 0, 0.35);
      border: 1px solid var(--card-border);
      color: #fff;
      padding: 11px 14px;
      border-radius: 10px;
      font-size: 14px;
      outline: none;
      transition: border-color 0.2s;
    }
    select:focus, input:focus {
      border-color: var(--blue);
    }
    .file-dropzone {
      position: relative;
      border: 2px dashed rgba(255, 255, 255, 0.15);
      border-radius: 12px;
      padding: 24px 16px;
      text-align: center;
      cursor: pointer;
      background: rgba(0, 0, 0, 0.2);
      transition: all 0.2s;
    }
    .file-dropzone:hover {
      border-color: var(--amber);
      background: rgba(245, 158, 11, 0.05);
    }
    .file-dropzone input[type="file"] {
      position: absolute;
      inset: 0;
      opacity: 0;
      cursor: pointer;
    }
    .dropzone-text {
      font-size: 13px;
      color: var(--text-muted);
    }
    .file-selected {
      font-size: 14px;
      font-weight: 600;
      color: var(--amber);
      margin-top: 6px;
    }
    .btn {
      width: 100%;
      padding: 13px 20px;
      font-size: 14px;
      font-weight: 600;
      border-radius: 10px;
      border: none;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      transition: all 0.2s;
    }
    .btn-primary {
      background: #f59e0b;
      color: #0b0f17;
    }
    .btn-primary:hover {
      background: #d97706;
      box-shadow: 0 4px 16px rgba(245, 158, 11, 0.4);
    }
    .btn-secondary {
      background: rgba(239, 68, 68, 0.12);
      border: 1px solid rgba(239, 68, 68, 0.3);
      color: var(--rose);
      margin-top: 10px;
    }
    .btn-secondary:hover {
      background: rgba(239, 68, 68, 0.25);
    }
    .progress-box {
      display: none;
      margin-top: 18px;
    }
    .progress-bar-bg {
      height: 10px;
      background: rgba(0, 0, 0, 0.5);
      border-radius: 9999px;
      overflow: hidden;
      margin-bottom: 8px;
    }
    .progress-bar-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, #f59e0b, #10b981);
      transition: width 0.15s;
    }
    .progress-status {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
      color: var(--text-muted);
    }
    .footer-note {
      text-align: center;
      font-size: 12px;
      color: var(--text-muted);
      margin-top: 18px;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="badge-emergency">
        <span class="dot-pulse"></span>
        <span>Emergency Recovery Mode</span>
      </div>
      <h1>Pemulihan Filesystem Gateway</h1>
      <p class="subtitle">Sistem berjalan via Flash C++ internal (ESP-IDF Native)</p>
    </div>

    <div class="card">
      <div class="alert-box">
        <strong>Peringatan Sistem:</strong> Partisi Filesystem (LittleFS) tidak ditemukan atau belum terpasang. Fungsi utama gateway (penyerapan BLE nRF & transmisi MQTT) <strong>tetap beroperasi normal</strong> di latar belakang.
      </div>

      <div class="grid-info">
        <div class="info-item">
          <div class="info-label">Model Perangkat</div>
          <div class="info-val">%DEVICE_MODEL%</div>
        </div>
        <div class="info-item">
          <div class="info-label">Device ID</div>
          <div class="info-val">%DEVICE_ID%</div>
        </div>
        <div class="info-item">
          <div class="info-label">Alamat IP & Jaringan</div>
          <div class="info-val">%IP_ADDRESS% (%NET_MODE%)</div>
        </div>
        <div class="info-item">
          <div class="info-label">Status MQTT Broker</div>
          <div class="info-val"><span class="tag-online">%MQTT_STATUS%</span></div>
        </div>
      </div>
    </div>

    <div class="card">
      <h2 style="font-size:16px; margin-bottom:14px; color:#fff;">Unggah File Pemulihan</h2>
      <form id="recoveryForm">
        <div class="form-group">
          <label for="targetType">Target Pemulihan</label>
          <select id="targetType" name="targetType">
            <option value="filesystem" selected>Partisi Filesystem Web (littlefs.bin)</option>
            <option value="firmware">Partisi Firmware C++ (firmware.bin)</option>
          </select>
        </div>

        <div class="form-group">
          <label>Pilih Berkas Biner (.bin)</label>
          <div class="file-dropzone" id="dropzone">
            <input type="file" id="binFile" accept=".bin" required>
            <div class="dropzone-text" id="dropzoneText">Klik atau seret file <strong>littlefs.bin</strong> ke sini</div>
            <div class="file-selected" id="fileSelected"></div>
          </div>
        </div>

        <div class="form-group">
          <label for="adminPass">Kata Sandi Administrator</label>
          <input type="password" id="adminPass" placeholder="Masukkan password administrator" required>
        </div>

        <button type="submit" class="btn btn-primary" id="btnUpload">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
          <span>Mulai Pemulihan Flash</span>
        </button>

        <div class="progress-box" id="progressBox">
          <div class="progress-bar-bg">
            <div class="progress-bar-fill" id="progressBar"></div>
          </div>
          <div class="progress-status">
            <span id="progressText">Mengunggah...</span>
            <span id="progressPercent">0%</span>
          </div>
        </div>
      </form>
    </div>

    <div class="card" style="padding:16px 22px;">
      <button type="button" class="btn btn-secondary" onclick="rebootDevice()">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/></svg>
        <span>Reboot Gateway Sekarang</span>
      </button>
    </div>

    <div class="footer-note">
      IOTARU %DEVICE_MODEL% Industrial Gateway &bull; Firmware %FIRMWARE_VERSION%
    </div>
  </div>

  <script>
    const fileInput = document.getElementById('binFile');
    const fileSelected = document.getElementById('fileSelected');
    const dropzoneText = document.getElementById('dropzoneText');
    const form = document.getElementById('recoveryForm');
    const btnUpload = document.getElementById('btnUpload');
    const progressBox = document.getElementById('progressBox');
    const progressBar = document.getElementById('progressBar');
    const progressText = document.getElementById('progressText');
    const progressPercent = document.getElementById('progressPercent');

    fileInput.addEventListener('change', function() {
      if (this.files.length > 0) {
        fileSelected.textContent = this.files[0].name + ' (' + Math.round(this.files[0].size / 1024) + ' KB)';
        dropzoneText.style.display = 'none';
      }
    });

    form.addEventListener('submit', async function(e) {
      e.preventDefault();
      if (!fileInput.files.length) {
        alert('Silakan pilih file .bin terlebih dahulu.');
        return;
      }

      const file = fileInput.files[0];
      const targetType = document.getElementById('targetType').value;
      const pass = document.getElementById('adminPass').value;

      try {
        const sliceBuf = await file.slice(0, 36).arrayBuffer();
        const bytes = new Uint8Array(sliceBuf);
        if (bytes.length < 36) {
          alert('Ukuran berkas terlalu kecil untuk dianalisis sebagai binary ESP32 valid.');
          return;
        }
        const b0 = bytes[0];
        const b1 = bytes[1];
        if (b0 === 0xAA && b1 === 0x50) {
          alert('Berkas yang dipilih adalah Tabel Partisi ESP32 (partitions.bin), bukan Firmware atau LittleFS!');
          return;
        }
        let isLfs = false;
        if (bytes.length >= 16) {
          let sig = '';
          for (let i = 8; i < 16; i++) sig += String.fromCharCode(bytes[i]);
          if (sig === 'littlefs') isLfs = true;
        }
        const isAppDesc = (bytes[32] === 0x32 && bytes[33] === 0x54 && bytes[34] === 0xCD && bytes[35] === 0xAB);

        if (targetType === 'filesystem') {
          if (!isLfs) {
            alert('Berkas bukan citra partisi LittleFS yang valid (signature "littlefs" tidak ditemukan pada offset 0x08)!');
            return;
          }
        } else if (targetType === 'firmware') {
          if (!isAppDesc) {
            alert('Berkas yang dipilih terdeteksi sebagai Bootloader ESP32 (bootloader.bin) atau bukan Firmware Aplikasi valid (Header 0xABCD5432 tidak cocok)!');
            return;
          }
        }
      } catch (err) {
        console.warn('Gagal membaca signature berkas:', err);
      }

      btnUpload.disabled = true;
      progressBox.style.display = 'block';
      progressText.textContent = 'Mengotentikasi sesi Administrator...';
      progressBar.style.width = '10%';
      progressPercent.textContent = '10%';

      try {
        const authData = new URLSearchParams();
        authData.append('username', 'admin');
        authData.append('password', pass);
        const authRes = await fetch('/api/v1/auth/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: authData
        });
        if (!authRes.ok) {
          const errMsg = await authRes.text();
          throw new Error('Autentikasi Gagal: ' + errMsg);
        }
      } catch (err) {
        alert(err.message);
        btnUpload.disabled = false;
        progressBox.style.display = 'none';
        return;
      }

      progressText.textContent = 'Menulis ke partisi flash ' + targetType + '...';
      const xhr = new XMLHttpRequest();
      const uploadUrl = '/api/v1/ota/upload?type=' + encodeURIComponent(targetType) + '&size=' + file.size;
      xhr.open('POST', uploadUrl, true);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');

      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          const pct = Math.round((e.loaded / e.total) * 100);
          progressBar.style.width = pct + '%';
          progressPercent.textContent = pct + '%';
          progressText.textContent = 'Menulis: ' + Math.round(e.loaded / 1024) + ' KB / ' + Math.round(e.total / 1024) + ' KB';
        }
      };

      xhr.onload = function() {
        if (xhr.status >= 200 && xhr.status < 300) {
          progressBar.style.width = '100%';
          progressPercent.textContent = '100%';
          progressText.textContent = 'Pemulihan Berhasil! Gateway me-reboot dalam 5 detik...';
          let countdown = 5;
          let iv = setInterval(() => {
            countdown--;
            if (countdown > 0) {
              progressText.textContent = 'Pemulihan Berhasil! Me-reboot dalam ' + countdown + ' detik...';
            } else {
              clearInterval(iv);
              window.location.href = '/';
            }
          }, 1000);
        } else {
          alert('Upload Gagal: ' + xhr.responseText);
          btnUpload.disabled = false;
          progressBox.style.display = 'none';
        }
      };

      xhr.onerror = function() {
        alert('Koneksi terputus saat proses upload.');
        btnUpload.disabled = false;
        progressBox.style.display = 'none';
      };

      xhr.send(file);
    });

    async function rebootDevice() {
      if (!confirm('Apakah Anda yakin ingin me-reboot gateway?')) return;
      const pass = prompt('Masukkan password admin untuk reboot:');
      if (!pass) return;
      try {
        const authData = new URLSearchParams();
        authData.append('username', 'admin');
        authData.append('password', pass);
        const rLogin = await fetch('/api/v1/auth/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: authData
        });
        if (!rLogin.ok) throw new Error('Password salah!');
        await fetch('/api/v1/system/reboot', { method: 'POST' });
        alert('Gateway me-reboot... Halaman akan dimuat ulang dalam 6 detik.');
        setTimeout(() => { window.location.href = '/'; }, 6000);
      } catch (err) {
        alert(err.message);
      }
    }
  </script>
</body>
</html>)rawliteral";

} // namespace Iotaru::Web
