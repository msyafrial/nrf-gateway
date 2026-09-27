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
  let currentFilter = "";

  // DOM Elements
  const btnStart = document.getElementById("btnStartTest");
  const btnStop = document.getElementById("btnStopTest");
  const statusBadge = document.getElementById("testStatusBadge");
  const statusText = document.getElementById("testStatusText");
  const tableBody = document.getElementById("beaconTableBody");
  const filterInput = document.getElementById("tagFilterInput");

  // Metrics elements
  const elReadSpeed = document.getElementById("stat_read_speed");
  const elMqttSpeed = document.getElementById("stat_mqtt_speed");
  const elFreeHeap = document.getElementById("stat_free_heap");
  const elQueueLoad = document.getElementById("stat_queue_load");
  const elUniqueTags = document.getElementById("stat_unique_tags");
  const elBatchLoad = document.getElementById("stat_batch_load");
  const elBatchPktSize = document.getElementById("stat_batch_pkt_size");
  const elNetBandwidth = document.getElementById("stat_net_bandwidth");

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

  function getWsUrl() {
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

    // Saat filter aktif, paket yang tidak cocok DIBUANG dan TIDAK disimpan ke riwayat
    const filterVal = currentFilter.toLowerCase().trim();
    if (filterVal && !matchBeacon(beacon, filterVal)) {
      return;
    }

    // Masukkan setiap paket yang lolos sebagai entri baru (continuous stream, non-unique)
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

  function renderTable() {
    if (!tableBody) return;

    const filterVal = currentFilter.toLowerCase().trim();
    let matched = packetStreamHistory;
    if (filterVal) {
      matched = packetStreamHistory.filter(b => matchBeacon(b, filterVal));
    }

    // Tampilkan tepat hingga 10 data teratas sesuai filter
    const displayList = matched.slice(0, 10);

    if (displayList.length === 0) {
      tableBody.innerHTML = `<tr><td colspan="6" style="text-align:center;color:var(--text-dim);padding:24px;">${filterVal ? "Tidak ada beacon yang cocok dengan filter." : "Menunggu paket data beacon..."}</td></tr>`;
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
  }

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
          }
          if (msg.mqtt_speed !== undefined) {
            curMqttSpeed = msg.mqtt_speed;
            if (elMqttSpeed) elMqttSpeed.textContent = curMqttSpeed;
          }
          if (msg.free_heap !== undefined && elFreeHeap) {
            elFreeHeap.textContent = (msg.free_heap > 1024) ? Math.round(msg.free_heap / 1024) : msg.free_heap;
          }
          if (msg.queue_load !== undefined) {
            curQueueLoad = (typeof msg.queue_load === "number") ? msg.queue_load : parseInt(msg.queue_load, 10) || 0;
            if (elQueueLoad) elQueueLoad.textContent = (typeof msg.queue_load === "number") ? `${msg.queue_load} / 300` : msg.queue_load;
          }
          if (msg.batch_load !== undefined) {
            curBatchLoad = msg.batch_load;
            if (msg.batch_max !== undefined) curBatchMax = msg.batch_max;
            if (elBatchLoad) elBatchLoad.textContent = `${curBatchLoad} / ${curBatchMax}`;
          }
          if (msg.batch_pkt_size !== undefined) {
            curBatchPktSize = msg.batch_pkt_size;
            if (elBatchPktSize) elBatchPktSize.textContent = curBatchPktSize;
          }
          if (msg.net_bandwidth !== undefined) {
            curNetBandwidth = (typeof msg.net_bandwidth === "number") ? msg.net_bandwidth : (parseFloat(msg.net_bandwidth) || 0.0);
            if (elNetBandwidth) elNetBandwidth.textContent = curNetBandwidth.toFixed(2);
          }

          // Process incoming beacons (single or array)
          if (msg.beacons && Array.isArray(msg.beacons) && msg.beacons.length > 0) {
            for (let i = 0; i < msg.beacons.length; i++) {
              insertBeacon(msg.beacons[i]);
            }
            renderTable();
          } else if (msg.code && msg.tag) {
            insertBeacon(msg);
            renderTable();
          }
        } catch (e) {
          console.error("[WS] Parse error:", e);
        }
      };

      ws.onclose = () => {
        console.log("[WS] Disconnected");
        // Verifikasi apakah sesi masih valid (misal ESP32 baru saja restart atau sesi expired)
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

      // Reset Unique Beacon Counter & Batch Pkt Size
      uniqueBeaconsSet.clear();
      if (elUniqueTags) elUniqueTags.textContent = "0";
      curBatchPktSize = 0;
      if (elBatchPktSize) elBatchPktSize.textContent = "0";
      curNetBandwidth = 0.0;
      if (elNetBandwidth) elNetBandwidth.textContent = "0.00";

      // Reset & Start 60-Second Realtime Chart
      chartData.length = 0;
      if (chartInterval) clearInterval(chartInterval);
      sampleChartData(); // Sample initial data point immediately
      chartInterval = setInterval(sampleChartData, 1000); // 1Hz sampling rate

      initWebSocket();
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ action: "start" }));
      }
    });
  }

  // Stop Test Button
  if (btnStop) {
    btnStop.addEventListener("click", function() {
      isTesting = false;
      btnStop.disabled = true;
      btnStop.style.display = "none";
      btnStart.disabled = false;
      btnStart.style.display = "inline-flex";

      statusBadge.className = "status-pill";
      statusBadge.style.background = "rgba(148, 163, 184, 0.12)";
      statusBadge.style.borderColor = "rgba(148, 163, 184, 0.3)";
      statusBadge.style.color = "#94a3b8";
      statusBadge.innerHTML = `<span>STANDBY / STOPPED</span>`;

      // Freeze Chart (Stop sampling interval so technician can inspect curve)
      if (chartInterval) {
        clearInterval(chartInterval);
        chartInterval = null;
      }
      drawChart();

      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ action: "stop" }));
      }
    });
  }

  // Filter Search Input
  if (filterInput) {
    filterInput.addEventListener("input", function() {
      currentFilter = this.value;
      const filterVal = currentFilter.toLowerCase().trim();
      if (filterVal) {
        // Buang data yang tidak cocok dari riwayat agar 30 slot riwayat khusus untuk data yang lolos filter
        packetStreamHistory = packetStreamHistory.filter(b => matchBeacon(b, filterVal));
      }
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
      }
      if (msg.mqtt_speed !== undefined) {
        curMqttSpeed = msg.mqtt_speed;
        if (elMqttSpeed) elMqttSpeed.textContent = curMqttSpeed;
      }
      if (msg.free_heap !== undefined && elFreeHeap) {
        elFreeHeap.textContent = (msg.free_heap > 1024) ? Math.round(msg.free_heap / 1024) : msg.free_heap;
      }
      if (msg.queue_load !== undefined) {
        curQueueLoad = (typeof msg.queue_load === "number") ? msg.queue_load : parseInt(msg.queue_load, 10) || 0;
        if (elQueueLoad) elQueueLoad.textContent = (typeof msg.queue_load === "number") ? `${msg.queue_load} / 300` : msg.queue_load;
      }
      if (msg.batch_load !== undefined) {
        curBatchLoad = msg.batch_load;
        if (msg.batch_max !== undefined) curBatchMax = msg.batch_max;
        if (elBatchLoad) elBatchLoad.textContent = `${curBatchLoad} / ${curBatchMax}`;
      }
      if (msg.batch_pkt_size !== undefined) {
        curBatchPktSize = msg.batch_pkt_size;
        if (elBatchPktSize) elBatchPktSize.textContent = curBatchPktSize;
      }
      if (msg.net_bandwidth !== undefined) {
        curNetBandwidth = (typeof msg.net_bandwidth === "number") ? msg.net_bandwidth : (parseFloat(msg.net_bandwidth) || 0.0);
        if (elNetBandwidth) elNetBandwidth.textContent = curNetBandwidth.toFixed(2);
      }
      if (msg.beacons && Array.isArray(msg.beacons)) {
        for (let i = 0; i < msg.beacons.length; i++) insertBeacon(msg.beacons[i]);
        renderTable();
      } else if (msg.code && msg.tag) {
        insertBeacon(msg);
        renderTable();
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
    getState: function() {
      return {
        isTesting: isTesting,
        uniqueCount: uniqueBeaconsSet.size,
        historyCount: packetStreamHistory.length,
        chartDataPoints: chartData.length,
        currentFilter: currentFilter
      };
    }
  };

  // Initial Empty Chart Drawing
  drawChart();
});