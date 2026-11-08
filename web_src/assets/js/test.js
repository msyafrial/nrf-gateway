/**
 * Cubeacon BLE Gateway Live Diagnostics & Monitor
 * High-Throughput Realtime Table & 60-Second Performance Telemetry Chart (Task Manager Style)
 */
document.addEventListener("DOMContentLoaded", function() {
  let ws = null;
  let isTesting = false;
  const maxHistory = 30; // Batas maksimal riwayat buffer (30 paket terbaru)
  let packetStreamHistory = []; // FIFO stream (terbaru di indeks 0)
  const uniqueBeaconsSet = new Set(); // Pelacak perangkat beacon unik
  const uniqueBeaconsMap = new Map(); // Pelacak detail perangkat unik { code, tag, rssi, batt, lastSeen, hits }
  const devicesMap = new Map(); // MAC -> device object { mac, name, pkts, seen, frames: [...] }
  let devices = []; // Array of device objects untuk perenderan & filter
  const open = new Set(); // MAC yang sedang terbuka (.card.open)
  const frOpen = new Set(); // Frame key yang sedang terbuka ("MAC#0", dll)
  const rawOpen = new Set(); // Raw drawer yang sedang tampil ("MAC#0", dll)

  // =================================================================
  // BLE BEACON MONITOR SCHEMA (13 PROTOCOL TYPES EXACT UI/UX)
  // =================================================================
  const SCHEMA = {
    moko_alarm:   { label: 'MOKO BXP Alarm',        color: '--moko',   fields: [['mode','Mode'],['status','Status'],['count','Count'],['deviceId','Device ID'],['firmware','Firmware'],['password','Password'],['name','Name']] },
    moko_sensor:  { label: 'MOKO BXP Sensor Info',  color: '--moko',   fields: [['battery','Battery'],['temp','Temp'],['accel','Accel'],['scale','Scale'],['threshold','Threshold'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power']] },
    moko_device:  { label: 'MOKO Device Info',      color: '--moko',   fields: [['battery','Battery'],['firmware','Firmware'],['lightSensor','Light sensor'],['light','Light'],['connectable','Connectable'],['interval','Interval'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power'],['name','Name']] },
    moko_acc:     { label: 'MOKO 3-Axis Acc',       color: '--moko',   fields: [['accel','Accel'],['scale','Scale'],['rate','Rate'],['threshold','Threshold'],['battery','Battery'],['interval','Interval'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power']] },
    moko_th:      { label: 'MOKO Temp & Humidity',  color: '--moko',   fields: [['temp','Temp'],['humidity','Humidity'],['battery','Battery'],['interval','Interval'],['ranging','Ranging'],['mac','MAC'],['txPower','Tx Power']] },
    moko_ibeacon: { label: 'MOKO iBeacon Info',     color: '--moko',   fields: [['interval','Interval'],['major','Major'],['minor','Minor'],['uuid','UUID'],['ranging','Ranging'],['txPower','Tx Power']] },
    ibeacon:      { label: 'iBeacon',               color: '--ibc',    fields: [['uuid','UUID'],['major','Major'],['minor','Minor'],['ranging','Ranging']] },
    eddy_uid:     { label: 'Eddystone-UID',         color: '--eddy',   fields: [['type','Type'],['namespace','Namespace'],['instance','Instance'],['ranging','Ranging']] },
    eddy_url:     { label: 'Eddystone-URL',         color: '--eddy',   fields: [['type','Type'],['url','URL'],['ranging','Ranging']] },
    eddy_tlm:     { label: 'Eddystone-TLM',         color: '--eddy',   fields: [['type','Type'],['battery','Battery'],['temp','Temp'],['advCount','Adv count'],['uptime','Uptime']] },
    minew_ext:    { label: 'Minew iBeacon_ext',     color: '--minew',  fields: [['uuid','UUID'],['major','Major'],['minor','Minor'],['battery','Battery'],['ranging','Ranging']] },
    minew_info:   { label: 'Minew Device Info',     color: '--minew',  fields: [['name','Name']] },
    raw:          { label: 'Raw BLE Data',          color: '--raw',    fields: [['advType','AdvType'],['length','Length'],['adType','AD Type']] }
  };

  const fmtAgo = s => s < 60 ? s + 's ago' : s < 3600 ? Math.floor(s/60) + 'm ' + (s%60) + 's ago' : Math.floor(s/3600) + 'h ' + Math.floor(s%3600/60) + 'm ago';
  const level = r => r > -60 ? 4 : r > -70 ? 3 : r > -80 ? 2 : 1;
  const tone = r => r > -70 ? 'var(--good)' : r > -82 ? 'var(--warn)' : 'var(--bad)';
  const bars = r => {
    const n = level(r);
    return `<span class="bars" style="color:${tone(r)}">${[1,2,3,4].map(i => `<span class="${i<=n?'on':''}"></span>`).join('')}</span>`;
  };
  const bestRssi = d => (d.frames && d.frames.length) ? Math.max(...d.frames.map(f => f.rssi)) : (d.rssi || -99);

  const ICON_COPY = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 0 1 2-2h9"/></svg>';
  const ICON_CHEV = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M9 6l6 6-6 6"/></svg>';
  const ICON_CHECK = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l5 5 9-10"/></svg>';
  const ICON_TRASH = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/></svg>';

  let activeTableTab = "fifo"; // "fifo" | "unique" | "dropped"
  let droppedBeaconsList = []; // FIFO stream for dropped beacons (strictly capped to 10 for enterprise high-rate durability)
  const DROPPED_MAX_ITEMS = 10;
  let activeDropFilterReason = "all"; // "all" | "0" | "1" | "2" | "3" | "4" | "5" | "6"
  let dropTableRenderScheduled = false;

  // Live dropped beacon accumulator for resilient real-time KPI synchronization
  let liveDropCounts = {
    ttl: 0,
    rssi: 0,
    uuid: 0,
    major: 0,
    unknown: 0,
    queue: 0,
    mqtt: 0
  };
  let lastReceivedDropSummary = {};
  let sessionStartDropSummary = null;

  function recordLiveDrop(reasonCode) {
    switch (Number(reasonCode)) {
      case 0: liveDropCounts.ttl++; break;
      case 1: liveDropCounts.rssi++; break;
      case 2: liveDropCounts.uuid++; break;
      case 3: liveDropCounts.major++; break;
      case 4: liveDropCounts.queue++; break;
      case 5: liveDropCounts.mqtt++; break;
      case 6: liveDropCounts.unknown++; break;
      default: liveDropCounts.unknown++; break;
    }
  }

  function scheduleDropTableRender() {
    if (dropTableRenderScheduled) return;
    dropTableRenderScheduled = true;
    requestAnimationFrame(() => {
      dropTableRenderScheduled = false;
      if (activeTableTab === "dropped") {
        renderTable();
      }
    });
  }
  let currentFilter = "";
  const UNIQUE_PER_PAGE = 10;
  let uniqueCurrentPage = 1;
  let cachedFilteredDevices = [];

  // DOM Elements
  const btnStart = document.getElementById("btnStartTest");
  const btnStop = document.getElementById("btnStopTest");
  const btnExportCsv = document.getElementById("btnExportCsv");
  const statusBadge = document.getElementById("testStatusBadge");
  const statusText = document.getElementById("testStatusText");
  const tableHead = document.getElementById("beaconTableHead");
  const tableBody = document.getElementById("beaconTableBody");
  const beaconTableWrapper = document.getElementById("beaconTableWrapper");
  const uniqueCardsList = document.getElementById("uniqueCardsList");
  const uniquePaginationBar = document.getElementById("uniquePaginationBar");
  const pagStart = document.getElementById("pagStart");
  const pagEnd = document.getElementById("pagEnd");
  const pagTotal = document.getElementById("pagTotal");
  const pagPageCurrent = document.getElementById("pagPageCurrent");
  const btnPagFirst = document.getElementById("btnPagFirst");
  const btnPagPrev = document.getElementById("btnPagPrev");
  const btnPagNext = document.getElementById("btnPagNext");
  const btnPagLast = document.getElementById("btnPagLast");
  const uiToast = document.getElementById("uiToast");
  const filterInput = document.getElementById("tagFilterInput");
  const tabFifo = document.getElementById("tabFifo");
  const tabUnique = document.getElementById("tabUnique");
  const tabDropped = document.getElementById("tabDropped");
  const dropSummaryGrid = document.getElementById("dropSummaryGrid");
  const toolbarLeftInfo = document.getElementById("toolbarLeftInfo");
  const totalDroppedDisplay = document.getElementById("totalDroppedDisplay");
  const countDropTotal = document.getElementById("count_drop_total");
  const rateDropTotal = document.getElementById("rate_drop_total");
  const countDropTtl = document.getElementById("count_drop_ttl");
  const rateDropTtl = document.getElementById("rate_drop_ttl");
  const countDropRssi = document.getElementById("count_drop_rssi");
  const rateDropRssi = document.getElementById("rate_drop_rssi");
  const countDropUuid = document.getElementById("count_drop_uuid");
  const rateDropUuid = document.getElementById("rate_drop_uuid");
  const countDropMajor = document.getElementById("count_drop_major");
  const rateDropMajor = document.getElementById("rate_drop_major");
  const countDropUnknown = document.getElementById("count_drop_unknown");
  const rateDropUnknown = document.getElementById("rate_drop_unknown");
  const countDropQueue = document.getElementById("count_drop_queue");
  const rateDropQueue = document.getElementById("rate_drop_queue");
  const countDropMqtt = document.getElementById("count_drop_mqtt");
  const rateDropMqtt = document.getElementById("rate_drop_mqtt");
  const dropActiveFilterBadge = document.getElementById("dropActiveFilterBadge");
  const badgeUniqueCount = document.getElementById("badgeUniqueCount");

  // Session Control Elements
  const elSessionElapsed = document.getElementById("sessionElapsedTime");
  const elSessionTotalScan = document.getElementById("sessionTotalScan");
  const elSessionValidData = document.getElementById("sessionValidData") || document.getElementById("sessionPacketsReceived");
  const elSessionMqttSend = document.getElementById("sessionMqttSend") || document.getElementById("sessionPacketsSent");
  const elSessionDropData = document.getElementById("sessionDropData") || document.getElementById("sessionDropRate");
  const elSessionTotalBandwidth = document.getElementById("sessionTotalBandwidth");
  // Backward-compatibility aliases
  const elSessionPktsRecv = elSessionValidData;
  const elSessionPktsSent = elSessionMqttSend;
  const elSessionDropRate = elSessionDropData;

  // Metrics elements
  const elReadSpeed = document.getElementById("stat_read_speed");
  const elReadAvg = document.getElementById("stat_read_avg");
  const elReadMax = document.getElementById("stat_read_max");
  const elScanSpeed = document.getElementById("stat_scan_speed");
  const elDropSpeed = document.getElementById("stat_drop_speed");

  const elUniqueTags = document.getElementById("stat_unique_tags");
  const elBeaconType = document.getElementById("stat_beacon_type");
  const elBeaconRssi = document.getElementById("stat_beacon_rssi");

  const elMqttSpeed = document.getElementById("stat_mqtt_speed");
  const elMqttAvg = document.getElementById("stat_mqtt_avg");
  const elMqttMax = document.getElementById("stat_mqtt_max");

  const elQueueLoad = document.getElementById("stat_queue_load");
  const elQueueBadge = document.getElementById("stat_queue_badge");
  const barQueueLoad = document.getElementById("bar_queue_load");

  const elBatchLoad = document.getElementById("stat_batch_load");
  const elBatchBadge = document.getElementById("stat_batch_badge");
  const barBatchLoad = document.getElementById("bar_batch_load");

  const elBatchPktSize = document.getElementById("stat_batch_pkt_size");
  const elBatchPktBadge = document.getElementById("stat_batch_pkt_badge");
  const barBatchPkt = document.getElementById("bar_batch_pkt");

  const elNetBandwidth = document.getElementById("stat_net_bandwidth");
  const elNetLinkText = document.getElementById("stat_net_link_text");

  const elFreeHeap = document.getElementById("stat_free_heap");
  const elHeapBadge = document.getElementById("stat_heap_badge");
  const barFreeHeap = document.getElementById("bar_free_heap");

  // Session State
  let sessionStartTime = null;
  let sessionTimerInterval = null;
  let sessionTotalRecv = 0;
  let sessionTotalSent = 0;
  let sessionStartRawIngress = null;
  let sessionTotalRawIngress = 0;
  let sessionStartBeaconsRead = null;
  let sessionStartMqttPublished = null;
  let sessionStartDropped = null;
  let sessionDropped = 0;
  let curTotalDropped = 0;
  let sessionStartBytes = null;
  let sessionTotalBytes = 0;
  let fullSessionLog = [];
  let readSpeedSamples = [];
  let mqttSpeedSamples = [];
  let rssiSamples = [];
  let maxReadSpeed = 0;
  let maxMqttSpeed = 0;

  // Keepalive interval during testing to prevent session timeout
  let keepAliveTimer = null;

  // Telemetry Snapshot for 1Hz Chart Recording
  let curReadSpeed = 0;
  let curMqttSpeed = 0;
  let curQueueLoad = 0;
  let curBatchLoad = 0;
  let curBatchMax = 100;
  // Initialize dynamic limits from DOM if present
  document.addEventListener("DOMContentLoaded", () => {
    const elBMax = document.getElementById("stat_batch_max");
    if (elBMax) {
      const v = parseInt(elBMax.textContent, 10);
      if (!isNaN(v) && v > 0) curBatchMax = v;
    }
  });
  let curBatchPktSize = 0;
  let curNetBandwidth = 0.0;

  // Chart Engine Variables
  const canvas = document.getElementById("telemetryChart");
  const chartWrapper = document.getElementById("chartWrapper");
  const chartTooltip = document.getElementById("chartTooltip");
  const chartData = []; // Rolling 60-second array (max 60 data points)
  let chartInterval = null;
  let hoverIndex = -1;

  function updateSessionTimer() {
    if (!sessionStartTime) return;
    const elapsedMs = Date.now() - sessionStartTime;
    const hours = Math.floor(elapsedMs / 3600000);
    const mins = Math.floor((elapsedMs % 3600000) / 60000);
    const secs = Math.floor((elapsedMs % 60000) / 1000);
    const timeFormatted = `${String(hours).padStart(2, "0")}:${String(mins).padStart(2, "0")}:${String(secs).padStart(2, "0")}`;
    if (elSessionElapsed) elSessionElapsed.textContent = timeFormatted;
  }

  // Tick timer: update last seen secara in-place tiap detik tanpa re-render DOM
  setInterval(() => {
    devices.forEach(d => {
      d.seen++;
      d.frames.forEach(f => {
        if (f.seen != null) f.seen++;
      });
    });
    if (activeTableTab === "unique" && uniqueCardsList) {
      uniqueCardsList.querySelectorAll(".card").forEach(c => {
        const d = devicesMap.get(c.dataset.mac);
        if (!d) return;
        const n = c.querySelector("[data-seen]");
        if (n) n.textContent = fmtAgo(d.seen);
        c.querySelectorAll("[data-fseen]").forEach(e => {
          const idx = parseInt(e.dataset.fseen, 10);
          const f = d.frames[idx];
          if (f) e.textContent = fmtAgo(f.seen != null ? f.seen : d.seen);
        });
      });
    }
  }, 1000);

  function updateDropRate() {
    const totalScan = Math.max(sessionTotalRawIngress, sessionTotalRecv + sessionDropped);

    if (elSessionTotalScan) {
      elSessionTotalScan.textContent = formatMetricK(totalScan);
      elSessionTotalScan.title = `${totalScan.toLocaleString()} pkt Total Scan (sebelum filter)`;
    }
    if (elSessionValidData) {
      elSessionValidData.textContent = formatMetricK(sessionTotalRecv);
      elSessionValidData.title = `${sessionTotalRecv.toLocaleString()} pkt Valid Data (lolos filter)`;
    }
    if (elSessionMqttSend) {
      elSessionMqttSend.textContent = formatMetricK(sessionTotalSent);
      elSessionMqttSend.title = `${sessionTotalSent.toLocaleString()} pkt MQTT Send (sukses terkirim)`;
    }

    if (elSessionDropData) {
      if (totalScan > 0) {
        const rate = Math.min(100, Math.max(0, (sessionDropped / totalScan) * 100));
        elSessionDropData.textContent = `${formatMetricK(sessionDropped)} (${rate.toFixed(2)}%)`;
        elSessionDropData.title = `${sessionDropped.toLocaleString()} pkt Drop Data dari ${totalScan.toLocaleString()} Total Scan (${rate.toFixed(2)}%)`;
      } else {
        elSessionDropData.textContent = "0 (0.00%)";
        elSessionDropData.title = "0 pkt Drop Data";
      }
    }
  }

    function formatMetricK(val) {
    if (val === null || val === undefined || isNaN(val)) return "0";
    const n = Number(val);
    if (n >= 1000000) {
      return (n / 1000000).toFixed(1).replace(/\.0$/, "") + "M";
    }
    if (n >= 1000) {
      return (n / 1000).toFixed(1).replace(/\.0$/, "") + "K";
    }
    return Number.isInteger(n) ? String(n) : n.toFixed(1);
  }

  function formatBytes(bytes) {
    if (!bytes || bytes <= 0) return "0.00 KB";
    const kb = bytes / 1024;
    if (kb < 1024) return kb.toFixed(2) + " KB";
    const mb = kb / 1024;
    if (mb < 1024) return mb.toFixed(2) + " MB";
    const gb = mb / 1024;
    return gb.toFixed(2) + " GB";
  }

  function exportToCsv() {
    const dataToExport = fullSessionLog.length > 0 ? fullSessionLog : packetStreamHistory;
    if (dataToExport.length === 0) {
      if (typeof Swal !== "undefined") {
        Swal.fire({
          icon: "info",
          title: "No Telemetry Data Available",
          text: "Start test stream first to accumulate telemetry data.",
          confirmButtonColor: "#0284c7"
        });
      } else {
        alert("No beacon telemetry available to export. Start test first.");
      }
      return;
    }

    let csvContent = "\uFEFF#,Timestamp,MAC Address,Protocol,Tag / Payload,RSSI (dBm),Battery (%),Raw 44B Hex\r\n";
    dataToExport.forEach((item, index) => {
      const timeStr = item.timeStr || formatTime(item.time);
      const macStr = item.mac || "-";
      const codeStr = item.code || "BLE";
      const tagStr = item.tag || "-";
      const rssiStr = item.rssi !== undefined && item.rssi !== null ? `${item.rssi}` : "-";
      const battStr = item.batt !== undefined && item.batt !== null && item.batt !== 0 ? `${item.batt}%` : "-";
      const raw44Str = convertRaw42ToMqtt44(item.raw, item.time);
      csvContent += `${index + 1},"${timeStr}","${macStr}","${codeStr}","${tagStr}","${rssiStr}","${battStr}","${raw44Str}"\r\n`;
    });

    const now = new Date();
    const dateStr = now.toISOString().replace(/[-:T]/g, "").slice(0, 14);
    const filename = `ble_gateway_telemetry_${dateStr}.csv`;

    const blob = new Blob([csvContent], { type: "text/csv;charset=utf-8;" });
    const link = document.createElement("a");
    const url = URL.createObjectURL(blob);
    link.setAttribute("href", url);
    link.setAttribute("download", filename);
    link.style.visibility = "hidden";
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
  }

  function fetchNetworkInfo() {
    fetch("/api/v1/system/version", { cache: "no-store" })
      .then(res => res.json())
      .then(data => {
        if (!elNetLinkText) return;
        if (data.active_mode === "Ethernet" || data.net_mode === "Ethernet") {
          const spd = data.eth_speed || 100;
          const dpx = data.eth_duplex || "Full";
          elNetLinkText.textContent = `Ethernet LAN ${spd}M ${dpx}`;
        } else {
          elNetLinkText.textContent = "WiFi Station Online";
        }
      })
      .catch(() => {});
  }

  function getWsUrl() {
    if (location.protocol === "file:") return "ws://192.168.4.1/ws";
    if (location.hostname === "" || location.hostname === "localhost" || location.hostname === "127.0.0.1") {
      return "ws://192.168.4.1/ws";
    }
    const protocol = location.protocol === "https:" ? "wss:" : "ws:";
    return `${protocol}//${location.host}/ws`;
  }

  function formatTime(d) {
    if (!d) return "-";
    const date = (d instanceof Date) ? d : new Date(d);
    const h = String(date.getHours()).padStart(2, "0");
    const m = String(date.getMinutes()).padStart(2, "0");
    const s = String(date.getSeconds()).padStart(2, "0");
    const ms = String(date.getMilliseconds()).padStart(3, "0");
    return `${h}:${m}:${s}.${ms}`;
  }

  function escapeHtml(text) {
    if (!text && text !== 0) return "";
    return String(text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }

  function formatSpacedHex(hexStr) {
    if (!hexStr || hexStr.length < 2) return "-";
    const clean = hexStr.replace(/\s+/g, "").toUpperCase();
    return clean.match(/.{1,2}/g)?.join(" ") || clean;
  }

  let toastTimer = null;
  function showToast(msg) {
    if (!uiToast) return;
    uiToast.textContent = msg;
    uiToast.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => {
      uiToast.classList.remove("show");
    }, 1500);
  }

  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text);
    }
    return new Promise((ok, no) => {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.cssText = "position:fixed;opacity:0;top:0";
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      try {
        document.execCommand("copy") ? ok() : no();
      } catch (e) {
        no(e);
      }
      ta.remove();
    });
  }

  function getDropReasonInfo(reasonCode) {
    switch (Number(reasonCode)) {
      case 0: return { label: "TTL Expired", cssClass: "drop-ttl" };
      case 1: return { label: "RSSI < Min", cssClass: "drop-rssi" };
      case 2: return { label: "UUID Mismatch", cssClass: "drop-uuid" };
      case 3: return { label: "Major Mismatch", cssClass: "drop-major" };
      case 4: return { label: "Queue Full", cssClass: "drop-queue" };
      case 5: return { label: "MQTT Fail", cssClass: "drop-mqtt" };
      case 6: return { label: "Unknown Format", cssClass: "drop-unknown" };
      default: return { label: "Unknown Drop", cssClass: "drop-unknown" };
    }
  }

  function convertRaw42ToMqtt44(rawHex, dateObj) {
    if (!rawHex) return "";
    const clean = String(rawHex).replace(/[^0-9A-Fa-f]/g, "").toUpperCase();
    if (clean.length < 84) {
      if (clean.length === 88) return clean;
      return clean;
    }
    const d = (dateObj instanceof Date) ? dateObj : new Date();
    const epochSec = Math.floor(d.getTime() / 1000);
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

  function parseRawBleBreakdown(hexStr, dateObj) {
    if (!hexStr) return `<span style="color:var(--text-dim);">-</span>`;
    const clean = String(hexStr).replace(/[^0-9A-Fa-f]/g, "").toUpperCase();
    if (clean.length < 84) {
      return `<div class="full-hex-string"><strong>Raw Hex:</strong> ${formatSpacedHex(hexStr)}</div>`;
    }

    const frame44 = convertRaw42ToMqtt44(clean, dateObj);
    const epochHex = frame44.substring(0, 8);
    const mac = frame44.substring(8, 20).match(/.{1,2}/g).join(":");
    const pdu = frame44.substring(20, 22);
    const rssiHex = frame44.substring(22, 24);
    const len = frame44.substring(24, 26);
    const pay = frame44.substring(26, 88).match(/.{1,2}/g).join(" ");

    const epochSec = parseInt(epochHex, 16);
    const humanTime = !isNaN(epochSec) ? formatTime(new Date(epochSec * 1000)) : "-";

    return `
      <div class="raw-breakdown">
        <div class="raw-tag-item"><span class="raw-tag-lbl">Timestamp (4B)</span><span class="raw-tag-bytes byte-time" title="Waktu: ${humanTime}">${epochHex}</span></div>
        <div class="raw-tag-item"><span class="raw-tag-lbl">MAC (6B)</span><span class="raw-tag-bytes byte-mac">${mac}</span></div>
        <div class="raw-tag-item"><span class="raw-tag-lbl">PDU Type</span><span class="raw-tag-bytes byte-pdu">${pdu}</span></div>
        <div class="raw-tag-item"><span class="raw-tag-lbl">RSSI</span><span class="raw-tag-bytes byte-rssi">${rssiHex}</span></div>
        <div class="raw-tag-item"><span class="raw-tag-lbl">Length</span><span class="raw-tag-bytes byte-len">${len}</span></div>
        <div class="raw-tag-item"><span class="raw-tag-lbl">AD Payload (31B)</span><span class="raw-tag-bytes byte-pay" style="word-break:break-all;">${pay}</span></div>
      </div>
      <div class="full-hex-string" style="display:flex; justify-content:space-between; align-items:center; gap:12px;">
        <span style="overflow-x:auto; white-space:nowrap; scrollbar-width:none;"><strong>Raw 44B:</strong> ${formatSpacedHex(frame44)}</span>
        <button type="button" class="btn-raw-hex" data-act="copy" data-raw="${frame44}" style="flex:none;">Copy Hex</button>
      </div>
    `;
  }

  function getRssiBarsHtml(rssi) {
    if (rssi === null || rssi === undefined) {
      return `<span style="color:var(--text-dim);">-</span>`;
    }
    let cls = "weak";
    if (rssi >= -65) cls = "strong";
    else if (rssi >= -80) cls = "medium";
    return `
      <div class="rssi-cell">
        <div class="rssi-bars rssi-${cls}"><span></span><span></span><span></span><span></span></div>
        <span class="rssi-val ${cls}">${rssi} dBm</span>
      </div>
    `;
  }

  function getProtoBadgeHtml(code) {
    const safe = (code || "BLE").trim();
    const lower = safe.toLowerCase();
    let cls = "proto-universal";
    let label = safe;
    if (lower.includes("ibeacon_ext") || lower.includes("ext")) {
      cls = "proto-ibeacon"; label = "iBeacon Ext";
    } else if (lower.includes("ibeacon")) {
      cls = "proto-ibeacon"; label = "Apple iBeacon";
    } else if (lower.includes("minew")) {
      cls = "proto-minew"; label = "Minew Beacon";
    } else if (lower.includes("alarm")) {
      cls = "proto-alarm"; label = "MOKO Alarm";
    } else if (lower.includes("sensor")) {
      cls = "proto-sensor"; label = "MOKO Sensor";
    } else if (lower.includes("eddy")) {
      cls = "proto-eddystone"; label = "Eddystone";
    } else if (lower.includes("raw")) {
      cls = "proto-universal"; label = "RAW BLE";
    }
    return `<span class="proto-badge ${cls}">${label}</span>`;
  }

  function fmtVal(f, k, v) {
    if (f.type === 'moko_alarm' && k === 'status') {
      return `<span class="alarm ${/trigger/i.test(v) ? 'on' : ''}"><i></i>${escapeHtml(v)}</span>`;
    }
    if (v === "-" || v === "" || v === null || v === undefined) {
      return `<span style="color:var(--text-dim);">-</span>`;
    }
    return escapeHtml(v);
  }

  function frameHTML(d, f, i) {
    const sc = SCHEMA[f.type] || SCHEMA.raw, data = f.data || {};
    const key = d.mac + '#' + i, ex = rawOpen.has(key), fo = frOpen.has(key);
    const n = (f.raw || "").trim().split(/\s+/).filter(Boolean).length;
    const seen = f.seen != null ? f.seen : d.seen;
    const alert = f.type === 'moko_alarm' && /trigger/i.test(data.status || '');
    const rows = sc.fields.filter(([k]) => data[k] != null && data[k] !== '').map(([k, l]) => `<div><dt>${escapeHtml(l)}</dt><dd>${fmtVal(f, k, data[k])}</dd></div>`).join('');
    return `<section class="frame ${fo ? 'open' : ''}">
      <button class="fh" data-act="frame" data-key="${escapeHtml(key)}" aria-expanded="${fo}"><span class="dot ${alert ? 'alert' : ''}" style="--c:var(${sc.color})"></span><span class="ft">${escapeHtml(sc.label)}</span><span class="fs">Last seen <b data-fseen="${i}">${fmtAgo(seen)}</b></span><svg class="chev2" viewBox="0 0 24 24" fill="none" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M6 9l6 6 6-6"/></svg></button>
      <div class="fbody"><div><div class="fin">
      <dl class="dl"><div><dt>RSSI</dt><dd style="color:${tone(f.rssi)}">${f.rssi} dBm</dd></div>${rows}</dl>
      <div class="rawbox">
        <div class="rawh">
          <button class="rawt" data-act="raw" data-key="${escapeHtml(key)}" aria-expanded="${ex}">${ICON_CHEV}Raw payload <em>· 44 bytes</em></button>
          <button class="icobtn" data-act="copy" data-raw="${escapeHtml(convertRaw42ToMqtt44(f.raw, f.time || new Date()))}" aria-label="Copy raw payload" title="Copy raw payload">${ICON_COPY}</button>
        </div>
        <div class="hex ${ex ? 'show' : ''}" data-hex="${escapeHtml(key)}">${escapeHtml(formatSpacedHex(convertRaw42ToMqtt44(f.raw, f.time || new Date())))}</div>
      </div>
      </div></div></div>
    </section>`;
  }

  function cardHTML(d) {
    const r = bestRssi(d), isOpen = open.has(d.mac);
    return `<article class="card ${isOpen ? 'open' : ''}" data-mac="${escapeHtml(d.mac)}">
      <button class="head" data-act="toggle" aria-expanded="${isOpen}">
        <div class="mac">${escapeHtml(d.mac)}</div>
        <div class="rssi">${bars(r)}<span>${r} dBm</span></div>
        <div class="meta"><span>Frames <b>${d.frames.length}</b></span><span>Last seen <b data-seen>${fmtAgo(d.seen)}</b></span>
          <svg class="chev" viewBox="0 0 24 24" fill="none" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M6 9l6 6 6-6"/></svg></div>
      </button>
      <div class="body"><div><div class="inner">
        <div class="sum"><span>Total packets<b>${d.pkts.toLocaleString('en-US')}</b></span><button class="del" data-act="del">${ICON_TRASH}Delete</button></div>
        ${d.frames.map((f, i) => frameHTML(d, f, i)).join('')}
      </div></div></div>
    </article>`;
  }

  function calcMokoAcc(raw, s) {
    if (raw < 0x8000) {
      return Math.floor(raw >> 4) * s;
    } else {
      return (Math.floor(raw >> 4) - 0x1000) * s;
    }
  }

  function parseBeaconToFrame(beacon) {
    let rawClean = (beacon.raw || "").replace(/[^0-9A-Fa-f]/g, "").toUpperCase();
    const mac = (beacon.mac && beacon.mac !== "-" && beacon.mac !== "") ? beacon.mac.toUpperCase() : ((beacon.tag && beacon.tag.includes(":")) ? beacon.tag.toUpperCase() : "-");
    const spacedRaw = rawClean.length > 0 ? formatSpacedHex(rawClean) : "-";

    // 1. Scan AD Structures in raw payload (bytes 10..40 = hex chars 20..82)
    let localName = "";
    let txPowerLevel = null;
    let firstAdType = "-";

    if (rawClean.length >= 22) {
      const payHex = rawClean.substring(20, 82);
      let pIdx = 0;
      while (pIdx < payHex.length - 2) {
        const adLen = parseInt(payHex.substring(pIdx, pIdx + 2), 16);
        if (adLen === 0 || pIdx + 2 + (adLen * 2) > payHex.length) break;
        const adType = payHex.substring(pIdx + 2, pIdx + 4);
        if (firstAdType === "-") firstAdType = `0x${adType}`;
        const adDataHex = payHex.substring(pIdx + 4, pIdx + 2 + (adLen * 2));

        // AD 0x08 (Shortened Local Name) or 0x09 (Complete Local Name)
        if (adType === "08" || adType === "09") {
          try {
            let str = "";
            for (let i = 0; i < adDataHex.length; i += 2) {
              const code = parseInt(adDataHex.substring(i, i + 2), 16);
              if (code >= 32 && code <= 126) str += String.fromCharCode(code);
            }
            if (str.length > 0) localName = str;
          } catch (e) {}
        }
        // AD 0x0A (Tx Power Level)
        else if (adType === "0A" && adDataHex.length >= 2) {
          let pwr = parseInt(adDataHex.substring(0, 2), 16);
          if (pwr > 127) pwr -= 256;
          txPowerLevel = `${pwr} dBm`;
        }
        pIdx += 2 + (adLen * 2);
      }
    }

    // 2. Classify Protocol Type
    let type = "raw";
    const c = (beacon.code || "").toLowerCase();
    const t = (beacon.tag || "").toLowerCase();

    if (c.includes("moko_alarm") || c.includes("alarm") || rawClean.includes("E0FE")) {
      type = "moko_alarm";
    } else if (c.includes("moko_sensor") || c.includes("bxp-b-d") || rawClean.includes("00EA") || rawClean.includes("00EABC")) {
      type = "moko_sensor";
    } else if (c.includes("moko_device") || c.includes("bxp-d") || rawClean.includes("ABFE40")) {
      type = "moko_device";
    } else if (c.includes("moko_acc") || c.includes("bxp-acc") || rawClean.includes("ABFE60")) {
      type = "moko_acc";
    } else if (c.includes("moko_th") || c.includes("th") || rawClean.includes("ABFE70")) {
      type = "moko_th";
    } else if (c.includes("moko_ibeacon") || rawClean.includes("ABFE50") || (rawClean.includes("ABFE") && rawClean.length >= 72)) {
      type = "moko_ibeacon";
    } else if (c.includes("minew_ext") || c.includes("ibeacon_ext")) {
      type = "minew_ext";
    } else if (c.includes("minew_info") || c.includes("minew info")) {
      type = "minew_info";
    } else if (c.includes("eddy_uid") || (rawClean.includes("AAFE") && rawClean.includes("AAFE00"))) {
      type = "eddy_uid";
    } else if (c.includes("eddy_url") || (rawClean.includes("AAFE") && rawClean.includes("AAFE10")) || t.startsWith("http")) {
      type = "eddy_url";
    } else if (c.includes("eddy_tlm") || (rawClean.includes("AAFE") && rawClean.includes("AAFE20"))) {
      type = "eddy_tlm";
    } else if (c.includes("ibeacon") || rawClean.includes("4C000215")) {
      type = "ibeacon";
    } else {
      type = "raw";
    }

    // 3. Extract Protocol-Specific Data (Strictly aligned with esp32_nrf_parsing/src/main.c)
    const data = {};

    if (type === "moko_alarm") {
      // MOKO BXP Alarm: fields = [mode, status, count, deviceId, firmware, password, name]
      let mode = "-";
      let status = "Standby";
      let count = "-";
      let deviceId = (beacon.tag && !beacon.tag.includes(":")) ? beacon.tag : "-";
      let firmware = "-";
      let password = "OFF";
      let name = localName || beacon.name || "-";

      const idx = rawClean.indexOf("E0FE");
      if (idx >= 0 && rawClean.length >= idx + 18) {
        // E0 FE [FrameType 1B] [StatusFlag 1B] [Count 2B] [DeviceID 3B] [Firmware 1B]
        const frameType = parseInt(rawClean.substring(idx + 4, idx + 6), 16);
        if (frameType === 0x20) mode = "Single Press";
        else if (frameType === 0x21) mode = "Double Press";
        else if (frameType === 0x22) mode = "Long Press";
        else if (frameType === 0x23) mode = "Inactivity";
        else mode = `Mode 0x${frameType.toString(16).toUpperCase()}`;

        const statusFlag = parseInt(rawClean.substring(idx + 6, idx + 8), 16);
        const pwdEnabled = (statusFlag & 0x01) !== 0;
        const triggered = (statusFlag & 0x02) !== 0;
        const subTriggered = (statusFlag & 0x04) !== 0;

        status = triggered ? "TRIGGERED (ALARM!)" : (subTriggered ? "SUB_TRIGGERED" : "Standby");
        password = pwdEnabled ? "ON" : "OFF";

        const countVal = parseInt(rawClean.substring(idx + 8, idx + 12), 16);
        count = !isNaN(countVal) ? String(countVal) : "-";

        deviceId = rawClean.substring(idx + 12, idx + 18).toUpperCase();

        if (rawClean.length >= idx + 20) {
          firmware = `0x${rawClean.substring(idx + 18, idx + 20).toUpperCase()}`;
        }
      } else if (beacon.tag && beacon.tag.toLowerCase().includes("trig")) {
        status = "TRIGGERED (ALARM!)";
      }

      data.mode = mode;
      data.status = status;
      data.count = count;
      data.deviceId = deviceId;
      data.firmware = firmware;
      data.password = password;
      data.name = name;

    } else if (type === "moko_sensor") {
      // MOKO BXP Sensor Info: fields = [battery, temp, accel, scale, threshold, ranging, mac, txPower]
      let battery = (beacon.batt !== undefined && beacon.batt !== null) ? `${beacon.batt}%` : "-";
      let temp = "-";
      let accel = "-";
      let scale = "-";
      let threshold = "-";
      let ranging = "-";

      const idxEa = rawClean.indexOf("00EA");
      if (idxEa >= 0 && rawClean.length >= idxEa + 34) {
        // 00 EA [FrameType 1B (0x00)] [Scale 1B] [MotionThresh 2B] [AccX 2B] [AccY 2B] [AccZ 2B] [Temp 2B] [Ranging 1B] [RawBatt 2B] [MAC 6B]
        const scaleCode = parseInt(rawClean.substring(idxEa + 6, idxEa + 8), 16);
        scale = (scaleCode === 1) ? "±4g" : (scaleCode === 2 ? "±8g" : (scaleCode === 3 ? "±16g" : "±2g"));
        
        const mThresh = parseInt(rawClean.substring(idxEa + 8, idxEa + 12), 16);
        threshold = !isNaN(mThresh) ? `${mThresh} mg` : "-";

        let ax = parseInt(rawClean.substring(idxEa + 12, idxEa + 16), 16);
        if (ax > 32767) ax -= 65536;
        let ay = parseInt(rawClean.substring(idxEa + 16, idxEa + 20), 16);
        if (ay > 32767) ay -= 65536;
        let az = parseInt(rawClean.substring(idxEa + 20, idxEa + 24), 16);
        if (az > 32767) az -= 65536;
        accel = `X:${ax} Y:${ay} Z:${az} mg`;

        let rawT = parseInt(rawClean.substring(idxEa + 24, idxEa + 28), 16);
        if (rawT > 32767) rawT -= 65536;
        temp = `${(rawT * 0.1).toFixed(1)} °C`;

        let rng = parseInt(rawClean.substring(idxEa + 28, idxEa + 30), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;

        const rawB = parseInt(rawClean.substring(idxEa + 30, idxEa + 34), 16);
        if (!isNaN(rawB)) {
          if (rawB > 100) {
            let pct = (rawB > 2200) ? Math.round(((rawB - 2200) * 100) / (3000 - 2200)) : 0;
            pct = Math.min(100, Math.max(0, pct));
            battery = `${rawB} mV (${pct}%)`;
          } else {
            battery = `${rawB}%`;
          }
        }
      }

      data.battery = battery;
      data.temp = temp;
      data.accel = accel;
      data.scale = scale;
      data.threshold = threshold;
      data.ranging = ranging;
      data.mac = mac;
      if (txPowerLevel) data.txPower = txPowerLevel;

    } else if (type === "moko_device") {
      // MOKO Device Info: fields = [battery, firmware, lightSensor, light, connectable, interval, ranging, mac, txPower, name]
      let battery = (beacon.batt !== undefined && beacon.batt !== null) ? `${beacon.batt}%` : "-";
      let firmware = "-";
      let lightSensor = "None";
      let light = "None";
      let connectable = "No";
      let interval = "-";
      let ranging = "-";

      const idxDev = rawClean.indexOf("ABFE40");
      if (idxDev >= 0 && rawClean.length >= idxDev + 34) {
        let rng = parseInt(rawClean.substring(idxDev + 6, idxDev + 8), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;

        const advI = parseInt(rawClean.substring(idxDev + 8, idxDev + 10), 16);
        if (!isNaN(advI)) interval = `${advI * 100} ms`;

        const vb = parseInt(rawClean.substring(idxDev + 10, idxDev + 14), 16);
        if (!isNaN(vb) && vb > 1000) battery = `${vb} mV`;

        const devProp = parseInt(rawClean.substring(idxDev + 14, idxDev + 16), 16);
        lightSensor = (devProp & 0x04) !== 0 ? "Equipped" : "None";

        const swStat = parseInt(rawClean.substring(idxDev + 16, idxDev + 18), 16);
        connectable = (swStat & 0x01) !== 0 ? "Yes" : "No";
        light = (swStat & 0x02) !== 0 ? "Detected" : "None";

        const fwType = parseInt(rawClean.substring(idxDev + 30, idxDev + 32), 16);
        const fwTypeStr = (fwType === 1) ? "BXP-D" : (fwType === 2 ? "BXP-TLA" : "BXP-C");
        const fwVer = parseInt(rawClean.substring(idxDev + 32, idxDev + 34), 16);
        firmware = `${fwTypeStr} v${(fwVer >> 4) & 0x0F}.${fwVer & 0x0F}`;
      }

      data.battery = battery;
      data.firmware = firmware;
      data.lightSensor = lightSensor;
      data.light = light;
      data.connectable = connectable;
      data.interval = interval;
      data.ranging = ranging;
      data.mac = mac;
      if (txPowerLevel) data.txPower = txPowerLevel;
      data.name = localName || beacon.name || "-";

    } else if (type === "moko_acc") {
      // MOKO 3-Axis Acc: fields = [accel, scale, rate, threshold, battery, interval, ranging, mac, txPower]
      let accel = "-";
      let scale = "-";
      let rate = "-";
      let threshold = "-";
      let battery = (beacon.batt !== undefined && beacon.batt !== null) ? `${beacon.batt}%` : "-";
      let interval = "-";
      let ranging = "-";

      const idxAcc = rawClean.indexOf("ABFE60");
      if (idxAcc >= 0 && rawClean.length >= idxAcc + 32) {
        let rng = parseInt(rawClean.substring(idxAcc + 6, idxAcc + 8), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;

        const advI = parseInt(rawClean.substring(idxAcc + 8, idxAcc + 10), 16);
        if (!isNaN(advI)) interval = `${advI * 100} ms`;

        const rateCode = parseInt(rawClean.substring(idxAcc + 10, idxAcc + 12), 16);
        rate = (rateCode === 0) ? "1Hz" : (rateCode === 1 ? "10Hz" : (rateCode === 2 ? "25Hz" : (rateCode === 3 ? "50Hz" : "100Hz")));

        const scaleCode = parseInt(rawClean.substring(idxAcc + 12, idxAcc + 14), 16);
        scale = (scaleCode === 1) ? "±4g" : (scaleCode === 2 ? "±8g" : (scaleCode === 3 ? "±16g" : "±2g"));
        const sFactor = (scaleCode === 1) ? 2 : (scaleCode === 2 ? 4 : (scaleCode === 3 ? 12 : 1));

        const threshCode = parseInt(rawClean.substring(idxAcc + 14, idxAcc + 16), 16);
        threshold = `${(threshCode * 0.1).toFixed(1)}g`;

        const rawX = parseInt(rawClean.substring(idxAcc + 16, idxAcc + 20), 16);
        const rawY = parseInt(rawClean.substring(idxAcc + 20, idxAcc + 24), 16);
        const rawZ = parseInt(rawClean.substring(idxAcc + 24, idxAcc + 28), 16);
        const ax = calcMokoAcc(rawX, sFactor);
        const ay = calcMokoAcc(rawY, sFactor);
        const az = calcMokoAcc(rawZ, sFactor);
        accel = `X:${ax} Y:${ay} Z:${az} mg`;

        const vb = parseInt(rawClean.substring(idxAcc + 28, idxAcc + 32), 16);
        if (!isNaN(vb) && vb > 1000) battery = `${vb} mV`;
      }

      data.accel = accel;
      data.scale = scale;
      data.rate = rate;
      data.threshold = threshold;
      data.battery = battery;
      data.interval = interval;
      data.ranging = ranging;
      data.mac = mac;
      if (txPowerLevel) data.txPower = txPowerLevel;

    } else if (type === "moko_th") {
      // MOKO Temp & Humidity: fields = [temp, humidity, battery, interval, ranging, mac, txPower]
      let temp = "-";
      let humidity = "-";
      let battery = (beacon.batt !== undefined && beacon.batt !== null) ? `${beacon.batt}%` : "-";
      let interval = "-";
      let ranging = "-";

      const idxTh = rawClean.indexOf("ABFE70");
      if (idxTh >= 0 && rawClean.length >= idxTh + 22) {
        let rng = parseInt(rawClean.substring(idxTh + 6, idxTh + 8), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;

        const advI = parseInt(rawClean.substring(idxTh + 8, idxTh + 10), 16);
        if (!isNaN(advI)) interval = `${advI * 100} ms`;

        let rawT = parseInt(rawClean.substring(idxTh + 10, idxTh + 14), 16);
        if (rawT > 32767) rawT -= 65536;
        temp = `${(rawT * 0.1).toFixed(1)} °C`;

        let rawH = parseInt(rawClean.substring(idxTh + 14, idxTh + 18), 16);
        humidity = `${(rawH * 0.1).toFixed(1)}%`;

        const vb = parseInt(rawClean.substring(idxTh + 18, idxTh + 22), 16);
        if (!isNaN(vb) && vb > 1000) battery = `${vb} mV`;
      }

      data.temp = temp;
      data.humidity = humidity;
      data.battery = battery;
      data.interval = interval;
      data.ranging = ranging;
      data.mac = mac;
      if (txPowerLevel) data.txPower = txPowerLevel;

    } else if (type === "moko_ibeacon") {
      // MOKO iBeacon Info: fields = [interval, major, minor, uuid, ranging, txPower]
      let uuid = "-";
      let major = "-";
      let minor = "-";
      let ranging = "-";
      let interval = "-";

      const idx = rawClean.indexOf("ABFE");
      if (idx >= 0 && rawClean.length >= idx + 50) {
        // AB FE [50] [Ranging 1B] [Interval 1B] [UUID 16B] [Major 2B] [Minor 2B]
        let rng = parseInt(rawClean.substring(idx + 6, idx + 8), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;
        const intvVal = parseInt(rawClean.substring(idx + 8, idx + 10), 16);
        if (!isNaN(intvVal)) interval = `${intvVal * 100} ms`;
        const u = rawClean.substring(idx + 10, idx + 42);
        uuid = `${u.substring(0,8)}-${u.substring(8,12)}-${u.substring(12,16)}-${u.substring(16,20)}-${u.substring(20,32)}`;
        major = String(parseInt(rawClean.substring(idx + 42, idx + 46), 16));
        minor = String(parseInt(rawClean.substring(idx + 46, idx + 50), 16));
      }

      data.interval = interval;
      data.major = major;
      data.minor = minor;
      data.uuid = uuid;
      data.ranging = ranging;
      if (txPowerLevel) data.txPower = txPowerLevel;

    } else if (type === "ibeacon") {
      // Standard Apple iBeacon: fields = [uuid, major, minor, ranging] (NO txPower per reference main.c)
      let uuid = "-";
      let major = "-";
      let minor = "-";
      let ranging = "-";

      if (rawClean.includes("4C000215")) {
        const idx = rawClean.indexOf("4C000215") + 8;
        if (rawClean.length >= idx + 32) {
          const u = rawClean.substring(idx, idx + 32);
          uuid = `${u.substring(0,8)}-${u.substring(8,12)}-${u.substring(12,16)}-${u.substring(16,20)}-${u.substring(20,32)}`;
        }
        if (rawClean.length >= idx + 36) major = String(parseInt(rawClean.substring(idx + 32, idx + 36), 16));
        if (rawClean.length >= idx + 40) minor = String(parseInt(rawClean.substring(idx + 36, idx + 40), 16));
        if (rawClean.length >= idx + 42) {
          let rng = parseInt(rawClean.substring(idx + 40, idx + 42), 16);
          if (rng > 127) rng -= 256;
          ranging = `${rng} dBm`;
        }
      }

      if (major === "-" && beacon.tag && beacon.tag.includes("-")) {
        const pts = beacon.tag.split("-");
        if (pts.length >= 2 && !isNaN(parseInt(pts[0], 10)) && !isNaN(parseInt(pts[1], 10))) {
          major = String(parseInt(pts[0], 10));
          minor = String(parseInt(pts[1], 10));
        }
      }

      data.uuid = uuid;
      data.major = major;
      data.minor = minor;
      data.ranging = ranging;

    } else if (type === "minew_ext") {
      // Minew ibeacon_ext: fields = [uuid, major, minor, battery, ranging] (NO txPower per reference main.c)
      let uuid = "-";
      let major = "-";
      let minor = "-";
      let battery = (beacon.batt !== undefined && beacon.batt !== null) ? `${beacon.batt}%` : "-";
      let ranging = "-";

      if (rawClean.length >= 64) {
        const payHex = rawClean.substring(20, 82);
        let pIdx = 0;
        while (pIdx < payHex.length - 2) {
          const adLen = parseInt(payHex.substring(pIdx, pIdx + 2), 16);
          if (adLen === 0 || pIdx + 2 + (adLen * 2) > payHex.length) break;
          const adType = payHex.substring(pIdx + 2, pIdx + 4);
          if (adType === "16" && adLen >= 22) {
            const dataHex = payHex.substring(pIdx + 4, pIdx + 2 + (adLen * 2));
            if (dataHex.length >= 44) {
              const u = dataHex.substring(0, 32);
              uuid = `${u.substring(0,8)}-${u.substring(8,12)}-${u.substring(12,16)}-${u.substring(16,20)}-${u.substring(20,32)}`;
              major = String(parseInt(dataHex.substring(32, 36), 16));
              minor = String(parseInt(dataHex.substring(36, 40), 16));
              let rng = parseInt(dataHex.substring(40, 42), 16);
              if (rng > 127) rng -= 256;
              ranging = `${rng} dBm`;
              const b = parseInt(dataHex.substring(42, 44), 16);
              if (!isNaN(b)) battery = `${b}%`;
              break;
            }
          }
          pIdx += 2 + (adLen * 2);
        }
      }

      if (major === "-" && beacon.tag && beacon.tag.includes("-")) {
        const pts = beacon.tag.split("-");
        if (pts.length >= 2 && !isNaN(parseInt(pts[0], 10)) && !isNaN(parseInt(pts[1], 10))) {
          major = String(parseInt(pts[0], 10));
          minor = String(parseInt(pts[1], 10));
        }
      }

      data.uuid = uuid;
      data.major = major;
      data.minor = minor;
      data.battery = battery;
      data.ranging = ranging;

    } else if (type === "eddy_uid") {
      // Eddystone-UID: fields = [type, namespace, instance, ranging] (NO txPower per reference main.c)
      let namespace = "-";
      let instance = "-";
      let ranging = "-";

      const idx = rawClean.indexOf("AAFE00");
      if (idx >= 0 && rawClean.length >= idx + 40) {
        let rng = parseInt(rawClean.substring(idx + 6, idx + 8), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;
        namespace = rawClean.substring(idx + 8, idx + 28).toUpperCase();
        instance = rawClean.substring(idx + 28, idx + 40).toUpperCase();
      }

      data.type = "UID";
      data.namespace = namespace;
      data.instance = instance;
      data.ranging = ranging;

    } else if (type === "eddy_url") {
      // Eddystone-URL: fields = [type, url, ranging] (NO txPower per reference main.c)
      let url = "-";
      let ranging = "-";

      const idx = rawClean.indexOf("AAFE10");
      if (idx >= 0 && rawClean.length >= idx + 10) {
        let rng = parseInt(rawClean.substring(idx + 6, idx + 8), 16);
        if (rng > 127) rng -= 256;
        ranging = `${rng} dBm`;

        const schemeCode = parseInt(rawClean.substring(idx + 8, idx + 10), 16);
        const schemes = ["http://www.", "https://www.", "http://", "https://"];
        let decoded = (schemeCode >= 0 && schemeCode < schemes.length) ? schemes[schemeCode] : "";

        const suffixes = [
          ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
          ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov"
        ];

        for (let i = idx + 10; i < rawClean.length; i += 2) {
          const byteVal = parseInt(rawClean.substring(i, i + 2), 16);
          if (byteVal === 0 && i > idx + 16) break;
          if (byteVal < suffixes.length) {
            decoded += suffixes[byteVal];
          } else if (byteVal >= 32 && byteVal <= 126) {
            decoded += String.fromCharCode(byteVal);
          }
        }
        if (decoded.length > 8) url = decoded;
      }

      if (url === "-" && beacon.tag && beacon.tag.startsWith("http")) {
        url = beacon.tag;
      }

      data.type = "URL";
      data.url = url;
      data.ranging = ranging;

    } else if (type === "eddy_tlm") {
      // Eddystone-TLM: fields = [type, battery, temp, advCount, uptime]
      let battery = (beacon.batt !== undefined && beacon.batt !== null) ? `${beacon.batt}%` : "-";
      let temp = "-";
      let advCount = "-";
      let uptime = "-";

      const idx = rawClean.indexOf("AAFE20");
      if (idx >= 0 && rawClean.length >= idx + 32) {
        const tlmVer = parseInt(rawClean.substring(idx + 6, idx + 8), 16);
        data.type = `TLM v${isNaN(tlmVer) ? 0 : tlmVer}`;

        const vbatt = parseInt(rawClean.substring(idx + 8, idx + 12), 16);
        if (!isNaN(vbatt) && vbatt > 500) {
          battery = `${vbatt} mV`;
        }
        let tRaw = parseInt(rawClean.substring(idx + 12, idx + 16), 16);
        if (tRaw > 32767) tRaw -= 65536;
        temp = `${(tRaw / 256.0).toFixed(1)} °C`;

        const adv = parseInt(rawClean.substring(idx + 16, idx + 24), 16);
        if (!isNaN(adv)) advCount = adv.toLocaleString();

        const sec10 = parseInt(rawClean.substring(idx + 24, idx + 32), 16);
        if (!isNaN(sec10)) {
          const totSec = Math.floor(sec10 / 10);
          const hrs = String(Math.floor(totSec / 3600)).padStart(2, "0");
          const mins = String(Math.floor((totSec % 3600) / 60)).padStart(2, "0");
          const secs = String(totSec % 60).padStart(2, "0");
          uptime = `${hrs}:${mins}:${secs}`;
        }
      }

      data.type = data.type || "TLM v0";
      data.battery = battery;
      data.temp = temp;
      data.advCount = advCount;
      data.uptime = uptime;

    } else if (type === "minew_info") {
      // Minew Device Info: fields = [name]
      data.name = localName || beacon.name || (beacon.tag && !beacon.tag.includes(":") ? beacon.tag : "-");

    } else {
      // Raw BLE Data: fields = [advType, length, adType]
      data.advType = rawClean.length >= 16 ? `0x${rawClean.substring(14, 16)}` : "-";
      data.length = rawClean.length >= 20 ? `${parseInt(rawClean.substring(18, 20), 16)} bytes` : "-";
      data.adType = firstAdType;
    }

    if (beacon.data) Object.assign(data, beacon.data);

    return {
      type: type,
      rssi: (beacon.rssi !== undefined && beacon.rssi !== null) ? Number(beacon.rssi) : -85,
      seen: 0,
      data: data,
      raw: spacedRaw
    };
  }

  function matchBeacon(b, filterVal) {
    if (!filterVal) return true;

    const tagStr = (b.tag || "").toLowerCase();
    const codeStr = (b.code || "").toLowerCase();
    const macStr = (b.mac || "").toLowerCase();
    const rawStr = (b.raw || "").toLowerCase();

    // 1. Kecocokan langsung pada string mac, tag, format, atau raw hex
    if (macStr.includes(filterVal) || tagStr.includes(filterVal) || codeStr.includes(filterVal) || rawStr.includes(filterVal)) {
      return true;
    }

    // Ambil angka Major dan Minor dari b.tag ("00001-00002")
    let bMajor = NaN;
    let bMinor = NaN;
    if (tagStr.includes("-")) {
      const tagParts = tagStr.split("-");
      bMajor = parseInt(tagParts[0], 10);
      bMinor = parseInt(tagParts[1], 10);
    }

    // 2. Pencocokan pasangan Major-Minor jika input mengandung separator (- : /)
    if (filterVal.includes("-") || filterVal.includes(":") || filterVal.includes("/")) {
      const parts = filterVal.split(/[-:\/]/);
      const strMaj = parts[0].trim();
      const strMin = (parts.length > 1) ? parts[1].trim() : "";

      const qMaj = strMaj !== "" ? parseInt(strMaj, 10) : NaN;
      const qMin = strMin !== "" ? parseInt(strMin, 10) : NaN;

      if (!isNaN(qMaj) && !isNaN(qMin)) {
        return bMajor === qMaj && bMinor === qMin;
      }
      if (!isNaN(qMaj) && strMin === "") {
        return bMajor === qMaj;
      }
      if (strMaj === "" && !isNaN(qMin)) {
        return bMinor === qMin;
      }
    }

    // 3. Pencocokan angka tunggal (cocokkan dengan integer Major ATAU Minor)
    const searchNum = parseInt(filterVal, 10);
    if (!isNaN(searchNum)) {
      if (bMajor === searchNum || bMinor === searchNum) {
        return true;
      }
    }

    return false;
  }

  function insertBeacon(beacon) {
    if (!beacon) return;
    const mac = (beacon.mac && beacon.mac !== "-" && beacon.mac !== "") ? beacon.mac.toUpperCase() : ((beacon.tag && beacon.tag.includes(":")) ? beacon.tag.toUpperCase() : "-");
    const key = (mac !== "-") ? mac : (beacon.tag || "BLE_" + Math.random().toString(36).substring(2, 7));

    // Hitung unique beacons selama sesi test berjalan
    uniqueBeaconsSet.add(key);
    if (elUniqueTags) elUniqueTags.textContent = uniqueBeaconsSet.size;

    const f = parseBeaconToFrame(beacon);
    let d = devicesMap.get(key);
    const isNew = !d;

    if (isNew) {
      d = {
        mac: key,
        name: beacon.name || (beacon.code || ""),
        pkts: 1,
        seen: 0,
        frames: [f]
      };
      buildDeviceSearchIndex(d);
      devicesMap.set(key, d);
      devices.push(d);
    } else {
      d.pkts++;
      d.seen = 0;
      let frIdx = d.frames.findIndex(x => x.type === f.type);
      if (frIdx >= 0) {
        d.frames[frIdx].rssi = f.rssi;
        d.frames[frIdx].seen = 0;
        d.frames[frIdx].raw = f.raw;
        d.frames[frIdx].data = Object.assign(d.frames[frIdx].data, f.data);
      } else {
        d.frames.push(f);
      }
      buildDeviceSearchIndex(d);
    }

    // Backward compatibility tracking
    uniqueBeaconsMap.set(key, {
      mac: key,
      code: beacon.code || f.type,
      tag: beacon.tag || "-",
      rssi: beacon.rssi !== undefined ? beacon.rssi : f.rssi,
      batt: beacon.batt,
      raw: beacon.raw || f.raw,
      lastSeen: new Date(),
      hits: d.pkts
    });

    if (badgeUniqueCount) badgeUniqueCount.textContent = devicesMap.size;

    // IN-PLACE DOM TEXT UPDATE: KARTU TIDAK PERNAH DIHANCURKAN
    if (uniqueCardsList) {
      if (isNew) {
        if (activeTableTab === "unique") {
          const total = getFilteredUniqueDevices().length;
          const totalPages = Math.max(1, Math.ceil(total / UNIQUE_PER_PAGE));
          const currentDomCount = uniqueCardsList.querySelectorAll(".card").length;
          if ((uniqueCurrentPage === 1 && currentDomCount < UNIQUE_PER_PAGE) ||
              (uniqueCurrentPage === totalPages && currentDomCount < UNIQUE_PER_PAGE)) {
            renderUniquePage();
          } else {
            updatePaginationControls(total, totalPages);
          }
        }
      } else {
        const cardEl = uniqueCardsList.querySelector(`.card[data-mac="${CSS.escape(key)}"]`);
        if (cardEl) {
          // 1. Update RSSI & bars di header kartu
          const r = bestRssi(d);
          const rssiEl = cardEl.querySelector(".head .rssi");
          if (rssiEl) {
            rssiEl.innerHTML = `${bars(r)}<span>${r} dBm</span>`;
          }

          // 2. Update Total packets di sum
          const sumB = cardEl.querySelector(".sum b");
          if (sumB) {
            sumB.textContent = d.pkts.toLocaleString("en-US");
          }

          // 3. Update Frames count di meta
          const metaFramesB = cardEl.querySelector(".meta span:first-child b");
          if (metaFramesB) {
            metaFramesB.textContent = d.frames.length;
          }

          // 4. Update Last seen kartu
          const seenB = cardEl.querySelector(".meta [data-seen]");
          if (seenB) {
            seenB.textContent = fmtAgo(0);
          }

          // 5. Update Frame di dalam inner
          const frIdx = d.frames.findIndex(x => x.type === f.type);
          const frameEls = cardEl.querySelectorAll(".frame");
          if (frIdx >= 0 && frameEls[frIdx]) {
            const frEl = frameEls[frIdx];
            const fr = d.frames[frIdx];

            // Last seen frame
            const fseenB = frEl.querySelector("[data-fseen]");
            if (fseenB) fseenB.textContent = fmtAgo(0);

            // RSSI di dl
            const rssiDd = frEl.querySelector(".dl dd");
            if (rssiDd) {
              rssiDd.style.color = tone(fr.rssi);
              rssiDd.textContent = `${fr.rssi} dBm`;
            }

            // Update field dl
            const sc = SCHEMA[fr.type] || SCHEMA.raw;
            const dlDivs = frEl.querySelectorAll(".dl > div");
            dlDivs.forEach(row => {
              const dt = row.querySelector("dt");
              const dd = row.querySelector("dd");
              if (dt && dd && dt.textContent !== "RSSI") {
                const fieldEntry = sc.fields.find(([k, l]) => l === dt.textContent);
                if (fieldEntry && fr.data[fieldEntry[0]] !== undefined) {
                  dd.innerHTML = fmtVal(fr, fieldEntry[0], fr.data[fieldEntry[0]]);
                }
              }
            });

            // Update raw payload bytes info
            const frame44 = convertRaw42ToMqtt44(fr.raw, new Date());
            const spaced44 = formatSpacedHex(frame44);
            const rawEm = frEl.querySelector(".rawt em");
            if (rawEm) rawEm.textContent = `· 44 bytes`;

            // Update hex string
            const hexDiv = frEl.querySelector(".hex");
            if (hexDiv) hexDiv.textContent = spaced44;

            // Update copy raw button data
            const copyBtn = frEl.querySelector('.icobtn[data-act="copy"]');
            if (copyBtn) copyBtn.dataset.raw = frame44;
          } else if (frIdx >= 0) {
            const innerEl = cardEl.querySelector(".inner");
            if (innerEl) {
              const tempDiv = document.createElement("div");
              tempDiv.innerHTML = frameHTML(d, f, frIdx);
              if (tempDiv.firstElementChild) innerEl.appendChild(tempDiv.firstElementChild);
            }
          }
        }
      }
    }

    // Catat RSSI untuk kalkulasi rolling average sinyal
    if (beacon.rssi !== undefined && beacon.rssi !== null) {
      rssiSamples.push(beacon.rssi);
      if (rssiSamples.length > 50) rssiSamples.shift();
      if (elBeaconRssi && rssiSamples.length > 0) {
        const avgRssi = Math.round(rssiSamples.reduce((a, b) => a + b, 0) / rssiSamples.length);
        elBeaconRssi.textContent = avgRssi;
      }
    }

    // Deteksi tipe beacon
    if (elBeaconType && beacon.code) {
      elBeaconType.textContent = beacon.code;
    }

    // Rekam ke log lengkap sesi (untuk CSV export)
    fullSessionLog.push({
      mac: key,
      code: beacon.code || f.type,
      tag: beacon.tag || "-",
      rssi: beacon.rssi !== undefined ? beacon.rssi : f.rssi,
      batt: beacon.batt,
      raw: beacon.raw || f.raw,
      time: new Date(),
      timeStr: formatTime(new Date())
    });
    if (fullSessionLog.length > 5000) fullSessionLog.shift();

    // Masukkan ke continuous FIFO buffer (continuous stream, non-unique)
    packetStreamHistory.unshift({
      mac: key,
      code: beacon.code || f.type,
      tag: beacon.tag || "-",
      rssi: beacon.rssi !== undefined ? beacon.rssi : f.rssi,
      batt: beacon.batt,
      raw: beacon.raw || f.raw,
      time: new Date()
    });
    if (packetStreamHistory.length > maxHistory) {
      packetStreamHistory.pop();
    }
  }

  function updateTableHead() {
    if (!tableHead) return;
    if (activeTableTab === "fifo") {
      tableHead.innerHTML = `
        <tr>
          <th style="width:45px;">#</th>
          <th style="width:105px;">Timestamp</th>
          <th style="width:155px;">MAC Address</th>
          <th>Protocol / Format</th>
          <th style="width:105px;">RSSI</th>
          <th style="width:110px; text-align:center;">Raw 44B</th>
        </tr>
      `;
    } else if (activeTableTab === "unique") {
      tableHead.innerHTML = "";
    } else {
      tableHead.innerHTML = `
        <tr>
          <th style="width:45px;">#</th>
          <th style="width:105px;">Timestamp</th>
          <th style="width:155px;">MAC Address</th>
          <th style="width:130px;">Frame Type</th>
          <th>Identifier / Payload</th>
          <th style="width:105px;">RSSI</th>
          <th>Drop Reason</th>
          <th style="width:110px; text-align:center;">Raw 44B</th>
        </tr>
      `;
    }
  }

  // Fast Search Indexing Engine
  function buildDeviceSearchIndex(d) {
    if (!d) return;
    const parts = [d.mac || "", d.name || ""];
    if (d.frames && d.frames.length) {
      for (let i = 0; i < d.frames.length; i++) {
        const fr = d.frames[i];
        if (fr.type) parts.push(fr.type);
        if (fr.tag) parts.push(fr.tag);
        if (fr.data) {
          if (fr.data.uuid) parts.push(fr.data.uuid);
          if (fr.data.major !== undefined) parts.push(String(fr.data.major));
          if (fr.data.minor !== undefined) parts.push(String(fr.data.minor));
          if (fr.data.namespace) parts.push(fr.data.namespace);
          if (fr.data.instance) parts.push(fr.data.instance);
        }
      }
    }
    d._searchStr = parts.join(" ").toLowerCase();
  }

  function getFilteredUniqueDevices() {
    const q = (currentFilter || "").toLowerCase().trim();
    if (!q) return devices;
    return devices.filter(d => {
      if (!d._searchStr) buildDeviceSearchIndex(d);
      return d._searchStr.indexOf(q) !== -1 || matchBeacon(d, q);
    });
  }

  function updatePaginationControls(total, totalPages) {
    if (!uniquePaginationBar) return;
    if (total === 0 || activeTableTab !== "unique") {
      uniquePaginationBar.style.display = "none";
      return;
    }
    uniquePaginationBar.style.display = "flex";
    const startIdx = (uniqueCurrentPage - 1) * UNIQUE_PER_PAGE;
    const endIdx = Math.min(startIdx + UNIQUE_PER_PAGE, total);

    if (pagStart) pagStart.textContent = (startIdx + 1).toLocaleString("en-US");
    if (pagEnd) pagEnd.textContent = endIdx.toLocaleString("en-US");
    if (pagTotal) pagTotal.textContent = total.toLocaleString("en-US");
    if (pagPageCurrent) pagPageCurrent.textContent = `Page ${uniqueCurrentPage} of ${totalPages}`;

    if (btnPagFirst) btnPagFirst.disabled = (uniqueCurrentPage <= 1);
    if (btnPagPrev) btnPagPrev.disabled = (uniqueCurrentPage <= 1);
    if (btnPagNext) btnPagNext.disabled = (uniqueCurrentPage >= totalPages);
    if (btnPagLast) btnPagLast.disabled = (uniqueCurrentPage >= totalPages);
  }

  function renderUniquePage() {
    if (!uniqueCardsList) return;

    cachedFilteredDevices = getFilteredUniqueDevices();
    const total = cachedFilteredDevices.length;
    const totalPages = Math.max(1, Math.ceil(total / UNIQUE_PER_PAGE));

    if (uniqueCurrentPage > totalPages) uniqueCurrentPage = totalPages;
    if (uniqueCurrentPage < 1) uniqueCurrentPage = 1;

    if (total === 0) {
      const q = (currentFilter || "").trim();
      uniqueCardsList.innerHTML = `<div class="empty">${q ? "No results found" : (isTesting ? "No unique beacons detected yet..." : "Click <strong>Start Test</strong> above to scan unique beacons.")}</div>`;
      updatePaginationControls(0, 1);
      return;
    }

    const startIdx = (uniqueCurrentPage - 1) * UNIQUE_PER_PAGE;
    const endIdx = Math.min(startIdx + UNIQUE_PER_PAGE, total);
    const pageSlice = cachedFilteredDevices.slice(startIdx, endIdx);

    // Exact 10 items in DOM!
    uniqueCardsList.innerHTML = pageSlice.map(cardHTML).join("");
    updatePaginationControls(total, totalPages);
  }

  function goToUniquePage(p) {
    const total = getFilteredUniqueDevices().length;
    const totalPages = Math.max(1, Math.ceil(total / UNIQUE_PER_PAGE));
    if (p < 1) p = 1;
    if (p > totalPages) p = totalPages;
    uniqueCurrentPage = p;
    renderUniquePage();
    if (uniqueCardsList) {
      uniqueCardsList.scrollIntoView({ behavior: "smooth", block: "nearest" });
    }
  }

  function renderTable() {
    updateTableHead();
    const filterVal = currentFilter.toLowerCase().trim();

    if (activeTableTab === "fifo") {
      if (beaconTableWrapper) beaconTableWrapper.style.display = "block";
      if (uniqueCardsList) {
        uniqueCardsList.style.setProperty("display", "none", "important");
        uniqueCardsList.classList.add("is-hidden");
      }
      if (uniquePaginationBar) uniquePaginationBar.style.display = "none";
      if (!tableBody) return;

      let matched = packetStreamHistory;
      if (filterVal) {
        matched = packetStreamHistory.filter(b => matchBeacon(b, filterVal));
      }

      const displayList = matched.slice(0, 10);
      if (displayList.length === 0) {
        tableBody.innerHTML = `<tr><td colspan="6" style="text-align:center;color:var(--text-dim);padding:30px;">${filterVal ? "No beacon frames match the current filter." : (isTesting ? "Waiting for ingress beacon frames..." : "Click <strong>Start Test</strong> above to initiate live telemetry streaming.")}</td></tr>`;
        return;
      }

      let html = "";
      displayList.forEach((b, idx) => {
        const timeStr = formatTime(b.time);
        const hasRaw = b.raw && b.raw.length >= 10;

        html += `
          <tr class="${idx === 0 ? 'new-row' : ''}">
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);">${idx + 1}</td>
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);font-size:0.84rem;">${timeStr}</td>
            <td><span class="mono-mac">${escapeHtml(b.mac || "-")}</span></td>
            <td>${getProtoBadgeHtml(b.code)}</td>
            <td>${getRssiBarsHtml(b.rssi)}</td>
            <td style="text-align: center;">
              ${hasRaw ? `<button type="button" class="btn-raw-hex" data-act="toggle-fifo-raw" data-idx="${idx}">Raw Hex</button>` : `<span style="color:var(--text-dim);">-</span>`}
            </td>
          </tr>
        `;

        if (hasRaw) {
          html += `
            <tr id="fifo-drawer-${idx}" class="raw-drawer">
              <td colspan="6">
                ${parseRawBleBreakdown(b.raw)}
              </td>
            </tr>
          `;
        }
      });
      tableBody.innerHTML = html;

    } else if (activeTableTab === "unique") {
      if (beaconTableWrapper) beaconTableWrapper.style.display = "none";
      if (uniqueCardsList) {
        uniqueCardsList.style.setProperty("display", "flex", "important");
        uniqueCardsList.classList.remove("is-hidden");
      }
      renderUniquePage();
    } else {
      if (uniquePaginationBar) uniquePaginationBar.style.display = "none";
      // Dropped Data Tab Mode (8 Kolom: # | Waktu | MAC Address | Tipe / Format | Identitas / Tag | RSSI | Alasan Drop | Raw 44B)
      if (beaconTableWrapper) beaconTableWrapper.style.display = "block";
      if (uniqueCardsList) {
        uniqueCardsList.style.setProperty("display", "none", "important");
        uniqueCardsList.classList.add("is-hidden");
      }
      if (!tableBody) return;

      let matched = droppedBeaconsList;
      if (activeDropFilterReason !== "all") {
        const targetCode = Number(activeDropFilterReason);
        matched = matched.filter(b => Number(b.reason) === targetCode);
      }
      if (filterVal) {
        matched = matched.filter(b => {
          const reasonInfo = getDropReasonInfo(b.reason);
          return matchBeacon(b, filterVal) || reasonInfo.label.toLowerCase().includes(filterVal);
        });
      }
      matched = matched.slice(0, DROPPED_MAX_ITEMS);

      if (matched.length === 0) {
        const isFiltered = filterVal || (activeDropFilterReason !== "all");
        tableBody.innerHTML = `<tr><td colspan="8" style="text-align:center;color:var(--text-dim);padding:30px;">${isFiltered ? "No rejected packets match current filter." : (isTesting ? "No dropped or rejected packets recorded..." : "Click <strong>Start Test</strong> above to monitor live telemetry.")}</td></tr>`;
        return;
      }

      let html = "";
      matched.forEach((b, idx) => {
        const reasonInfo = getDropReasonInfo(b.reason);
        const timeStr = formatTime(b.time);
        const hasRaw = b.raw && b.raw.length >= 10;

        html += `
          <tr class="${idx === 0 ? 'new-row' : ''}">
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);">${idx + 1}</td>
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);font-size:0.84rem;">${timeStr}</td>
            <td><span class="mono-mac">${escapeHtml(b.mac || "-")}</span></td>
            <td>${getProtoBadgeHtml(b.code)}</td>
            <td><strong style="font-family:var(--font-mono, monospace);color:#38bdf8;">${escapeHtml(b.tag || "-")}</strong></td>
            <td>${getRssiBarsHtml(b.rssi)}</td>
            <td><span class="badge-drop ${reasonInfo.cssClass}">${reasonInfo.label}</span></td>
            <td style="text-align: center;">
              ${hasRaw ? `<button type="button" class="btn-raw-hex" data-act="toggle-drop-raw" data-idx="${idx}">Raw Hex</button>` : `<span style="color:var(--text-dim);">-</span>`}
            </td>
          </tr>
        `;

        if (hasRaw) {
          html += `
            <tr id="drop-drawer-${idx}" class="raw-drawer">
              <td colspan="8">
                ${parseRawBleBreakdown(b.raw, b.time)}
              </td>
            </tr>
          `;
        }
      });
      tableBody.innerHTML = html;
    }
  }

  // Drop Reason Filter Selector & UI Syncer
  function setDropReasonFilter(reason) {
    activeDropFilterReason = String(reason);
    const allCards = document.querySelectorAll(".drop-summary-card");
    allCards.forEach(c => {
      const cardReason = c.getAttribute("data-reason");
      if (cardReason === activeDropFilterReason) {
        c.classList.add("active");
      } else {
        c.classList.remove("active");
      }
    });

    if (dropActiveFilterBadge) {
      if (activeDropFilterReason === "all") {
        dropActiveFilterBadge.innerHTML = 'Showing: <strong>All Rejected Packets</strong>';
      } else {
        const info = getDropReasonInfo(activeDropFilterReason);
        dropActiveFilterBadge.innerHTML = `Filtered: <strong>${info.label}</strong> <span class="badge-clear-icon" title="Reset filter">×</span>`;
      }
    }
    renderTable();
  }

  function updateDropSummaryUI(ds) {
    if (ds && typeof ds === "object") {
      lastReceivedDropSummary = ds;
      if (sessionStartDropSummary === null && isTesting) {
        sessionStartDropSummary = {
          ttl: Number(ds.ttl) || 0,
          rssi: Number(ds.rssi) || 0,
          uuid: Number(ds.uuid) || 0,
          major: Number(ds.major) || 0,
          unknown: Number(ds.unknown !== undefined ? ds.unknown : ds.unknown_format) || 0,
          queue: Number(ds.queue) || 0,
          mqtt: Number(ds.mqtt) || 0
        };
      }
    } else {
      ds = lastReceivedDropSummary || {};
    }

    const base = (isTesting && sessionStartDropSummary) ? sessionStartDropSummary : { ttl: 0, rssi: 0, uuid: 0, major: 0, unknown: 0, queue: 0, mqtt: 0 };
    const rawUnknown = ds.unknown !== undefined ? ds.unknown : ds.unknown_format;

    const deltaTtl = Math.max(0, (Number(ds.ttl) || 0) - (Number(base.ttl) || 0));
    const deltaRssi = Math.max(0, (Number(ds.rssi) || 0) - (Number(base.rssi) || 0));
    const deltaUuid = Math.max(0, (Number(ds.uuid) || 0) - (Number(base.uuid) || 0));
    const deltaMajor = Math.max(0, (Number(ds.major) || 0) - (Number(base.major) || 0));
    const deltaUnknown = Math.max(0, (Number(rawUnknown) || 0) - (Number(base.unknown) || 0));
    const deltaQueue = Math.max(0, (Number(ds.queue) || 0) - (Number(base.queue) || 0));
    const deltaMqtt = Math.max(0, (Number(ds.mqtt) || 0) - (Number(base.mqtt) || 0));

    const ttl = Math.max(deltaTtl, liveDropCounts.ttl);
    const rssi = Math.max(deltaRssi, liveDropCounts.rssi);
    const uuid = Math.max(deltaUuid, liveDropCounts.uuid);
    const major = Math.max(deltaMajor, liveDropCounts.major);
    const unknown = Math.max(deltaUnknown, liveDropCounts.unknown);
    const queue = Math.max(deltaQueue, liveDropCounts.queue);
    const mqtt = Math.max(deltaMqtt, liveDropCounts.mqtt);
    const totalRejected = ttl + rssi + uuid + major + unknown + queue + mqtt;

    if (countDropTtl) countDropTtl.textContent = ttl.toLocaleString();
    if (countDropRssi) countDropRssi.textContent = rssi.toLocaleString();
    if (countDropUuid) countDropUuid.textContent = uuid.toLocaleString();
    if (countDropMajor) countDropMajor.textContent = major.toLocaleString();
    if (countDropUnknown) countDropUnknown.textContent = unknown.toLocaleString();
    if (countDropQueue) countDropQueue.textContent = queue.toLocaleString();
    if (countDropMqtt) countDropMqtt.textContent = mqtt.toLocaleString();

    if (countDropTotal) countDropTotal.textContent = totalRejected.toLocaleString();
    if (totalDroppedDisplay) totalDroppedDisplay.textContent = totalRejected.toLocaleString();

    // Rates calculation
    const totalIngress = sessionTotalRecv + totalRejected;
    if (rateDropTotal) {
      if (totalIngress > 0) {
        const rate = (totalRejected / totalIngress) * 100;
        rateDropTotal.textContent = rate.toFixed(2) + "% Ingress Rate";
      } else {
        rateDropTotal.textContent = "0.00% Ingress Rate";
      }
    }

    if (totalRejected > 0) {
      if (rateDropRssi) rateDropRssi.textContent = ((rssi / totalRejected) * 100).toFixed(1) + "% of drops";
      if (rateDropUuid) rateDropUuid.textContent = ((uuid / totalRejected) * 100).toFixed(1) + "% of drops";
      if (rateDropMajor) rateDropMajor.textContent = ((major / totalRejected) * 100).toFixed(1) + "% of drops";
      if (rateDropUnknown) rateDropUnknown.textContent = ((unknown / totalRejected) * 100).toFixed(1) + "% of drops";
      if (rateDropQueue) rateDropQueue.textContent = queue === 0 ? "Normal (0 Loss)" : ((queue / totalRejected) * 100).toFixed(1) + "% of drops";
      if (rateDropMqtt) rateDropMqtt.textContent = mqtt === 0 ? "Normal (0 Loss)" : ((mqtt / totalRejected) * 100).toFixed(1) + "% of drops";
      if (rateDropTtl) rateDropTtl.textContent = ttl === 0 ? "Normal (0 Loss)" : ((ttl / totalRejected) * 100).toFixed(1) + "% of drops";
    } else {
      if (rateDropRssi) rateDropRssi.textContent = "0% of drops";
      if (rateDropUuid) rateDropUuid.textContent = "0% of drops";
      if (rateDropMajor) rateDropMajor.textContent = "0% of drops";
      if (rateDropUnknown) rateDropUnknown.textContent = "0% of drops";
      if (rateDropQueue) rateDropQueue.textContent = "Normal (0 Loss)";
      if (rateDropMqtt) rateDropMqtt.textContent = "Normal (0 Loss)";
      if (rateDropTtl) rateDropTtl.textContent = "Normal (0 Loss)";
    }
  }

  function switchTab(tab) {
    activeTableTab = tab;
    if (tabFifo) {
      tabFifo.classList.toggle("active", tab === "fifo");
      tabFifo.setAttribute("aria-selected", tab === "fifo");
    }
    if (tabUnique) {
      tabUnique.classList.toggle("active", tab === "unique");
      tabUnique.setAttribute("aria-selected", tab === "unique");
    }
    if (tabDropped) {
      tabDropped.classList.toggle("active", tab === "dropped");
      tabDropped.setAttribute("aria-selected", tab === "dropped");
    }

    if (dropSummaryGrid) {
      dropSummaryGrid.style.display = (tab === "dropped") ? "grid" : "none";
    }
    if (toolbarLeftInfo) {
      toolbarLeftInfo.style.display = (tab === "dropped") ? "flex" : "none";
    }

    renderTable();
  }

  if (tabFifo) tabFifo.addEventListener("click", () => switchTab("fifo"));
  if (tabUnique) tabUnique.addEventListener("click", () => switchTab("unique"));
  if (tabDropped) tabDropped.addEventListener("click", () => switchTab("dropped"));

  // Interactive Click Delegation (Raw Drawers, Unique Cards & Frames, Copy, Delete)
  document.addEventListener("click", function(e) {
    const el = e.target.closest("[data-act]");
    if (!el) return;
    const act = el.getAttribute("data-act");
    const card = el.closest(".card");
    const mac = card && card.dataset.mac;

    if (act === "toggle") {
      if (!mac) return;
      const o = open.has(mac) ? (open.delete(mac), false) : (open.add(mac), true);
      card.classList.toggle("open", o);
      el.setAttribute("aria-expanded", o);
      if (!o) { // tutup kartu utama → tutup semua frame & raw di dalamnya
        [frOpen, rawOpen].forEach(set => [...set].forEach(k => { if (k.startsWith(mac + "#")) set.delete(k); }));
        card.querySelectorAll(".frame.open").forEach(f => f.classList.remove("open"));
        card.querySelectorAll(".hex.show").forEach(h => h.classList.remove("show"));
        card.querySelectorAll(".fh, .rawt").forEach(b => b.setAttribute("aria-expanded", "false"));
      }
    } else if (act === "frame") {
      const k = el.dataset.key;
      const frame = el.closest(".frame");
      if (frame) {
        const o = frame.classList.toggle("open");
        if (o) frOpen.add(k); else frOpen.delete(k);
        el.setAttribute("aria-expanded", o);
      }
    } else if (act === "raw") {
      const k = el.dataset.key;
      if (card) {
        const box = card.querySelector(`[data-hex="${CSS.escape(k)}"]`);
        if (box) {
          const s = box.classList.toggle("show");
          if (s) rawOpen.add(k); else rawOpen.delete(k);
          el.setAttribute("aria-expanded", s);
        }
      }
    } else if (act === "copy") {
      const raw = el.dataset.raw || el.getAttribute("data-raw");
      if (raw) {
        copyText(raw).then(() => {
          const old = el.innerHTML;
          el.classList.add("ok");
          el.innerHTML = ICON_CHECK;
          showToast("Raw hex payload copied to clipboard");
          setTimeout(() => {
            el.classList.remove("ok");
            el.innerHTML = old;
          }, 1500);
        }).catch(() => showToast("Failed to copy to clipboard"));
      }
    } else if (act === "del" || act === "delete") {
      const targetMac = mac || el.getAttribute("data-mac");
      if (targetMac) {
        devicesMap.delete(targetMac);
        uniqueBeaconsMap.delete(targetMac);
        uniqueBeaconsSet.delete(targetMac);
        devices = devices.filter(d => d.mac !== targetMac);
        open.delete(targetMac);
        [frOpen, rawOpen].forEach(set => [...set].forEach(k => { if (k.startsWith(targetMac + "#")) set.delete(k); }));
        if (card) card.remove();
        if (badgeUniqueCount) badgeUniqueCount.textContent = devicesMap.size;
        if (elUniqueTags) elUniqueTags.textContent = uniqueBeaconsSet.size;
        showToast(`Device ${targetMac} removed`);
        if (devices.length === 0 && uniqueCardsList) {
          uniqueCardsList.innerHTML = `<div class="empty">No beacon devices detected yet. Activate scanner to capture frames.</div>`;
        }
      }
    } else if (act === "toggle-fifo-raw") {
      const idx = el.getAttribute("data-idx");
      const drawer = document.getElementById("fifo-drawer-" + idx);
      if (drawer) drawer.classList.toggle("open");
    } else if (act === "toggle-drop-raw") {
      const idx = el.getAttribute("data-idx");
      const drawer = document.getElementById("drop-drawer-" + idx);
      if (drawer) drawer.classList.toggle("open");
    }
  });

  // =================================================================
  // 60-SECOND REALTIME CANVAS TELEMETRY CHART ENGINE (TASK MANAGER STYLE)
  // =================================================================
  function sampleChartData() {
    if (!isTesting) return;

    chartData.push({
      readSpeed: curReadSpeed,
      mqttSpeed: curMqttSpeed,
      queueLoad: curQueueLoad,
      batchLoad: curBatchLoad,
      batchPktSize: curBatchPktSize,
      netBandwidth: curNetBandwidth,
      time: new Date()
    });

    if (chartData.length > 60) {
      chartData.shift();
    }

    drawChart();
  }

  function drawChart() {
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    // Retina / High-DPI Scaling
    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.getBoundingClientRect();
    const w = rect.width;
    const h = rect.height;

    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
    }

    ctx.save();
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, w, h);

    const padLeft = 40;
    const padRight = 14;
    const padTop = 18;
    const padBottom = 26;
    const plotW = w - padLeft - padRight;
    const plotH = h - padTop - padBottom;

    if (plotW <= 0 || plotH <= 0) {
      ctx.restore();
      return;
    }

    // 1. Tentukan Skala Maksimum Y secara dinamis
    let maxVal = 50; // Nilai dasar minimal agar skala tidak 0
    for (let i = 0; i < chartData.length; i++) {
      const d = chartData[i];
      if (d.readSpeed > maxVal) maxVal = d.readSpeed;
      if (d.mqttSpeed > maxVal) maxVal = d.mqttSpeed;
      if (d.queueLoad > maxVal) maxVal = d.queueLoad;
      if (d.batchLoad > maxVal) maxVal = d.batchLoad;
      if (d.batchPktSize && d.batchPktSize > maxVal) maxVal = d.batchPktSize;
    }
    // Round up ke kelipatan terdekat (misal 50, 100, 200, 500)
    maxVal = Math.ceil(maxVal * 1.15);
    if (maxVal < 10) maxVal = 10;

    // 2. Gambar Kisi-Kisi Halus (Task Manager Grid)
    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(255, 255, 255, 0.05)";

    // Garis Grid Horizontal (4 baris)
    const ySteps = 4;
    ctx.font = "10px monospace";
    ctx.fillStyle = "rgba(148, 163, 184, 0.5)";
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";

    for (let i = 0; i <= ySteps; i++) {
      const y = padTop + (plotH / ySteps) * i;
      ctx.beginPath();
      ctx.moveTo(padLeft, y);
      ctx.lineTo(padLeft + plotW, y);
      ctx.stroke();

      // Label Skala Y di kiri
      const valLabel = Math.round(maxVal - (maxVal / ySteps) * i);
      ctx.fillText(valLabel, padLeft - 6, y);
    }

    // Garis Grid Vertikal (Interval 10 Detik: 6 Kolom)
    const xSteps = 6;
    for (let i = 0; i <= xSteps; i++) {
      const x = padLeft + (plotW / xSteps) * i;
      ctx.beginPath();
      ctx.moveTo(x, padTop);
      ctx.lineTo(x, padTop + plotH);
      ctx.stroke();
    }

    // 3. Render 4 Garis Kurva Telemetri
    const series = [
      { key: "readSpeed", color: "#22d3ee", fill: "rgba(34, 211, 238, 0.12)" },
      { key: "mqttSpeed", color: "#34d399", fill: "rgba(52, 211, 153, 0.08)" },
      { key: "queueLoad", color: "#fbbf24", fill: "rgba(251, 191, 36, 0.08)" },
      { key: "batchLoad", color: "#f43f5e", fill: "rgba(244, 63, 94, 0.10)" },
      { key: "batchPktSize", color: "#818cf8", fill: "rgba(129, 140, 248, 0.10)" },
      { key: "netBandwidth", color: "#38bdf8", fill: "rgba(56, 189, 248, 0.08)" }
    ];

    if (chartData.length > 1) {
      const totalPoints = 60; // Lebar grafik mewakili 60 detik
      const stepX = plotW / (totalPoints - 1);
      const startIndex = totalPoints - chartData.length;

      series.forEach(s => {
        ctx.beginPath();
        let firstX = 0, firstY = 0, lastX = 0;

        for (let i = 0; i < chartData.length; i++) {
          const pt = chartData[i];
          const x = padLeft + (startIndex + i) * stepX;
          const val = Math.max(0, pt[s.key] || 0);
          const y = padTop + plotH - (val / maxVal) * plotH;

          if (i === 0) {
            ctx.moveTo(x, y);
            firstX = x;
            firstY = y;
          } else {
            ctx.lineTo(x, y);
          }
          lastX = x;
        }

        // Stroke Line
        ctx.strokeStyle = s.color;
        ctx.lineWidth = 1.8;
        ctx.shadowColor = s.color;
        ctx.shadowBlur = 6;
        ctx.stroke();

        // Area Fill
        ctx.shadowBlur = 0;
        ctx.lineTo(lastX, padTop + plotH);
        ctx.lineTo(firstX, padTop + plotH);
        ctx.closePath();
        ctx.fillStyle = s.fill;
        ctx.fill();
      });

      // 4. Hover Crosshair & Data Indicator
      if (hoverIndex >= 0 && hoverIndex < chartData.length) {
        const hx = padLeft + (startIndex + hoverIndex) * stepX;
        ctx.beginPath();
        ctx.setLineDash([3, 3]);
        ctx.strokeStyle = "rgba(255, 255, 255, 0.4)";
        ctx.lineWidth = 1;
        ctx.moveTo(hx, padTop);
        ctx.lineTo(hx, padTop + plotH);
        ctx.stroke();
        ctx.setLineDash([]); // Reset line dash

        // Draw circles on series points
        const hoveredPt = chartData[hoverIndex];
        series.forEach(s => {
          const val = Math.max(0, hoveredPt[s.key] || 0);
          const hy = padTop + plotH - (val / maxVal) * plotH;
          ctx.beginPath();
          ctx.arc(hx, hy, 3.5, 0, Math.PI * 2);
          ctx.fillStyle = s.color;
          ctx.fill();
          ctx.strokeStyle = "#0f172a";
          ctx.lineWidth = 1.5;
          ctx.stroke();
        });
      }
    }

    ctx.restore();
  }

  // Canvas Mouse Interaction for Tooltip
  if (canvas && chartWrapper) {
    canvas.addEventListener("mousemove", function(e) {
      if (chartData.length === 0) return;
      const rect = canvas.getBoundingClientRect();
      const padLeft = 40;
      const padRight = 14;
      const plotW = rect.width - padLeft - padRight;
      const mouseX = e.clientX - rect.left;

      if (mouseX < padLeft || mouseX > padLeft + plotW) {
        if (chartTooltip) chartTooltip.style.display = "none";
        hoverIndex = -1;
        drawChart();
        return;
      }

      const totalPoints = 60;
      const stepX = plotW / (totalPoints - 1);
      const startIndex = totalPoints - chartData.length;
      const rawIdx = Math.round((mouseX - padLeft) / stepX) - startIndex;

      if (rawIdx >= 0 && rawIdx < chartData.length) {
        hoverIndex = rawIdx;
        drawChart();

        if (chartTooltip) {
          const pt = chartData[hoverIndex];
          const timeStr = formatTime(pt.time);
          const secAgo = totalPoints - (startIndex + hoverIndex) - 1;

          chartTooltip.innerHTML = `
            <div class="chart-tooltip-title">${secAgo === 0 ? 'Sekarang (Live)' : secAgo + 's lalu'} • ${timeStr}</div>
            <div class="chart-tooltip-row"><span style="color:#22d3ee;">Tag Read Speed</span><strong>${pt.readSpeed} pkt/s</strong></div>
            <div class="chart-tooltip-row"><span style="color:#34d399;">MQTT Speed</span><strong>${pt.mqttSpeed} batch/s</strong></div>
            <div class="chart-tooltip-row"><span style="color:#fbbf24;">Queue RTOS</span><strong>${pt.queueLoad} / 300</strong></div>
            <div class="chart-tooltip-row"><span style="color:#f43f5e;">Batch Buffer</span><strong>${pt.batchLoad} / ${curBatchMax}</strong></div>
            <div class="chart-tooltip-row"><span style="color:#818cf8;">Pkt / Batch</span><strong>${pt.batchPktSize || 0} pkt</strong></div>
            <div class="chart-tooltip-row"><span style="color:#38bdf8;">Net Bandwidth</span><strong>${(pt.netBandwidth || 0).toFixed(2)} KB/s</strong></div>
          `;

          // Position Tooltip
          let posX = mouseX + 15;
          if (posX + 180 > rect.width) posX = mouseX - 190;
          chartTooltip.style.left = posX + "px";
          chartTooltip.style.top = "20px";
          chartTooltip.style.display = "block";
        }
      }
    });

    canvas.addEventListener("mouseleave", function() {
      hoverIndex = -1;
      if (chartTooltip) chartTooltip.style.display = "none";
      drawChart();
    });

    // Resize Observer for Fluid Responsive Canvas
    window.addEventListener("resize", drawChart);
  }

  function initWebSocket() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
      return;
    }

    try {
      ws = new WebSocket(getWsUrl());

      ws.onopen = () => {
        console.log("[WS] Connected");
        if (isTesting) {
          ws.send(JSON.stringify({ action: "start" }));
        }
      };

      ws.onmessage = (event) => {
        if (!isTesting) return;
        try {
          const msg = JSON.parse(event.data);

          // Update Throughput HUD & Snapshot values
          if (msg.read_speed !== undefined) {
            curReadSpeed = msg.read_speed;
            if (elReadSpeed) {
              elReadSpeed.textContent = (curReadSpeed >= 1000) ? formatMetricK(curReadSpeed) : curReadSpeed;
              elReadSpeed.title = `${curReadSpeed.toLocaleString('en-US')} pkt/s`;
            }
            readSpeedSamples.push(curReadSpeed);
            if (readSpeedSamples.length > 60) readSpeedSamples.shift();
            const avgRead = (readSpeedSamples.reduce((a, b) => a + b, 0) / readSpeedSamples.length).toFixed(1);
            maxReadSpeed = Math.max(maxReadSpeed, curReadSpeed);
            if (elReadAvg) elReadAvg.textContent = formatMetricK(avgRead);
            if (elReadMax) elReadMax.textContent = formatMetricK(maxReadSpeed);
          }
          if (msg.scan_speed !== undefined && elScanSpeed) {
            elScanSpeed.textContent = formatMetricK(msg.scan_speed);
            elScanSpeed.title = `${Number(msg.scan_speed).toLocaleString('en-US')} pkt/s`;
          }
          if (msg.drop_speed !== undefined && elDropSpeed) {
            elDropSpeed.textContent = formatMetricK(msg.drop_speed);
            elDropSpeed.title = `${Number(msg.drop_speed).toLocaleString('en-US')} pkt/s`;
          }
          if (msg.mqtt_speed !== undefined) {
            curMqttSpeed = msg.mqtt_speed;
            if (elMqttSpeed) elMqttSpeed.textContent = curMqttSpeed;
            mqttSpeedSamples.push(curMqttSpeed);
            if (mqttSpeedSamples.length > 60) mqttSpeedSamples.shift();
            const avgMqtt = (mqttSpeedSamples.reduce((a, b) => a + b, 0) / mqttSpeedSamples.length).toFixed(1);
            maxMqttSpeed = Math.max(maxMqttSpeed, curMqttSpeed);
            if (elMqttAvg) elMqttAvg.textContent = formatMetricK(avgMqtt);
            if (elMqttMax) elMqttMax.textContent = formatMetricK(maxMqttSpeed);
          }
          if (msg.raw_ingress !== undefined) {
            if (sessionStartRawIngress === null) {
              sessionStartRawIngress = msg.raw_ingress;
            }
            sessionTotalRawIngress = Math.max(0, msg.raw_ingress - sessionStartRawIngress);
            updateDropRate();
          }
          if (msg.beacons_read !== undefined) {
            if (sessionStartBeaconsRead === null) {
              sessionStartBeaconsRead = msg.beacons_read;
            }
            sessionTotalRecv = Math.max(0, msg.beacons_read - sessionStartBeaconsRead);
            updateDropRate();
          }
          if (msg.mqtt_published !== undefined) {
            if (sessionStartMqttPublished === null) {
              sessionStartMqttPublished = msg.mqtt_published;
            }
            sessionTotalSent = Math.max(0, msg.mqtt_published - sessionStartMqttPublished);
            updateDropRate();
          }
          if (msg.dropped_count !== undefined) {
            curTotalDropped = msg.dropped_count;
            if (sessionStartDropped === null) {
              sessionStartDropped = curTotalDropped;
            }
            sessionDropped = Math.max(0, curTotalDropped - sessionStartDropped);
            updateDropRate();
          }
          if (msg.free_heap !== undefined) {
            const heapKb = (msg.free_heap > 1024) ? Math.round(msg.free_heap / 1024) : msg.free_heap;
            if (elFreeHeap) elFreeHeap.textContent = heapKb;
            const heapPct = Math.min(100, Math.max(0, Math.round((heapKb / 320) * 100)));
            if (barFreeHeap) barFreeHeap.style.width = heapPct + "%";
            if (elHeapBadge) elHeapBadge.textContent = `${heapPct}% Left`;
          }
          if (msg.queue_load !== undefined) {
            curQueueLoad = (typeof msg.queue_load === "number") ? msg.queue_load : parseInt(msg.queue_load, 10) || 0;
            const qMax = (typeof msg.queue_max === "number") ? msg.queue_max : (parseInt(document.getElementById("stat_queue_max")?.textContent, 10) || 150);
            const qPct = Math.min(100, Math.round((curQueueLoad / qMax) * 100));
            if (elQueueLoad) elQueueLoad.textContent = curQueueLoad;
            if (msg.queue_max !== undefined && document.getElementById("stat_queue_max")) {
              document.getElementById("stat_queue_max").textContent = qMax;
            }
            if (barQueueLoad) barQueueLoad.style.width = qPct + "%";
            if (elQueueBadge) {
              if (qPct > 75) {
                elQueueBadge.className = "hud-badge badge-amber";
                elQueueBadge.textContent = `${qPct}% High Load`;
              } else {
                elQueueBadge.className = "hud-badge badge-green";
                elQueueBadge.textContent = `${qPct}% Load Safe`;
              }
            }
          }
          if (msg.batch_load !== undefined) {
            curBatchLoad = msg.batch_load;
            if (msg.batch_max !== undefined) curBatchMax = msg.batch_max;
            else if (document.getElementById("stat_batch_max")) {
              const bM = parseInt(document.getElementById("stat_batch_max").textContent, 10);
              if (!isNaN(bM) && bM > 0) curBatchMax = bM;
            }
            const bPct = Math.min(100, Math.round((curBatchLoad / curBatchMax) * 100));
            if (elBatchLoad) elBatchLoad.textContent = curBatchLoad;
            if (msg.batch_max !== undefined && document.getElementById("stat_batch_max")) {
              document.getElementById("stat_batch_max").textContent = curBatchMax;
            }
            if (barBatchLoad) barBatchLoad.style.width = bPct + "%";
            if (elBatchBadge) elBatchBadge.textContent = `${bPct}% Buffer`;
          }
          if (msg.batch_pkt_size !== undefined) {
            curBatchPktSize = msg.batch_pkt_size;
            const pPct = Math.min(100, Math.round((curBatchPktSize / curBatchMax) * 100));
            if (elBatchPktSize) elBatchPktSize.textContent = curBatchPktSize;
            if (barBatchPkt) barBatchPkt.style.width = pPct + "%";
          }
          if (msg.net_bandwidth !== undefined) {
            curNetBandwidth = (typeof msg.net_bandwidth === "number") ? msg.net_bandwidth : (parseFloat(msg.net_bandwidth) || 0.0);
            if (elNetBandwidth) elNetBandwidth.textContent = curNetBandwidth.toFixed(2);
          }
          if (msg.total_bytes !== undefined) {
            if (sessionStartBytes === null) {
              sessionStartBytes = msg.total_bytes;
            }
            sessionTotalBytes = Math.max(0, msg.total_bytes - sessionStartBytes);
            if (elSessionTotalBandwidth) elSessionTotalBandwidth.textContent = formatBytes(sessionTotalBytes);
          } else if (msg.net_bandwidth !== undefined && isTesting) {
            sessionTotalBytes += Math.round(curNetBandwidth * 1024);
            if (elSessionTotalBandwidth) elSessionTotalBandwidth.textContent = formatBytes(sessionTotalBytes);
          }

          // Process incoming beacons (single or array)
          if (msg.beacons && Array.isArray(msg.beacons) && msg.beacons.length > 0) {
            for (let i = 0; i < msg.beacons.length; i++) {
              insertBeacon(msg.beacons[i]);
            }
            if (activeTableTab === "fifo") {
              renderTable();
            }
          } else if (msg.code && msg.tag) {
            insertBeacon(msg);
            if (activeTableTab === "fifo") {
              renderTable();
            }
          }

          // Process drop summary metrics & live dropped beacons
          if (msg.drop_summary) {
            updateDropSummaryUI(msg.drop_summary);
          }

          if (msg.dropped_beacons && Array.isArray(msg.dropped_beacons) && msg.dropped_beacons.length > 0) {
            for (let i = 0; i < msg.dropped_beacons.length; i++) {
              const db = msg.dropped_beacons[i];
              recordLiveDrop(db.reason);
              droppedBeaconsList.unshift({
                code: db.code || "BLE",
                tag: db.tag || "-",
                mac: db.mac || "-",
                raw: db.raw || "",
                rssi: db.rssi,
                reason: db.reason,
                time: new Date()
              });
              if (droppedBeaconsList.length > DROPPED_MAX_ITEMS) {
                droppedBeaconsList.length = DROPPED_MAX_ITEMS;
              }
            }
            updateDropSummaryUI(lastReceivedDropSummary);
            if (activeTableTab === "dropped") {
              scheduleDropTableRender();
            }
          }
        } catch (e) {
          console.error("[WS] Parse error:", e);
        }
      };

      ws.onclose = () => {
        console.log("[WS] Disconnected");
        // Hanya verifikasi status jika pengetesan sedang aktif (bukan saat user bernavigasi keluar)
        if (isTesting) {
          fetch("/api/v1/auth/check", { cache: "no-store" })
            .then(res => {
              if (res.status === 401) {
                // Sesi hangus karena restart/expired -> Redirect ke login
                window.location.href = "/login.html";
              } else if (isTesting) {
                setTimeout(initWebSocket, 2000);
              }
            })
            .catch(() => {
              if (isTesting) {
                setTimeout(initWebSocket, 2000);
              }
            });
        }
      };

      ws.onerror = (err) => {
        console.warn("[WS] Error:", err);
      };
    } catch (err) {
      console.warn("[WS] Init error:", err);
    }
  }

  // Start Test Button
  if (btnStart) {
    btnStart.addEventListener("click", function() {
      isTesting = true;
      btnStart.disabled = true;
      btnStart.style.display = "none";
      btnStop.disabled = false;
      btnStop.style.display = "inline-flex";

      if (statusBadge) {
        statusBadge.className = "status-pill";
        statusBadge.innerHTML = `<span class="pulse-dot"></span><span id="testStatusText">STREAMING LIVE</span>`;
      }

      // Reset Session Metrics & Timers
      sessionStartTime = Date.now();
      sessionTotalRecv = 0;
      sessionTotalSent = 0;
      sessionStartRawIngress = null;
      sessionTotalRawIngress = 0;
      sessionStartBeaconsRead = null;
      sessionStartMqttPublished = null;
      sessionStartDropped = null;
      sessionDropped = 0;
      curTotalDropped = 0;
      fullSessionLog = [];
      readSpeedSamples = [];
      mqttSpeedSamples = [];
      rssiSamples = [];
      maxReadSpeed = 0;
      maxMqttSpeed = 0;

      if (elSessionElapsed) elSessionElapsed.textContent = "00:00:00";
      if (sessionTimerInterval) clearInterval(sessionTimerInterval);
      sessionTimerInterval = setInterval(updateSessionTimer, 1000);
      updateDropRate();

      if (elReadAvg) elReadAvg.textContent = "0.0";
      if (elReadMax) elReadMax.textContent = "0";
      if (elScanSpeed) elScanSpeed.textContent = "0";
      if (elDropSpeed) elDropSpeed.textContent = "0";
      if (elReadSpeed) elReadSpeed.textContent = "0";
      if (elMqttAvg) elMqttAvg.textContent = "0.0";
      if (elMqttMax) elMqttMax.textContent = "0";
      if (elBeaconRssi) elBeaconRssi.textContent = "-";

      // Reset Ingress Packet Stream & FIFO Table
      packetStreamHistory = [];

      // Reset Unique Beacon Counter & Batch Pkt Size
      uniqueBeaconsSet.clear();
      uniqueBeaconsMap.clear();
      devicesMap.clear();
      devices = [];
      uniqueCurrentPage = 1;
      open.clear();
      frOpen.clear();
      rawOpen.clear();
      if (uniqueCardsList) uniqueCardsList.innerHTML = `<div class="empty">No beacon devices detected yet. Activate scanner to capture frames.</div>`;
      if (uniquePaginationBar) uniquePaginationBar.style.display = "none";
      if (badgeUniqueCount) badgeUniqueCount.textContent = "0";
      if (elUniqueTags) elUniqueTags.textContent = "0";
      curBatchPktSize = 0;
      if (elBatchPktSize) elBatchPktSize.textContent = "0";
      curNetBandwidth = 0.0;
      if (elNetBandwidth) elNetBandwidth.textContent = "0.00";
      sessionStartBytes = null;
      sessionTotalBytes = 0;
      if (elSessionTotalBandwidth) elSessionTotalBandwidth.textContent = "0.00 KB";

      // Reset Dropped Data
      droppedBeaconsList = [];
      liveDropCounts = { ttl: 0, rssi: 0, uuid: 0, major: 0, unknown: 0, queue: 0, mqtt: 0 };
      lastReceivedDropSummary = {};
      sessionStartDropSummary = null;
      if (totalDroppedDisplay) totalDroppedDisplay.textContent = "0";
      if (countDropTotal) countDropTotal.textContent = "0";
      if (rateDropTotal) rateDropTotal.textContent = "0.00% Ingress Rate";
      if (countDropTtl) countDropTtl.textContent = "0";
      if (rateDropTtl) rateDropTtl.textContent = "Normal (0 Loss)";
      if (countDropRssi) countDropRssi.textContent = "0";
      if (rateDropRssi) rateDropRssi.textContent = "0% of drops";
      if (countDropUuid) countDropUuid.textContent = "0";
      if (rateDropUuid) rateDropUuid.textContent = "0% of drops";
      if (countDropMajor) countDropMajor.textContent = "0";
      if (rateDropMajor) rateDropMajor.textContent = "0% of drops";
      if (countDropUnknown) countDropUnknown.textContent = "0";
      if (rateDropUnknown) rateDropUnknown.textContent = "0% of drops";
      if (countDropQueue) countDropQueue.textContent = "0";
      if (rateDropQueue) rateDropQueue.textContent = "Normal (0 Loss)";
      if (countDropMqtt) countDropMqtt.textContent = "0";
      if (rateDropMqtt) rateDropMqtt.textContent = "Normal (0 Loss)";
      setDropReasonFilter("all");
      renderTable();

      // Reset & Start 60-Second Realtime Chart
      chartData.length = 0;
      if (chartInterval) clearInterval(chartInterval);
      sampleChartData(); // Sample initial data point immediately
      chartInterval = setInterval(sampleChartData, 1000); // 1Hz sampling rate

      // Start keepalive ping every 30 seconds during active testing (sliding cookie renewal)
      if (keepAliveTimer) clearInterval(keepAliveTimer);
      keepAliveTimer = setInterval(() => {
        if (isTesting) {
          fetch("/api/v1/auth/check", { cache: "no-store" }).catch(() => {});
        }
      }, 30000);

      initWebSocket();
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ action: "start" }));
      }
    });
  }

  // Centralized Stop Test Handler
  function stopTest() {
    if (!isTesting) return;
    isTesting = false;

    if (btnStop) {
      btnStop.disabled = true;
      btnStop.style.display = "none";
    }
    if (btnStart) {
      btnStart.disabled = false;
      btnStart.style.display = "inline-flex";
    }

    if (statusBadge) {
      statusBadge.className = "status-pill";
      statusBadge.style.background = "rgba(148, 163, 184, 0.12)";
      statusBadge.style.borderColor = "rgba(148, 163, 184, 0.3)";
      statusBadge.style.color = "#94a3b8";
      statusBadge.innerHTML = `<span>STANDBY</span>`;
    }

    // Freeze Session Timer (Keep elapsed time visible for inspection)
    if (sessionTimerInterval) {
      clearInterval(sessionTimerInterval);
      sessionTimerInterval = null;
    }

    // Freeze Chart (Stop sampling interval so technician can inspect curve)
    if (chartInterval) {
      clearInterval(chartInterval);
      chartInterval = null;
    }
    if (keepAliveTimer) {
      clearInterval(keepAliveTimer);
      keepAliveTimer = null;
    }
    drawChart();

    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ action: "stop" }));
    }
  }

  // Stop Test Button
  if (btnStop) {
    btnStop.addEventListener("click", stopTest);
  }

  // =================================================================
  // NAVIGATION GUARD & ACTIVE TEST WARNING
  // =================================================================
  function confirmLeavePage(targetUrl) {
    if (!isTesting) {
      window.location.href = targetUrl;
      return;
    }

    if (typeof Swal !== "undefined") {
      Swal.fire({
        title: "Active Test Session in Progress",
        html: "Live telemetry test is currently active. If you navigate away, the live stream will stop and unsaved test session data will be lost.<br><br>Do you want to proceed?",
        icon: "warning",
        showCancelButton: true,
        confirmButtonText: "Lanjut",
        confirmButtonColor: "#ef4444",
        cancelButtonText: "Cancel",
        cancelButtonColor: "#64748b"
      }).then((result) => {
        if (result.isConfirmed) {
          stopTest();
          window.location.href = targetUrl;
        }
      });
    } else {
      const leave = confirm("Live telemetry test is currently active. If you navigate away, the live stream will stop and unsaved test session data will be lost.\n\nDo you want to proceed?");
      if (leave) {
        stopTest();
        window.location.href = targetUrl;
      }
    }
  }

  // Intercept all internal navigation link clicks when testing
  document.addEventListener("click", function(e) {
    if (!isTesting) return;

    const link = e.target.closest("a");
    if (!link) return;

    const href = link.getAttribute("href");
    if (!href || href === "#" || href.startsWith("javascript:") || href.startsWith("blob:") || href.startsWith("data:")) {
      return;
    }

    // Allow download links (e.g. export CSV created dynamically)
    if (link.hasAttribute("download")) {
      return;
    }

    // Ignore links targeting same test page
    const pathname = window.location.pathname;
    if (href === pathname || href === "/test" || href === "/test.html" || href.startsWith("#")) {
      return;
    }

    e.preventDefault();
    e.stopPropagation();
    confirmLeavePage(link.href);
  }, true);

  // Intercept power sheet buttons (Reboot & Factory Reset) if clicked during test
  const sheetBtnReboot = document.getElementById("sheetBtnReboot");
  if (sheetBtnReboot) {
    sheetBtnReboot.addEventListener("click", function(e) {
      if (isTesting) {
        e.preventDefault();
        e.stopImmediatePropagation();
        confirmLeavePage("/reboot");
      }
    }, true);
  }
  const sheetBtnReset = document.getElementById("sheetBtnReset");
  if (sheetBtnReset) {
    sheetBtnReset.addEventListener("click", function(e) {
      if (isTesting) {
        e.preventDefault();
        e.stopImmediatePropagation();
        confirmLeavePage("/reset");
      }
    }, true);
  }

  // Browser-level reload / close tab guard
  window.addEventListener("beforeunload", function(e) {
    if (isTesting) {
      e.preventDefault();
      e.returnValue = "Live beacon monitor is active. If you navigate away, the monitoring session will terminate and real-time capture data will be reset.";
      return e.returnValue;
    }
  });

  // Export CSV Button

  // Drop Summary Cards Click-To-Filter Listener
  if (dropSummaryGrid) {
    dropSummaryGrid.addEventListener("click", function(e) {
      const card = e.target.closest(".drop-summary-card");
      if (!card) return;
      const reason = card.getAttribute("data-reason");
      if (reason) {
        setDropReasonFilter(reason);
      }
    });
  }

  if (dropActiveFilterBadge) {
    dropActiveFilterBadge.addEventListener("click", function() {
      setDropReasonFilter("all");
    });
  }

  if (btnExportCsv) {
    btnExportCsv.addEventListener("click", exportToCsv);
  }

  // Filter Search Input (Debounced 50ms for Butter-Smooth Keystrokes)
  if (filterInput) {
    let searchDebounceTimer = null;
    filterInput.addEventListener("input", function() {
      currentFilter = this.value;
      uniqueCurrentPage = 1; // Always reset to page 1 on new query
      clearTimeout(searchDebounceTimer);
      searchDebounceTimer = setTimeout(() => {
        renderTable();
      }, 50);
    });
  }

  // Unique Beacons Windowed Pagination Navigation Listeners
  if (btnPagFirst) btnPagFirst.addEventListener("click", () => goToUniquePage(1));
  if (btnPagPrev) btnPagPrev.addEventListener("click", () => goToUniquePage(uniqueCurrentPage - 1));
  if (btnPagNext) btnPagNext.addEventListener("click", () => goToUniquePage(uniqueCurrentPage + 1));
  if (btnPagLast) {
    btnPagLast.addEventListener("click", () => {
      const total = getFilteredUniqueDevices().length;
      const totalPages = Math.max(1, Math.ceil(total / UNIQUE_PER_PAGE));
      goToUniquePage(totalPages);
    });
  }

  // Testing hooks for automated browser verification
  window._testApi = {
    injectMessage: function(msg) {
      if (!isTesting) isTesting = true;
      if (msg.raw_ingress !== undefined) {
        if (sessionStartRawIngress === null) {
          sessionStartRawIngress = msg.raw_ingress;
        }
        sessionTotalRawIngress = Math.max(0, msg.raw_ingress - sessionStartRawIngress);
        updateDropRate();
      }
      if (msg.beacons_read !== undefined) {
        if (sessionStartBeaconsRead === null) {
          sessionStartBeaconsRead = msg.beacons_read;
        }
        sessionTotalRecv = Math.max(0, msg.beacons_read - sessionStartBeaconsRead);
        updateDropRate();
      }
      if (msg.read_speed !== undefined) {
        curReadSpeed = msg.read_speed;
        if (elReadSpeed) {
          elReadSpeed.textContent = (curReadSpeed >= 1000) ? formatMetricK(curReadSpeed) : curReadSpeed;
          elReadSpeed.title = `${curReadSpeed.toLocaleString('en-US')} pkt/s`;
        }
        readSpeedSamples.push(curReadSpeed);
        if (readSpeedSamples.length > 60) readSpeedSamples.shift();
        const avgRead = (readSpeedSamples.reduce((a, b) => a + b, 0) / readSpeedSamples.length).toFixed(1);
        maxReadSpeed = Math.max(maxReadSpeed, curReadSpeed);
        if (elReadAvg) elReadAvg.textContent = formatMetricK(avgRead);
        if (elReadMax) elReadMax.textContent = formatMetricK(maxReadSpeed);
      }
      if (msg.scan_speed !== undefined && elScanSpeed) {
        elScanSpeed.textContent = formatMetricK(msg.scan_speed);
        elScanSpeed.title = `${Number(msg.scan_speed).toLocaleString('en-US')} pkt/s`;
      }
      if (msg.drop_speed !== undefined && elDropSpeed) {
        elDropSpeed.textContent = formatMetricK(msg.drop_speed);
        elDropSpeed.title = `${Number(msg.drop_speed).toLocaleString('en-US')} pkt/s`;
      }
      if (msg.mqtt_speed !== undefined) {
        curMqttSpeed = msg.mqtt_speed;
        if (elMqttSpeed) elMqttSpeed.textContent = curMqttSpeed;
        mqttSpeedSamples.push(curMqttSpeed);
        if (mqttSpeedSamples.length > 60) mqttSpeedSamples.shift();
        const avgMqtt = (mqttSpeedSamples.reduce((a, b) => a + b, 0) / mqttSpeedSamples.length).toFixed(1);
        maxMqttSpeed = Math.max(maxMqttSpeed, curMqttSpeed);
        if (elMqttAvg) elMqttAvg.textContent = formatMetricK(avgMqtt);
        if (elMqttMax) elMqttMax.textContent = formatMetricK(maxMqttSpeed);
      }
      if (msg.mqtt_published !== undefined) {
        if (sessionStartMqttPublished === null) {
          sessionStartMqttPublished = msg.mqtt_published;
        }
        sessionTotalSent = Math.max(0, msg.mqtt_published - sessionStartMqttPublished);
        if (sessionTotalRecv < sessionTotalSent) {
          sessionTotalRecv = sessionTotalSent;
        }
        updateDropRate();
      }
      if (msg.dropped_count !== undefined) {
        curTotalDropped = msg.dropped_count;
        if (sessionStartDropped === null) {
          sessionStartDropped = curTotalDropped;
        }
        sessionDropped = Math.max(0, curTotalDropped - sessionStartDropped);
        updateDropRate();
      }
      if (msg.free_heap !== undefined) {
        const heapKb = (msg.free_heap > 1024) ? Math.round(msg.free_heap / 1024) : msg.free_heap;
        if (elFreeHeap) elFreeHeap.textContent = heapKb;
        const heapPct = Math.min(100, Math.max(0, Math.round((heapKb / 320) * 100)));
        if (barFreeHeap) barFreeHeap.style.width = heapPct + "%";
        if (elHeapBadge) elHeapBadge.textContent = `${heapPct}% Left`;
      }
      if (msg.queue_load !== undefined) {
        curQueueLoad = (typeof msg.queue_load === "number") ? msg.queue_load : parseInt(msg.queue_load, 10) || 0;
        const qMax = (typeof msg.queue_max === "number") ? msg.queue_max : (parseInt(document.getElementById("stat_queue_max")?.textContent, 10) || 150);
        const qPct = Math.min(100, Math.round((curQueueLoad / qMax) * 100));
        if (elQueueLoad) elQueueLoad.textContent = curQueueLoad;
        if (msg.queue_max !== undefined && document.getElementById("stat_queue_max")) {
          document.getElementById("stat_queue_max").textContent = qMax;
        }
        if (barQueueLoad) barQueueLoad.style.width = qPct + "%";
        if (elQueueBadge) {
          if (qPct > 75) {
            elQueueBadge.className = "hud-badge badge-amber";
            elQueueBadge.textContent = `${qPct}% High Load`;
          } else {
            elQueueBadge.className = "hud-badge badge-green";
            elQueueBadge.textContent = `${qPct}% Load Safe`;
          }
        }
      }
      if (msg.batch_load !== undefined) {
        curBatchLoad = msg.batch_load;
        if (msg.batch_max !== undefined) curBatchMax = msg.batch_max;
        else if (document.getElementById("stat_batch_max")) {
          const bM = parseInt(document.getElementById("stat_batch_max").textContent, 10);
          if (!isNaN(bM) && bM > 0) curBatchMax = bM;
        }
        const bPct = Math.min(100, Math.round((curBatchLoad / curBatchMax) * 100));
        if (elBatchLoad) elBatchLoad.textContent = curBatchLoad;
        if (msg.batch_max !== undefined && document.getElementById("stat_batch_max")) {
          document.getElementById("stat_batch_max").textContent = curBatchMax;
        }
        if (barBatchLoad) barBatchLoad.style.width = bPct + "%";
        if (elBatchBadge) elBatchBadge.textContent = `${bPct}% Buffer`;
      }
      if (msg.batch_pkt_size !== undefined) {
        curBatchPktSize = msg.batch_pkt_size;
        const pPct = Math.min(100, Math.round((curBatchPktSize / curBatchMax) * 100));
        if (elBatchPktSize) elBatchPktSize.textContent = curBatchPktSize;
        if (barBatchPkt) barBatchPkt.style.width = pPct + "%";
      }
      if (msg.net_bandwidth !== undefined) {
        curNetBandwidth = (typeof msg.net_bandwidth === "number") ? msg.net_bandwidth : (parseFloat(msg.net_bandwidth) || 0.0);
        if (elNetBandwidth) elNetBandwidth.textContent = curNetBandwidth.toFixed(2);
      }
      if (msg.total_bytes !== undefined) {
        if (sessionStartBytes === null) sessionStartBytes = msg.total_bytes;
        sessionTotalBytes = Math.max(0, msg.total_bytes - sessionStartBytes);
        if (elSessionTotalBandwidth) elSessionTotalBandwidth.textContent = formatBytes(sessionTotalBytes);
      }
      if (msg.beacons && Array.isArray(msg.beacons)) {
        for (let i = 0; i < msg.beacons.length; i++) insertBeacon(msg.beacons[i]);
        if (activeTableTab === "fifo") renderTable();
      } else if (msg.code && msg.tag) {
        insertBeacon(msg);
        if (activeTableTab === "fifo") renderTable();
      }
      if (msg.drop_summary) {
        updateDropSummaryUI(msg.drop_summary);
      }
      if (msg.dropped_beacons && Array.isArray(msg.dropped_beacons) && msg.dropped_beacons.length > 0) {
        for (let i = 0; i < msg.dropped_beacons.length; i++) {
          const db = msg.dropped_beacons[i];
          recordLiveDrop(db.reason);
          droppedBeaconsList.unshift({
            code: db.code || "BLE",
            tag: db.tag || "-",
            mac: db.mac || "-",
            raw: db.raw || "",
            rssi: db.rssi,
            reason: db.reason,
            time: new Date()
          });
          if (droppedBeaconsList.length > DROPPED_MAX_ITEMS) {
            droppedBeaconsList.length = DROPPED_MAX_ITEMS;
          }
        }
        updateDropSummaryUI(lastReceivedDropSummary);
        if (activeTableTab === "dropped") scheduleDropTableRender();
      }
    },
    pushChartPoint: function(pt) {
      chartData.push({
        readSpeed: pt.readSpeed !== undefined ? pt.readSpeed : (pt.read_speed || 0),
        mqttSpeed: pt.mqttSpeed !== undefined ? pt.mqttSpeed : (pt.mqtt_speed || 0),
        queueLoad: pt.queueLoad !== undefined ? pt.queueLoad : (pt.queue_load || 0),
        batchLoad: pt.batchLoad !== undefined ? pt.batchLoad : (pt.batch_load || 0),
        batchPktSize: pt.batchPktSize !== undefined ? pt.batchPktSize : (pt.batch_pkt_size || 0),
        netBandwidth: pt.netBandwidth !== undefined ? pt.netBandwidth : (pt.net_bandwidth || 0.0),
        time: pt.time || new Date()
      });
      if (chartData.length > 60) chartData.shift();
      drawChart();
    },
    exportToCsv: exportToCsv,
    stopTest: stopTest,
    confirmLeavePage: confirmLeavePage,
    switchTableTab: switchTab,
    goToUniquePage: goToUniquePage,
    setDropReasonFilter: setDropReasonFilter,
    getUniquePaginationState: function() {
      const filtered = getFilteredUniqueDevices();
      return {
        currentPage: uniqueCurrentPage,
        perPage: UNIQUE_PER_PAGE,
        totalItems: filtered.length,
        totalPages: Math.max(1, Math.ceil(filtered.length / UNIQUE_PER_PAGE)),
        domCardsCount: uniqueCardsList ? uniqueCardsList.querySelectorAll(".card").length : 0
      };
    },
    getState: function() {
      return {
        isTesting: isTesting,
        activeTableTab: activeTableTab,
        uniqueCount: uniqueBeaconsSet.size,
        uniqueBeaconsMapCount: uniqueBeaconsMap.size,
        historyCount: packetStreamHistory.length,
        droppedCount: droppedBeaconsList.length,
        fullSessionLogCount: fullSessionLog.length,
        chartDataPoints: chartData.length,
        currentFilter: currentFilter,
        sessionTotalRecv: sessionTotalRecv,
        sessionTotalSent: sessionTotalSent,
        sessionTotalBytes: sessionTotalBytes
      };
    }
  };

  // Initializations
  fetchNetworkInfo();
  drawChart();
});