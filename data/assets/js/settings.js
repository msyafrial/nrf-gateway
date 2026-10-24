/**
 * IOTARU ESP32 Gateway (SG-10) - Device Settings Controller
 * Modern 2026 Ergonomics: Centered Attached Minus RSSI, Instant Clamping, Exact Slider Bounds
 */

document.addEventListener("DOMContentLoaded", function () {
  const form = document.getElementById("settingsValidation");
  if (!form) return;

  // Form Elements
  const filterUuidToggle = document.getElementById("filterUuidToggle");
  const filterUuidEnHidden = document.getElementById("filter_uuid_en");
  const uuidContainer = document.getElementById("uuidContainer");
  const filterUuidInput = document.getElementById("filter_uuid");
  const uuidBadge = document.getElementById("uuidBadge");
  const btnPasteUuid = document.getElementById("btnPasteUuid");

  const filterMajorToggle = document.getElementById("filterMajorToggle");
  const filterMajorEnHidden = document.getElementById("filter_major_en");
  const majorContainer = document.getElementById("majorContainer");
  const filterMajorInput = document.getElementById("filter_major");
  const majorBadge = document.getElementById("majorBadge");

  const rssiPill = document.getElementById("rssiPill");
  const rssiInputAbs = document.getElementById("rssi_input_abs");
  const rssiSlider = document.getElementById("rssi_slider");
  const rssiMinHidden = document.getElementById("rssi_min");

  const restartScheduleToggle = document.getElementById("restartScheduleToggle");
  const restartScheduleContainer = document.getElementById("restartScheduleContainer");
  const restartScheduleInput = document.getElementById("restart_schedule");

  const floatingSaveDock = document.getElementById("floatingSaveDock");
  const btnCancelChanges = document.getElementById("btnCancelChanges");

  let lastSavedSchedule = restartScheduleInput ? restartScheduleInput.value : "";
  const initialValues = {};

  function recordInitialValues() {
    form.querySelectorAll("input:not([type='button']), select").forEach(el => {
      const key = el.id || el.name;
      if (key) {
        if (el.type === "checkbox") {
          initialValues[key] = el.checked;
        } else {
          initialValues[key] = el.value;
        }
      }
    });
  }

  // 1. Filter UUID Toggle Switch Sync
  function syncUuidToggle(fromUser) {
    if (!filterUuidToggle || !filterUuidEnHidden) return;
    if (fromUser) {
      filterUuidEnHidden.value = filterUuidToggle.checked ? "1" : "0";
    }
    const enabled = filterUuidToggle.checked;
    if (enabled) {
      if (uuidContainer) uuidContainer.classList.remove("dimmed");
      if (filterUuidInput) {
        filterUuidInput.disabled = false;
        filterUuidInput.required = true;
      }
      if (btnPasteUuid) btnPasteUuid.disabled = false;
    } else {
      if (uuidContainer) uuidContainer.classList.add("dimmed");
      if (filterUuidInput) {
        filterUuidInput.disabled = true;
        filterUuidInput.required = false;
      }
      if (btnPasteUuid) btnPasteUuid.disabled = true;
    }
  }

  if (filterUuidToggle) {
    filterUuidToggle.addEventListener("change", function () {
      syncUuidToggle(true);
      checkDirty();
    });
  }

  // 2. UUID Hex Validator
  function validateUuidHex() {
    if (!filterUuidInput || !uuidBadge) return;
    let val = filterUuidInput.value.replace(/[^0-9A-Fa-f]/g, "").toUpperCase();
    filterUuidInput.value = val;

    const len = val.length;
    if (len === 32) {
      uuidBadge.className = "uuid-badge valid";
      uuidBadge.textContent = "32/32 Hex Valid";
    } else if (len === 0) {
      uuidBadge.className = "uuid-badge neutral";
      uuidBadge.textContent = "0/32 Hex";
    } else {
      uuidBadge.className = "uuid-badge invalid";
      uuidBadge.textContent = len + "/32 Hex";
    }
  }

  if (filterUuidInput) {
    filterUuidInput.addEventListener("input", function () {
      validateUuidHex();
      checkDirty();
    });
  }

  if (btnPasteUuid && filterUuidInput) {
    btnPasteUuid.addEventListener("click", async function () {
      try {
        if (navigator.clipboard && navigator.clipboard.readText) {
          const text = await navigator.clipboard.readText();
          if (text) {
            const clean = text.replace(/[^0-9A-Fa-f]/g, "").toUpperCase().slice(0, 32);
            filterUuidInput.value = clean;
            validateUuidHex();
            checkDirty();
            if (typeof Swal !== "undefined") {
              Swal.fire({
                toast: true,
                position: "top-end",
                icon: "success",
                title: "UUID ditempel dari clipboard",
                showConfirmButton: false,
                timer: 1500
              });
            }
            return;
          }
        }
      } catch (err) {
        console.warn("Clipboard access:", err);
      }

      const manual = prompt("Tempel teks UUID di sini:");
      if (manual) {
        const clean = manual.replace(/[^0-9A-Fa-f]/g, "").toUpperCase().slice(0, 32);
        filterUuidInput.value = clean;
        validateUuidHex();
        checkDirty();
      }
    });
  }

  // 2.5 Filter Major Toggle & Validator (Max 5 Digits & Max 65535)
  function syncMajorToggle(fromUser) {
    if (!filterMajorToggle || !filterMajorEnHidden) return;
    if (fromUser) {
      filterMajorEnHidden.value = filterMajorToggle.checked ? "1" : "0";
    }
    const enabled = filterMajorToggle.checked;
    if (enabled) {
      if (majorContainer) majorContainer.classList.remove("dimmed");
      if (filterMajorInput) {
        filterMajorInput.disabled = false;
        filterMajorInput.required = true;
      }
    } else {
      if (majorContainer) majorContainer.classList.add("dimmed");
      if (filterMajorInput) {
        filterMajorInput.disabled = true;
        filterMajorInput.required = false;
      }
    }
  }

  if (filterMajorToggle) {
    filterMajorToggle.addEventListener("change", function () {
      syncMajorToggle(true);
      checkDirty();
    });
  }

  function validateMajorFilter() {
    if (!filterMajorInput || !majorBadge) return;
    let val = filterMajorInput.value.replace(/[^0-9]/g, "");
    if (val.length > 5) val = val.slice(0, 5);

    if (val.length > 0) {
      let num = parseInt(val, 10);
      if (num > 65535) {
        val = "65535";
        num = 65535;
      }
      filterMajorInput.value = val;
      majorBadge.className = "uuid-badge valid";
      majorBadge.textContent = "Major " + num + " Valid";
    } else {
      filterMajorInput.value = "";
      majorBadge.className = "uuid-badge neutral";
      majorBadge.textContent = "0 - 65535";
    }
  }

  if (filterMajorInput) {
    filterMajorInput.addEventListener("input", function () {
      validateMajorFilter();
      checkDirty();
    });
  }

  // 3. RSSI Slider & Centered Attached-Minus Input with Auto-Clamp (<30 -> 30, >100 -> 100)
  function syncInputWidth(el) {
    if (!el) return;
    const len = (el.value || "80").length;
    el.style.width = len + "ch";
  }

  function updateRssiState(absVal, source) {
    let val = parseInt(absVal, 10);
    if (isNaN(val)) val = 80;
    if (val < 30) val = 30;
    if (val > 100) val = 100;

    // ESP32 backend expects negative RSSI
    if (rssiMinHidden) rssiMinHidden.value = -val;

    // Update input box if source was slider
    if (source !== "input" && rssiInputAbs) {
      rssiInputAbs.value = val;
      syncInputWidth(rssiInputAbs);
    }

    // Update slider if source was input
    if (source !== "slider" && rssiSlider) {
      rssiSlider.value = val;
    }

    // Exact track fill calculation
    if (rssiSlider) {
      const pct = ((val - 30) / (100 - 30)) * 100;
      rssiSlider.style.background = `linear-gradient(to right, #0284c7 0%, #38bdf8 ${pct}%, #1e293b ${pct}%, #1e293b 100%)`;
    }
  }

  if (rssiPill && rssiInputAbs) {
    rssiPill.addEventListener("click", function () {
      rssiInputAbs.focus();
    });
  }

  if (rssiSlider) {
    rssiSlider.addEventListener("input", function () {
      updateRssiState(this.value, "slider");
      checkDirty();
    });
  }

  let clampTimer = null;

  function clampRssiInput(el) {
    let rawStr = el.value.replace(/[^0-9]/g, "");
    let num = parseInt(rawStr, 10);
    if (isNaN(num) || num < 30) {
      num = 30;
    } else if (num > 100) {
      num = 100;
    }
    el.value = num;
    updateRssiState(num, "input");
    syncInputWidth(el);
    checkDirty();
  }

  if (rssiInputAbs) {
    syncInputWidth(rssiInputAbs);

    rssiInputAbs.addEventListener("input", function () {
      clearTimeout(clampTimer);
      let rawStr = this.value.replace(/[^0-9]/g, "");
      if (rawStr === "") {
        this.style.width = "2ch";
        return;
      }
      let num = parseInt(rawStr, 10);

      // Auto clamp > 100 immediately back to 100
      if (num > 100) {
        this.value = "100";
        updateRssiState(100, "input");
        syncInputWidth(this);
        checkDirty();
        return;
      }

      // If user typed 2 digits that can never be 100 (e.g. 20, 25, 15) and < 30:
      if (rawStr.length >= 2 && rawStr !== "10" && num < 30) {
        this.value = "30";
        updateRssiState(30, "input");
        syncInputWidth(this);
        checkDirty();
        return;
      }

      // If valid range 30..100, sync immediately
      if (num >= 30 && num <= 100) {
        updateRssiState(num, "input");
        syncInputWidth(this);
        checkDirty();
      } else {
        // Debounce single digits or '10' so user can finish typing
        clampTimer = setTimeout(() => {
          clampRssiInput(rssiInputAbs);
        }, 700);
      }
    });

    rssiInputAbs.addEventListener("blur", function () {
      clearTimeout(clampTimer);
      clampRssiInput(this);
    });

    rssiInputAbs.addEventListener("change", function () {
      clearTimeout(clampTimer);
      clampRssiInput(this);
    });

    rssiInputAbs.addEventListener("keydown", function (e) {
      if (e.key === "Enter") {
        e.preventDefault();
        this.blur();
      }
    });
  }

  // 4. Daily Reboot Schedule Toggle
  function syncRestartToggle(fromUser) {
    if (!restartScheduleToggle || !restartScheduleInput) return;
    const enabled = restartScheduleToggle.checked;
    if (enabled) {
      if (restartScheduleContainer) restartScheduleContainer.classList.remove("dimmed");
      if (!restartScheduleInput.value) {
        restartScheduleInput.value = lastSavedSchedule || "03:00";
      }
    } else {
      if (restartScheduleContainer) restartScheduleContainer.classList.add("dimmed");
      if (restartScheduleInput.value) {
        lastSavedSchedule = restartScheduleInput.value;
      }
      restartScheduleInput.value = "";
    }
  }

  if (restartScheduleToggle) {
    restartScheduleToggle.addEventListener("change", function () {
      syncRestartToggle(true);
      checkDirty();
    });
  }

  // Stepper buttons
  document.querySelectorAll(".stepper-btn").forEach(btn => {
    btn.addEventListener("click", function () {
      setTimeout(checkDirty, 50);
    });
  });

  // 5. Dirty-State Tracker & Floating Save Dock
  function isFormDirty() {
    let dirty = false;
    form.querySelectorAll("input:not([type='button']), select").forEach(el => {
      const key = el.id || el.name;
      if (key && key in initialValues) {
        if (el.type === "checkbox") {
          if (el.checked !== initialValues[key]) dirty = true;
        } else {
          if (el.value !== initialValues[key]) dirty = true;
        }
      }
    });
    return dirty;
  }

  function checkDirty() {
    if (!floatingSaveDock) return;
    if (isFormDirty()) {
      floatingSaveDock.style.display = "block";
    } else {
      floatingSaveDock.style.display = "none";
    }
  }

  form.addEventListener("input", checkDirty);
  form.addEventListener("change", checkDirty);

  // Rollback on Cancel
  if (btnCancelChanges) {
    btnCancelChanges.addEventListener("click", function () {
      form.querySelectorAll("input:not([type='button']), select").forEach(el => {
        const key = el.id || el.name;
        if (key && key in initialValues) {
          if (el.type === "checkbox") {
            el.checked = initialValues[key];
          } else {
            el.value = initialValues[key];
          }
        }
      });

      syncUuidToggle(false);
      validateUuidHex();
      syncMajorToggle(false);
      validateMajorFilter();
      if (rssiMinHidden && rssiMinHidden.value) {
        updateRssiState(Math.abs(parseInt(rssiMinHidden.value, 10)), "rollback");
      }
      syncRestartToggle(false);
      checkDirty();

      if (ntpTestBadge) {
        ntpTestBadge.className = "uuid-badge neutral";
        ntpTestBadge.textContent = "Siap diuji";
      }
      if (ntpResultBox) {
        ntpResultBox.style.display = "none";
      }

      if (typeof Swal !== "undefined") {
        Swal.fire({
          toast: true,
          position: "top-end",
          icon: "info",
          title: "Perubahan dibatalkan",
          showConfirmButton: false,
          timer: 1500
        });
      }
    });
  }

  // 6. NTP Server Probe Tester
  const btnTestNtp = document.getElementById("btnTestNtp");
  const ntpServerInput = document.getElementById("ntp_server");
  const ntpTestBadge = document.getElementById("ntpTestBadge");
  const ntpResultBox = document.getElementById("ntpResultBox");

  if (btnTestNtp && ntpServerInput) {
    btnTestNtp.addEventListener("click", function () {
      let targetServer = (ntpServerInput.value || "").trim();
      if (!targetServer) {
        targetServer = "pool.ntp.org";
        ntpServerInput.value = targetServer;
      }

      btnTestNtp.disabled = true;
      const originalHtml = btnTestNtp.innerHTML;
      btnTestNtp.innerHTML = '<span class="spinner-inline"></span><span>Menguji...</span>';

      if (ntpTestBadge) {
        ntpTestBadge.className = "uuid-badge testing";
        ntpTestBadge.textContent = "Menguji...";
      }
      if (ntpResultBox) {
        ntpResultBox.style.display = "none";
      }

      fetch("/api/v1/system/test-ntp", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "server=" + encodeURIComponent(targetServer)
      })
      .then(function (res) {
        if (!res.ok) throw new Error("HTTP " + res.status);
        return res.json();
      })
      .then(function (data) {
        btnTestNtp.disabled = false;
        btnTestNtp.innerHTML = originalHtml;

        if (data.success) {
          if (ntpTestBadge) {
            ntpTestBadge.className = "uuid-badge valid";
            ntpTestBadge.textContent = (data.rtt_ms !== undefined ? data.rtt_ms + " ms" : "OK");
          }
          if (ntpResultBox) {
            ntpResultBox.className = "ntp-result-feedback success";
            ntpResultBox.innerHTML =
              '<strong>✓ Terhubung</strong> &bull; IP: <code>' + (data.ip || '-') + '</code> &bull; RTT: <code>' + data.rtt_ms + ' ms</code><br>' +
              '<span style="font-size:0.78rem;opacity:0.9;">Waktu Server: ' + (data.server_time || '-') + '</span>';
            ntpResultBox.style.display = "block";
          }
          if (typeof Swal !== "undefined") {
            Swal.fire({
              toast: true,
              position: "top-end",
              icon: "success",
              title: "NTP Aktif (" + data.rtt_ms + " ms)",
              showConfirmButton: false,
              timer: 2500
            });
          }
        } else {
          if (ntpTestBadge) {
            ntpTestBadge.className = "uuid-badge invalid";
            ntpTestBadge.textContent = "Gagal";
          }
          if (ntpResultBox) {
            ntpResultBox.className = "ntp-result-feedback error";
            ntpResultBox.innerHTML = '<strong>✗ Uji Gagal:</strong> ' + (data.message || "Server tidak merespons");
            ntpResultBox.style.display = "block";
          }
          if (typeof Swal !== "undefined") {
            Swal.fire({
              toast: true,
              position: "top-end",
              icon: "error",
              title: data.message || "Uji NTP Gagal",
              showConfirmButton: false,
              timer: 3500
            });
          }
        }
      })
      .catch(function (err) {
        btnTestNtp.disabled = false;
        btnTestNtp.innerHTML = originalHtml;
        if (ntpTestBadge) {
          ntpTestBadge.className = "uuid-badge invalid";
          ntpTestBadge.textContent = "Error";
        }
        if (ntpResultBox) {
          ntpResultBox.className = "ntp-result-feedback error";
          ntpResultBox.innerHTML = '<strong>✗ Terjadi Kesalahan:</strong> ' + (err.message || "Koneksi gateway terputus");
          ntpResultBox.style.display = "block";
        }
        if (typeof Swal !== "undefined") {
          Swal.fire({
            toast: true,
            position: "top-end",
            icon: "error",
            title: "Gagal memproses pengujian",
            showConfirmButton: false,
            timer: 2500
          });
        }
      });
    });
  }

  // Initial sync for toggles & inputs
  syncUuidToggle(false);
  validateUuidHex();
  syncMajorToggle(false);
  validateMajorFilter();
  syncRestartToggle(false);

  // Initialize RSSI Slider & Pill state on page load
  const initialRssi = rssiInputAbs && rssiInputAbs.value ? rssiInputAbs.value : (rssiSlider ? rssiSlider.value : 80);
  updateRssiState(initialRssi, "init");

  // Record initial values
  recordInitialValues();
  if (rssiInputAbs) syncInputWidth(rssiInputAbs);
});
