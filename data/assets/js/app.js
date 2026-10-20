/**
 * IOTARU ESP32 Dashboard Core Application JS
 * Modern Bottom Action Sheets (Config & Power) + 2-Mode Segmented Controls
 */
document.addEventListener("DOMContentLoaded", function() {
  // 1. Injeksi Bottom Action Sheet untuk Config (Settings & Auth)
  let sheetConfigBackdrop = document.getElementById("sheetConfigBackdrop");
  let sheetConfig = document.getElementById("sheetConfig");

  if (!sheetConfig) {
    sheetConfigBackdrop = document.createElement("div");
    sheetConfigBackdrop.id = "sheetConfigBackdrop";
    sheetConfigBackdrop.className = "bottom-sheet-backdrop";

    sheetConfig = document.createElement("div");
    sheetConfig.id = "sheetConfig";
    sheetConfig.className = "bottom-sheet";
    sheetConfig.innerHTML = `
      <div class="sheet-handle"></div>
      <div class="sheet-header">
        <div class="sheet-title">Pengaturan Sistem</div>
        <div class="sheet-desc">Pilih menu konfigurasi gateway IOTARU</div>
      </div>
      <div class="sheet-options">
        <a href="/settings" class="sheet-btn">
          <div class="sheet-btn-icon blue">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"/>
              <circle cx="12" cy="12" r="3"/>
            </svg>
          </div>
          <div class="sheet-btn-info">
            <div class="sheet-btn-title">Device Settings</div>
            <div class="sheet-btn-sub">Filter UUID, RSSI threshold & batching MQTT</div>
          </div>
        </a>

        <a href="/auth" class="sheet-btn">
          <div class="sheet-btn-icon purple">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <rect width="18" height="11" x="3" y="11" rx="2" ry="2"/>
              <path d="M7 11V7a5 5 0 0 1 10 0v4"/>
            </svg>
          </div>
          <div class="sheet-btn-info">
            <div class="sheet-btn-title">Auth & Network Settings</div>
            <div class="sheet-btn-sub">Kredensial login, Wi-Fi & LAN Ethernet</div>
          </div>
        </a>

        <button type="button" class="sheet-btn-cancel" id="btnCancelConfigSheet">Batal</button>
      </div>
    `;

    document.body.appendChild(sheetConfigBackdrop);
    document.body.appendChild(sheetConfig);
  }

  // 2. Injeksi Bottom Action Sheet untuk Power / Reboot / Reset
  let sheetPowerBackdrop = document.getElementById("sheetPowerBackdrop");
  let sheetPower = document.getElementById("sheetPower");

  if (!sheetPower) {
    sheetPowerBackdrop = document.createElement("div");
    sheetPowerBackdrop.id = "sheetPowerBackdrop";
    sheetPowerBackdrop.className = "bottom-sheet-backdrop";

    sheetPower = document.createElement("div");
    sheetPower.id = "sheetPower";
    sheetPower.className = "bottom-sheet";
    sheetPower.innerHTML = `
      <div class="sheet-handle"></div>
      <div class="sheet-header">
        <div class="sheet-title">Kontrol Daya Perangkat</div>
        <div class="sheet-desc">Pilih tindakan untuk mikrokontroler ESP32</div>
      </div>
      <div class="sheet-options">
        <button type="button" class="sheet-btn" id="sheetBtnReboot">
          <div class="sheet-btn-icon danger">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M18.36 6.64a9 9 0 1 1-12.73 0"/>
              <line x1="12" y1="2" x2="12" y2="12"/>
            </svg>
          </div>
          <div class="sheet-btn-info">
            <div class="sheet-btn-title">Reboot Perangkat</div>
            <div class="sheet-btn-sub">Mulai ulang sistem modul ESP32</div>
          </div>
        </button>

        <button type="button" class="sheet-btn" id="sheetBtnReset">
          <div class="sheet-btn-icon warning">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"/>
              <path d="M21 3v5h-5"/>
              <path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"/>
              <path d="M8 16H3v5"/>
            </svg>
          </div>
          <div class="sheet-btn-info">
            <div class="sheet-btn-title">Reset Konfigurasi (Factory)</div>
            <div class="sheet-btn-sub">Kembalikan semua setelan NVS ke default</div>
          </div>
        </button>

        <button type="button" class="sheet-btn-cancel" id="btnCancelPowerSheet">Batal</button>
      </div>
    `;

    document.body.appendChild(sheetPowerBackdrop);
    document.body.appendChild(sheetPower);
  }

  // Drawer Helpers
  function openSheet(backdrop, sheet) {
    if (backdrop) backdrop.classList.add("active");
    if (sheet) sheet.classList.add("open");
  }

  function closeAllSheets() {
    if (sheetConfigBackdrop) sheetConfigBackdrop.classList.remove("active");
    if (sheetConfig) sheetConfig.classList.remove("open");
    if (sheetPowerBackdrop) sheetPowerBackdrop.classList.remove("active");
    if (sheetPower) sheetPower.classList.remove("open");
  }

  // Triggers
  document.querySelectorAll('[data-action="config-sheet"]').forEach(btn => {
    btn.addEventListener("click", function(e) {
      e.preventDefault();
      closeAllSheets();
      openSheet(sheetConfigBackdrop, sheetConfig);
    });
  });

  document.querySelectorAll('[data-action="power-sheet"], .nav-tab-reboot').forEach(btn => {
    btn.addEventListener("click", function(e) {
      e.preventDefault();
      closeAllSheets();
      openSheet(sheetPowerBackdrop, sheetPower);
    });
  });

  if (sheetConfigBackdrop) sheetConfigBackdrop.addEventListener("click", closeAllSheets);
  if (sheetPowerBackdrop) sheetPowerBackdrop.addEventListener("click", closeAllSheets);
  const cancelConfigBtn = document.getElementById("btnCancelConfigSheet");
  if (cancelConfigBtn) cancelConfigBtn.addEventListener("click", closeAllSheets);
  const cancelPowerBtn = document.getElementById("btnCancelPowerSheet");
  if (cancelPowerBtn) cancelPowerBtn.addEventListener("click", closeAllSheets);

  // 3. SweetAlert Handlers for Reboot & Reset
  function triggerReboot() {
    closeAllSheets();
    Swal.fire({
      title: "Konfirmasi Reboot",
      text: "Apakah Anda yakin ingin me-reboot perangkat ESP32?",
      icon: "warning",
      showCancelButton: true,
      confirmButtonColor: "#2563eb",
      cancelButtonColor: "#64748b",
      confirmButtonText: "Ya, Reboot!",
      cancelButtonText: "Batal"
    }).then(res => {
      if (res.isConfirmed) {
        Swal.fire({
          title: "Sedang Me-reboot...",
          text: "Mohon tunggu 5-10 detik",
          icon: "info",
          allowOutsideClick: false,
          showConfirmButton: false,
          didOpen: () => Swal.showLoading()
        });

        fetch("/api/v1/system/reboot", { method: "POST" })
          .then(res => res.text())
          .then(text => {
            setTimeout(() => {
              Swal.close();
              Swal.fire({
                title: "Me-reboot...",
                text: "Perangkat sedang memulai ulang",
                icon: "info",
                confirmButtonText: "OK",
                confirmButtonColor: "#0284c7",
                allowOutsideClick: false
              }).then(() => {
                window.location.href = "/login.html";
              });
            }, 1000);
          })
          .catch(err => {
            Swal.close();
            Swal.fire("Gagal", err.message || "Terjadi kesalahan saat reboot.", "error");
          });
      }
    });
  }

  function triggerReset() {
    closeAllSheets();
    Swal.fire({
      title: "Reset Pabrik (Factory Reset)",
      text: "Semua konfigurasi NVS akan dihapus dan dikembalikan ke default. Lanjutkan?",
      icon: "warning",
      showCancelButton: true,
      confirmButtonColor: "#ef4444",
      cancelButtonColor: "#64748b",
      confirmButtonText: "Ya, Reset!",
      cancelButtonText: "Batal"
    }).then(res => {
      if (res.isConfirmed) {
        Swal.fire({
          title: "Sedang Me-reset...",
          text: "Menghapus partisi NVS dan me-reboot",
          icon: "info",
          allowOutsideClick: false,
          showConfirmButton: false,
          didOpen: () => Swal.showLoading()
        });

        fetch("/api/v1/system/reset", { method: "POST" })
          .then(res => res.text())
          .then(text => {
            setTimeout(() => {
              Swal.close();
              Swal.fire({
                title: "Reset Berhasil!",
                text: "Konfigurasi berhasil direset ke default. Sistem me-reboot...",
                icon: "success",
                confirmButtonText: "OK",
                confirmButtonColor: "#0284c7",
                allowOutsideClick: false
              }).then(() => {
                window.location.href = "/login.html";
              });
            }, 1000);
          })
          .catch(err => {
            Swal.close();
            Swal.fire("Gagal", err.message || "Terjadi kesalahan saat reset.", "error");
          });
      }
    });
  }

  const btnSheetReboot = document.getElementById("sheetBtnReboot");
  if (btnSheetReboot) btnSheetReboot.addEventListener("click", triggerReboot);

  const btnSheetReset = document.getElementById("sheetBtnReset");
  if (btnSheetReset) btnSheetReset.addEventListener("click", triggerReset);

  // Desktop sidebar reboot/reset links
  document.querySelectorAll('.sidebar a[href="/reboot"]').forEach(link => {
    link.addEventListener("click", function(e) {
      e.preventDefault();
      triggerReboot();
    });
  });

  document.querySelectorAll('.sidebar a[href="/reset"]').forEach(link => {
    link.addEventListener("click", function(e) {
      e.preventDefault();
      triggerReset();
    });
  });

  // 4. Logout Handler
  function triggerLogout() {
    Swal.fire({
      title: "Konfirmasi Keluar",
      text: "Apakah Anda yakin ingin logout dari sistem ESP32?",
      icon: "warning",
      showCancelButton: true,
      confirmButtonColor: "#ef4444",
      cancelButtonColor: "#64748b",
      confirmButtonText: "Ya, Keluar!",
      cancelButtonText: "Batal"
    }).then(res => {
      if (res.isConfirmed) {
        fetch("/api/v1/auth/logout", { method: "POST" })
          .then(() => {
            window.location.href = "/login.html";
          })
          .catch(() => {
            window.location.href = "/login.html";
          });
      }
    });
  }

  document.querySelectorAll('.sidebar-logout a, a[href="/logout"], .mobile-logout-btn, [data-action="logout"]').forEach(link => {
    link.addEventListener("click", function(e) {
      e.preventDefault();
      triggerLogout();
    });
  });

  // 5. DHCP / Static IP Dynamic Toggle
  function syncDhcpState(group) {
    const sel = document.querySelector(`.dhcp-selector[data-group="${group}"]`);
    if (!sel) return;

    const panel = sel.closest(".connection-panel");
    if (!panel || panel.style.display === "none") return;

    // "1" = DHCP, "0" = Static IP
    const isDhcp = sel.value === "1";
    const staticInputs = panel.querySelectorAll(`.static-field-${group}`);

    staticInputs.forEach(input => {
      if (isDhcp) {
        input.disabled = true;
        if (!input.dataset.normalPlaceholder) {
          input.dataset.normalPlaceholder = input.placeholder;
        }
        input.placeholder = "Otomatis via DHCP";
        const item = input.closest(".item");
        if (item) item.classList.remove("has-error");
      } else {
        input.disabled = false;
        if (input.dataset.normalPlaceholder) {
          input.placeholder = input.dataset.normalPlaceholder;
        }
      }
    });
  }

  document.querySelectorAll(".dhcp-selector").forEach(sel => {
    if (!sel.value || sel.value.indexOf("%") >= 0) {
      sel.value = "1";
    }
    sel.addEventListener("change", function() {
      syncDhcpState(this.dataset.group);
    });
  });

  // 6. 2-Mode Segmented Control (Wi-Fi vs Ethernet Only)
  function updateConnectionMode(modeVal) {
    const panels = document.querySelectorAll(".connection-panel");
    if (!panels.length) return;

    panels.forEach(panel => {
      const isCurrent = panel.getAttribute("data-mode") === String(modeVal);
      if (isCurrent) {
        panel.style.display = "";
        panel.querySelectorAll("input, select, textarea").forEach(el => {
          el.disabled = false;
        });
        const dhcpSel = panel.querySelector(".dhcp-selector");
        if (dhcpSel) {
          syncDhcpState(dhcpSel.dataset.group);
        }
      } else {
        panel.style.display = "none";
        panel.querySelectorAll("input, select, textarea").forEach(el => {
          el.disabled = true;
          const item = el.closest(".item");
          if (item) item.classList.remove("has-error");
        });
      }
    });
  }

  document.querySelectorAll(".segmented-control").forEach(seg => {
    const targetInputId = seg.dataset.target;
    const targetInput = targetInputId ? document.getElementById(targetInputId) : null;
    const buttons = seg.querySelectorAll(".segmented-btn");

    let currentVal = targetInput ? targetInput.value : "";
    if (!currentVal || currentVal.indexOf("%") >= 0) {
      currentVal = buttons.length ? buttons[0].dataset.value : "0";
      if (targetInput) targetInput.value = currentVal;
    }

    let hasActive = false;
    buttons.forEach(btn => {
      if (btn.dataset.value === String(currentVal)) {
        btn.classList.add("active");
        hasActive = true;
      } else {
        btn.classList.remove("active");
      }
    });
    if (!hasActive && buttons.length > 0) {
      buttons[0].classList.add("active");
      if (targetInput) targetInput.value = buttons[0].dataset.value;
      currentVal = buttons[0].dataset.value;
    }

    if (targetInputId === "connection_mode") {
      updateConnectionMode(currentVal);
    }

    buttons.forEach(btn => {
      btn.addEventListener("click", function(e) {
        e.preventDefault();
        buttons.forEach(b => b.classList.remove("active"));
        this.classList.add("active");
        if (targetInput) {
          targetInput.value = this.dataset.value;
        }
        if (targetInputId === "connection_mode") {
          updateConnectionMode(this.dataset.value);
        }
      });
    });
  });

  // Stepper Control
  document.querySelectorAll(".stepper-btn").forEach(btn => {
    btn.addEventListener("click", function(e) {
      e.preventDefault();
      const parent = this.closest(".stepper-control");
      if (!parent) return;
      const input = parent.querySelector(".stepper-input");
      if (!input) return;
      let val = parseInt(input.value) || 0;
      const min = input.hasAttribute("min") ? parseInt(input.getAttribute("min")) : 0;
      const max = input.hasAttribute("max") ? parseInt(input.getAttribute("max")) : 200;
      const action = this.dataset.action;

      if (action === "minus" && val > min) val -= 10;
      else if (action === "plus" && val < max) val += 10;
      input.value = val;
    });
  });

  // Time Inputs Auto-format (HH:MM)
  document.querySelectorAll(".time-input").forEach(input => {
    input.addEventListener("input", function() {
      let v = this.value.replace(/\D/g, "");
      if (v.length > 4) v = v.slice(0, 4);
      if (v.length >= 3) v = v.slice(0, 2) + ":" + v.slice(2);
      this.value = v;
    });

    input.addEventListener("blur", function() {
      let v = this.value.replace(/\D/g, "");
      if (!v) return;
      if (v.length === 1) v = "0" + v + "00";
      else if (v.length === 2) v = v + "00";
      else if (v.length === 3) v = v + "0";
      let h = Math.min(23, parseInt(v.slice(0, 2)) || 0);
      let m = Math.min(59, parseInt(v.slice(2, 4)) || 0);
      this.value = `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}`;
    });
  });
});

/**
 * Universal Form Submitter with SweetAlert Confirmation
 */
function handleFormSubmit(formId, endpoint, successMsg) {
  const form = document.getElementById(formId);
  if (!form) return false;

  let isValid = true;
  const inputs = form.querySelectorAll("input:not([disabled]), select:not([disabled])");
  inputs.forEach(el => {
    const item = el.closest(".item");
    if (el.required && !el.value.trim()) {
      isValid = false;
      if (item) item.classList.add("has-error");
    } else if (el.dataset.nospace === "true" && (el.value.indexOf(" ") >= 0 || !el.value)) {
      isValid = false;
      if (item) item.classList.add("has-error");
    } else {
      if (item) item.classList.remove("has-error");
    }
  });

  if (!isValid) {
    Swal.fire({
      title: "Validasi Gagal",
      text: "Harap isi semua kolom wajib dengan benar tanpa spasi!",
      icon: "error"
    });
    return false;
  }

  Swal.fire({
    title: "Konfirmasi Simpan",
    text: "Apakah Anda yakin ingin memperbarui konfigurasi ini?",
    icon: "warning",
    showCancelButton: true,
    confirmButtonColor: "#2563eb",
    cancelButtonColor: "#64748b",
    confirmButtonText: "Ya, Simpan!",
    cancelButtonText: "Batal"
  }).then(res => {
    if (res.isConfirmed) {
      Swal.fire({
        title: "Sedang Menyimpan...",
        text: "Menyimpan ke NVS memori ESP32",
        allowOutsideClick: false,
        showConfirmButton: false,
        didOpen: () => Swal.showLoading()
      });

      const disabledInputs = form.querySelectorAll("input:disabled, select:disabled");
      disabledInputs.forEach(el => el.disabled = false);
      const formData = new FormData(form);
      disabledInputs.forEach(el => el.disabled = true);
      const urlParams = new URLSearchParams(formData);
      fetch(endpoint, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: urlParams
      })
        .then(response => {
          Swal.close();
          if (response.status === 200) {
            return response.text().then(text => {
              Swal.fire({
                title: "Berhasil!",
                text: text || successMsg || "Konfigurasi berhasil disimpan ke NVS.",
                icon: "success",
                showCancelButton: true,
                confirmButtonText: "Reboot Sekarang",
                cancelButtonText: "Selesai",
                confirmButtonColor: "#10b981",
                cancelButtonColor: "#64748b"
              }).then(choice => {
                if (choice.isConfirmed) {
                  fetch("/api/v1/system/reboot", { method: "POST" });
                  Swal.fire({
                    title: "Me-reboot...",
                    text: "Perangkat sedang memulai ulang",
                    icon: "info",
                    confirmButtonText: "OK",
                    confirmButtonColor: "#0284c7",
                    allowOutsideClick: false
                  }).then(() => {
                    window.location.href = "/login.html";
                  });
                } else {
                  location.reload();
                }
              });
            });
          } else {
            return response.text().then(text => {
              Swal.fire("Gagal", text || "Terjadi kesalahan saat menyimpan.", "error");
            });
          }
        })
        .catch(err => {
          Swal.close();
          Swal.fire("Error Koneksi", err.message || "Gagal menghubungi ESP32.", "error");
        });
    }
  });

  return false;
}

// =============================================================================
// WI-FI NETWORK SCANNER MODULE (POP-UP MODAL DENGAN PROGRESS BAR)
// =============================================================================
document.addEventListener("DOMContentLoaded", function() {
  const btnOpen = document.getElementById("btnOpenWifiModal");
  const modal = document.getElementById("wifiScanModal");
  const btnClose = document.getElementById("btnCloseWifiModal");
  const btnRescan = document.getElementById("btnRescanWifi");
  const statusEl = document.getElementById("wifiScanStatus");
  const listContainer = document.getElementById("wifiListContainer");
  const progressContainer = document.getElementById("wifiProgressContainer");
  const progressBar = document.getElementById("wifiProgressBar");
  const inputSsid = document.getElementById("w_ssid");
  const inputPass = document.getElementById("w_password");

  if (!btnOpen || !modal) return;

  let pollTimer = null;
  let progressTimer = null;
  let isScanning = false;
  let currentProgress = 0;

  function openModal() {
    modal.style.display = "flex";
    triggerScan(true);
  }

  function closeModal() {
    modal.style.display = "none";
    stopScanningTimers();
    isScanning = false;
    if (btnOpen) btnOpen.classList.remove("scanning");
    if (progressContainer) progressContainer.style.display = "none";
    fetch("/api/v1/wifi/scan-stop", { method: "POST" }).catch(() => {});
  }

  function stopScanningTimers() {
    if (pollTimer) { clearTimeout(pollTimer); pollTimer = null; }
    if (progressTimer) { clearInterval(progressTimer); progressTimer = null; }
  }

  btnOpen.addEventListener("click", openModal);
  if (btnClose) btnClose.addEventListener("click", closeModal);

  modal.addEventListener("click", function(e) {
    if (e.target === modal) closeModal();
  });

  if (btnRescan) {
    btnRescan.addEventListener("click", function() {
      if (!isScanning) triggerScan(true);
    });
  }

  function updateProgressUI(pct) {
    currentProgress = pct;
    if (progressBar) progressBar.style.width = pct + "%";
    const percentBadge = document.getElementById("wifiProgressPct");
    if (percentBadge) percentBadge.textContent = Math.round(pct) + "%";
  }

  function startProgressSimulation() {
    currentProgress = 10;
    updateProgressUI(currentProgress);
    if (progressContainer) progressContainer.style.display = "block";

    if (progressTimer) clearInterval(progressTimer);
    // Simulasikan progress secara halus dari 10% hingga ~92% selama 2.5 - 3 detik
    progressTimer = setInterval(() => {
      if (currentProgress < 85) {
        currentProgress += Math.random() * 5 + 3;
      } else if (currentProgress < 94) {
        currentProgress += Math.random() * 1.5 + 0.5;
      }
      if (currentProgress > 94) currentProgress = 94;
      updateProgressUI(currentProgress);
    }, 120);
  }

  function triggerScan(forceRefresh) {
    isScanning = true;
    stopScanningTimers();

    if (btnOpen) btnOpen.classList.add("scanning");
    if (btnRescan) btnRescan.disabled = true;
    if (statusEl) statusEl.innerHTML = '<span class="pulse-dot"></span> Memindai saluran Wi-Fi...';

    // Visual Loader dengan Radar Sweep & Counter Persentase
    if (listContainer) {
      listContainer.innerHTML = `
        <div class="wifi-scanner-loader">
          <div class="radar-sweep-box">
            <div class="radar-sweep-beam"></div>
            <div class="radar-center-dot"></div>
          </div>
          <div class="wifi-scanner-text">
            <span>Memindai Sinyal Sekitar</span>
            <span class="wifi-scanner-percent" id="wifiProgressPct">10%</span>
          </div>
          <div class="wifi-scanner-sub">Mencari frekuensi 2.4 GHz (Channel 1 – 13)...</div>
        </div>
      `;
    }

    startProgressSimulation();

    const url = "/api/v1/wifi/scan" + (forceRefresh ? "?refresh=1" : "");
    fetch(url, { cache: "no-store" })
      .then(res => res.json())
      .then(data => {
        if (data.status === "complete") {
          completeScanProgress(data.networks || []);
        } else {
          pollScanResult();
        }
      })
      .catch(err => {
        console.warn("[WiFi Scan Error]", err);
        showScanError("Gagal menghubungi ESP32.");
      });
  }

  function pollScanResult() {
    if (pollTimer) clearTimeout(pollTimer);
    pollTimer = setTimeout(() => {
      fetch("/api/v1/wifi/scan", { cache: "no-store" })
        .then(res => res.json())
        .then(data => {
          if (data.status === "complete") {
            completeScanProgress(data.networks || []);
          } else {
            pollScanResult();
          }
        })
        .catch(() => {
          showScanError("Koneksi terputus saat memindai.");
        });
    }, 700);
  }

  function completeScanProgress(networks) {
    stopScanningTimers();
    updateProgressUI(100);

    // Beri jeda 250ms agar animasi mencapai 100% sebelum menampilkan daftar
    setTimeout(() => {
      if (progressContainer) progressContainer.style.display = "none";
      renderWifiList(networks);
    }, 280);
  }

  function renderWifiList(networks) {
    isScanning = false;
    if (btnOpen) btnOpen.classList.remove("scanning");
    if (btnRescan) btnRescan.disabled = false;

    if (!listContainer) return;

    if (!networks || networks.length === 0) {
      if (statusEl) statusEl.textContent = "Tidak ada Wi-Fi ditemukan";
      listContainer.innerHTML = `
        <div style="text-align:center;padding:32px 16px;color:var(--text-dim);">
          <div style="font-size:1.4rem;margin-bottom:8px;">🔍</div>
          <div>Tidak ada jaringan Wi-Fi yang tertangkap.</div>
          <div style="font-size:0.8rem;margin-top:6px;color:var(--text-muted);">Pastikan access point berada dalam jangkauan sinyal.</div>
        </div>
      `;
      return;
    }

    if (statusEl) statusEl.textContent = `Ditemukan ${networks.length} jaringan`;

    let html = "";
    networks.forEach(net => {
      const rssi = net.rssi;
      let badgeClass = "strong";
      if (rssi < -75) {
        badgeClass = "weak";
      } else if (rssi < -60) {
        badgeClass = "medium";
      }

      // Lock SVG icon
      const lockIcon = net.secure
        ? `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#f59e0b" stroke-width="2" title="Terproteksi Password"><rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>`
        : `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#10b981" stroke-width="2" title="Jaringan Terbuka (Open)"><rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/></svg>`;

      // Signal waves SVG
      const signalBars = `<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor"><path d="M12 18a2 2 0 1 0 0-4 2 2 0 0 0 0 4z"/><path d="M8.5 13.5a5 5 0 0 1 7 0" fill="none" stroke="currentColor" stroke-width="2"/><path d="M5 10a10 10 0 0 1 14 0" fill="none" stroke="currentColor" stroke-width="2"/></svg>`;

      html += `
        <div class="wifi-item" data-ssid="${escapeHtml(net.ssid)}">
          <div class="wifi-info">
            <span style="display:inline-flex;align-items:center;">${lockIcon}</span>
            <span class="wifi-ssid-text">${escapeHtml(net.ssid)}</span>
          </div>
          <div class="wifi-meta">
            <span class="wifi-rssi-badge ${badgeClass}">
              ${signalBars}
              <span>${rssi} dBm</span>
            </span>
          </div>
        </div>
      `;
    });

    listContainer.innerHTML = html;

    // Attach click event to all items
    listContainer.querySelectorAll(".wifi-item").forEach(item => {
      item.addEventListener("click", function() {
        const selectedSsid = this.getAttribute("data-ssid");
        if (inputSsid) {
          inputSsid.value = selectedSsid;
          inputSsid.dispatchEvent(new Event("change"));
        }
        closeModal();
        if (inputPass) {
          inputPass.focus();
        }
      });
    });
  }

  function showScanError(msg) {
    isScanning = false;
    stopScanningTimers();
    if (btnOpen) btnOpen.classList.remove("scanning");
    if (btnRescan) btnRescan.disabled = false;
    if (progressContainer) progressContainer.style.display = "none";
    if (statusEl) statusEl.textContent = "Gagal memindai";
    if (listContainer) {
      listContainer.innerHTML = `
        <div style="text-align:center;padding:28px 16px;color:#f87171;">
          <div style="font-size:1.3rem;margin-bottom:6px;">⚠️</div>
          <div>${msg}</div>
        </div>
      `;
    }
  }

  function escapeHtml(str) {
    if (!str) return "";
    return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }
});


// =============================================================================
// HOLD-TO-REVEAL PASSWORD TOGGLE MODULE
// =============================================================================
function initPasswordToggles() {
  document.querySelectorAll(".btn-toggle-pwd").forEach(btn => {
    if (btn.dataset.pwdBound) return;
    btn.dataset.pwdBound = "true";

    const wrapper = btn.closest(".password-wrapper");
    if (!wrapper) return;
    const input = wrapper.querySelector("input");
    if (!input) return;

    function reveal(e) {
      if (e) e.preventDefault();
      input.type = "text";
      btn.classList.add("revealed");
    }

    function conceal(e) {
      if (e) e.preventDefault();
      input.type = "password";
      btn.classList.remove("revealed");
    }

    // Pointer events support mouse hold, touch hold on mobile, and stylus
    btn.addEventListener("pointerdown", reveal);
    btn.addEventListener("pointerup", conceal);
    btn.addEventListener("pointercancel", conceal);
    btn.addEventListener("pointerleave", conceal);

    // Keyboard navigation: hold Space or Enter to peek
    btn.addEventListener("keydown", function(e) {
      if (e.key === " " || e.key === "Enter") {
        reveal(e);
      }
    });
    btn.addEventListener("keyup", function(e) {
      if (e.key === " " || e.key === "Enter") {
        conceal(e);
      }
    });

    btn.addEventListener("click", function(e) {
      e.preventDefault();
    });
  });
}

document.addEventListener("DOMContentLoaded", initPasswordToggles);

// =================================================================
// AP MODE WATCHER & BANNER (1-Minute Inactivity Auto-Off & Stop Button)
// =================================================================
let apCountdownInterval = null;
let currentApRemainingSec = 60;

function bindApStopButton() {
  const btnStop = document.getElementById("btnStopAp");
  const banner = document.getElementById("apModeBanner");
  if (!btnStop || btnStop.dataset.bound) return;
  btnStop.dataset.bound = "true";

  btnStop.addEventListener("click", function(e) {
    e.preventDefault();
    if (typeof Swal !== "undefined") {
      Swal.fire({
        title: "Nonaktifkan Mode AP?",
        text: "Hotspot konfigurasi akan dimatikan dan indikator LED akan kembali ke status operasional normal.",
        icon: "warning",
        showCancelButton: true,
        confirmButtonColor: "#f97316",
        cancelButtonColor: "#64748b",
        confirmButtonText: "Ya, Matikan AP!",
        cancelButtonText: "Batal"
      }).then(res => {
        if (res.isConfirmed) {
          executeStopAp(banner);
        }
      });
    } else {
      if (confirm("Nonaktifkan Mode Hotspot AP?")) {
        executeStopAp(banner);
      }
    }
  });
}

function executeStopAp(banner) {
  if (typeof Swal !== "undefined") {
    Swal.fire({
      title: "Menonaktifkan...",
      text: "Mematikan Hotspot SoftAP...",
      allowOutsideClick: false,
      showConfirmButton: false,
      didOpen: () => Swal.showLoading()
    });
  }

  fetch("/api/v1/system/config-ap/stop", { method: "POST" })
    .then(r => r.json().catch(() => ({ status: "ok" })))
    .then(resData => {
      handleStopSuccess(banner, (resData && resData.message) ? resData.message : "Mode AP telah dinonaktifkan.");
    })
    .catch(err => {
      // Karena mematikan AP otomatis memutus sambungan Wi-Fi ke 192.168.4.1,
      // network disconnection menandakan AP berhasil dimatikan!
      handleStopSuccess(banner, "Hotspot AP berhasil dinonaktifkan. Perangkat kembali ke mode normal.");
    });
}

function handleStopSuccess(banner, msg) {
  if (typeof Swal !== "undefined") {
    Swal.fire({
      title: "Hotspot Dinonaktifkan",
      text: msg,
      icon: "success",
      confirmButtonColor: "#0284c7"
    });
  } else {
    alert(msg);
  }
  if (banner) banner.style.display = "none";
  if (apCountdownInterval) clearInterval(apCountdownInterval);
}

function updateApTimerDisplay() {
  const timerEl = document.getElementById("apBannerTimer");
  if (timerEl) {
    timerEl.textContent = `${currentApRemainingSec}s`;
  }
}

function startApCountdown(remainSec) {
  currentApRemainingSec = remainSec;
  updateApTimerDisplay();

  if (apCountdownInterval) clearInterval(apCountdownInterval);
  apCountdownInterval = setInterval(() => {
    if (currentApRemainingSec > 0) {
      currentApRemainingSec--;
      updateApTimerDisplay();
    } else {
      clearInterval(apCountdownInterval);
      const timerEl = document.getElementById("apBannerTimer");
      if (timerEl) timerEl.textContent = "Off";
      const banner = document.getElementById("apModeBanner");
      setTimeout(() => {
        if (banner) banner.style.display = "none";
      }, 2000);
    }
  }, 1000);
}

let lastApTouchTime = 0;
let apActivityListenersInitialized = false;

function touchApActivity(forceSync = false) {
  const isApHost = (location.hostname === "192.168.4.1" || location.hostname.startsWith("192.168.4."));
  const banner = document.getElementById("apModeBanner");
  const isVisible = banner && banner.style.display !== "none";
  if (!isApHost && !isVisible) return;

  const now = Date.now();
  // Immediately reset local UI counter to 60s for immediate feedback if it has decremented
  if (currentApRemainingSec < 55 || forceSync) {
    currentApRemainingSec = 60;
    updateApTimerDisplay();
  }

  // Throttle touch network request to once every 12 seconds
  if (now - lastApTouchTime < 12000 && !forceSync) {
    return;
  }
  lastApTouchTime = now;

  fetch("/api/v1/system/config-ap/touch", { method: "POST" })
    .then(res => res.json())
    .then(data => {
      if (data && data.ap_remain_sec !== undefined) {
        currentApRemainingSec = data.ap_remain_sec;
        updateApTimerDisplay();
      }
    })
    .catch(() => {});
}

function initApUserActivityListeners() {
  if (apActivityListenersInitialized) return;
  apActivityListenersInitialized = true;

  const events = ["mousedown", "keydown", "input", "touchstart", "scroll"];
  events.forEach(evt => {
    window.addEventListener(evt, () => touchApActivity(false), { passive: true });
  });

  // Periodic heartbeat every 20 seconds if user has page active and visible
  setInterval(() => {
    if (!document.hidden) {
      touchApActivity(false);
    }
  }, 20000);
}

function initApModeWatcher() {
  let banner = document.getElementById("apModeBanner");
  const isApHost = (location.hostname === "192.168.4.1" || location.hostname.startsWith("192.168.4."));

  // Jika diakses via IP AP 192.168.4.1, langsung munculkan banner tanpa menunggu fetch!
  if (isApHost && banner) {
    banner.style.display = "flex";
  }

  bindApStopButton();
  initApUserActivityListeners();

  // Fetch status dari server untuk sinkronisasi SSID & sisa waktu countdown
  fetch("/api/v1/system/version")
    .then(res => res.json())
    .then(data => {
      if (data && data.ap_active) {
        if (!banner) {
          const content = document.querySelector(".content");
          if (content) {
            banner = document.createElement("div");
            banner.id = "apModeBanner";
            banner.className = "ap-banner-card";
            banner.style.marginBottom = "20px";
            banner.innerHTML = `
              <div class="ap-banner-left">
                <div class="ap-pulse-ring"><span class="ap-pulse-dot"></span></div>
                <div class="ap-banner-text">
                  <div class="ap-banner-title">Hotspot Mode Konfigurasi Aktif</div>
                  <div class="ap-banner-subtitle">
                    SSID: <strong id="apBannerSsid">${data.ap_ssid || "CONFIG-AP"}</strong> &bull; Auto-off idle: <span id="apBannerTimer" class="ap-timer-pill">60s</span>
                  </div>
                </div>
              </div>
              <div class="ap-banner-right">
                <button type="button" id="btnStopAp" class="btn-ap-stop" title="Matikan Hotspot Konfigurasi">
                  <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <line x1="1" y1="1" x2="23" y2="23"/>
                    <path d="M16.72 11.06A10.94 10.94 0 0 1 19 12.55"/>
                    <path d="M5 12.55a10.94 10.94 0 0 1 5.17-2.39"/>
                    <path d="M10.71 5.05A16 16 0 0 1 22.58 9"/>
                    <path d="M1.42 9a15.91 15.91 0 0 1 4.7-2.88"/>
                    <path d="M8.53 16.11a6 6 0 0 1 6.95 0"/>
                    <line x1="12" y1="20" x2="12.01" y2="20"/>
                  </svg>
                  <span>Nonaktifkan AP</span>
                </button>
              </div>
            `;
            content.insertBefore(banner, content.firstChild);
            bindApStopButton();
          }
        }

        if (banner) banner.style.display = "flex";
        const ssidEl = document.getElementById("apBannerSsid");
        if (ssidEl && data.ap_ssid) ssidEl.textContent = data.ap_ssid;
        startApCountdown(data.ap_remain_sec !== undefined ? data.ap_remain_sec : 60);
      } else {
        // AP tidak aktif — sembunyikan banner (selalu, tidak bergantung reboot_required)
        if (!isApHost && banner) banner.style.display = "none";
      }
      // Reboot banner diupdate secara independen
      if (data && data.reboot_required !== undefined) {
        updatePendingRebootBanner(!!data.reboot_required);
      }
    })
    .catch(err => {
      console.warn("[AP WATCHER] Status fetch warning:", err);
      if (isApHost) {
        if (banner) banner.style.display = "flex";
        bindApStopButton();
        startApCountdown(60);
      }
    });
}

// =================================================================
// GLOBAL SESSION KEEPALIVE & SLIDING COOKIE RENEWAL
// =================================================================
function initGlobalSessionKeepalive() {
  const isLoginPage = (location.pathname.endsWith("login") || location.pathname.endsWith("login.html"));
  if (isLoginPage) return;

  // Heartbeat setiap 45 detik selama tab browser terbuka
  setInterval(() => {
    if (!document.hidden) {
      fetch("/api/v1/auth/check", { cache: "no-store" })
        .then(res => {
          if (res.status === 401) {
            console.warn("[SESSION] Sesi kedaluwarsa di server.");
          }
        })
        .catch(() => {});
    }
  }, 45000);
}

// Inisialisasi aman: jalankan langsung jika DOM sudah siap, atau listen DOMContentLoaded
if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", () => {
    initApModeWatcher();
    initGlobalSessionKeepalive();
  });
} else {
  initApModeWatcher();
  initGlobalSessionKeepalive();
}

// =================================================================
// PENDING REBOOT & NETWORK ROLLBACK HANDLER (INDUSTRIAL SAFETY)
// =================================================================
function triggerRebootNow() {
  Swal.fire({
    title: "Reboot Gateway?",
    text: "Perangkat akan memulai ulang untuk menerapkan pengaturan jaringan baru.",
    icon: "warning",
    showCancelButton: true,
    confirmButtonText: "Ya, Reboot Sekarang",
    cancelButtonText: "Batal",
    confirmButtonColor: "#10b981",
    cancelButtonColor: "#64748b"
  }).then(res => {
    if (res.isConfirmed) {
      fetch("/api/v1/system/reboot", { method: "POST" });
      Swal.fire({
        title: "Me-reboot...",
        text: "Gateway sedang memulai ulang. Halaman akan dialihkan ke Login.",
        icon: "info",
        allowOutsideClick: false,
        showConfirmButton: false,
        timer: 6000
      }).then(() => {
        window.location.href = "/login.html";
      });
    }
  });
}

function discardPendingNetworkChanges() {
  Swal.fire({
    title: "Batalkan Perubahan Jaringan?",
    text: "Pengaturan jaringan yang belum di-reboot akan dikembalikan ke konfigurasi aktif saat ini.",
    icon: "question",
    showCancelButton: true,
    confirmButtonText: "Ya, Batalkan Perubahan",
    cancelButtonText: "Kembali",
    confirmButtonColor: "#ef4444",
    cancelButtonColor: "#64748b"
  }).then(res => {
    if (res.isConfirmed) {
      Swal.fire({
        title: "Sedang Membatalkan...",
        allowOutsideClick: false,
        showConfirmButton: false,
        didOpen: () => Swal.showLoading()
      });

      fetch("/api/v1/config/network/discard", { method: "POST" })
        .then(r => r.json())
        .then(d => {
          Swal.close();
          const banner = document.getElementById("pendingRebootBanner");
          if (banner) banner.style.display = "none";
          const detailText = `Pengaturan jaringan yang belum di-reboot telah dibatalkan kembali ke mode ${d.reverted_mode}${d.reverted_ssid ? ` (${d.reverted_ssid})` : ''}.\n\n✓ Kredensial Login Admin/Teknisi dan MQTT Anda tetap aman tersimpan.`;
          const detailHtml = `<div style="text-align:left; font-size:0.875rem; line-height:1.6; color:#475569;">
              <p>Pengaturan jaringan yang belum di-reboot telah dibatalkan kembali ke status aktif:</p>
              <ul style="margin: 8px 0; padding-left: 20px;">
                <li><b>Mode Jaringan:</b> ${d.reverted_mode}</li>
                ${d.reverted_ssid ? `<li><b>Wi-Fi SSID:</b> ${d.reverted_ssid}</li>` : ''}
              </ul>
              <p style="margin-top:10px; color:#059669; font-weight:600;">✓ Kredensial Login Admin/Teknisi dan MQTT Anda tetap aman tersimpan.</p>
            </div>`;
          Swal.fire({
            title: "Perubahan Jaringan Dibatalkan",
            text: detailText,
            html: detailHtml,
            icon: "success",
            confirmButtonText: "OK",
            confirmButtonColor: "#0284c7"
          }).then(() => {
            location.reload();
          });
        })
        .catch(() => {
          Swal.close();
          Swal.fire("Error", "Gagal membatalkan perubahan jaringan.", "error");
        });
    }
  });
}

function updatePendingRebootBanner(rebootRequired) {
  let banner = document.getElementById("pendingRebootBanner");
  if (!banner && rebootRequired) {
    const content = document.querySelector(".content");
    if (content) {
      banner = document.createElement("div");
      banner.id = "pendingRebootBanner";
      banner.className = "pending-reboot-banner";
      banner.innerHTML = `
        <div class="pending-banner-text">
          <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" style="min-width:22px;min-height:22px;width:22px;height:22px;flex-shrink:0;">
            <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
            <line x1="12" y1="9" x2="12" y2="13"/>
            <line x1="12" y1="17" x2="12.01" y2="17"/>
          </svg>
          <span>Perubahan jaringan tersimpan di NVS. Reboot diperlukan untuk mengaktifkan ke hardware.</span>
        </div>
        <div class="pending-banner-actions">
          <button type="button" class="btn-banner-reboot" onclick="triggerRebootNow()">Reboot Sekarang</button>
          <button type="button" class="btn-banner-discard" onclick="discardPendingNetworkChanges()">Batalkan</button>
        </div>
      `;
      content.insertBefore(banner, content.firstChild);
    }
  }
  if (banner) {
    banner.style.display = rebootRequired ? "flex" : "none";
  }
}
