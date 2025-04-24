#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ra01s.h"
#include "esp_netif.h"
#include <esp_event.h>
#include "wifi.h"
#include "http.h"

static const char *TAG = "main";

// Queue variable definitions
static uint8_t msg_queue_len = 10;
QueueHandle_t incoming;
QueueHandle_t outgoing;
static uint8_t image_queue_len = 100;
QueueHandle_t image_out;
TaskHandle_t tx_task_handle = NULL;  // Declare the task handle globally

// This task gets the network up and running (see wifi.c for more info)
void start_network_task(void *pvParameters) {
    ESP_LOGI(pcTaskGetName(NULL), "init softAP");
    ESP_ERROR_CHECK(wifi_init_softap());
    vTaskDelete(NULL);
}

// This task gets the HTTP server going (see http.c for more info)
void webserver_task(void *pvParameters) {
    httpd_handle_t server = NULL;
    server = start_webserver(&outgoing, &incoming, &image_out);
    vTaskDelete(NULL);
}

// LoRa Transmit Task - Send messages from the outgoing queue
void tx_task(void *pvParameters) {
    char out[200];

    while (1) {
        // Wait indefinitely for data in the outgoing queue or a task notification
        if (xQueueReceive(outgoing, (void *)&out, portMAX_DELAY) == pdTRUE) {
            int txLen = strlen(out) + 1; // Ensure correct length
            ESP_LOGI(pcTaskGetName(NULL), "Sending %d-byte packet: %s", txLen, out);

            if (!LoRaSend((uint8_t *)out, txLen, SX126x_TXMODE_SYNC)) {
                ESP_LOGE(pcTaskGetName(NULL), "LoRaSend failed!");
            }
        }

        // Block waiting for further messages or a notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}


// LoRa Receive Task - Receive messages and put them in the incoming queue
void rx_task(void *pvParameters) {
    char in[110];

    while (1) {
        uint8_t rxLen = LoRaReceive((uint8_t *)in, sizeof(in));

        if (rxLen > 0) {
            if(in[0] == 'I'){
                if(xQueueSend(image_out, (void *)in, pdMS_TO_TICKS(10)) != pdTRUE) {
                    ESP_LOGI(TAG, "Incoming queue full!");
                }
            }else if (xQueueSend(incoming, (void *)in, pdMS_TO_TICKS(10)) != pdTRUE) {
                ESP_LOGI(TAG, "Incoming queue full!");
            } else {
                ESP_LOGI(pcTaskGetName(NULL), "Received: %s", in);
            }
        }

        // Small delay to prevent excessive CPU usage
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void app_main(void)
{
    LoRaInit();

    int8_t txPowerInDbm = 22;
    uint32_t frequencyInHz = 909000000;
    ESP_LOGI(TAG, "Frequency is 909MHz");

    float tcxoVoltage = 3.3;
    bool useRegulatorLDO = true;

    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0) {
        ESP_LOGE(TAG, "Does not recognize the module");
        while (1) {
            vTaskDelay(1);
        }
    }

    uint8_t spreadingFactor = 7;
    uint8_t bandwidth = 4;
    uint8_t codingRate = 1;
    uint16_t preambleLength = 8;
    uint8_t payloadLen = 0;
    bool crcOn = true;
    bool invertIrq = false;

    LoRaConfig(spreadingFactor, bandwidth, codingRate, preambleLength, payloadLen, crcOn, invertIrq);

    outgoing = xQueueCreate(msg_queue_len, sizeof(char[100]));
    incoming = xQueueCreate(msg_queue_len, sizeof(char[400]));
    image_out = xQueueCreate(msg_queue_len, sizeof(char[110]));

    // I don't know what all this does, and I am too fearful to touch it
    ESP_LOGI(TAG, "NVS init");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "Eventloop create");
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    // Create tasks with TX task having a higher priority
    xTaskCreate(start_network_task, "start network task", 5000, NULL, 10, NULL);
    xTaskCreate(webserver_task, "webserver task", 10000, NULL, 10, NULL);
    xTaskCreate(tx_task, "tx task", 5000, NULL, 20, &tx_task_handle); // Higher priority for TX
    xTaskCreate(rx_task, "rx task", 5000, NULL, 10, NULL); // RX has lower priority

    // Allow time for tasks to be created and scheduled(this shouldnt be required, app_main should not end until all tasks have been terminated)
    // vTaskDelay(pdMS_TO_TICKS(1000));  
}
