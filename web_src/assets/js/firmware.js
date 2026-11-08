/**
 * Firmware & Filesystem OTA Upload Handler
 * Realtime Flash Hardware Tracking via WebSocket (/ws)
 * Modal Pop-up Progress, Single-Click Lock, and Comprehensive Error Handling
 * Client-Side Unified Pre-Flight Validator:
 *   - Firmware (.bin): Byte 0 = 0xE9, Offset 0x20 = 0xABCD5432 (esp_app_desc_t magic).
 *     * Validasi Model Hardware: Offset 0x50 (project_name) di-split (MODEL@BOARD_VER)
 *     * Validasi Versi Board: Ditolak jika revisi PCB/board berbeda
 *     * Ekstraksi Tanggal Firmware: Offset 0x80 (compile date)
 *   - Filesystem (.bin): Offset 0x08..0x0F = "littlefs". Rejects firmware/bootloader (0xE9) and partitions (0x50AA).
 * Completion Lock Guard (Prevents Stale WS Downgrades)
 */
let isOtaInProgress = false;
let isOtaCompleted = false;
let hasReceivedWsProgress = false;
let currentUploadFile = null;
let ws = null;

// Cache metadata firmware yang lolos validasi pre-flight
let validFirmwareMetadata = null;

function getWsUrl() {
  if (location.protocol === "file:") return "ws://192.168.4.1/ws";
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  return `${protocol}//${location.host}/ws`;
}

function initWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    return;
  }
  try {
    ws = new WebSocket(getWsUrl());
    ws.onopen = () => {
      console.log("[WS] Connected to OTA telemetry socket.");
    };
    ws.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        if (data.type === "ota_progress") {
          updateOtaHardwareProgress(data.written, data.total, data.percent);
        }
      } catch (e) {
        // Abaikan frame WS non-JSON
      }
    };
    ws.onerror = (err) => {
      console.warn("[WS] Error pada kanal telemetry OTA:", err);
    };
    ws.onclose = () => {
      console.log("[WS] OTA telemetry socket closed.");
      if (isOtaInProgress && !isOtaCompleted) {
        setTimeout(initWebSocket, 2000);
      }
    };
  } catch (err) {
    console.error("[WS] Failed to initialize WebSocket:", err);
  }
}

function updateOtaHardwareProgress(written, total, percent) {
  if (!isOtaInProgress || isOtaCompleted) return;
  hasReceivedWsProgress = true;

  const progressFill = document.getElementById("otaProgressFill");
  const percentBadge = document.getElementById("otaPercentBadge");
  const statusDesc = document.getElementById("otaStatusDesc");
  const fileSizeEl = document.getElementById("otaFileSize");

  const safePercent = Math.min(Math.max(percent, 0), 100);
  progressFill.style.width = `${safePercent}%`;
  percentBadge.textContent = `${safePercent}%`;

  if (fileSizeEl && total > 0) {
    fileSizeEl.textContent = `${formatBytes(written)} / ${formatBytes(total)}`;
  }

  if (safePercent < 100) {
    statusDesc.innerHTML = `<span style="color:#38bdf8;font-weight:600;">Writing to Flash (${safePercent}%):</span> Do not power off the gateway...`;
  } else {
    // 100% from Gateway Hardware Telemetry
    isOtaCompleted = true;
    const spinner = document.getElementById("otaSpinner");
    const resultIcon = document.getElementById("otaResultIcon");
    const closeBtn = document.getElementById("btnCloseOtaModal");
    const actionsBox = document.getElementById("otaModalActions");
    const actionBtn = document.getElementById("btnOtaAction");

    progressFill.style.background = "#22c55e";
    percentBadge.style.color = "#4ade80";
    percentBadge.style.borderColor = "rgba(34, 197, 94, 0.4)";
    percentBadge.style.background = "rgba(34, 197, 94, 0.15)";
    spinner.style.display = "none";
    resultIcon.style.display = "block";
    resultIcon.innerHTML = `
      <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#22c55e" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
        <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/>
        <polyline points="22 4 12 14.01 9 11.01"/>
      </svg>
    `;

    statusDesc.innerHTML = `<strong style="color:#4ade80;">Update Successful!</strong> Flash verified 100%. Gateway rebooting in <span id="otaCountdown">10</span>s...`;

    closeBtn.style.display = "none";
    actionsBox.style.display = "flex";
    actionBtn.textContent = "Rebooting...";
    actionBtn.disabled = true;

    startCountdownAndRedirect(10);
  }
}

/**
 * VALIDATOR TERPADU BINARY PRE-FLIGHT
 * Membaca 256 byte pertama berkas binary untuk memverifikasi struktur header binary secara instan:
 * 1. partitions.bin (Magic 0x50AA di byte 0-1) -> Tolak di Firmware & Filesystem
 * 2. bootloader.bin (Byte 0 = 0xE9, tapi offset 0x20 != 0xABCD5432) -> Tolak di Firmware & Filesystem
 * 3. firmware.bin (Byte 0 = 0xE9, offset 0x20 = 0xABCD5432 esp_app_desc_t):
 *    - Validasi Model Perangkat (offset 0x50) terhadap model aktif
 *    - Validasi Versi Board Hardware (offset 0x50) terhadap versi board aktif
 *    - Ekstraksi Tanggal Kompilasi (offset 0x80)
 * 4. littlefs.bin (Offset 0x08..0x0F = "littlefs") -> Valid untuk Filesystem, Tolak di Firmware
 */
function validateBinaryFile(file, type, onSuccess, onError) {
  if (!file) return;

  if (!file.name.toLowerCase().endsWith(".bin")) {
    Swal.fire({
      icon: "warning",
      title: "Invalid File Format",
      text: "Selected file must have a .bin extension!"
    });
    if (onError) onError();
    return;
  }

  const reader = new FileReader();
  reader.onload = function(e) {
    const bytes = new Uint8Array(e.target.result);
    if (bytes.length < 36) {
      Swal.fire({
        icon: "error",
        title: "Invalid File Size",
        text: "File size is too small to be a valid binary image."
      });
      if (onError) onError();
      return;
    }

    const b0 = bytes[0];
    const b1 = bytes[1];
    const isPartitionTable = (b0 === 0xAA && b1 === 0x50);

    const activeModel = (typeof CURRENT_DEVICE_MODEL !== "undefined" && CURRENT_DEVICE_MODEL && CURRENT_DEVICE_MODEL !== "%device_model%") ? CURRENT_DEVICE_MODEL : "SG-10";
    const activeBoardVer = (typeof CURRENT_DEVICE_VERSION !== "undefined" && CURRENT_DEVICE_VERSION && CURRENT_DEVICE_VERSION !== "%device_version%") ? CURRENT_DEVICE_VERSION : "v1.0.0";

    // Cek Filesystem Magic String di offset 8..15 ("littlefs")
    let lfsSig = "";
    for (let i = 8; i < 16; i++) {
      lfsSig += String.fromCharCode(bytes[i]);
    }
    const isLittleFS = (lfsSig === "littlefs");

    // Cek esp_app_desc_t Magic Word (0xABCD5432 / [0x32, 0x54, 0xCD, 0xAB]) di offset 0x20..0x23 (bytes 32..35)
    const isAppDescMagic = (
      bytes[32] === 0x32 &&
      bytes[33] === 0x54 &&
      bytes[34] === 0xCD &&
      bytes[35] === 0xAB
    );

    if (type === "firmware") {
      if (isPartitionTable) {
        Swal.fire({
          icon: "error",
          title: "Not a Firmware Binary!",
          html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
            File <strong>${escapeHtml(file.name)}</strong> detected as an <strong>Partition Table (partitions.bin)</strong> (Magic <code>0x50AA</code>).<br><br>
            <span style="color:#ef4444;font-weight:600;">Partition tables cannot be flashed through this OTA menu!</span><br>
            Please select the correct application <strong>firmware.bin</strong> file.
          </div>`
        });
        if (onError) onError();
        return;
      }

      if (isLittleFS) {
        Swal.fire({
          icon: "error",
          title: "Not a Firmware Binary!",
          html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
            File <strong>${escapeHtml(file.name)}</strong> detected as a <strong>Web Filesystem Image (.bin)</strong>.<br><br>
            <span style="color:#38bdf8;font-weight:600;">Please upload in the "Filesystem Update (.bin)" card below.</span>
          </div>`
        });
        if (onError) onError();
        return;
      }

      // Ekstraksi Metadata dari esp_app_desc_t jika ada
      let fwVer = "";
      let projName = "";
      let compileDate = "";
      if (isAppDescMagic) {
        for (let i = 0x30; i < 0x50 && i < bytes.length && bytes[i] !== 0; i++) {
          fwVer += String.fromCharCode(bytes[i]);
        }
        for (let i = 0x50; i < 0x70 && i < bytes.length && bytes[i] !== 0; i++) {
          projName += String.fromCharCode(bytes[i]);
        }
        for (let i = 0x80; i < 0x90 && i < bytes.length && bytes[i] !== 0; i++) {
          compileDate += String.fromCharCode(bytes[i]);
        }
      }

      // Parse format MODEL@BOARD_VERSION
      let fileModel = projName;
      let fileBoardVer = "";
      if (projName.includes("@")) {
        const parts = projName.split("@");
        fileModel = parts[0];
        fileBoardVer = parts[1];
      }

      // activeModel & activeBoardVer are defined in reader.onload scope

      // 1. Validasi Model Hardware
      const isKnownValidModel = (!fileModel || 
                                 !activeModel || 
                                 activeModel === "EMPTY" ||
                                 fileModel === activeModel || 
                                 fileModel === "SG-10" || 
                                 fileModel === "esp32_ibeacon_espidf" || 
                                 fileModel === "arduino-lib-builder" ||
                                 fileModel.startsWith("SG-10"));
      if (!isKnownValidModel) {
        Swal.fire({
          icon: "error",
          title: "Hardware Model Incompatible!",
          html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
            File <strong>${escapeHtml(file.name)}</strong> designed for model <code>${escapeHtml(fileModel)}</code>.<br><br>
            Current gateway model is <code>${escapeHtml(activeModel)}</code>.<br><br>
            <span style="color:#ef4444;font-weight:600;">Flashing aborted to prevent system damage from model incompatibility!</span>
          </div>`
        });
        if (onError) onError();
        return;
      }

      // 2. Validasi Versi Board / PCB Hardware
      if (fileBoardVer && activeBoardVer && activeBoardVer !== "EMPTY" && fileBoardVer !== activeBoardVer) {
        Swal.fire({
          icon: "error",
          title: "Incompatible Board Version!",
          html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
            This firmware binary was compiled specifically for Board revision <code>${escapeHtml(fileBoardVer)}</code>.<br><br>
            Physical gateway has board revision <code>${escapeHtml(activeBoardVer)}</code>.<br><br>
            <span style="color:#ef4444;font-weight:600;">Flashing aborted for hardware safety! Board revision mismatch detected.</span>
          </div>`
        });
        if (onError) onError();
        return;
      }

      const metadata = {
        model: fileModel || activeModel,
        boardVer: fileBoardVer || activeBoardVer,
        version: fwVer || "Unknown",
        date: compileDate || "-"
      };
      validFirmwareMetadata = metadata;

      if (onSuccess) onSuccess(metadata);
      return;

    } else if (type === "filesystem") {
      if (isPartitionTable) {
        Swal.fire({
          icon: "error",
          title: "Invalid Filesystem Binary!",
          html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
            File <strong>${escapeHtml(file.name)}</strong> detected as an <strong>Partition Table (partitions.bin)</strong> (Magic <code>0x50AA</code>).<br><br>
            <span style="color:#ef4444;font-weight:600;">Partition tables cannot be flashed through this OTA menu!</span><br>
            Please select a valid <strong>filesystem.bin</strong> file.
          </div>`
        });
        if (onError) onError();
        return;
      }



      if (!isLittleFS) {
        Swal.fire({
          icon: "error",
          title: "Invalid Filesystem Binary!",
          html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
            File <strong>${escapeHtml(file.name)}</strong> is not a valid Filesystem image (valid filesystem signature not found at offset 0x08).<br><br>
            <span style="color:#38bdf8;font-weight:600;">Only official filesystem binary images generated by PlatformIO filesystem builds can be uploaded!</span>
          </div>`
        });
        if (onError) onError();
        return;
      }

      // Ekstraksi Metadata Filesystem dari version.json di dalam binary
      let fsMetadata = null;
      try {
        const text = new TextDecoder("utf-8", { fatal: false }).decode(bytes);
        const match = text.match(/\{[\s\r\n]*"device_model"[\s\S]*?"build_date"[\s\S]*?\}/);
        if (match) {
          const parsed = JSON.parse(match[0]);
          fsMetadata = {
            model: parsed.device_model || activeModel,
            boardVer: parsed.device_board_ver || activeBoardVer,
            uiVersion: parsed.ui_version || parsed.version || "v1.0.0",
            buildDate: parsed.build_date || "-"
          };
        }
      } catch (err) {
        console.warn("Failed to parse Filesystem metadata:", err);
      }

      if (fsMetadata) {
        // 1. Validasi Model Hardware
        const isKnownFsModel = (!fsMetadata.model || 
                                !activeModel || 
                                activeModel === "EMPTY" || 
                                fsMetadata.model === activeModel || 
                                fsMetadata.model === "SG-10" || 
                                fsMetadata.model.startsWith("SG-10"));
        if (!isKnownFsModel) {
          Swal.fire({
            icon: "error",
            title: "Hardware Model Incompatible!",
            html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
              This filesystem image was built specifically for model <code>${escapeHtml(fsMetadata.model)}</code>.<br><br>
              Your gateway is model <code>${escapeHtml(activeModel)}</code>.<br><br>
              <span style="color:#ef4444;font-weight:600;">Flashing aborted for safety! Incompatible filesystem image.</span>
            </div>`
          });
          if (onError) onError();
          return;
        }

        // 2. Validasi Versi Board Hardware
        if (fsMetadata.boardVer && activeBoardVer && activeBoardVer !== "EMPTY" && fsMetadata.boardVer !== activeBoardVer) {
          Swal.fire({
            icon: "error",
            title: "Incompatible Board Version!",
            html: `<div style="text-align:left;font-size:0.9rem;line-height:1.5;">
              This filesystem image was built for Board revision <code>${escapeHtml(fsMetadata.boardVer)}</code>.<br><br>
              Physical gateway has board revision <code>${escapeHtml(activeBoardVer)}</code>.<br><br>
              <span style="color:#ef4444;font-weight:600;">Flashing aborted for hardware safety!</span>
            </div>`
          });
          if (onError) onError();
          return;
        }
      }

      if (onSuccess) onSuccess(fsMetadata);
      return;
    }

    if (onSuccess) onSuccess(null);
  };

  reader.onerror = function() {
    Swal.fire("Read Failed", "Failed to read binary file from local storage.", "error");
    if (onError) onError();
  };

  reader.readAsArrayBuffer(type === "filesystem" ? file : file.slice(0, 512));
}

function uploadOTA(fileInputId, typeName, queryType) {
  if (isOtaInProgress) {
    Swal.fire("Operation in Progress", "OTA update is running. Please wait...", "info");
    return;
  }

  const fileInput = document.getElementById(fileInputId);
  if (!fileInput || !fileInput.files.length) {
    Swal.fire("Warning", `Please select a ${typeName} (.bin) file first!`, "warning");
    return;
  }

  const file = fileInput.files[0];

  validateBinaryFile(
    file,
    queryType,
    function onSuccess(meta) {
      startUploadExecution(file, typeName, queryType);
    },
    function onError() {
      clearDropzone(queryType);
    }
  );
}

function startUploadExecution(file, typeName, queryType) {
  // Lock process & disable all interactive buttons
  isOtaInProgress = true;
  isOtaCompleted = false;
  hasReceivedWsProgress = false;
  currentUploadFile = file;
  document.querySelectorAll("button, input[type='file']").forEach(el => el.disabled = true);

  // Pastikan WebSocket aktif
  initWebSocket();

  // Grab modal elements
  const modal = document.getElementById("otaModal");
  const modalTitle = document.getElementById("otaModalTitle");
  const closeBtn = document.getElementById("btnCloseOtaModal");
  const fileNameEl = document.getElementById("otaFileName");
  const fileSizeEl = document.getElementById("otaFileSize");
  const progressFill = document.getElementById("otaProgressFill");
  const percentBadge = document.getElementById("otaPercentBadge");
  const statusDesc = document.getElementById("otaStatusDesc");
  const spinner = document.getElementById("otaSpinner");
  const resultIcon = document.getElementById("otaResultIcon");
  const actionsBox = document.getElementById("otaModalActions");
  const actionBtn = document.getElementById("btnOtaAction");

  // Reset modal UI to upload start state
  modalTitle.textContent = `${typeName} Flashing Progress`;
  fileNameEl.textContent = file.name;
  fileSizeEl.textContent = `0 B / ${formatBytes(file.size)}`;
  progressFill.style.width = "0%";
  progressFill.style.background = "linear-gradient(90deg, #0284c7, #38bdf8, #818cf8)";
  percentBadge.textContent = "0%";
  percentBadge.style.color = "#38bdf8";
  percentBadge.style.borderColor = "rgba(56, 189, 248, 0.3)";
  percentBadge.style.background = "rgba(56, 189, 248, 0.12)";
  statusDesc.textContent = "Connecting to gateway and initiating upload stream...";
  spinner.style.display = "block";
  resultIcon.style.display = "none";
  actionsBox.style.display = "none";

  // Modal can NOT be dismissed during active upload
  closeBtn.style.display = "none";
  modal.style.setProperty("display", "flex", "important");

  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/api/v1/ota/upload?type=${queryType}&size=${file.size}`, true);
  xhr.setRequestHeader("Content-Type", "application/octet-stream");

  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable && !isOtaCompleted) {
      if (!hasReceivedWsProgress) {
        const percent = Math.min(Math.round((e.loaded / e.total) * 100), 99);
        progressFill.style.width = `${percent}%`;
        percentBadge.textContent = `${percent}%`;
        fileSizeEl.textContent = `${formatBytes(e.loaded)} / ${formatBytes(e.total)}`;
        statusDesc.textContent = `Uploading binary image (${percent}%)...`;
      }
    }
  };

  xhr.onload = function() {
    if (xhr.status === 200) {
      if (!isOtaCompleted) {
        isOtaCompleted = true;
        progressFill.style.width = "100%";
        progressFill.style.background = "#22c55e";
        percentBadge.textContent = "100%";
        percentBadge.style.color = "#4ade80";
        percentBadge.style.borderColor = "rgba(34, 197, 94, 0.4)";
        percentBadge.style.background = "rgba(34, 197, 94, 0.15)";
        fileSizeEl.textContent = `${formatBytes(file.size)} / ${formatBytes(file.size)}`;
        spinner.style.display = "none";
        resultIcon.style.display = "block";
        resultIcon.innerHTML = `
          <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#22c55e" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
            <path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/>
            <polyline points="22 4 12 14.01 9 11.01"/>
          </svg>
        `;

        statusDesc.innerHTML = `<strong style="color:#4ade80;">Update Successful!</strong> Gateway rebooting in <span id="otaCountdown">10</span>s...`;

        closeBtn.style.display = "none";
        actionsBox.style.display = "flex";
        actionBtn.textContent = "Rebooting...";
        actionBtn.disabled = true;

        startCountdownAndRedirect(10);
      }
    } else {
      isOtaInProgress = false;
      isOtaCompleted = false;
      document.querySelectorAll("button, input[type='file']").forEach(el => el.disabled = false);

      progressFill.style.background = "#ef4444";
      percentBadge.style.color = "#f87171";
      percentBadge.style.borderColor = "rgba(239, 68, 68, 0.4)";
      percentBadge.style.background = "rgba(239, 68, 68, 0.15)";
      spinner.style.display = "none";
      resultIcon.style.display = "block";
      resultIcon.innerHTML = `
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#ef4444" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <circle cx="12" cy="12" r="10"/>
          <line x1="15" y1="9" x2="9" y2="15"/>
          <line x1="9" y1="9" x2="15" y2="15"/>
        </svg>
      `;

      statusDesc.innerHTML = `<strong style="color:#f87171;">Update Rejected:</strong> ${escapeHtml(xhr.responseText || "Binary header or image format mismatch.")}`;

      closeBtn.style.display = "block";
      actionsBox.style.display = "flex";
      actionBtn.textContent = "Close & Retry";
      actionBtn.onclick = () => { modal.style.setProperty("display", "none", "important"); };
      closeBtn.onclick = () => { modal.style.setProperty("display", "none", "important"); };
    }
  };

  xhr.onerror = function() {
    isOtaInProgress = false;
    isOtaCompleted = false;
    document.querySelectorAll("button, input[type='file']").forEach(el => el.disabled = false);

    progressFill.style.background = "#ef4444";
    percentBadge.style.color = "#f87171";
    spinner.style.display = "none";
    resultIcon.style.display = "block";
    resultIcon.innerHTML = `
      <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="#f59e0b" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
        <path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/>
        <line x1="12" y1="9" x2="12" y2="13"/>
        <line x1="12" y1="17" x2="12.01" y2="17"/>
      </svg>
    `;

    statusDesc.innerHTML = `<strong style="color:#f87171;">Connection Lost:</strong> Failed to upload file to gateway. Check network connection.`;

    closeBtn.style.display = "block";
    actionsBox.style.display = "flex";
    actionBtn.textContent = "Close & Retry";
    actionBtn.onclick = () => { modal.style.setProperty("display", "none", "important"); };
    closeBtn.onclick = () => { modal.style.setProperty("display", "none", "important"); };
  };

  xhr.send(file);
}

function startCountdownAndRedirect(seconds) {
  let remain = seconds;
  const timer = setInterval(() => {
    remain--;
    const el = document.getElementById("otaCountdown");
    if (el) el.textContent = remain;
    if (remain <= 0) {
      clearInterval(timer);
      const isAp = location.hostname.startsWith("192.168.4");
      location.href = isAp ? "http://192.168.4.1/auth" : "/auth";
    }
  }, 1000);
}

// ================= INDUSTRIAL BENTO DROPZONE CONTROLLER =================
function initDropzone(dropzoneId, inputId, idleId, selectedId, nameId, sizeId, type) {
  const dropzone = document.getElementById(dropzoneId);
  const input = document.getElementById(inputId);
  const idle = document.getElementById(idleId);
  const selected = document.getElementById(selectedId);
  const nameEl = document.getElementById(nameId);
  const sizeEl = document.getElementById(sizeId);

  if (!dropzone || !input) return;

  if (idle) idle.style.display = "flex";
  if (selected) selected.style.display = "none";

  // Prevent input click from bubbling to dropzone
  input.addEventListener('click', (e) => {
    e.stopPropagation();
  });

  // Click dropzone triggers input file chooser
  dropzone.addEventListener('click', (e) => {
    if (e.target.closest('.dropzone-remove-btn')) return;
    input.click();
  });

  function preventDefaults(e) {
    e.preventDefault();
    e.stopPropagation();
  }

  ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
    dropzone.addEventListener(eventName, preventDefaults, false);
  });

  ['dragenter', 'dragover'].forEach(eventName => {
    dropzone.addEventListener(eventName, () => {
      dropzone.classList.add('dragover');
    }, false);
  });

  ['dragleave', 'drop'].forEach(eventName => {
    dropzone.addEventListener(eventName, () => {
      dropzone.classList.remove('dragover');
    }, false);
  });

  function processFile(file) {
    if (!file) return;

    validateBinaryFile(
      file,
      type,
      function onSuccess(meta) {
        if (idle) idle.style.display = "none";
        if (selected) selected.style.display = "flex";
        if (nameEl) nameEl.textContent = file.name;
        if (sizeEl) {
          if (type === "firmware" && meta) {
            sizeEl.innerHTML = `<span style="color:#94a3b8;">${formatBytes(file.size)}</span> &bull; <span style="color:#38bdf8;font-weight:600;">${escapeHtml(meta.model)}</span> <span style="color:#64748b;">(Board ${escapeHtml(meta.boardVer)})</span> &bull; <span style="color:#4ade80;">${escapeHtml(meta.version)}</span>`;
          } else if (type === "filesystem" && meta) {
            sizeEl.innerHTML = `<span style="color:#94a3b8;">${formatBytes(file.size)}</span> &bull; <span style="color:#38bdf8;font-weight:600;">${escapeHtml(meta.model)}</span> <span style="color:#64748b;">(Board ${escapeHtml(meta.boardVer)})</span> &bull; <span style="color:#4ade80;">UI ${escapeHtml(meta.uiVersion)}</span>`;
          } else {
            sizeEl.textContent = formatBytes(file.size);
          }
        }
      },
      function onError() {
        clearDropzone(type);
      }
    );
  }

  // Handle Drop
  dropzone.addEventListener('drop', (e) => {
    const dt = e.dataTransfer;
    if (dt && dt.files && dt.files.length > 0) {
      const file = dt.files[0];
      try {
        const transfer = new DataTransfer();
        transfer.items.add(file);
        input.files = transfer.files;
      } catch (err) {
        console.warn("[Dropzone] DataTransfer fallback:", err);
      }
      processFile(file);
    }
  }, false);

  // Handle Input File Dialog Change
  input.addEventListener('change', () => {
    if (input.files && input.files.length > 0) {
      processFile(input.files[0]);
    }
  });
}

function clearDropzone(type) {
  const isFw = (type === "firmware");
  if (isFw) validFirmwareMetadata = null;

  const input = document.getElementById(isFw ? "fileFirmware" : "filesystem");
  const idle = document.getElementById(isFw ? "dropzoneFirmwareIdle" : "dropzoneFSIdle");
  const selected = document.getElementById(isFw ? "dropzoneFirmwareSelected" : "dropzoneFSSelected");
  const nameEl = document.getElementById(isFw ? "firmwareFileName" : "fsFileName");
  const sizeEl = document.getElementById(isFw ? "firmwareFileSize" : "fsFileSize");

  if (input) input.value = "";
  if (idle) idle.style.display = "flex";
  if (selected) selected.style.display = "none";
  if (nameEl) nameEl.textContent = "-";
  if (sizeEl) sizeEl.textContent = "-";
}

function uploadFirmware() {
  if (isOtaInProgress) {
    Swal.fire("Operation in Progress", "OTA update is running. Please wait...", "info");
    return;
  }

  const fileInput = document.getElementById("fileFirmware");
  if (!fileInput || !fileInput.files.length) {
    Swal.fire("Warning", "Please select a Firmware (.bin) file first!", "warning");
    return;
  }

  const file = fileInput.files[0];

  const defaultModel = (typeof CURRENT_DEVICE_MODEL !== "undefined" && CURRENT_DEVICE_MODEL && CURRENT_DEVICE_MODEL !== "%device_model%") ? CURRENT_DEVICE_MODEL : "SG-10";
  const defaultBoard = (typeof CURRENT_DEVICE_VERSION !== "undefined" && CURRENT_DEVICE_VERSION && CURRENT_DEVICE_VERSION !== "%device_version%") ? CURRENT_DEVICE_VERSION : "v1.0.0";

  validateBinaryFile(
    file,
    "firmware",
    function onSuccess(meta) {
      const showModel = meta ? meta.model : defaultModel;
      const showBoard = meta ? meta.boardVer : defaultBoard;
      const showVersion = meta ? meta.version : "Unknown";
      const showDate = meta ? meta.date : "-";

      Swal.fire({
        title: "Confirm Firmware Flash",
        icon: "question",
        html: `<div style="text-align:left;font-size:0.9rem;line-height:1.6;background:rgba(15,23,42,0.6);padding:14px 18px;border-radius:10px;border:1px solid rgba(56,189,248,0.25);">
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">File:</span> <strong style="color:#f8fafc;">${escapeHtml(file.name)}</strong> <span style="color:#64748b;font-size:0.8rem;">(${formatBytes(file.size)})</span></div>
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">Device Model:</span> <strong style="color:#38bdf8;">${escapeHtml(showModel)}</strong></div>
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">Board Version:</span> <strong style="color:#38bdf8;">${escapeHtml(showBoard)}</strong></div>
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">Firmware Version:</span> <strong style="color:#4ade80;">${escapeHtml(showVersion)}</strong></div>
          <div><span style="color:#94a3b8;">Firmware Date:</span> <strong style="color:#e2e8f0;">${escapeHtml(showDate)}</strong></div>
        </div><br><span style="font-size:0.85rem;color:#94a3b8;">Ensure gateway power supply is stable during OTA flashing. Proceed with upload?</span>`,
        showCancelButton: true,
        confirmButtonColor: "#0284c7",
        cancelButtonColor: "#475569",
        confirmButtonText: "Proceed with Update",
        cancelButtonText: "Cancel"
      }).then((result) => {
        if (result.isConfirmed) {
          startUploadExecution(file, "Gateway Firmware", "firmware");
        }
      });
    },
    function onError() {
      clearDropzone("firmware");
    }
  );
}

function uploadFS() {
  if (isOtaInProgress) {
    Swal.fire("Operation in Progress", "OTA update is running. Please wait...", "info");
    return;
  }

  const fileInput = document.getElementById("filesystem");
  if (!fileInput || !fileInput.files.length) {
    Swal.fire("Warning", "Please select a Filesystem (.bin) file first!", "warning");
    return;
  }

  const file = fileInput.files[0];

  const defaultModel = (typeof CURRENT_DEVICE_MODEL !== "undefined" && CURRENT_DEVICE_MODEL && CURRENT_DEVICE_MODEL !== "%device_model%") ? CURRENT_DEVICE_MODEL : "SG-10";
  const defaultBoard = (typeof CURRENT_DEVICE_VERSION !== "undefined" && CURRENT_DEVICE_VERSION && CURRENT_DEVICE_VERSION !== "%device_version%") ? CURRENT_DEVICE_VERSION : "v1.0.0";

  validateBinaryFile(
    file,
    "filesystem",
    function onSuccess(meta) {
      const showModel = meta ? meta.model : defaultModel;
      const showBoard = meta ? meta.boardVer : defaultBoard;
      const showUiVer = meta ? meta.uiVersion : "v1.0.0";
      const showDate = (meta && meta.buildDate) ? meta.buildDate : "-";

      Swal.fire({
        title: "Confirm Filesystem Flash",
        icon: "question",
        html: `<div style="text-align:left;font-size:0.9rem;line-height:1.6;background:rgba(15,23,42,0.6);padding:14px 18px;border-radius:10px;border:1px solid rgba(56,189,248,0.25);">
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">File:</span> <strong style="color:#f8fafc;">${escapeHtml(file.name)}</strong> <span style="color:#64748b;font-size:0.8rem;">(${formatBytes(file.size)})</span></div>
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">Device Model:</span> <strong style="color:#38bdf8;">${escapeHtml(showModel)}</strong></div>
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">Board Version:</span> <strong style="color:#38bdf8;">${escapeHtml(showBoard)}</strong></div>
          <div style="margin-bottom:6px;"><span style="color:#94a3b8;">Web UI Version:</span> <strong style="color:#4ade80;">${escapeHtml(showUiVer)}</strong></div>
          <div><span style="color:#94a3b8;">FS Build Date:</span> <strong style="color:#e2e8f0;">${escapeHtml(showDate)}</strong></div>
        </div><br><span style="font-size:0.85rem;color:#94a3b8;">Filesystem update will refresh web dashboard assets. Proceed with upload?</span>`,
        showCancelButton: true,
        confirmButtonColor: "#0284c7",
        cancelButtonColor: "#475569",
        confirmButtonText: "Proceed with Update",
        cancelButtonText: "Cancel"
      }).then((result) => {
        if (result.isConfirmed) {
          startUploadExecution(file, "Filesystem Web Dashboard", "filesystem");
        }
      });
    },
    function onError() {
      clearDropzone("filesystem");
    }
  );
}

function formatBytes(bytes) {
  if (!bytes || bytes === 0) return "0 B";
  const k = 1024;
  const sizes = ["B", "KB", "MB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i];
}

function escapeHtml(str) {
  if (!str) return "";
  return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

// Inisialisasi WebSocket saat halaman dimuat
document.addEventListener("DOMContentLoaded", () => {
  initWebSocket();
  initDropzone("dropzoneFirmware", "fileFirmware", "dropzoneFirmwareIdle", "dropzoneFirmwareSelected", "firmwareFileName", "firmwareFileSize", "firmware");
  initDropzone("dropzoneFS", "filesystem", "dropzoneFSIdle", "dropzoneFSSelected", "fsFileName", "fsFileSize", "filesystem");
});
