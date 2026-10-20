/**
 * IOTARU BLE Gateway Live Diagnostics & Monitor
 * High-Throughput Realtime Table & 60-Second Performance Telemetry Chart (Task Manager Style)
 */
document.addEventListener("DOMContentLoaded", function() {
  let ws = null;
  let isTesting = false;
  const maxHistory = 30; // Batas maksimal riwayat buffer (30 paket terbaru)
  let packetStreamHistory = []; // FIFO stream (terbaru di indeks 0)
  const uniqueBeaconsSet = new Set(); // Pelacak perangkat beacon unik
  const uniqueBeaconsMap = new Map(); // Pelacak detail perangkat unik { code, tag, rssi, batt, lastSeen, hits }
  let activeTableTab = "fifo"; // "fifo" | "unique" | "dropped"
  let droppedBeaconsList = []; // FIFO stream for dropped beacons (max 50)
  let currentFilter = "";

  // DOM Elements
  const btnStart = document.getElementById("btnStartTest");
  const btnStop = document.getElementById("btnStopTest");
  const btnExportCsv = document.getElementById("btnExportCsv");
  const statusBadge = document.getElementById("testStatusBadge");
  const statusText = document.getElementById("testStatusText");
  const tableHead = document.getElementById("beaconTableHead");
  const tableBody = document.getElementById("beaconTableBody");
  const filterInput = document.getElementById("tagFilterInput");
  const tabFifo = document.getElementById("tabFifo");
  const tabUnique = document.getElementById("tabUnique");
  const tabDropped = document.getElementById("tabDropped");
  const dropSummaryGrid = document.getElementById("dropSummaryGrid");
  const toolbarLeftInfo = document.getElementById("toolbarLeftInfo");
  const totalDroppedDisplay = document.getElementById("totalDroppedDisplay");
  const countDropTtl = document.getElementById("count_drop_ttl");
  const countDropRssi = document.getElementById("count_drop_rssi");
  const countDropUuid = document.getElementById("count_drop_uuid");
  const countDropMajor = document.getElementById("count_drop_major");
  const countDropQueue = document.getElementById("count_drop_queue");
  const countDropMqtt = document.getElementById("count_drop_mqtt");
  const badgeUniqueCount = document.getElementById("badgeUniqueCount");

  // Session Control Elements
  const elSessionElapsed = document.getElementById("sessionElapsedTime");
  const elSessionPktsRecv = document.getElementById("sessionPacketsReceived");
  const elSessionPktsSent = document.getElementById("sessionPacketsSent");
  const elSessionDropRate = document.getElementById("sessionDropRate");
  const elSessionTotalBandwidth = document.getElementById("sessionTotalBandwidth");

  // Metrics elements
  const elReadSpeed = document.getElementById("stat_read_speed");
  const elReadAvg = document.getElementById("stat_read_avg");
  const elReadMax = document.getElementById("stat_read_max");

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

  function updateDropRate() {
    if (elSessionPktsRecv) elSessionPktsRecv.textContent = sessionTotalRecv.toLocaleString();
    if (elSessionPktsSent) elSessionPktsSent.textContent = sessionTotalSent.toLocaleString();
    if (elSessionDropRate) {
      if (sessionTotalSent === 0) {
        elSessionDropRate.textContent = `${sessionDropped.toLocaleString()} (0.00%)`;
      } else {
        const rate = (sessionDropped / sessionTotalSent) * 100;
        elSessionDropRate.textContent = `${sessionDropped.toLocaleString()} (${rate.toFixed(2)}%)`;
      }
    }
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
          title: "Belum Ada Data",
          text: "Mulai pengujian (Start Test) terlebih dahulu untuk mengumpulkan log beacon.",
          confirmButtonColor: "#0284c7"
        });
      } else {
        alert("Belum ada data beacon untuk diekspor. Jalankan pengujian terlebih dahulu.");
      }
      return;
    }

    let csvContent = "\uFEFFNo,Waktu,Tipe Beacon,Tag / MAC,RSSI (dBm),Baterai (%)\r\n";
    dataToExport.forEach((item, index) => {
      const timeStr = item.timeStr || formatTime(item.time);
      const codeStr = item.code || "BLE";
      const tagStr = item.tag || "-";
      const rssiStr = item.rssi !== undefined && item.rssi !== null ? `${item.rssi}` : "-";
      const battStr = item.batt !== undefined && item.batt !== null && item.batt !== 0 ? `${item.batt}%` : "-";
      csvContent += `${index + 1},"${timeStr}","${codeStr}","${tagStr}","${rssiStr}","${battStr}"\r\n`;
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
          elNetLinkText.textContent = `LAN W5500 ${spd}M ${dpx}`;
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

  function getDropReasonInfo(reasonCode) {
    switch (Number(reasonCode)) {
      case 0: return { label: "TTL Expired", cssClass: "drop-ttl" };
      case 1: return { label: "RSSI < Min", cssClass: "drop-rssi" };
      case 2: return { label: "UUID Mismatch", cssClass: "drop-uuid" };
      case 3: return { label: "Major Mismatch", cssClass: "drop-major" };
      case 4: return { label: "Queue Full", cssClass: "drop-queue" };
      case 5: return { label: "MQTT Fail", cssClass: "drop-mqtt" };
      default: return { label: "Other Drop", cssClass: "drop-ttl" };
    }
  }

  function matchBeacon(b, filterVal) {
    if (!filterVal) return true;

    const tagStr = (b.tag || "").toLowerCase();
    const codeStr = (b.code || "").toLowerCase();

    // 1. Kecocokan langsung pada string tag atau tipe format (misal "ibeacon", "ext", "00001")
    if (tagStr.includes(filterVal) || codeStr.includes(filterVal)) {
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

      // Jika keduanya diisi angka (contoh: "1-2" atau "10-20")
      if (!isNaN(qMaj) && !isNaN(qMin)) {
        return bMajor === qMaj && bMinor === qMin;
      }
      // Jika hanya Major diisi (contoh: "1-" atau "1- ")
      if (!isNaN(qMaj) && strMin === "") {
        return bMajor === qMaj;
      }
      // Jika hanya Minor diisi (contoh: "-2" atau "- 2")
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
    if (!beacon || !beacon.tag) return;

    // Hitung unique beacons selama sesi test berjalan
    uniqueBeaconsSet.add(beacon.tag);
    if (elUniqueTags) elUniqueTags.textContent = uniqueBeaconsSet.size;

    // Update Unique Beacons Map (In-place live state)
    const existing = uniqueBeaconsMap.get(beacon.tag);
    const prevHits = existing ? existing.hits : 0;

    // Pertahankan nilai baterai jika sebelumnya ada tapi paket baru standard tanpa baterai
    let battVal = beacon.batt;
    if ((battVal === undefined || battVal === null || battVal === "" || battVal === 0 || battVal === "0") && existing && existing.batt) {
      battVal = existing.batt;
    }

    uniqueBeaconsMap.set(beacon.tag, {
      code: beacon.code || (existing ? existing.code : "BLE"),
      tag: beacon.tag,
      rssi: (beacon.rssi !== undefined && beacon.rssi !== null) ? beacon.rssi : (existing ? existing.rssi : null),
      batt: battVal,
      lastSeen: new Date(),
      hits: prevHits + 1
    });

    if (badgeUniqueCount) {
      badgeUniqueCount.textContent = uniqueBeaconsMap.size;
    }

    // Catat RSSI untuk kalkulasi rolling average sinyal
    if (beacon.rssi !== undefined && beacon.rssi !== null) {
      rssiSamples.push(beacon.rssi);
      if (rssiSamples.length > 50) rssiSamples.shift();
      if (elBeaconRssi && rssiSamples.length > 0) {
        const avgRssi = Math.round(rssiSamples.reduce((a, b) => a + b, 0) / rssiSamples.length);
        elBeaconRssi.textContent = `${avgRssi} dBm`;
      }
    }

    // Deteksi tipe beacon
    if (elBeaconType && beacon.code) {
      if (beacon.code.toLowerCase().includes("ext")) {
        elBeaconType.textContent = "iBeacon Ext";
      } else {
        elBeaconType.textContent = "iBeacon";
      }
    }

    // Rekam ke log lengkap sesi (untuk CSV export)
    fullSessionLog.push({
      code: beacon.code || "BLE",
      tag: beacon.tag,
      rssi: beacon.rssi,
      batt: beacon.batt,
      time: new Date(),
      timeStr: formatTime(new Date())
    });
    if (fullSessionLog.length > 5000) fullSessionLog.shift();

    // Masukkan ke continuous FIFO buffer (continuous stream, non-unique)
    const item = {
      code: beacon.code || "BLE",
      tag: beacon.tag,
      rssi: beacon.rssi,
      batt: beacon.batt,
      time: new Date()
    };

    packetStreamHistory.unshift(item);
    if (packetStreamHistory.length > maxHistory) {
      packetStreamHistory.pop();
    }
  }

  function updateTableHead() {
    if (!tableHead) return;
    if (activeTableTab === "fifo") {
      tableHead.innerHTML = `
        <tr>
          <th style="width:40px;">#</th>
          <th>Format</th>
          <th>Major-Minor</th>
          <th>RSSI</th>
          <th>Battery</th>
          <th>Waktu</th>
        </tr>
      `;
    } else if (activeTableTab === "unique") {
      tableHead.innerHTML = `
        <tr>
          <th style="width:40px;">#</th>
          <th>Major-Minor</th>
          <th>RSSI</th>
          <th>Battery</th>
          <th>Terakhir Dilihat</th>
          <th>Total Pkt</th>
        </tr>
      `;
    } else {
      tableHead.innerHTML = `
        <tr>
          <th style="width:40px;">#</th>
          <th>Tag / Identitas</th>
          <th>RSSI</th>
          <th>Alasan Drop</th>
          <th>Waktu Drop</th>
        </tr>
      `;
    }
  }

  function renderTable() {
    if (!tableBody) return;
    updateTableHead();

    const filterVal = currentFilter.toLowerCase().trim();

    if (activeTableTab === "fifo") {
      let matched = packetStreamHistory;
      if (filterVal) {
        matched = packetStreamHistory.filter(b => matchBeacon(b, filterVal));
      }

      const displayList = matched.slice(0, 10);
      if (displayList.length === 0) {
        tableBody.innerHTML = `<tr><td colspan="6" style="text-align:center;color:var(--text-dim);padding:24px;">${filterVal ? "Tidak ada beacon yang cocok dengan filter." : (isTesting ? "Menunggu paket data beacon..." : "Tekan tombol <strong>Start Test</strong> di atas untuk memulai streaming live.")}</td></tr>`;
        return;
      }

      let html = "";
      displayList.forEach((b, idx) => {
        const isStdIBeacon = (b.code || "").toLowerCase() === "ibeacon";
        const badgeClass = isStdIBeacon ? "badge-beacon ibeacon" : "badge-beacon ibeacon_ext";

        let batteryHtml = `<span style="color:var(--text-dim);">-</span>`;
        if (!isStdIBeacon && b.batt !== undefined && b.batt !== null && b.batt !== "" && b.batt !== 0 && b.batt !== "0") {
          const battVal = parseInt(b.batt, 10);
          if (!isNaN(battVal) && battVal > 0) {
            let battClass = "badge-battery full";
            if (battVal < 20) battClass = "badge-battery low";
            else if (battVal < 50) battClass = "badge-battery mid";
            batteryHtml = `<span class="${battClass}">${battVal}%</span>`;
          }
        }

        const timeStr = formatTime(b.time);

        html += `
          <tr class="${idx === 0 ? 'new-row' : ''}">
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);">${idx + 1}</td>
            <td><span class="${badgeClass}">${b.code || "BLE"}</span></td>
            <td><strong style="font-family:var(--font-mono, monospace);color:#38bdf8;">${b.tag || "-"}</strong></td>
            <td style="font-family:var(--font-mono, monospace);font-weight:600;">${b.rssi ? b.rssi + " dBm" : "-"}</td>
            <td>${batteryHtml}</td>
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);font-size:0.84rem;">${timeStr}</td>
          </tr>
        `;
      });
      tableBody.innerHTML = html;

    } else if (activeTableTab === "unique") {
      // Unique Beacons Tab Mode (Fixed First-Seen Order / No Row Jumping)
      let uniqueList = Array.from(uniqueBeaconsMap.values());
      if (filterVal) {
        uniqueList = uniqueList.filter(b => matchBeacon(b, filterVal));
      }

      if (uniqueList.length === 0) {
        tableBody.innerHTML = `<tr><td colspan="6" style="text-align:center;color:var(--text-dim);padding:24px;">${filterVal ? "Tidak ada beacon unik yang cocok dengan filter." : (isTesting ? "Belum ada beacon unik terdeteksi..." : "Tekan tombol <strong>Start Test</strong> di atas untuk memindai beacon unik.")}</td></tr>`;
        return;
      }

      let html = "";
      uniqueList.forEach((b, idx) => {
        const isStdIBeacon = (b.code || "").toLowerCase() === "ibeacon";
        const badgeClass = isStdIBeacon ? "badge-beacon ibeacon" : "badge-beacon ibeacon_ext";

        let batteryHtml = `<span style="color:var(--text-dim);">-</span>`;
        if (b.batt !== undefined && b.batt !== null && b.batt !== "" && b.batt !== 0 && b.batt !== "0") {
          const battVal = parseInt(b.batt, 10);
          if (!isNaN(battVal) && battVal > 0) {
            let battClass = "badge-battery full";
            if (battVal < 20) battClass = "badge-battery low";
            else if (battVal < 50) battClass = "badge-battery mid";
            batteryHtml = `<span class="${battClass}">${battVal}%</span>`;
          }
        }

        let rssiHtml = `<span style="color:var(--text-dim);">-</span>`;
        if (b.rssi !== null && b.rssi !== undefined) {
          let rssiClass = "good";
          if (b.rssi < -80) rssiClass = "weak";
          else if (b.rssi < -65) rssiClass = "mid";
          rssiHtml = `<span class="badge-rssi ${rssiClass}">${b.rssi} dBm</span>`;
        }

        const timeStr = formatTime(b.lastSeen);

        html += `
          <tr>
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);">${idx + 1}</td>
            <td><strong style="font-family:var(--font-mono, monospace);color:#38bdf8;">${b.tag || "-"}</strong></td>
            <td>${rssiHtml}</td>
            <td>${batteryHtml}</td>
            <td style="color:#f8fafc;font-family:var(--font-mono, monospace);font-size:0.84rem;">${timeStr}</td>
            <td><span class="badge-hits">${b.hits || 1} pkt</span></td>
          </tr>
        `;
      });
      tableBody.innerHTML = html;

    } else {
      // Dropped Data Tab Mode (5 Kolom: # | Tag / Identitas | RSSI | Alasan Drop | Waktu Drop)
      let matched = droppedBeaconsList;
      if (filterVal) {
        matched = droppedBeaconsList.filter(b => {
          const reasonInfo = getDropReasonInfo(b.reason);
          return matchBeacon(b, filterVal) || reasonInfo.label.toLowerCase().includes(filterVal);
        });
      }

      if (matched.length === 0) {
        tableBody.innerHTML = `<tr><td colspan="5" style="text-align:center;color:var(--text-dim);padding:24px;">${filterVal ? "Tidak ada data paket ditolak yang cocok dengan filter." : (isTesting ? "Belum ada paket data yang di-drop/ditolak..." : "Tekan tombol <strong>Start Test</strong> di atas untuk memantau streaming live.")}</td></tr>`;
        return;
      }

      let html = "";
      matched.forEach((b, idx) => {
        const reasonInfo = getDropReasonInfo(b.reason);
        const timeStr = formatTime(b.time);

        let rssiHtml = `<span style="color:var(--text-dim);">-</span>`;
        if (b.rssi !== null && b.rssi !== undefined && b.rssi !== 0) {
          let rssiClass = "good";
          if (b.rssi < -80) rssiClass = "weak";
          else if (b.rssi < -65) rssiClass = "mid";
          rssiHtml = `<span class="badge-rssi ${rssiClass}">${b.rssi} dBm</span>`;
        }

        html += `
          <tr class="${idx === 0 ? 'new-row' : ''}">
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);">${idx + 1}</td>
            <td><strong style="font-family:var(--font-mono, monospace);color:#38bdf8;">${b.tag || b.code || "-"}</strong></td>
            <td>${rssiHtml}</td>
            <td><span class="badge-drop ${reasonInfo.cssClass}">${reasonInfo.label}</span></td>
            <td style="color:var(--text-dim);font-family:var(--font-mono, monospace);font-size:0.84rem;">${timeStr}</td>
          </tr>
        `;
      });
      tableBody.innerHTML = html;
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
            if (elReadSpeed) elReadSpeed.textContent = curReadSpeed;
            readSpeedSamples.push(curReadSpeed);
            if (readSpeedSamples.length > 60) readSpeedSamples.shift();
            const avgRead = (readSpeedSamples.reduce((a, b) => a + b, 0) / readSpeedSamples.length).toFixed(1);
            maxReadSpeed = Math.max(maxReadSpeed, curReadSpeed);
            if (elReadAvg) elReadAvg.textContent = avgRead;
            if (elReadMax) elReadMax.textContent = maxReadSpeed;
          }
          if (msg.mqtt_speed !== undefined) {
            curMqttSpeed = msg.mqtt_speed;
            if (elMqttSpeed) elMqttSpeed.textContent = curMqttSpeed;
            mqttSpeedSamples.push(curMqttSpeed);
            if (mqttSpeedSamples.length > 60) mqttSpeedSamples.shift();
            const avgMqtt = (mqttSpeedSamples.reduce((a, b) => a + b, 0) / mqttSpeedSamples.length).toFixed(1);
            maxMqttSpeed = Math.max(maxMqttSpeed, curMqttSpeed);
            if (elMqttAvg) elMqttAvg.textContent = avgMqtt;
            if (elMqttMax) elMqttMax.textContent = maxMqttSpeed;
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
            const qMax = 300;
            const qPct = Math.min(100, Math.round((curQueueLoad / qMax) * 100));
            if (elQueueLoad) elQueueLoad.textContent = (typeof msg.queue_load === "number") ? `${msg.queue_load} / ${qMax}` : msg.queue_load;
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
            const bPct = Math.min(100, Math.round((curBatchLoad / curBatchMax) * 100));
            if (elBatchLoad) elBatchLoad.textContent = `${curBatchLoad} / ${curBatchMax}`;
            if (barBatchLoad) barBatchLoad.style.width = bPct + "%";
            if (elBatchBadge) elBatchBadge.textContent = `${bPct}% Buffer`;
          }
          if (msg.batch_pkt_size !== undefined) {
            curBatchPktSize = msg.batch_pkt_size;
            const pPct = Math.min(100, Math.round((curBatchPktSize / 100) * 100));
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
            if (activeTableTab !== "dropped") {
              renderTable();
            }
          } else if (msg.code && msg.tag) {
            insertBeacon(msg);
            if (activeTableTab !== "dropped") {
              renderTable();
            }
          }

          // Process drop summary metrics & live dropped beacons
          if (msg.drop_summary) {
            const ds = msg.drop_summary;
            if (countDropTtl) countDropTtl.textContent = (ds.ttl || 0).toLocaleString();
            if (countDropRssi) countDropRssi.textContent = (ds.rssi || 0).toLocaleString();
            if (countDropUuid) countDropUuid.textContent = (ds.uuid || 0).toLocaleString();
            if (countDropMajor) countDropMajor.textContent = (ds.major || 0).toLocaleString();
            if (countDropQueue) countDropQueue.textContent = (ds.queue || 0).toLocaleString();
            if (countDropMqtt) countDropMqtt.textContent = (ds.mqtt || 0).toLocaleString();

            const sum = (ds.ttl || 0) + (ds.rssi || 0) + (ds.uuid || 0) + (ds.major || 0) + (ds.queue || 0) + (ds.mqtt || 0);
            if (totalDroppedDisplay) totalDroppedDisplay.textContent = sum.toLocaleString();
          }

          if (msg.dropped_beacons && Array.isArray(msg.dropped_beacons) && msg.dropped_beacons.length > 0) {
            for (let i = 0; i < msg.dropped_beacons.length; i++) {
              const db = msg.dropped_beacons[i];
              droppedBeaconsList.unshift({
                code: db.code || "BLE",
                tag: db.tag || "-",
                rssi: db.rssi,
                reason: db.reason,
                time: new Date()
              });
              if (droppedBeaconsList.length > 50) droppedBeaconsList.pop();
            }
            if (activeTableTab === "dropped") {
              renderTable();
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

      statusBadge.className = "status-pill";
      statusBadge.innerHTML = `<span class="pulse-dot"></span><span id="testStatusText">STREAMING LIVE</span>`;

      // Reset Session Metrics & Timers
      sessionStartTime = Date.now();
      sessionTotalRecv = 0;
      sessionTotalSent = 0;
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
      if (elMqttAvg) elMqttAvg.textContent = "0.0";
      if (elMqttMax) elMqttMax.textContent = "0";
      if (elBeaconRssi) elBeaconRssi.textContent = "-";

      // Reset Unique Beacon Counter & Batch Pkt Size
      uniqueBeaconsSet.clear();
      uniqueBeaconsMap.clear();
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
      if (totalDroppedDisplay) totalDroppedDisplay.textContent = "0";
      if (countDropTtl) countDropTtl.textContent = "0";
      if (countDropRssi) countDropRssi.textContent = "0";
      if (countDropUuid) countDropUuid.textContent = "0";
      if (countDropMajor) countDropMajor.textContent = "0";
      if (countDropQueue) countDropQueue.textContent = "0";
      if (countDropMqtt) countDropMqtt.textContent = "0";
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
        title: "Sesi Pengujian Sedang Berjalan",
        html: "Pengujian live beacon sedang aktif. Jika Anda berpindah halaman, koneksi pengujian akan dihentikan dan seluruh data sesi uji yang belum diekspor akan hilang.<br><br>Yakin ingin tetap berpindah halaman?",
        icon: "warning",
        showCancelButton: true,
        confirmButtonText: "Lanjut",
        confirmButtonColor: "#ef4444",
        cancelButtonText: "Batal",
        cancelButtonColor: "#64748b"
      }).then((result) => {
        if (result.isConfirmed) {
          stopTest();
          window.location.href = targetUrl;
        }
      });
    } else {
      const leave = confirm("Pengujian live beacon sedang aktif. Jika Anda berpindah halaman, koneksi pengujian akan dihentikan dan seluruh data sesi uji yang belum diekspor akan hilang.\n\nTetap lanjut berpindah halaman?");
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
      e.returnValue = "Pengujian live beacon sedang aktif. Jika Anda meninggalkan halaman, pengujian akan dihentikan dan data sesi uji akan hilang.";
      return e.returnValue;
    }
  });

  // Export CSV Button
  if (btnExportCsv) {
    btnExportCsv.addEventListener("click", exportToCsv);
  }

  // Filter Search Input
  if (filterInput) {
    filterInput.addEventListener("input", function() {
      currentFilter = this.value;
      renderTable();
    });
  }

  // Testing hooks for automated browser verification
  window._testApi = {
    injectMessage: function(msg) {
      if (!isTesting) return;
      if (msg.read_speed !== undefined) {
        curReadSpeed = msg.read_speed;
        if (elReadSpeed) elReadSpeed.textContent = curReadSpeed;
        readSpeedSamples.push(curReadSpeed);
        if (readSpeedSamples.length > 60) readSpeedSamples.shift();
        const avgRead = (readSpeedSamples.reduce((a, b) => a + b, 0) / readSpeedSamples.length).toFixed(1);
        maxReadSpeed = Math.max(maxReadSpeed, curReadSpeed);
        if (elReadAvg) elReadAvg.textContent = avgRead;
        if (elReadMax) elReadMax.textContent = maxReadSpeed;
      }
      if (msg.mqtt_speed !== undefined) {
        curMqttSpeed = msg.mqtt_speed;
        if (elMqttSpeed) elMqttSpeed.textContent = curMqttSpeed;
        mqttSpeedSamples.push(curMqttSpeed);
        if (mqttSpeedSamples.length > 60) mqttSpeedSamples.shift();
        const avgMqtt = (mqttSpeedSamples.reduce((a, b) => a + b, 0) / mqttSpeedSamples.length).toFixed(1);
        maxMqttSpeed = Math.max(maxMqttSpeed, curMqttSpeed);
        if (elMqttAvg) elMqttAvg.textContent = avgMqtt;
        if (elMqttMax) elMqttMax.textContent = maxMqttSpeed;
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
        const qMax = 300;
        const qPct = Math.min(100, Math.round((curQueueLoad / qMax) * 100));
        if (elQueueLoad) elQueueLoad.textContent = (typeof msg.queue_load === "number") ? `${msg.queue_load} / ${qMax}` : msg.queue_load;
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
        const bPct = Math.min(100, Math.round((curBatchLoad / curBatchMax) * 100));
        if (elBatchLoad) elBatchLoad.textContent = `${curBatchLoad} / ${curBatchMax}`;
        if (barBatchLoad) barBatchLoad.style.width = bPct + "%";
        if (elBatchBadge) elBatchBadge.textContent = `${bPct}% Buffer`;
      }
      if (msg.batch_pkt_size !== undefined) {
        curBatchPktSize = msg.batch_pkt_size;
        const pPct = Math.min(100, Math.round((curBatchPktSize / 100) * 100));
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
        if (activeTableTab !== "dropped") renderTable();
      } else if (msg.code && msg.tag) {
        insertBeacon(msg);
        if (activeTableTab !== "dropped") renderTable();
      }
      if (msg.drop_summary) {
        const ds = msg.drop_summary;
        if (countDropTtl) countDropTtl.textContent = (ds.ttl || 0).toLocaleString();
        if (countDropRssi) countDropRssi.textContent = (ds.rssi || 0).toLocaleString();
        if (countDropUuid) countDropUuid.textContent = (ds.uuid || 0).toLocaleString();
        if (countDropMajor) countDropMajor.textContent = (ds.major || 0).toLocaleString();
        if (countDropQueue) countDropQueue.textContent = (ds.queue || 0).toLocaleString();
        if (countDropMqtt) countDropMqtt.textContent = (ds.mqtt || 0).toLocaleString();

        const sum = (ds.ttl || 0) + (ds.rssi || 0) + (ds.uuid || 0) + (ds.major || 0) + (ds.queue || 0) + (ds.mqtt || 0);
        if (totalDroppedDisplay) totalDroppedDisplay.textContent = sum.toLocaleString();
      }
      if (msg.dropped_beacons && Array.isArray(msg.dropped_beacons)) {
        for (let i = 0; i < msg.dropped_beacons.length; i++) {
          const db = msg.dropped_beacons[i];
          droppedBeaconsList.unshift({
            code: db.code || "BLE",
            tag: db.tag || "-",
            rssi: db.rssi,
            reason: db.reason,
            time: new Date()
          });
          if (droppedBeaconsList.length > 50) droppedBeaconsList.pop();
        }
        if (activeTableTab === "dropped") renderTable();
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