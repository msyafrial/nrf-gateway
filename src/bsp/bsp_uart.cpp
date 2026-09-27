#include "bsp_uart.hpp"
#include "beacon_pipeline.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "BSP_UART";

namespace Iotaru::Bsp {

static QueueHandle_t s_uartEventQueue = nullptr;
static TaskHandle_t  s_uartTaskHandle = nullptr;

static void uartEventTask(void *pvParameters) {
    uart_event_t event;
    uint8_t dtmp[1024];

    for (;;) {
        // Task terblokir (tidur tanpa memakan CPU) sampai ada data masuk via interrupt
        if (xQueueReceive(s_uartEventQueue, (void *)&event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA: {
                    size_t bufferedSize = 0;
                    uart_get_buffered_data_len(UART_NUM_2, &bufferedSize);
                    while (bufferedSize > 0) {
                        int toRead = (bufferedSize < sizeof(dtmp)) ? (int)bufferedSize : (int)sizeof(dtmp);
                        int len = uart_read_bytes(UART_NUM_2, dtmp, toRead, 0);
                        if (len > 0) {
                            Core::BeaconPipeline::processBytes(dtmp, len);
                            bufferedSize -= len;
                        } else {
                            break;
                        }
                    }
                    break;
                }
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "Hardware UART FIFO overflow!");
                    uart_flush_input(UART_NUM_2);
                    xQueueReset(s_uartEventQueue);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "Driver UART Ring Buffer full!");
                    uart_flush_input(UART_NUM_2);
                    xQueueReset(s_uartEventQueue);
                    break;
                case UART_BREAK:
                case UART_PARITY_ERR:
                case UART_FRAME_ERR:
                default:
                    break;
            }
        }
    }
}

esp_err_t UartManager::init() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = Pins::NRF_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity    = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 122;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, Pins::NRF_TX, Pins::NRF_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 4096, 0, 20, &s_uartEventQueue, 0));

    // Spawn task di Core 0 dengan prioritas tinggi
    BaseType_t res = xTaskCreatePinnedToCore(
        uartEventTask,
        "nRF_UART_Task",
        4096,
        nullptr,
        configMAX_PRIORITIES - 2,
        &s_uartTaskHandle,
        0 // Core 0 terdedikasi
    );

    ESP_LOGI(TAG, "UART2 nRF52840 diinisialisasi (%d bps, RX Pin %d, TX Pin %d, Task Core 0)",
             Pins::NRF_BAUD, Pins::NRF_RX, Pins::NRF_TX);
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void* UartManager::getEventQueue() {
    return s_uartEventQueue;
}

} // namespace Iotaru::Bsp
