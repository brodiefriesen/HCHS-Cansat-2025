/* Thanks thaaraak for all of your LoRa and OLED code from https://github.com/thaaraak/Lora!
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_tls_crypto.h"
#include <esp_http_server.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_wifi_types.h"
#include "ra01s.h"

#include "wifi.h"
#include "http.h"

static const char *TAG = "main";

/* An HTTP GET handler */

static uint8_t msg_queue_len = 5;

QueueHandle_t incoming;
QueueHandle_t outgoing;

void start_network_task (void *pvParameters) {
    ESP_LOGI(TAG, "init softAP");
    ESP_ERROR_CHECK(wifi_init_softap());
    vTaskDelete(NULL);
}

void webserver_task (void *pvParameters) {
    httpd_handle_t server = NULL;
    server = start_webserver(&outgoing, &incoming);
    vTaskDelete(NULL);
}

void lora_task(void *pvParameters) {
    while(1){
        char out[100];
        if(xQueueReceive(outgoing, (void *)&out, 0) == pdTRUE) {
            int txLen = sizeof(out);
            ESP_LOGI(pcTaskGetName(NULL), "%d byte packet sent...", txLen);

            // Wait for transmission to complete
            if (LoRaSend((uint8_t *)out, txLen, SX126x_TXMODE_SYNC) == false) {
                ESP_LOGE(pcTaskGetName(NULL),"LoRaSend fail");
            }

            // Do not wait for the transmission to be completed
            //LoRaSend(buf, txLen, SX126x_TXMODE_ASYNC );

            int lost = GetPacketLost();
            if (lost != 0) {
                ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
            }
                vTaskDelay(pdMS_TO_TICKS(500));
        }
        char in[100];
        uint8_t rxLen = LoRaReceive((uint8_t *)in, sizeof(in));
        if ( rxLen > 0 ) { 
			if(xQueueSend(incoming, (void *)in, 10) != pdTRUE){
                ESP_LOGI(TAG, "Queue full!");
            }
            ESP_LOGI(pcTaskGetName(NULL), "%s", in);
		}
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    vTaskDelete(NULL);
}

void app_main(void)
{
    LoRaInit();

    int8_t txPowerInDbm = 22;
    uint32_t frequencyInHz = 915000000;
	ESP_LOGI(TAG, "Frequency is 915MHz");

    float tcxoVoltage = 3.3; // don't use TCXO
	bool useRegulatorLDO = true;

    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0) {
		ESP_LOGE(TAG, "Does not recognize the module");
		while(1) {
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
    incoming = xQueueCreate(msg_queue_len, sizeof(char[100]));

    ESP_LOGI(TAG, "NVS init");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    
    ESP_LOGI(TAG, "Eventloop create");

    ESP_ERROR_CHECK(esp_event_loop_create_default());
   
    xTaskCreate(start_network_task, "start network task", 5000, NULL, 10, NULL);

    xTaskCreate(webserver_task, "webserver task", 10000, NULL, 10, NULL);

    xTaskCreate(lora_task, "lora task", 10000, NULL, 10, NULL);
}