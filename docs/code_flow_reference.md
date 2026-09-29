# Referensi Alur Kode & Variabel Terperinci - IOTARU SG-10

Dokumen ini melengkapi diagram [docs/diagram_pipeline.drawio](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/docs/diagram_pipeline.drawio) dan [docs/diagram_pipeline.mmd](file:///c:/Users/syafr/OneDrive/Documents/PlatformIO/Projects/esp32_ibeacon_espidf/docs/diagram_pipeline.mmd) sebagai panduan *maintenance* cepat kode program C++ di level fungsi, variabel, antrean FreeRTOS, dan mutex.

---

## 1. Peta Eksekusi Boot & Inisialisasi

| File Sumber | Fungsi Utama | Variabel Terkait | Deskripsi Eksekusi |
| :--- | :--- | :--- | :--- |
| `src/main.cpp` | `app_main()` | - | Titik masuk utama firmware ESP-IDF murni. |
| `src/core/config_store.cpp` | `ConfigStore::loadConfig()` | `Iotaru::g_config`, `g_deviceId` | Membaca konfigurasi dari partisi NVS (`cfg_*` atau Master). |
| `src/bsp/bsp_button.cpp` | `BspButton::checkBootAction()` | Pin `GPIO 34` | Deteksi synchronous durasi tekan tombol saat boot (<5s, 5-10s, >10s). |
| `src/bsp/bsp_led.cpp` | `s_ledTask()` | `s_sysState`, `s_comState`, `GPIO 13` | Task status WS2812B NeoPixel daisy-chain (Core 1). |
| `src/network/net_manager.cpp` | `NetManager::init()` | `g_config.connection_mode` | Inisialisasi Ethernet W5500 SPI DMA / Wi-Fi Station & SoftAP. |

---

## 2. Peta Ingestion Hot-Path (Core 0 - PRO_CPU)

| File Sumber | Fungsi Utama | Variabel / Buffer / Antrean | Logika & Alur Data |
| :--- | :--- | :--- | :--- |
| `src/bsp/bsp_uart.cpp` | `uart_event_task()` | `s_uartQueue`, `dtmp[1024]`, `GPIO 16` | Menerima event UART2 1 Mbps dari hardware FIFO, memanggil `processByte()`. |
| `src/core/beacon_pipeline.cpp` | `BeaconPipeline::processByte()` | `s_parserState`, `s_rxFrameBuffer[25]`, `s_rxIndex` | State machine pendeteksi Sync Byte `0xAA 0x55` hingga 25 byte terkumpul. |
| `src/utils/crc_utils.hpp` | `CrcUtils::isValidXorChecksum()` | `s_rxFrameBuffer[25]`, `s_crcErrorCount` | Validasi XOR byte 0..23 terhadap byte 24. Jika salah, drop frame. |
| `src/core/beacon_pipeline.cpp` | `BeaconPipeline::pushPacket()` | `g_config.rssi_min`, `g_config.filter_uuid`, `g_config.filter_major` | Evaluasi kaskade filter hardware: RSSI, UUID 16-byte, dan Tag Major. |
| `src/core/beacon_pipeline.cpp` | `BeaconPipeline::pushPacket()` | `s_readCount` (`std::atomic<uint32_t>`) | Menambah counter paket valid yang lolos filter. |
| `src/core/beacon_pipeline.cpp` | `BeaconPipeline::pushPacket()` | `s_beaconQueue` (`QueueHandle_t`, 300 slot) | Mengirim ke antrean. Jika penuh, panggil `xQueueReceive` (drop-oldest). |
| `src/core/beacon_pipeline.cpp` | `BeaconPipeline::recordLiveBeacon()` | `s_liveBeaconBuf[50]`, `s_liveBufMutex` | Menyimpan paket ke ring-buffer live telemetri di bawah proteksi mutex 5ms. |

---

## 3. Peta MQTT Batch Publisher (Core 1 - APP_CPU)

| File Sumber | Fungsi Utama | Variabel / Buffer | Logika & Alur Data |
| :--- | :--- | :--- | :--- |
| `src/network/mqtt_service.cpp` | `MqttService::publisherTask()` | `s_beaconQueue`, `BeaconPacket pkt` | Loop FreeRTOS membaca paket dari `s_beaconQueue`. |
| `src/network/mqtt_service.cpp` | `MqttService::appendToBatch()` | `m_batchBuffer[4096]`, `m_batchCount` | Mengemas paket ke dalam format biner terkompresi 6-Byte atau 7-Byte. |
| `src/network/mqtt_service.cpp` | `MqttService::flushBatch()` | `g_config.batch_max`, `g_config.batch_flush` | Memicu transmisi saat volume mencapai `batch_max` atau timer habis. |
| `src/network/mqtt_service.cpp` | `esp_mqtt_client_publish()` | Topik `beacon/<device_id>` | Transmisi paket biner batch ke broker MQTT via soket TCP lwIP. |
| `src/network/mqtt_service.cpp` | Kalkulasi Bandwidth | `m_netBandwidth` (KB/s), `m_mqttPublishCount` | Menghitung seluruh egress layer data (Payload + MQTT + TCP/IP + Ethernet MAC). |
| `src/network/drivers/EthernetW5500.cpp` | Transmisi Fisik | SPI2 Bus (25 MHz), `CS:26, SCK:18...` | Pengiriman paket melalui hardware akselerasi SPI DMA controller. |

---

## 4. Peta Web UI & WebSocket Telemetri (Core 1 - APP_CPU)

| File Sumber | Fungsi Utama | Variabel / Endpoint | Logika & Alur Data |
| :--- | :--- | :--- | :--- |
| `src/web/web_server.cpp` | `WebServer::wsBroadcastTask()` | Interval `100 ms`, Port `80` | Task periodik Core 1 yang memantau klien WebSocket aktif. |
| `src/core/beacon_pipeline.cpp` | `BeaconPipeline::drainLiveBeacons()` | `s_liveBeaconBuf[50]`, `s_liveBufMutex` | Mengosongkan live buffer di bawah proteksi mutex 5ms ke array lokal. |
| `src/web/web_server.cpp` | JSON Serializer | JSON string (HUD metrics + packets array) | Membundel kecepatan baca, MQTT speed, utilisasi antrean, dan 50 tag beacon. |
| `src/web/web_server.cpp` | `httpd_ws_send_frame_async()` | Endpoint `/ws` (`HTTPD_WS_TYPE_TEXT`) | Mengirimkan frame WebSocket secara asynchronous tanpa memblokir server. |
| `data/assets/js/test.js` | `ws.onmessage()` | `stat_read_speed`, `stat_unique_tags`... | Browser merender 8-Card HUD Grid dan grafik 6-Series Canvas 60 detik. |
