/**
 * Cubeacon Gateway - System & Data Logs JS
 * Dual-Engine Realtime Auditor:
 * 1. Network & System Event Logs (Persistent Filesystem, Max 50)
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
  const kpiMqttHost = document.getElementById("kpiMqttHost");
  const kpiMqttPort = document.getElementById("kpiMqttPort");
  const kpiMqttDc = document.getElementById("kpiMqttDc");

  const kpiBootCount = document.getElementById("kpiBootCount");
  const kpiResetReason = document.getElementById("kpiResetReason");
  const kpiBootStorageBadge = document.getElementById("kpiBootStorageBadge");

  const kpiLogCount = document.getElementById("kpiLogCount");
  const countAllNet = document.getElementById("countAllNet");
  const lastUpdatedTimeNet = document.getElementById("lastUpdatedTimeNet");

  // Header Persistent Telemetry Uptime
  const lifetimeUptimeVal = document.getElementById("lifetimeUptimeVal");
  const sessionUptimeVal = document.getElementById("sessionUptimeVal");

  // DOM Elements - Drop KPI (4-Pillar MECE Enterprise Architecture)
  const kpiDropRssi = document.getElementById("kpiDropRssi");
  const kpiDropPolicy = document.getElementById("kpiDropPolicy");
  const kpiDropUuid = document.getElementById("kpiDropUuid");
  const kpiDropMajor = document.getElementById("kpiDropMajor");
  const kpiDropUnknown = document.getElementById("kpiDropUnknown");
  const kpiDropBuffer = document.getElementById("kpiDropBuffer");
  const kpiDropOvr = document.getElementById("kpiDropOvr");
  const kpiDropTtl = document.getElementById("kpiDropTtl");
  const badgeBufferLoss = document.getElementById("badgeBufferLoss");
  const kpiDropMqtt = document.getElementById("kpiDropMqtt");
  const kpiDropMqttSub = document.getElementById("kpiDropMqttSub");
  const badgeMqttLoss = document.getElementById("badgeMqttLoss");
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

  function formatUptime(sec) {
    if (sec === undefined || sec === null || isNaN(sec)) return "--";
    sec = Math.max(0, Math.floor(sec));
    const d = Math.floor(sec / 86400);
    const remD = sec % 86400;
    const h = Math.floor(remD / 3600);
    const remH = remD % 3600;
    const m = Math.floor(remH / 60);
    const s = remH % 60;
    if (d > 0) {
      return `${d}d ${String(h).padStart(2, '0')}h`;
    }
    if (h > 0) {
      return `${h}h ${String(m).padStart(2, '0')}m`;
    }
    return `${m}m ${String(s).padStart(2, '0')}s`;
  }

  function updateHeaderUptime(data) {
    if (!data) return;
    const sSec = (data.session_uptime_sec !== undefined) ? data.session_uptime_sec : data.uptime_sec;
    if (sessionUptimeVal && sSec !== undefined) {
      sessionUptimeVal.textContent = formatUptime(sSec);
    }
    const lSec = (data.lifetime_uptime_sec !== undefined) ? data.lifetime_uptime_sec : sSec;
    if (lifetimeUptimeVal && lSec !== undefined) {
      lifetimeUptimeVal.textContent = formatUptime(lSec);
    }
  }

  function formatCompact(num) {
    if (num === undefined || num === null || isNaN(num)) return "0";
    num = Math.max(0, Math.floor(num));
    if (num < 1000) return num.toString();

    if (num >= 1000000000) {
      const b = num / 1000000000;
      if (b >= 100) return Math.round(b) + "B";
      const s = b.toFixed(1);
      return (s.endsWith(".0") ? Math.round(b) : s) + "B";
    } else if (num >= 1000000) {
      const m = num / 1000000;
      if (Math.round(m) >= 1000) return "1B";
      if (m >= 100) return Math.round(m) + "M";
      const s = m.toFixed(1);
      return (s.endsWith(".0") ? Math.round(m) : s) + "M";
    } else {
      const k = num / 1000;
      if (Math.round(k) >= 1000) return "1M";
      if (k >= 100) return Math.round(k) + "K";
      const s = k.toFixed(1);
      return (s.endsWith(".0") ? Math.round(k) : s) + "K";
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
        updateHeaderUptime(data);

        // Wi-Fi KPI
        const isWifiConnected = (data.wifi_status === "connected");
        if (textWifiStatus) textWifiStatus.textContent = isWifiConnected ? "CONNECTED" : "DISCONNECTED";
        if (kpiWifiBadge) kpiWifiBadge.className = "kpi-badge " + (isWifiConnected ? "badge-green" : "badge-red");
        if (dotWifi) dotWifi.className = "pulse-dot-sm " + (isWifiConnected ? "green" : "red");
        if (kpiWifiSsid) kpiWifiSsid.textContent = isWifiConnected ? (data.wifi_ssid || "Wi-Fi Active") : "Offline";
        if (kpiWifiIp) kpiWifiIp.textContent = isWifiConnected ? (data.wifi_ip || "-") : "-";
        if (kpiWifiDc) kpiWifiDc.textContent = data.wifi_disconnects !== undefined ? data.wifi_disconnects : 0;

        // Ethernet KPI
        const isEthConnected = (data.eth_status === "connected");
        const isEthLinkUp = (data.eth_status === "link_up" || isEthConnected);
        const isEthHwDetected = (data.eth_status !== "uninstalled");

        if (textEthStatus) {
          if (isEthConnected) {
            textEthStatus.textContent = "CONNECTED";
          } else if (isEthLinkUp) {
            textEthStatus.textContent = "LINK UP";
          } else if (!isEthHwDetected) {
            textEthStatus.textContent = "NOT DETECTED";
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
            kpiEthLink.textContent = "Hardware Disconnected";
          } else {
            kpiEthLink.textContent = "Cable Disconnected";
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

        // Total Gateway Reboot
        if (kpiBootCount) kpiBootCount.textContent = (data.boot_count !== undefined) ? data.boot_count : 1;
        if (kpiResetReason) kpiResetReason.textContent = data.reset_reason || "Power-On Cold Boot";
        if (kpiBootStorageBadge) kpiBootStorageBadge.textContent = data.eeprom_detected ? "EEPROM" : "RAM";

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
        console.warn("[LOG] Failed to fetch network logs:", err);
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
        updateHeaderUptime(data);

        const sum = data.summary || {};
        // Card 1: RF Signal Filter
        const rssiDropped = sum.rssi_min || 0;
        if (kpiDropRssi) {
          kpiDropRssi.textContent = formatCompact(rssiDropped);
          kpiDropRssi.title = rssiDropped.toLocaleString() + " pkt";
        }

        // Card 2: Target & Format Filter
        const policyTotal = (sum.uuid_mismatch || 0) + (sum.major_mismatch || 0) + (sum.unknown_format || 0);
        if (kpiDropPolicy) {
          kpiDropPolicy.textContent = formatCompact(policyTotal);
          kpiDropPolicy.title = policyTotal.toLocaleString() + " pkt";
        }
        if (kpiDropUuid) {
          kpiDropUuid.textContent = formatCompact(sum.uuid_mismatch || 0);
          kpiDropUuid.title = (sum.uuid_mismatch || 0).toLocaleString();
        }
        if (kpiDropMajor) {
          kpiDropMajor.textContent = formatCompact(sum.major_mismatch || 0);
          kpiDropMajor.title = (sum.major_mismatch || 0).toLocaleString();
        }
        if (kpiDropUnknown) {
          kpiDropUnknown.textContent = formatCompact(sum.unknown_format || 0);
          kpiDropUnknown.title = (sum.unknown_format || 0).toLocaleString();
        }

        // Card 3: Buffer & Queue Loss
        const bufferLoss = (sum.queue_overflow || 0) + (sum.ttl_expired || 0);
        if (kpiDropBuffer) {
          kpiDropBuffer.textContent = formatCompact(bufferLoss);
          kpiDropBuffer.title = bufferLoss.toLocaleString() + " pkt";
        }
        if (kpiDropOvr) {
          kpiDropOvr.textContent = formatCompact(sum.queue_overflow || 0);
          kpiDropOvr.title = (sum.queue_overflow || 0).toLocaleString();
        }
        if (kpiDropTtl) {
          kpiDropTtl.textContent = formatCompact(sum.ttl_expired || 0);
          kpiDropTtl.title = (sum.ttl_expired || 0).toLocaleString();
        }
        if (badgeBufferLoss) {
          if (bufferLoss === 0) {
            badgeBufferLoss.className = "kpi-badge badge-emerald";
            badgeBufferLoss.innerHTML = '<span class="pulse-dot-sm emerald"></span>Healthy (0 Loss)';
          } else {
            badgeBufferLoss.className = "kpi-badge badge-rose";
            badgeBufferLoss.innerHTML = '<span class="pulse-dot-sm rose"></span>Backpressure';
          }
        }

        // Card 4: Uplink Dispatch Loss
        const mqttLoss = (sum.mqtt_failed !== undefined) ? sum.mqtt_failed : 0;
        if (kpiDropMqtt) {
          kpiDropMqtt.textContent = formatCompact(mqttLoss);
          kpiDropMqtt.title = mqttLoss.toLocaleString() + " pkt";
        }
        if (kpiDropMqttSub) {
          kpiDropMqttSub.textContent = formatCompact(mqttLoss);
          kpiDropMqttSub.title = mqttLoss.toLocaleString();
        }
        if (badgeMqttLoss) {
          const isMqttConnected = (data.mqtt_connected !== false);
          if (isMqttConnected) {
            badgeMqttLoss.className = "kpi-badge badge-emerald";
            badgeMqttLoss.innerHTML = '<span class="pulse-dot-sm emerald"></span>Broker Connected';
          } else {
            badgeMqttLoss.className = "kpi-badge badge-rose";
            badgeMqttLoss.innerHTML = '<span class="pulse-dot-sm rose"></span>Broker Disconnected';
          }
        }

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
        console.warn("[LOG] Failed to fetch drop logs:", err);
      });
  }

  function fetchCurrentTabLogs() {
    if (currentTab === "net") {
      return fetchNetLogs();
    } else {
      return fetchDropLogs();
    }
  }

  // Pagination Component
  function renderPagination(containerEl, infoEl, currentPage, totalItems, onPageChange) {
    if (!containerEl || !infoEl) return;
    if (totalItems <= 0) {
      infoEl.innerHTML = `Showing <strong>0 - 0</strong> of <strong>0</strong> entries`;
      containerEl.innerHTML = "";
      return;
    }

    const totalPages = Math.ceil(totalItems / PAGE_SIZE) || 1;
    if (currentPage > totalPages) currentPage = totalPages;
    if (currentPage < 1) currentPage = 1;

    const startIdx = (currentPage - 1) * PAGE_SIZE + 1;
    const endIdx = Math.min(currentPage * PAGE_SIZE, totalItems);

    infoEl.innerHTML = `Showing <strong>${startIdx} - ${endIdx}</strong> of <strong>${totalItems}</strong> entries`;

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
            <div style="font-weight: 600; color: #cbd5e1; margin-bottom: 4px;">No Events Found</div>
            <div style="font-size: 0.8rem;">${allNetLogs.length === 0 ? "No event records stored in Filesystem." : "No log entries match the current filter or search criteria."}</div>
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

  function formatSpacedHex(hexStr) {
    if (!hexStr || hexStr.length < 2) return "-";
    const clean = hexStr.replace(/\s+/g, "").toUpperCase();
    return clean.match(/.{1,2}/g)?.join(" ") || clean;
  }

  function convertRaw42ToMqtt44(rawHex42, timeStr) {
    if (!rawHex42) return "";
    const clean = String(rawHex42).replace(/[^0-9A-Fa-f]/g, "").toUpperCase();
    if (clean.length < 84) return clean;

    let epochSec = Math.floor(Date.now() / 1000);
    if (timeStr && timeStr.includes(" ")) {
      const parsedDate = new Date(timeStr.replace(/-/g, "/"));
      if (!isNaN(parsedDate.getTime())) {
        epochSec = Math.floor(parsedDate.getTime() / 1000);
      }
    }
    const epochHex = epochSec.toString(16).padStart(8, "0").toUpperCase();

    // 42B UART: [Header 1B (0..2)][MAC 6B (2..14)][PDU 1B (14..16)][RSSI 1B (16..18)][LEN 1B (18..20)][Payload 31B (20..82)][CRC 1B (82..84)]
    // 44B MQTT: [Epoch 4B (0..8)][MAC 6B (8..20)][PDU 1B (20..22)][RSSI 1B (22..24)][LEN 1B (24..26)][Payload 31B (26..88)]
    const mac = clean.substring(2, 14);
    const pdu = clean.substring(14, 16);
    const rssi = clean.substring(16, 18);
    const len = clean.substring(18, 20);
    const pay = clean.substring(20, 82);

    return epochHex + mac + pdu + rssi + len + pay;
  }

  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text);
    }
    return new Promise((resolve, reject) => {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.cssText = "position:fixed;opacity:0;top:0;left:0;pointer-events:none;";
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      try {
        const success = document.execCommand("copy");
        ta.remove();
        if (success) resolve();
        else reject(new Error("execCommand failed"));
      } catch (err) {
        ta.remove();
        reject(err);
      }
    });
  }

  let toastTimerLog = null;
  function showToastLog(msg) {
    const t = document.getElementById("uiToast");
    if (!t) return;
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(toastTimerLog);
    toastTimerLog = setTimeout(() => {
      t.classList.remove("show");
    }, 1500);
  }

  // 4. Render Dropped Beacon Table (8 Kolom Presisi Termasuk MAC & Raw 44B)
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
               (item.mac && item.mac.toLowerCase().includes(q)) ||
               (item.code && item.code.toLowerCase().includes(q)) ||
               (item.tag && item.tag.toLowerCase().includes(q)) ||
               (item.raw && item.raw.toLowerCase().includes(q)) ||
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
          <td colspan="8" class="empty-state">
            <svg class="empty-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
              <circle cx="12" cy="12" r="10"/>
              <line x1="4.93" y1="4.93" x2="19.07" y2="19.07"/>
            </svg>
            <div style="font-weight: 600; color: #cbd5e1; margin-bottom: 4px;">No Dropped Packets Recorded</div>
            <div style="font-size: 0.8rem;">${allDropLogs.length === 0 ? "No beacon frames dropped during this session." : "No dropped records match the current filter."}</div>
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
      const hasRaw = item.raw && item.raw.length >= 10;
      const frame44 = hasRaw ? convertRaw42ToMqtt44(item.raw, item.time) : "";
      const spacedHex = frame44 ? formatSpacedHex(frame44) : "";

      html += `
        <tr>
          <td style="color:#94a3b8; font-weight:700; font-family:monospace;">#${item.id}</td>
          <td><span class="time-mono">${escapeHtml(item.time)}</span></td>
          <td><span class="mono-mac">${escapeHtml(item.mac || "-")}</span></td>
          <td>${typeBadge}</td>
          <td><span class="tag-mono">${escapeHtml(item.tag)}</span></td>
          <td>${rssiDisplay}</td>
          <td>
            <span class="badge-reason ${statusClass}">
              <span class="pulse-dot-sm ${statusClass}"></span>
              ${escapeHtml(item.reason_str || "Drop")}
            </span>
          </td>
          <td style="text-align: center;">
            ${hasRaw ? `<button type="button" class="btn-raw-hex" data-act="toggle-log-raw" data-id="${item.id}">Raw Hex</button>` : `<span style="color:#64748b;">-</span>`}
          </td>
        </tr>
      `;

      if (hasRaw) {
        html += `
          <tr id="log-drawer-${item.id}" class="raw-drawer">
            <td colspan="8">
              <div class="full-hex-string" style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
                <span style="font-family:var(--mono, monospace); font-size:0.75rem;"><strong>Raw 44B:</strong> ${spacedHex}</span>
                <button type="button" class="btn-raw-hex" data-act="copy-raw" data-raw="${frame44}" style="flex:none;">Salin Hex</button>
              </div>
            </td>
          </tr>
        `;
      }
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
      if (labelClearLogs) labelClearLogs.textContent = "Clear Logs";

      renderCurrentTable();
      fetchNetLogs();
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
      if (labelClearLogs) labelClearLogs.textContent = "Clear Logs";

      renderCurrentTable();
      fetchDropLogs();
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

  // Auto-Refresh Logic (Hemat RAM & Tab-Aware)
  function startAutoRefresh() {
    stopAutoRefresh();
    refreshInterval = setInterval(() => {
      if (!document.hidden && autoRefreshActive) {
        fetchCurrentTabLogs();
      }
    }, 8000);
  }

  function stopAutoRefresh() {
    if (refreshInterval) {
      clearInterval(refreshInterval);
      refreshInterval = null;
    }
  }

  // Tangguhkan auto-refresh jika tab diminimize, lanjutkan instan saat dibuka kembali
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden && autoRefreshActive) {
      fetchCurrentTabLogs();
    }
  });

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
      fetchCurrentTabLogs();
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
          title: "Purge Network & System Logs?",
          text: "All network and system event records (up to 50 entries) will be permanently erased from Filesystem.",
          icon: "warning",
          showCancelButton: true,
          confirmButtonText: "Confirm Purge",
          cancelButtonText: "Cancel",
          confirmButtonColor: "#ef4444",
          cancelButtonColor: "#64748b"
        }).then(res => {
          if (res.isConfirmed) {
            Swal.fire({ title: "Purging Logs...", allowOutsideClick: false, didOpen: () => Swal.showLoading() });
            fetch("/api/v1/system/net-logs/clear", { method: "POST" })
              .then(r => r.json())
              .then(resData => {
                Swal.close();
                Swal.fire("Success", resData.message || "Network logs successfully purged.", "success");
                fetchNetLogs();
              })
              .catch(err => {
                Swal.close();
                Swal.fire("Failed", "An error occurred while purging network logs.", "error");
              });
          }
        });
      } else {
        Swal.fire({
          title: "Purge Dropped Frame Telemetry?",
          text: "All 50 dropped packet records in the RAM buffer will be cleared.",
          icon: "warning",
          showCancelButton: true,
          confirmButtonText: "Confirm Purge",
          cancelButtonText: "Cancel",
          confirmButtonColor: "#ef4444",
          cancelButtonColor: "#64748b"
        }).then(res => {
          if (res.isConfirmed) {
            Swal.fire({ title: "Clearing Buffer...", allowOutsideClick: false, didOpen: () => Swal.showLoading() });
            fetch("/api/v1/system/drop-logs/clear", { method: "POST" })
              .then(r => r.json())
              .then(resData => {
                Swal.close();
                Swal.fire("Success", resData.message || "Telemetry buffer successfully cleared.", "success");
                fetchDropLogs();
              })
              .catch(err => {
                Swal.close();
                Swal.fire("Failed", "An error occurred while clearing telemetry buffer.", "error");
              });
          }
        });
      }
    });
  }

  // Click listener for raw hex drawers & copy in log table
  document.addEventListener("click", function(e) {
    const btnToggle = e.target.closest("[data-act='toggle-log-raw']");
    if (btnToggle) {
      const id = btnToggle.getAttribute("data-id");
      const drawer = document.getElementById("log-drawer-" + id);
      if (drawer) {
        drawer.classList.toggle("open");
      }
      return;
    }

    const btnCopy = e.target.closest("[data-act='copy-raw']");
    if (btnCopy) {
      const raw = btnCopy.getAttribute("data-raw");
      if (raw) {
        copyText(raw).then(() => {
          showToastLog("Raw 44B Disalin ke Clipboard!");
        }).catch(() => {
          showToastLog("Gagal menyalin data raw");
        });
      }
    }
  });

  // Inisialisasi awal (Hanya muat tab aktif pertama kali demi efisiensi DRAM)
  fetchCurrentTabLogs();
  startAutoRefresh();
});
