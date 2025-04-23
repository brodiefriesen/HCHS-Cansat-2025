#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ra01s.h"
#include "esp_netif.h"
#include <esp_event.h>
#include <i2c_bus.h>
#include "mpu9250.h"
#include <math.h>
#include "bmp180.h"
#include <esp_timer.h>

static mpu9250_t imu;

#define TIMEOUT 200
#define P_I2C_SDA 20
#define P_I2C_SCL 19
#define MPU9250_ADDRESS 0x68

static const char *TAG = "main";

SemaphoreHandle_t loraMutex;
TaskHandle_t rx_task_handle;

int mpu_enabled = 1;
int bmp_enabled = 1;

void getReport(char* out) {
    char rep[250];
    uint64_t timestamp_ms = esp_timer_get_time() / 1000;
    snprintf(rep, sizeof(rep), "DWL:1:%llu:", timestamp_ms);

    if (mpu_enabled && mpu9250_update(&imu) == 0) {
        float gyro_x = imu.gyro.x / 131.0f;
        float gyro_y = imu.gyro.y / 131.0f;
        float gyro_z = imu.gyro.z / 131.0f;

        float accel_x = imu.accel.x;
        float accel_y = imu.accel.y;
        float accel_z = imu.accel.z;

        float yaw = atan2(-accel_x, sqrt(accel_y * accel_y + accel_z * accel_z)) * (180.0f / M_PI);
        float pitch = atan2(accel_y, sqrt(accel_x * accel_x + accel_z * accel_z)) * (180.0f / M_PI);

        char mpuData[150];
        snprintf(mpuData, sizeof(mpuData),
                 "ACC:%.2f,%.2f,%.2f:GY:%.2f,%.2f,%.2f:PITCH:%.2f:YAW:%.2f:MAG:%d,%d,%d:",
                 accel_x, accel_y, accel_z,
                 gyro_x, gyro_y, gyro_z,
                 pitch, yaw,
                 imu.mag.x, imu.mag.y, imu.mag.z
        );

        if (bmp_enabled) {
            float temperature = 0.0f;
            uint32_t pressure = 0;
            if (bmp180_read_temperature(&temperature) == ESP_OK &&
                bmp180_read_pressure(&pressure) == ESP_OK) {

                float altitude = 44330.0f * (1.0f - pow((pressure / 102300.0f), 0.1903f));
                char bmpData[60];
                snprintf(bmpData, sizeof(bmpData), "BMP:TEMP:%.2f:PRES:%lu:ALT:%.2f:",
                         temperature, pressure, altitude);
                strncat(rep, bmpData, sizeof(rep) - strlen(rep) - 1);
            } else {
                strncat(rep, ":BMP_ERR:", sizeof(rep) - strlen(rep) - 1);
            }
        }

        strncat(rep, mpuData, sizeof(rep) - strlen(rep) - 1);
    } else {
        strncat(rep, ":MPU_ERR:", sizeof(rep) - strlen(rep) - 1);
    }



    strncat(rep, "EOT", sizeof(rep) - strlen(rep) - 1);
    strcpy(out, rep);
}

void i2c_scan(i2c_port_t port) {
    ESP_LOGI("I2C", "Scanning...");
    for (int addr = 1; addr < 127; ++addr) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            ESP_LOGI("I2C", "Found device at 0x%02X", addr);
        } else {
            ESP_LOGI("I2C", "No device at 0x%02X", addr);
        }
    }
}

void transmit_loop_task(void *pvParameters) {
    while (1) {
        char report[250];
        getReport(report);
        if (xSemaphoreTake(loraMutex, portMAX_DELAY) == pdTRUE) {
            vTaskSuspend(rx_task_handle);
            int txLen = strlen(report);
            if (!LoRaSend((uint8_t *)report, txLen, SX126x_TXMODE_SYNC)) {
                ESP_LOGE(pcTaskGetName(NULL), "LoRaSend fail");
            }
            int lost = GetPacketLost();
            if (lost != 0) {
                ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
            }
            xSemaphoreGive(loraMutex);
            vTaskResume(rx_task_handle);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void rx_task(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(loraMutex, portMAX_DELAY) == pdTRUE) {
            bool waiting = true;
            char buf[100];
            TickType_t startTick = xTaskGetTickCount();
            while (waiting) {
                uint8_t rxLen = LoRaReceive((uint8_t *)buf, sizeof(buf));
                TickType_t currentTick = xTaskGetTickCount();
                if (rxLen > 0) {
                    ESP_LOGI(pcTaskGetName(NULL), "Package Received: %s", buf);
                    waiting = false;
                }
                if ((currentTick - startTick) > TIMEOUT) {
                    waiting = false;
                }
            }
            xSemaphoreGive(loraMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



void app_main(void) {
    esp_log_level_set("RA01S", ESP_LOG_DEBUG);
    LoRaInit();
    int8_t txPowerInDbm = 22;
    uint32_t frequencyInHz = 915000000;
    ESP_LOGI(TAG, "Frequency is 915MHz");
    float tcxoVoltage = 3.3;
    bool useRegulatorLDO = true;

    if (LoRaBegin(frequencyInHz, txPowerInDbm, tcxoVoltage, useRegulatorLDO) != 0) {
        ESP_LOGE(TAG, "LoRa module not recognized");
        while (1) { vTaskDelay(1); }
    }

    LoRaConfig(7, 4, 1, 8, 0, true, false);
    loraMutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "NVS init");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "Eventloop create");
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // I2C configuration and installation
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = P_I2C_SDA,
        .scl_io_num = P_I2C_SCL,
        .master.clk_speed = 100000
    };
    i2c_param_config(I2C_NUM_0, &conf);
    i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

    vTaskDelay(pdMS_TO_TICKS(50));

    if (mpu_enabled) {
        if (mpu9250_begin(&imu, I2C_NUM_0, MPU9250_ADDRESS, P_I2C_SDA, P_I2C_SCL) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize MPU9250");
            i2c_scan(I2C_NUM_0);
        } else {
            ESP_LOGI(TAG, "MPU9250 initialized");
        }
    }

    if (bmp_enabled) {
        if (bmp180_init(P_I2C_SDA, P_I2C_SCL) != ESP_OK) {
            ESP_LOGE(TAG, "BMP180 init failed");
        } else {
            ESP_LOGI(TAG, "BMP180 initialized");
        }
    }

    xTaskCreate(rx_task, "receive loop task", 5000, NULL, 3, &rx_task_handle);
    xTaskCreate(transmit_loop_task, "transmit loop task", 5000, NULL, 4, NULL);
}
