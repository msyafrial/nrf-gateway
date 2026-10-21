/**
 * IOTARU SG-10 Gateway - System & Data Logs JS
 * Dual-Engine Realtime Auditor:
 * 1. Network & System Event Logs (Persistent LittleFS, Max 50)
 * 2. Dropped Beacon Data Logs (In-RAM FIFO Ring Buffer, Max 50)
 */

document.addEventListener("DOMContentLoaded", function () {
  let currentTab = "net"; // "net" | "drop"

  let allNetLogs = [];
  let allDropLogs = [];

  let activeNetCategory = "all";
  let activeDropReason = "all";
  let searchQuery = "";

  let autoRefreshActive = true;
  let refreshInterval = null;

  // DOM Elements - Tab Switcher
  const tabNetLog = document.getElementById("tabNetLog");
  const tabDropLog = document.getElementById("tabDropLog");
  const gridKpiNet = document.getElementById("gridKpiNet");
  const gridKpiDrop = document.getElementById("gridKpiDrop");
  const categoryFilterNet = document.getElementById("categoryFilterNet");
  const categoryFilterDrop = document.getElementById("categoryFilterDrop");
  const wrapTableNet = document.getElementById("wrapTableNet");
  const wrapTableDrop = document.getElementById("wrapTableDrop");
  const badgeCountNet = document.getElementById("badgeCountNet");
  const badgeCountDrop = document.getElementById("badgeCountDrop");

  // DOM Elements - Net KPI
  const kpiWifiBadge = document.getElementById("kpiWifiBadge");
  const textWifiStatus = document.getElementById("textWifiStatus");
  const dotWifi = document.getElementById("dotWifi");
  const kpiWifiSsid = document.getElementById("kpiWifiSsid");
  const kpiWifiIp = document.getElementById("kpiWifiIp");
  const kpiWifiDc = document.getElementById("kpiWifiDc");

  const kpiEthBadge = document.getElementById("kpiEthBadge");
  const textEthStatus = document.getElementById("textEthStatus");
  const dotEth = document.getElementById("dotEth");
  const kpiEthLink = document.getElementById("kpiEthLink");
  const kpiEthIp = document.getElementById("kpiEthIp");
  const kpiEthDc = document.getElementById("kpiEthDc");

  const kpiMqttBadge = document.getElementById("kpiMqttBadge");
  const textMqttStatus = document.getElementById("textMqttStatus");
  const dotMqtt = document.getElementById("dotMqtt");
  const kpiMqttState = document.getElementById("kpiMqttState");
  const kpiMqttHost = document.getElementById("kpiMqttHost");
  const kpiMqttPort = document.getElementById("kpiMqttPort");
  const kpiMqttDc = document.getElementById("kpiMqttDc");

  const kpiBootCount = document.getElementById("kpiBootCount");
  const kpiResetReason = document.getElementById("kpiResetReason");
  const kpiBootStorageBadge = document.getElementById("kpiBootStorageBadge");

  const kpiLogCount = document.getElementById("kpiLogCount");
  const countAllNet = document.getElementById("countAllNet");
  const lastUpdatedTimeNet = document.getElementById("lastUpdatedTimeNet");

  // DOM Elements - Drop KPI
  const kpiDropTotal = document.getElementById("kpiDropTotal");
  const kpiDropTtl = document.getElementById("kpiDropTtl");
  const kpiDropOvr = document.getElementById("kpiDropOvr");
  const kpiDropRssi = document.getElementById("kpiDropRssi");
  const kpiDropFilter = document.getElementById("kpiDropFilter");
  const kpiDropUuid = document.getElementById("kpiDropUuid");
  const kpiDropMajor = document.getElementById("kpiDropMajor");
  const kpiDropMqtt = document.getElementById("kpiDropMqtt");
  const countAllDrop = document.getElementById("countAllDrop");
  const lastUpdatedTimeDrop = document.getElementById("lastUpdatedTimeDrop");

  // Pagination Elements & State
  const PAGE_SIZE = 10;
  let currentPageNet = 1;
  let currentPageDrop = 1;

  const pageInfoNet = document.getElementById("pageInfoNet");
  const pageControlsNet = document.getElementById("pageControlsNet");
  const pageInfoDrop = document.getElementById("pageInfoDrop");
  const pageControlsDrop = document.getElementById("pageControlsDrop");

  // Tables & Controls
  const netLogTableBody = document.getElementById("netLogTableBody");
  const dropLogTableBody = document.getElementById("dropLogTableBody");
  const logSearchInput = document.getElementById("logSearchInput");
  const displayedLogCount = document.getElementById("displayedLogCount");
  const totalLogCount = document.getElementById("totalLogCount");

  const btnAutoRefresh = document.getElementById("btnAutoRefresh");
  const dotAutoRefresh = document.getElementById("dotAutoRefresh");
  const btnManualRefresh = document.getElementById("btnManualRefresh");
  const btnExportCsv = document.getElementById("btnExportCsv");
  const btnClearLogs = document.getElementById("btnClearLogs");
  const labelExportCsv = document.getElementById("labelExportCsv");
  const labelClearLogs = document.getElementById("labelClearLogs");

  function escapeHtml(text) {
    if (!text && text !== 0) return "";
    return String(text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }

  function getCatClass(cat) {
    if (!cat) return "";
    const lower = cat.toLowerCase();
    if (lower.includes("wi-fi") || lower.includes("wifi")) return "wifi";
    if (lower.includes("eth")) return "ethernet";
    if (lower.includes("mqtt")) return "mqtt";
    if (lower.includes("ap") || lower.includes("softap")) return "softap";
    if (lower.includes("sys")) return "system";
    return "";
  }

  function getRssiColorClass(rssi) {
    if (rssi >= -65) return "rssi-strong";
    if (rssi >= -80) return "rssi-medium";
    return "rssi-weak";
  }

  function getBeaconTypeBadge(code) {
    const safeCode = escapeHtml(code || "BLE");
    const lower = safeCode.toLowerCase();
    if (lower.includes("ext")) {
      return `<span class="beacon-type-pill ibeacon-ext">ibeacon_ext</span>`;
    } else if (lower.includes("mqtt")) {
      return `<span class="beacon-type-pill mqtt">MQTT</span>`;
    } else {
      return `<span class="beacon-type-pill ibeacon">${safeCode}</span>`;
    }
  }

  // 1. Fetching Network Logs
  function fetchNetLogs() {
    return fetch("/api/v1/system/net-logs", { cache: "no-store" })
      .then(res => {
        if (!res.ok) throw new Error("HTTP error " + res.status);
        return res.json();
      })
      .then(data => {
        // Wi-Fi KPI
        const isWifiConnected = (data.wifi_status === "connected");
        if (textWifiStatus) textWifiStatus.textContent = isWifiConnected ? "TERHUBUNG" : "TERPUTUS";
        if (kpiWifiBadge) kpiWifiBadge.className = "kpi-badge " + (isWifiConnected ? "badge-green" : "badge-red");
        if (dotWifi) dotWifi.className = "pulse-dot-sm " + (isWifiConnected ? "green" : "red");
        if (kpiWifiSsid) kpiWifiSsid.textContent = isWifiConnected ? (data.wifi_ssid || "Wi-Fi Aktif") : "Offline";
        if (kpiWifiIp) kpiWifiIp.textContent = isWifiConnected ? (data.wifi_ip || "-") : "-";
        if (kpiWifiDc) kpiWifiDc.textContent = data.wifi_disconnects !== undefined ? data.wifi_disconnects : 0;

        // Ethernet KPI
        const isEthConnected = (data.eth_status === "connected");
        const isEthLinkUp = (data.eth_status === "link_up" || isEthConnected);
        const isEthHwDetected = (data.eth_status !== "uninstalled");

        if (textEthStatus) {
          if (isEthConnected) {
            textEthStatus.textContent = "TERHUBUNG";
          } else if (isEthLinkUp) {
            textEthStatus.textContent = "LINK UP";
          } else if (!isEthHwDetected) {
            textEthStatus.textContent = "TIDAK TERPASANG";
          } else {
            textEthStatus.textContent = "LINK DOWN";
          }
        }
        if (kpiEthBadge) {
          kpiEthBadge.className = "kpi-badge " + (isEthLinkUp ? "badge-green" : "badge-red");
        }
        if (dotEth) {
          dotEth.className = "pulse-dot-sm " + (isEthLinkUp ? "green" : "red");
        }
        if (kpiEthLink) {
          if (isEthLinkUp) {
            kpiEthLink.textContent = "100 Mbps Full-Duplex";
          } else if (!isEthHwDetected) {
            kpiEthLink.textContent = "Modul SPI Disconnect";
          } else {
            kpiEthLink.textContent = "Kabel Terputus";
          }
        }
        if (kpiEthIp) kpiEthIp.textContent = isEthConnected ? (data.eth_ip || "-") : "-";
        if (kpiEthDc) kpiEthDc.textContent = data.eth_disconnects !== undefined ? data.eth_disconnects : 0;

        // MQTT KPI
        const isMqttOnline = (data.mqtt_status === "online");
        if (textMqttStatus) textMqttStatus.textContent = isMqttOnline ? "ONLINE" : "OFFLINE";
        if (kpiMqttBadge) kpiMqttBadge.className = "kpi-badge " + (isMqttOnline ? "badge-green" : "badge-red");
        if (dotMqtt) dotMqtt.className = "pulse-dot-sm " + (isMqttOnline ? "green" : "red");
        if (kpiMqttHost) kpiMqttHost.textContent = data.mqtt_host ? data.mqtt_host : "-";
        const isSsl = (data.mqtt_ssl === true);
        const kpiMqttTitle = document.getElementById("kpiMqttTitle");
        if (kpiMqttTitle) kpiMqttTitle.textContent = isSsl ? "MQTTS Broker" : "MQTT Broker";
        if (kpiMqttPort) kpiMqttPort.textContent = (isSsl ? "🔒 " : "") + (data.mqtt_port || (isSsl ? 8883 : 1883));
        if (kpiMqttDc) kpiMqttDc.textContent = data.mqtt_disconnects !== undefined ? data.mqtt_disconnects : 0;

        // Total Reboot ESP32
        if (kpiBootCount) kpiBootCount.textContent = (data.boot_count !== undefined) ? data.boot_count : 1;
        if (kpiResetReason) kpiResetReason.textContent = data.reset_reason || "Power-On Cold Boot";
        if (kpiBootStorageBadge) kpiBootStorageBadge.textContent = data.eeprom_detected ? "AT24C256" : "RAM";

        // Counts
        const total = (data.total !== undefined) ? data.total : (data.logs ? data.logs.length : 0);
        if (kpiLogCount) kpiLogCount.textContent = total;
        if (countAllNet) countAllNet.textContent = total;
        if (badgeCountNet) badgeCountNet.textContent = total;

        const now = new Date();
        const timeStr = String(now.getHours()).padStart(2, '0') + ":" +
                        String(now.getMinutes()).padStart(2, '0') + ":" +
                        String(now.getSeconds()).padStart(2, '0');
        if (lastUpdatedTimeNet) lastUpdatedTimeNet.textContent = "Updated: " + timeStr;

        allNetLogs = data.logs || [];
        if (currentTab === "net") renderCurrentTable();
      })
      .catch(err => {
        console.warn("[LOG] Gagal mengambil net logs:", err);
      });
  }

  // 2. Fetching Drop Data Logs
  function fetchDropLogs() {
    return fetch("/api/v1/system/drop-logs", { cache: "no-store" })
      .then(res => {
        if (!res.ok) throw new Error("HTTP error " + res.status);
        return res.json();
      })
      .then(data => {
        const sum = data.summary || {};
        if (kpiDropTotal) kpiDropTotal.textContent = (sum.total_dropped || 0).toLocaleString();
        if (kpiDropTtl) kpiDropTtl.textContent = sum.ttl_expired || 0;
        if (kpiDropOvr) kpiDropOvr.textContent = sum.queue_overflow || 0;
        if (kpiDropRssi) kpiDropRssi.textContent = (sum.rssi_min || 0).toLocaleString() + " pkt";

        const filterMismatch = (sum.uuid_mismatch || 0) + (sum.major_mismatch || 0);
        if (kpiDropFilter) kpiDropFilter.textContent = filterMismatch.toLocaleString() + " pkt";
        if (kpiDropUuid) kpiDropUuid.textContent = sum.uuid_mismatch || 0;
        if (kpiDropMajor) kpiDropMajor.textContent = sum.major_mismatch || 0;

        if (kpiDropMqtt) kpiDropMqtt.textContent = (sum.mqtt_failed !== undefined) ? (sum.mqtt_failed || 0).toLocaleString() : "0";

        const count = data.total || (data.logs ? data.logs.length : 0);
        if (countAllDrop) countAllDrop.textContent = count;
        if (badgeCountDrop) badgeCountDrop.textContent = count;

        const now = new Date();
        const timeStr = String(now.getHours()).padStart(2, '0') + ":" +
                        String(now.getMinutes()).padStart(2, '0') + ":" +
                        String(now.getSeconds()).padStart(2, '0');
        if (lastUpdatedTimeDrop) lastUpdatedTimeDrop.textContent = "Updated: " + timeStr;

        allDropLogs = data.logs || [];
        if (currentTab === "drop") renderCurrentTable();
      })
      .catch(err => {
        console.warn("[LOG] Gagal mengambil drop logs:", err);
      });
  }

  function fetchAll() {
    return Promise.all([fetchNetLogs(), fetchDropLogs()]);
  }

  // Pagination Component
  function renderPagination(containerEl, infoEl, currentPage, totalItems, onPageChange) {
    if (!containerEl || !infoEl) return;
    if (totalItems <= 0) {
      infoEl.innerHTML = `Menampilkan <strong>0 - 0</strong> dari <strong>0</strong> data`;
      containerEl.innerHTML = "";
      return;
    }

    const totalPages = Math.ceil(totalItems / PAGE_SIZE) || 1;
    if (currentPage > totalPages) currentPage = totalPages;
    if (currentPage < 1) currentPage = 1;

    const startIdx = (currentPage - 1) * PAGE_SIZE + 1;
    const endIdx = Math.min(currentPage * PAGE_SIZE, totalItems);

    infoEl.innerHTML = `Menampilkan <strong>${startIdx} - ${endIdx}</strong> dari <strong>${totalItems}</strong> data`;

    let html = "";
    // Prev button
    html += `<button type="button" class="btn-page" ${currentPage <= 1 ? "disabled" : ""} data-page="${currentPage - 1}">‹ Prev</button>`;

    // Page numbers: tampilkan hingga 5 tombol halaman
    let startPage = Math.max(1, currentPage - 2);
    let endPage = Math.min(totalPages, startPage + 4);
    if (endPage - startPage < 4) {
      startPage = Math.max(1, endPage - 4);
    }

    if (startPage > 1) {
      html += `<button type="button" class="btn-page" data-page="1">1</button>`;
      if (startPage > 2) html += `<span style="color:#64748b; padding: 0 4px;">…</span>`;
    }

    for (let p = startPage; p <= endPage; p++) {
      html += `<button type="button" class="btn-page ${p === currentPage ? "active" : ""}" data-page="${p}">${p}</button>`;
    }

    if (endPage < totalPages) {
      if (endPage < totalPages - 1) html += `<span style="color:#64748b; padding: 0 4px;">…</span>`;
      html += `<button type="button" class="btn-page" data-page="${totalPages}">${totalPages}</button>`;
    }

    // Next button
    html += `<button type="button" class="btn-page" ${currentPage >= totalPages ? "disabled" : ""} data-page="${currentPage + 1}">Next ›</button>`;

    containerEl.innerHTML = html;

    containerEl.querySelectorAll(".btn-page").forEach(btn => {
      btn.addEventListener("click", function () {
        const p = parseInt(this.getAttribute("data-page"), 10);
        if (!isNaN(p) && p !== currentPage && p >= 1 && p <= totalPages) {
          onPageChange(p);
        }
      });
    });
  }

  // 3. Render Network Table
  function renderNetTable() {
    if (!netLogTableBody) return;

    let filtered = allNetLogs;
    if (activeNetCategory !== "all") {
      filtered = filtered.filter(item => item.cat && item.cat.toLowerCase() === activeNetCategory.toLowerCase());
    }

    if (searchQuery.trim() !== "") {
      const q = searchQuery.toLowerCase().trim();
      filtered = filtered.filter(item => {
        return (item.time && item.time.toLowerCase().includes(q)) ||
               (item.cat && item.cat.toLowerCase().includes(q)) ||
               (item.event && item.event.toLowerCase().includes(q)) ||
               (item.detail && item.detail.toLowerCase().includes(q));
      });
    }

    const totalFiltered = filtered.length;
    if (displayedLogCount) displayedLogCount.textContent = totalFiltered;
    if (totalLogCount) totalLogCount.textContent = allNetLogs.length;

    const totalPages = Math.ceil(totalFiltered / PAGE_SIZE) || 1;
    if (currentPageNet > totalPages) currentPageNet = totalPages;
    if (currentPageNet < 1) currentPageNet = 1;

    renderPagination(pageControlsNet, pageInfoNet, currentPageNet, totalFiltered, (p) => {
      currentPageNet = p;
      renderNetTable();
    });

    if (totalFiltered === 0) {
      netLogTableBody.innerHTML = `
        <tr>
          <td colspan="5" class="empty-state">
            <svg class="empty-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
              <path d="M9 12h6m-6 4h6m2 5H7a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5.5L19 7.5V19a2 2 0 0 1-2 2z"/>
            </svg>
            <div style="font-weight: 600; color: #cbd5e1; margin-bottom: 4px;">Tidak Ada Peristiwa Ditemukan</div>
            <div style="font-size: 0.8rem;">${allNetLogs.length === 0 ? "Belum ada rekaman peristiwa jaringan di LittleFS." : "Tidak ada log yang cocok dengan filter atau pencarian Anda."}</div>
          </td>
        </tr>
      `;
      return;
    }

    const pagedItems = filtered.slice((currentPageNet - 1) * PAGE_SIZE, currentPageNet * PAGE_SIZE);

    let html = "";
    pagedItems.forEach(item => {
      const catClass = getCatClass(item.cat);
      const statusClass = item.status_class || "neutral";

      html += `
        <tr>
          <td style="color:#94a3b8; font-weight:700; font-family:monospace;">#${item.id}</td>
          <td><span class="time-mono">${escapeHtml(item.time)}</span></td>
          <td><span class="net-cat-pill ${catClass}">${escapeHtml(item.cat)}</span></td>
          <td><span class="net-event-pill ${statusClass}">${escapeHtml(item.event)}</span></td>
          <td class="col-detail"><span class="detail-text">${escapeHtml(item.detail)}</span></td>
        </tr>
      `;
    });
    netLogTableBody.innerHTML = html;
  }

  // 4. Render Dropped Beacon Table (6 Kolom Bersih Tanpa Keterangan)
  function renderDropTable() {
    if (!dropLogTableBody) return;

    let filtered = allDropLogs;
    if (activeDropReason !== "all") {
      filtered = filtered.filter(item => String(item.reason) === String(activeDropReason));
    }

    if (searchQuery.trim() !== "") {
      const q = searchQuery.toLowerCase().trim();
      filtered = filtered.filter(item => {
        return (item.time && item.time.toLowerCase().includes(q)) ||
               (item.code && item.code.toLowerCase().includes(q)) ||
               (item.tag && item.tag.toLowerCase().includes(q)) ||
               (item.reason_str && item.reason_str.toLowerCase().includes(q));
      });
    }

    const totalFiltered = filtered.length;
    if (displayedLogCount) displayedLogCount.textContent = totalFiltered;
    if (totalLogCount) totalLogCount.textContent = allDropLogs.length;

    const totalPages = Math.ceil(totalFiltered / PAGE_SIZE) || 1;
    if (currentPageDrop > totalPages) currentPageDrop = totalPages;
    if (currentPageDrop < 1) currentPageDrop = 1;

    renderPagination(pageControlsDrop, pageInfoDrop, currentPageDrop, totalFiltered, (p) => {
      currentPageDrop = p;
      renderDropTable();
    });

    if (totalFiltered === 0) {
      dropLogTableBody.innerHTML = `
        <tr>
          <td colspan="6" class="empty-state">
            <svg class="empty-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
              <circle cx="12" cy="12" r="10"/>
              <line x1="4.93" y1="4.93" x2="19.07" y2="19.07"/>
            </svg>
            <div style="font-weight: 600; color: #cbd5e1; margin-bottom: 4px;">Tidak Ada Data Paket Drop</div>
            <div style="font-size: 0.8rem;">${allDropLogs.length === 0 ? "Belum ada paket beacon yang di-drop pada sesi ini." : "Tidak ada rekaman drop yang sesuai dengan filter."}</div>
          </td>
        </tr>
      `;
      return;
    }

    const pagedItems = filtered.slice((currentPageDrop - 1) * PAGE_SIZE, currentPageDrop * PAGE_SIZE);

    let html = "";
    pagedItems.forEach(item => {
      const typeBadge = getBeaconTypeBadge(item.code);
      const isMqtt = (item.code && item.code.toLowerCase() === "mqtt");
      const rssiDisplay = isMqtt ? `<span style="color:#64748b;">-</span>` : `<span class="rssi-val ${getRssiColorClass(item.rssi)}">${item.rssi} dBm</span>`;
      const statusClass = item.status_class || "neutral";

      html += `
        <tr>
          <td style="color:#94a3b8; font-weight:700; font-family:monospace;">#${item.id}</td>
          <td><span class="time-mono">${escapeHtml(item.time)}</span></td>
          <td>${typeBadge}</td>
          <td><span class="tag-mono">${escapeHtml(item.tag)}</span></td>
          <td>${rssiDisplay}</td>
          <td>
            <span class="badge-reason ${statusClass}">
              <span class="pulse-dot-sm ${statusClass}"></span>
              ${escapeHtml(item.reason_str || "Drop")}
            </span>
          </td>
        </tr>
      `;
    });
    dropLogTableBody.innerHTML = html;
  }

  function renderCurrentTable() {
    if (currentTab === "net") {
      renderNetTable();
    } else {
      renderDropTable();
    }
  }

  // 5. Master Tab Switching Logic
  if (tabNetLog && tabDropLog) {
    tabNetLog.addEventListener("click", function () {
      if (currentTab === "net") return;
      currentTab = "net";

      tabNetLog.classList.add("active");
      tabNetLog.setAttribute("aria-selected", "true");
      tabDropLog.classList.remove("active");
      tabDropLog.setAttribute("aria-selected", "false");

      if (gridKpiNet) gridKpiNet.style.display = "grid";
      if (gridKpiDrop) gridKpiDrop.style.display = "none";

      if (categoryFilterNet) categoryFilterNet.style.display = "inline-flex";
      if (categoryFilterDrop) categoryFilterDrop.style.display = "none";

      if (wrapTableNet) wrapTableNet.style.display = "block";
      if (wrapTableDrop) wrapTableDrop.style.display = "none";

      if (labelExportCsv) labelExportCsv.textContent = "Export CSV";
      if (labelClearLogs) labelClearLogs.textContent = "Hapus Log";

      renderCurrentTable();
    });

    tabDropLog.addEventListener("click", function () {
      if (currentTab === "drop") return;
      currentTab = "drop";

      tabDropLog.classList.add("active");
      tabDropLog.setAttribute("aria-selected", "true");
      tabNetLog.classList.remove("active");
      tabNetLog.setAttribute("aria-selected", "false");

      if (gridKpiNet) gridKpiNet.style.display = "none";
      if (gridKpiDrop) gridKpiDrop.style.display = "grid";

      if (categoryFilterNet) categoryFilterNet.style.display = "none";
      if (categoryFilterDrop) categoryFilterDrop.style.display = "inline-flex";

      if (wrapTableNet) wrapTableNet.style.display = "none";
      if (wrapTableDrop) wrapTableDrop.style.display = "block";

      if (labelExportCsv) labelExportCsv.textContent = "Export CSV";
      if (labelClearLogs) labelClearLogs.textContent = "Hapus Log";

      renderCurrentTable();
    });
  }

  // Filter Kategori: Net Log
  if (categoryFilterNet) {
    categoryFilterNet.addEventListener("click", function (e) {
      const btn = e.target.closest(".segment-btn");
      if (!btn) return;

      categoryFilterNet.querySelectorAll(".segment-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");

      activeNetCategory = btn.getAttribute("data-cat") || "all";
      currentPageNet = 1;
      renderNetTable();
    });
  }

  // Filter Kategori: Drop Log
  if (categoryFilterDrop) {
    categoryFilterDrop.addEventListener("click", function (e) {
      const btn = e.target.closest(".segment-btn");
      if (!btn) return;

      categoryFilterDrop.querySelectorAll(".segment-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");

      activeDropReason = btn.getAttribute("data-reason") || "all";
      currentPageDrop = 1;
      renderDropTable();
    });
  }

  // Search Input
  if (logSearchInput) {
    logSearchInput.addEventListener("input", function (e) {
      searchQuery = e.target.value;
      currentPageNet = 1;
      currentPageDrop = 1;
      renderCurrentTable();
    });
  }

  // Auto-Refresh Logic
  function startAutoRefresh() {
    stopAutoRefresh();
    refreshInterval = setInterval(fetchAll, 5000);
  }

  function stopAutoRefresh() {
    if (refreshInterval) {
      clearInterval(refreshInterval);
      refreshInterval = null;
    }
  }

  if (btnAutoRefresh) {
    btnAutoRefresh.addEventListener("click", function () {
      autoRefreshActive = !autoRefreshActive;
      if (autoRefreshActive) {
        btnAutoRefresh.classList.add("active");
        if (dotAutoRefresh) dotAutoRefresh.className = "pulse-dot-sm green";
        startAutoRefresh();
      } else {
        btnAutoRefresh.classList.remove("active");
        if (dotAutoRefresh) dotAutoRefresh.className = "pulse-dot-sm";
        stopAutoRefresh();
      }
    });
  }

  // Manual Refresh
  if (btnManualRefresh) {
    btnManualRefresh.addEventListener("click", function () {
      fetchAll();
    });
  }

  // Export CSV
  if (btnExportCsv) {
    btnExportCsv.addEventListener("click", function () {
      if (currentTab === "net") {
        window.location.href = "/api/v1/system/net-logs/export";
      } else {
        window.location.href = "/api/v1/system/drop-logs/export";
      }
    });
  }

  // Clear Logs
  if (btnClearLogs) {
    btnClearLogs.addEventListener("click", function () {
      if (currentTab === "net") {
        Swal.fire({
          title: "Hapus Log Jaringan?",
          text: "Seluruh riwayat peristiwa jaringan (maksimal 50 data) akan dihapus secara permanen dari LittleFS.",
          icon: "warning",
          showCancelButton: true,
          confirmButtonText: "Ya, Hapus!",
          cancelButtonText: "Batal",
          confirmButtonColor: "#ef4444",
          cancelButtonColor: "#64748b"
        }).then(res => {
          if (res.isConfirmed) {
            Swal.fire({ title: "Menghapus Log...", allowOutsideClick: false, didOpen: () => Swal.showLoading() });
            fetch("/api/v1/system/net-logs/clear", { method: "POST" })
              .then(r => r.json())
              .then(resData => {
                Swal.close();
                Swal.fire("Berhasil!", resData.message || "Log jaringan dibersihkan.", "success");
                fetchNetLogs();
              })
              .catch(err => {
                Swal.close();
                Swal.fire("Gagal", "Terjadi kesalahan saat menghapus log jaringan.", "error");
              });
          }
        });
      } else {
        Swal.fire({
          title: "Hapus Riwayat Drop Data?",
          text: "Seluruh 50 riwayat paket beacon yang terbuang/gagal pada buffer RAM akan dikosongkan.",
          icon: "warning",
          showCancelButton: true,
          confirmButtonText: "Ya, Hapus!",
          cancelButtonText: "Batal",
          confirmButtonColor: "#ef4444",
          cancelButtonColor: "#64748b"
        }).then(res => {
          if (res.isConfirmed) {
            Swal.fire({ title: "Mengosongkan Buffer...", allowOutsideClick: false, didOpen: () => Swal.showLoading() });
            fetch("/api/v1/system/drop-logs/clear", { method: "POST" })
              .then(r => r.json())
              .then(resData => {
                Swal.close();
                Swal.fire("Berhasil!", resData.message || "Buffer drop log dikosongkan.", "success");
                fetchDropLogs();
              })
              .catch(err => {
                Swal.close();
                Swal.fire("Gagal", "Terjadi kesalahan saat mengosongkan log drop data.", "error");
              });
          }
        });
      }
    });
  }

  // Inisialisasi awal
  fetchAll();
  startAutoRefresh();
});
