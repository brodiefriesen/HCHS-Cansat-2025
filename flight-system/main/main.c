
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ra01s.h"
#include "esp_netif.h"
#include <esp_event.h>

//main task log tag
static const char *TAG = "main";

//duration of LoRa recieption period
#define TIMEOUT 200

//dont worry about these :) (okay, yay!)
SemaphoreHandle_t loraMutex;
TaskHandle_t rx_task_handle;


void getReport(char* out){
    char rep[100] = "h";
    //read and parse sensor data here
    //also check non periodic queue for data
    //basically do whatever you want, if it ends up in rep, it will end up in the package
    char ack[5] = "ACK";
    snprintf(rep + strlen(rep), sizeof(ack), ack); //heres how you concatonate strings
    strcpy(out, rep);
}

void transmit_loop_task(void *pvParameters){
    while(1){
        char report[100];
        getReport(report);
        //wait for lora mutex
        if(xSemaphoreTake(loraMutex, portMAX_DELAY)==pdTRUE)
        {
            //suspend rx task(the mutex should have us covered, but just in case)
            vTaskSuspend(rx_task_handle);

            int txLen = sizeof(report);

            // Wait for transmission to complete
            if (LoRaSend((uint8_t *)report, txLen, SX126x_TXMODE_SYNC) == false) {
                ESP_LOGE(pcTaskGetName(NULL),"LoRaSend fail");
            }

            // Do not wait for the transmission to be completed
            //LoRaSend(buf, txLen, SX126x_TXMODE_ASYNC );

            int lost = GetPacketLost();
            if (lost != 0) {
                ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
            }
            
            //return mutex
            xSemaphoreGive(loraMutex);
            //resume rx task
            vTaskResume(rx_task_handle);
        }
        //feel free to change this delay, it shouldnt break anything important
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void rx_task(void *pvParameters){
    while(1){
        //wait for mutex(if this task is running, it is probably available)
        if(xSemaphoreTake(loraMutex, portMAX_DELAY)==pdTRUE){
            bool waiting = true;
            char buf[100];
            TickType_t startTick = xTaskGetTickCount();
            while(waiting) {
                uint8_t rxLen = LoRaReceive((uint8_t *)buf, sizeof(buf));
                TickType_t currentTick = xTaskGetTickCount();
                TickType_t diffTick = currentTick - startTick;
                if ( rxLen > 0 ) {
                    //rx recieved
                    //do stuff
                    ESP_LOGI(pcTaskGetName(NULL), "Package Recieved: %s", buf);
                    waiting = false;
                }
                if (diffTick > TIMEOUT) {
                    //timeout condition
                    waiting = false;
                }
            } 
            //return mutex
            xSemaphoreGive(loraMutex);
        }
        //same with this delay
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
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

    loraMutex = xSemaphoreCreateMutex();

    //I dont know what all this does and I am too fearful to touch it
    ESP_LOGI(TAG, "NVS init");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "Eventloop create");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
   
    //create tasks
    xTaskCreate(rx_task, "recieve loop task", 5000, NULL, 3, &rx_task_handle);
    xTaskCreate(transmit_loop_task, "transmit loop task", 5000, NULL, 4, NULL);
}