#include "emergency_page.hpp"

namespace Iotaru::Web {

const char EMERGENCY_RECOVERY_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cubeacon %DEVICE_MODEL% - Emergency Recovery</title>
  <link rel="icon" type="image/png" href="data:image/png;base64,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">
  <script>try{var t=localStorage.getItem("cubeacon_theme");if(t!=="light"&&t!=="dark"){t=window.matchMedia&&window.matchMedia("(prefers-color-scheme: light)").matches?"light":"dark";}document.documentElement.dataset.theme=t;}catch(e){document.documentElement.dataset.theme=window.matchMedia&&window.matchMedia("(prefers-color-scheme: light)").matches?"light":"dark";}</script>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0b0f17;
      --card-bg: rgba(17, 24, 39, 0.85);
      --card-border: rgba(255, 255, 255, 0.08);
      --heading: #ffffff;
      --text: #f1f5f9;
      --text-muted: #94a3b8;
      --amber: #f59e0b;
      --amber-bg: rgba(245, 158, 11, 0.12);
      --amber-border: rgba(245, 158, 11, 0.35);
      --alert-text: #fef3c7;
      --emerald: #10b981;
      --emerald-bg: rgba(16, 185, 129, 0.15);
      --rose: #f87171;
      --rose-bg: rgba(239, 68, 68, 0.15);
      --blue: #38bdf8;
      --select-chevron: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%2394a3b8' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpolyline points='6 9 12 15 18 9'/%3E%3C/svg%3E");
      --info-bg: rgba(255, 255, 255, 0.03);
      --info-border: rgba(255, 255, 255, 0.05);
      --input-bg: rgba(0, 0, 0, 0.35);
      --dropzone-bg: rgba(0, 0, 0, 0.2);
      --dropzone-border: rgba(255, 255, 255, 0.15);
      --progress-bg: rgba(0, 0, 0, 0.5);
      --card-shadow: 0 8px 32px rgba(0, 0, 0, 0.35);
      --btn-sec-bg: rgba(239, 68, 68, 0.12);
      --btn-sec-border: rgba(239, 68, 68, 0.3);
      --toggle-bg: rgba(30, 41, 59, 0.72);
      --toggle-border: rgba(148, 163, 184, 0.25);
      --toggle-color: #cbd5e1;
    }
    html[data-theme="light"] {
      color-scheme: light;
      --bg: #f1f5f9;
      --card-bg: #ffffff;
      --card-border: #e2e8f0;
      --heading: #0f172a;
      --text: #0f172a;
      --text-muted: #475569;
      --amber: #b45309;
      --amber-bg: #fffbeb;
      --amber-border: #fde68a;
      --alert-text: #78350f;
      --emerald: #047857;
      --emerald-bg: #ecfdf5;
      --rose: #b91c1c;
      --rose-bg: #fef2f2;
      --blue: #0284c7;
      --select-chevron: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%23475569' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpolyline points='6 9 12 15 18 9'/%3E%3C/svg%3E");
      --info-bg: #f8fafc;
      --info-border: #e2e8f0;
      --input-bg: #ffffff;
      --dropzone-bg: #f8fafc;
      --dropzone-border: #cbd5e1;
      --progress-bg: #e2e8f0;
      --card-shadow: 0 1px 3px rgba(15, 23, 42, 0.08), 0 6px 18px rgba(15, 23, 42, 0.04);
      --btn-sec-bg: #fef2f2;
      --btn-sec-border: #fecaca;
      --toggle-bg: #ffffff;
      --toggle-border: #cbd5e1;
      --toggle-color: #334155;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    button:focus-visible,
    select:focus-visible,
    input:focus-visible {
      outline: 2px solid var(--blue);
      outline-offset: 2px;
    }
    @media (prefers-reduced-motion: reduce) {
      *, *::before, *::after {
        animation-duration: 0.01ms !important;
        transition-duration: 0.01ms !important;
      }
      ::view-transition-group(*),
      ::view-transition-old(*),
      ::view-transition-new(*) {
        animation-duration: 0.01ms !important;
      }
    }
    @keyframes recThemeFade {
      from { opacity: 0; }
      to { opacity: 1; }
    }
    @keyframes recIconSpring {
      0% { transform: rotate(-42deg) scale(0.78); opacity: 0.65; }
      100% { transform: rotate(0deg) scale(1); opacity: 1; }
    }
    ::view-transition { pointer-events: none; }
    ::view-transition-group(root) { animation-duration: 340ms; animation-timing-function: cubic-bezier(0.22, 1, 0.36, 1); }
    ::view-transition-old(root) { animation: none; opacity: 1; mix-blend-mode: normal; }
    ::view-transition-new(root) { animation: recThemeFade 340ms cubic-bezier(0.22, 1, 0.36, 1) both; mix-blend-mode: normal; }
    html.theme-animating,
    html.theme-animating *,
    html.theme-animating *::before,
    html.theme-animating *::after {
      transition-property: background-color, color, border-color, box-shadow, fill, stroke !important;
      transition-duration: 320ms !important;
      transition-timing-function: cubic-bezier(0.22, 1, 0.36, 1) !important;
      transition-delay: 0s !important;
    }
    html.theme-icon-pop .theme-toggle svg {
      animation: recIconSpring 360ms cubic-bezier(0.22, 1, 0.36, 1) both;
    }
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
    .theme-bar {
      display: flex;
      justify-content: flex-end;
      margin-bottom: 10px;
    }
    .theme-toggle {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      height: 34px;
      padding: 0 12px;
      border-radius: 8px;
      background: var(--toggle-bg);
      border: 1px solid var(--toggle-border);
      color: var(--toggle-color);
      font-size: 12px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.18s ease;
    }
    .theme-toggle:hover {
      border-color: var(--blue);
      color: var(--heading);
    }
    .theme-toggle svg {
      width: 15px;
      height: 15px;
      stroke: currentColor;
      fill: none;
      stroke-width: 2;
      stroke-linecap: round;
      stroke-linejoin: round;
    }
    .theme-toggle .theme-icon-moon,
    .theme-toggle .theme-label-dark { display: none; }
    html[data-theme="light"] .theme-toggle .theme-icon-sun,
    html[data-theme="light"] .theme-toggle .theme-label-light { display: none; }
    html[data-theme="light"] .theme-toggle .theme-icon-moon { display: block; }
    html[data-theme="light"] .theme-toggle .theme-label-dark { display: inline; }
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
      color: var(--heading);
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
      box-shadow: var(--card-shadow);
    }
    .alert-box {
      background: var(--amber-bg);
      border-left: 4px solid var(--amber);
      padding: 14px 16px;
      border-radius: 8px;
      font-size: 13px;
      line-height: 1.5;
      color: var(--alert-text);
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
      background: var(--info-bg);
      border: 1px solid var(--info-border);
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
      color: var(--heading);
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
      height: 42px;
      background-color: var(--input-bg);
      border: 1px solid var(--card-border);
      color: var(--text);
      padding: 0 14px;
      border-radius: 10px;
      font-size: 14px;
      outline: none;
      transition: border-color 0.2s;
    }
    select {
      appearance: none;
      -webkit-appearance: none;
      padding: 0 38px 0 14px;
      background-image: var(--select-chevron);
      background-repeat: no-repeat;
      background-position: right 12px center;
      background-size: 16px 16px;
      cursor: pointer;
    }
    select:focus, input:focus {
      border-color: var(--blue);
    }
    input::placeholder { color: #8c9eb5; opacity: 1; }
    html[data-theme="light"] input::placeholder { color: #64748b; opacity: 1; }
    .file-dropzone {
      position: relative;
      border: 2px dashed var(--dropzone-border);
      border-radius: 12px;
      padding: 24px 16px;
      text-align: center;
      cursor: pointer;
      background: var(--dropzone-bg);
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
      height: 42px;
      padding: 0 20px;
      font-size: 14px;
      font-weight: 600;
      border-radius: 10px;
      border: 1px solid transparent;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      transition: all 0.2s;
    }
    @media (max-width: 768px) {
      select, input[type="password"], input[type="text"], .btn {
        min-height: 44px;
      }
    }
    .btn-primary {
      background: #f59e0b;
      color: #0b0f17;
    }
    .btn-primary:hover {
      background: #d97706;
      box-shadow: 0 4px 16px rgba(245, 158, 11, 0.4);
    }
    html[data-theme="light"] .btn-primary {
      background: #b45309;
      color: #ffffff;
    }
    html[data-theme="light"] .btn-primary:hover {
      background: #b45309;
      box-shadow: 0 4px 14px rgba(180, 83, 9, 0.28);
    }
    .btn-secondary {
      background: var(--btn-sec-bg);
      border: 1px solid var(--btn-sec-border);
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
      background: var(--progress-bg);
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
    <div class="theme-bar">
      <button type="button" class="theme-toggle" id="recoveryThemeBtn" aria-label="Switch to Light Mode" title="Switch to Light Mode">
        <svg class="theme-icon-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/></svg>
        <svg class="theme-icon-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z"/></svg>
        <span class="theme-label-light">Light</span>
        <span class="theme-label-dark">Dark</span>
      </button>
    </div>
    <div class="header">
      <div class="badge-emergency">
        <span class="dot-pulse"></span>
        <span>Emergency Recovery Mode</span>
      </div>
      <h1>Gateway Filesystem Recovery</h1>
      <p class="subtitle">Running in Internal Flash Recovery Mode</p>
    </div>

    <div class="card">
      <div class="alert-box">
        <strong>System Alert:</strong> Filesystem partition not mounted or not found. Core gateway operations (BLE radio scanning & MQTT telemetry) <strong>remain operational</strong> in the background.
      </div>

      <div class="grid-info">
        <div class="info-item">
          <div class="info-label">Device Model</div>
          <div class="info-val">%DEVICE_MODEL%</div>
        </div>
        <div class="info-item">
          <div class="info-label">Device ID</div>
          <div class="info-val">%DEVICE_ID%</div>
        </div>
        <div class="info-item">
          <div class="info-label">IP & Network</div>
          <div class="info-val">%IP_ADDRESS% (%NET_MODE%)</div>
        </div>
        <div class="info-item">
          <div class="info-label">MQTT Broker Status</div>
          <div class="info-val"><span class="tag-online">%MQTT_STATUS%</span></div>
        </div>
      </div>
    </div>

    <div class="card">
      <h2 style="font-size:16px; margin-bottom:14px; color:var(--heading);">Upload Recovery Image</h2>
      <form id="recoveryForm">
        <div class="form-group">
          <label for="targetType">Recovery Target</label>
          <select id="targetType" name="targetType">
            <option value="filesystem" selected>Web Filesystem Partition (.bin)</option>
            <option value="firmware">Gateway Firmware Partition (firmware.bin)</option>
          </select>
        </div>

        <div class="form-group">
          <label>Select Binary File (.bin)</label>
          <div class="file-dropzone" id="dropzone">
            <input type="file" id="binFile" accept=".bin" required>
            <div class="dropzone-text" id="dropzoneText">Click or drag <strong>filesystem.bin</strong> here</div>
            <div class="file-selected" id="fileSelected"></div>
          </div>
        </div>

        <div class="form-group">
          <label for="adminPass">Administrator Password</label>
          <input type="password" id="adminPass" placeholder="Enter administrator password" required>
        </div>

        <button type="submit" class="btn btn-primary" id="btnUpload">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/></svg>
          <span>Start Flash Recovery</span>
        </button>

        <div class="progress-box" id="progressBox">
          <div class="progress-bar-bg">
            <div class="progress-bar-fill" id="progressBar"></div>
          </div>
          <div class="progress-status">
            <span id="progressText">Uploading...</span>
            <span id="progressPercent">0%</span>
          </div>
        </div>
      </form>
    </div>

    <div class="card" style="padding:16px 22px;">
      <button type="button" class="btn btn-secondary" onclick="rebootDevice()">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18.36 6.64a9 9 0 1 1-12.73 0"/><line x1="12" y1="2" x2="12" y2="12"/></svg>
        <span>Reboot Gateway Now</span>
      </button>
    </div>

    <div class="footer-note">
      Cubeacon %DEVICE_MODEL% Industrial Gateway &bull; Firmware %FIRMWARE_VERSION%
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
        alert('Please select a .bin file first.');
        return;
      }

      const file = fileInput.files[0];
      const targetType = document.getElementById('targetType').value;
      const pass = document.getElementById('adminPass').value;

      try {
        const sliceBuf = await file.slice(0, 36).arrayBuffer();
        const bytes = new Uint8Array(sliceBuf);
        if (bytes.length < 36) {
          alert('File size is too small to be a valid binary image.');
          return;
        }
        const b0 = bytes[0];
        const b1 = bytes[1];
        if (b0 === 0xAA && b1 === 0x50) {
          alert('Selected file is a Partition Table (partitions.bin), not Firmware or Filesystem.');
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
            alert('File is not a valid Filesystem image (valid signature not found at offset 0x08).');
            return;
          }
        } else if (targetType === 'firmware') {
          if (!isAppDesc) {
            alert('Selected file detected as Bootloader image (bootloader.bin) or not a valid Application Firmware image (Header 0xABCD5432 mismatch).');
            return;
          }
        }
      } catch (err) {
        console.warn('Failed to read file signature:', err);
      }

      btnUpload.disabled = true;
      progressBox.style.display = 'block';
      progressText.textContent = 'Authenticating administrator session...';
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
          throw new Error('Authentication Failed: ' + errMsg);
        }
      } catch (err) {
        alert(err.message);
        btnUpload.disabled = false;
        progressBox.style.display = 'none';
        return;
      }

      progressText.textContent = 'Writing to flash partition ' + targetType + '...';
      const xhr = new XMLHttpRequest();
      const uploadUrl = '/api/v1/ota/upload?type=' + encodeURIComponent(targetType) + '&size=' + file.size;
      xhr.open('POST', uploadUrl, true);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');

      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          const pct = Math.round((e.loaded / e.total) * 100);
          progressBar.style.width = pct + '%';
          progressPercent.textContent = pct + '%';
          progressText.textContent = 'Writing: ' + Math.round(e.loaded / 1024) + ' KB / ' + Math.round(e.total / 1024) + ' KB';
        }
      };

      xhr.onload = function() {
        if (xhr.status >= 200 && xhr.status < 300) {
          progressBar.style.width = '100%';
          progressPercent.textContent = '100%';
          progressText.textContent = 'Recovery Successful! Gateway rebooting in 5s...';
          let countdown = 5;
          let iv = setInterval(() => {
            countdown--;
            if (countdown > 0) {
              progressText.textContent = 'Recovery Successful! Rebooting in ' + countdown + 's...';
            } else {
              clearInterval(iv);
              window.location.href = '/';
            }
          }, 1000);
        } else {
          alert('Upload Failed: ' + xhr.responseText);
          btnUpload.disabled = false;
          progressBox.style.display = 'none';
        }
      };

      xhr.onerror = function() {
        alert('Connection lost during upload.');
        btnUpload.disabled = false;
        progressBox.style.display = 'none';
      };

      xhr.send(file);
    });

    async function rebootDevice() {
      if (!confirm('Are you sure you want to reboot the gateway?')) return;
      const pass = prompt('Enter administrator password to reboot:');
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
        if (!rLogin.ok) throw new Error('Invalid password.');
        await fetch('/api/v1/system/reboot', { method: 'POST' });
        alert('Gateway rebooting... Page will reload in 6s.');
        setTimeout(() => { window.location.href = '/'; }, 6000);
      } catch (err) {
        alert(err.message);
      }
    }
    (function() {
      const KEY = 'cubeacon_theme';
      const SUN = '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/></svg>';
      const MOON = '<svg viewBox="0 0 24 24"><path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z"/></svg>';
      let activeVt = null;
      let animTimer = null;
      let iconTimer = null;
      function curTheme() {
        return document.documentElement.dataset.theme === 'light' ? 'light' : 'dark';
      }
      function syncRecoveryTheme(t) {
        const isLight = t === 'light';
        document.documentElement.dataset.theme = t;
        const btn = document.getElementById('recoveryThemeBtn');
        if (btn) {
          btn.title = isLight ? 'Switch to Dark Mode' : 'Switch to Light Mode';
          btn.setAttribute('aria-label', btn.title);
          btn.setAttribute('aria-pressed', isLight ? 'true' : 'false');
          if (!btn.querySelector('.theme-icon-sun') || !btn.querySelector('.theme-icon-moon')) {
            btn.innerHTML = (isLight ? MOON : SUN) + '<span>' + (isLight ? 'Dark' : 'Light') + '</span>';
          }
        }
      }
      function applyRecoveryTheme(next, persist, animate) {
        const prev = curTheme();
        const commit = function() {
          if (persist) {
            try { localStorage.setItem(KEY, next); } catch (e) {}
          }
          syncRecoveryTheme(next);
        };
        const reduceMotion = !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);
        if (!animate || prev === next || document.hidden || reduceMotion) {
          if (activeVt) { try { activeVt.skipTransition(); } catch (e) {} activeVt = null; }
          document.documentElement.classList.remove('theme-animating');
          commit();
          return;
        }
        if (typeof document.startViewTransition === 'function') {
          if (activeVt) { try { activeVt.skipTransition(); } catch (e) {} activeVt = null; }
          document.documentElement.classList.remove('theme-animating');
          try {
            const vt = document.startViewTransition(commit);
            activeVt = vt;
            const clearVt = function() { if (activeVt === vt) activeVt = null; };
            if (vt.updateCallbackDone) vt.updateCallbackDone.catch(function() {});
            if (vt.ready) vt.ready.catch(function() {});
            if (vt.finished) vt.finished.catch(function() {}).finally(clearVt);
            return;
          } catch (e) {
            activeVt = null;
          }
        }
        const root = document.documentElement;
        root.classList.add('theme-animating');
        if (animTimer) clearTimeout(animTimer);
        animTimer = setTimeout(function() { root.classList.remove('theme-animating'); animTimer = null; }, 340);
        commit();
        root.classList.remove('theme-icon-pop');
        void root.offsetWidth;
        root.classList.add('theme-icon-pop');
        if (iconTimer) clearTimeout(iconTimer);
        iconTimer = setTimeout(function() { root.classList.remove('theme-icon-pop'); iconTimer = null; }, 380);
      }
      function hasSavedTheme() {
        try {
          const saved = localStorage.getItem(KEY);
          return saved === 'light' || saved === 'dark';
        } catch (e) {
          return false;
        }
      }
      const btn = document.getElementById('recoveryThemeBtn');
      if (btn) {
        btn.addEventListener('click', function() {
          const next = curTheme() === 'light' ? 'dark' : 'light';
          applyRecoveryTheme(next, true, true);
        });
      }
      if (window.matchMedia) {
        const mq = window.matchMedia('(prefers-color-scheme: light)');
        const onOsThemeChange = function(e) {
          if (!hasSavedTheme()) applyRecoveryTheme(e.matches ? 'light' : 'dark', false, !document.hidden);
        };
        if (mq.addEventListener) mq.addEventListener('change', onOsThemeChange);
        else if (mq.addListener) mq.addListener(onOsThemeChange);
      }
      syncRecoveryTheme(curTheme());
    })();
  </script>
</body>
</html>)rawliteral";

} // namespace Iotaru::Web
