#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ra01s.h"

#undef LOW
#undef HIGH
#include "esp_netif.h"
#include <esp_event.h>
#include <i2c_bus.h>
#include "mpu9250.h"
#include <math.h>
#include "bmp180.h"
#include <esp_timer.h>
#include "VL53L1X_api.h"
#include "i2c_platform_esp.h"
#include "driver/gpio.h"
#include "driver/uart.h"

static mpu9250_t imu;

#define LORA_FREQUECY       909000000
#define TIMEOUT             200
#define P_I2C_SDA           20
#define P_I2C_SCL           19
#define MPU9250_ADDRESS     0x68
#define TOF_I2C_ADDR        (0x29 << 1)
#define PARACHUTE_PIN       GPIO_NUM_2
#define DUO_PIN             GPIO_NUM_48
#define ACCEL_THRESHOLD     2.0f
//GPS stuff
#define TIME_ZONE (-6)
#define YEAR_BASE (2000)

static const char *TAG = "main";

SemaphoreHandle_t loraMutex;
TaskHandle_t rx_task_handle;
SemaphoreHandle_t queueMutex;

int mpu_enabled = 1;
int bmp_enabled = 1;
static bool parachute_deployed = false;
const uart_port_t image_uart_num = UART_NUM_2;
char send_queue[50]; 

typedef enum {
    STATE_GROUND = 0,
    STATE_LAUNCH,
    STATE_COAST,
    STATE_DEPLOY,
    STATE_PARACHUTE,
    STATE_LANDED
} flight_state_t;

typedef enum {
    TRANSMIT,
    SAVE,
    SHUTDOWN,
    NONE
} image_state_t;

static flight_state_t flight_state = STATE_GROUND;
static float ground_altitude = -1;
static float last_altitude = 0;
static bool last_tof_valid = true;
static image_state_t image_state = NONE;
float vel;
float init_vel;

static void deployParachute() {
    gpio_set_level(PARACHUTE_PIN, 0);
    vTaskDelay(3000);
    gpio_set_level(PARACHUTE_PIN, 1);
    ESP_LOGI(TAG, "Parachute deployed");
}

static void updateFlightState(float altitude, bool tof_valid, float tof_dist, float ax, float ay, float az) {
    float rel_alt = (ground_altitude < 0) ? 0 : (altitude - ground_altitude);
    float az_corrected = az - 1.0f;
    float acc_mag = sqrtf(ax * ax + ay * ay + az_corrected * az_corrected);
    float fall_vel = (last_altitude-rel_alt)/0.5;
    vel = fall_vel;

    switch (flight_state) {
        case STATE_GROUND:
            if (acc_mag > ACCEL_THRESHOLD && rel_alt > last_altitude) {
                flight_state = STATE_LAUNCH;
                ESP_LOGI(TAG, "State->LAUNCH");
            }
            break;
        case STATE_LAUNCH:
            if (acc_mag < ACCEL_THRESHOLD && rel_alt > last_altitude) {
                flight_state = STATE_COAST;
                ESP_LOGI(TAG, "State->COAST");
                gpio_set_level(DUO_PIN, 1);
            }
            break;
        case STATE_COAST:
            if (!tof_valid) {
                flight_state = STATE_DEPLOY;
                ESP_LOGI(TAG, "State->DEPLOY");
            }
            break;
        case STATE_DEPLOY:
            if (rel_alt < last_altitude && last_altitude < 600.0f) {
                flight_state = STATE_PARACHUTE;
                ESP_LOGI(TAG, "State->PARACHUTE");
                init_vel = fall_vel;
                deployParachute();

            }
            break;
        case STATE_PARACHUTE:
            if((init_vel - fall_vel) > 3){
                parachute_deployed = true;
            }
            if (rel_alt < 20.0f) {
                flight_state = STATE_LANDED;
                ESP_LOGI(TAG, "State->LANDED");
            }
            break;
        default:
            break;
    }
    last_altitude = rel_alt;
    last_tof_valid = tof_valid;
}

void getReport(char* out) {
    char rep[300];
    uint64_t ts = esp_timer_get_time() / 1000;

    // Read MPU and convert to Gs
    float ax=0, ay=0, az=0;
    if (mpu_enabled && mpu9250_update(&imu) == ESP_OK) {
        ax = imu.accel.x / 16384.0f;
        ay = imu.accel.y / 16384.0f;
        az = imu.accel.z / 16384.0f;
    }
    // Read gyro
    float gx=0, gy=0, gz=0;
    if (mpu_enabled) {
        gx = imu.gyro.x / 131.0f;
        gy = imu.gyro.y / 131.0f;
        gz = imu.gyro.z / 131.0f;
    }
    // Pitch & Yaw
    float pitch = atan2f(ay, sqrtf(ax*ax + az*az)) * (180.0f/M_PI);
    float yaw   = atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f/M_PI);

    // Read BMP altitude
    float altitude=0;
    float temp; uint32_t pres;
    if (bmp_enabled) {
        if (bmp180_read_temperature(&temp)==ESP_OK && bmp180_read_pressure(&pres)==ESP_OK) {
            altitude = 44330.0f * (1.0f - powf((float)pres / 102300.0f, 0.1903f));
        }
    }

    // Read ToF
    bool tof_valid=false;
    float tof_m=0;
    VL53L1X_Result_t r;
    if (VL53L1X_GetResult(TOF_I2C_ADDR, &r)==0) {
        tof_valid = (r.Status==0);
        tof_m = r.Distance/1000.0f;
        VL53L1X_ClearInterrupt(TOF_I2C_ADDR);
    }

    // update state
    float acc_mag = sqrtf(ax*ax + ay*ay + az*az);
    updateFlightState(altitude, tof_valid, tof_m, ax, ay, az);


    // build report
    char buf[280];
    if (xSemaphoreTake(queueMutex, portMAX_DELAY)==pdTRUE) {
        snprintf(buf, sizeof(buf),
            "DWL:%lld:PRES:%ld:TEMP:%.1f:ACC:%.2f,%.2f,%.2f:VEL::GY:%.2f,%.2f,%.2f:PITCH:%.2f:YAW:%.2f:ALT:%.2f:TOF:%.2f:STATE:%d:CHUTE:%d:DUO:%s",
            ts, pres, temp, ax,ay,az, gx,gy,gz, pitch,yaw, altitude, tof_m, flight_state, parachute_deployed, send_queue);
        xSemaphoreGive(queueMutex);
    }
    snprintf(rep, sizeof(rep), buf);
    strcat(rep, "EOT");
    strcpy(out, rep);
}

void transmit_loop_task(void*pv) {
    while(1) {
        char report[300];
        getReport(report);
        ESP_LOGI(TAG, "%s", report);
        if (xSemaphoreTake(loraMutex, portMAX_DELAY)==pdTRUE) {
            LoRaSend((const char*)report, strlen(report), SX126x_TXMODE_SYNC);
            xSemaphoreGive(loraMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void duo_comm_task(void*pv){
    while(image_state!=SHUTDOWN){
        switch(image_state){
            case SAVE:
                uart_write_bytes(image_uart_num, (const char*) "i", 1);
                ESP_ERROR_CHECK(uart_wait_tx_done(image_uart_num, 100));
                break;
            case TRANSMIT:
                ESP_LOGI(TAG, "TIMAGE");
                uart_write_bytes(image_uart_num, (const char*) "t", 1);
                ESP_ERROR_CHECK(uart_wait_tx_done(image_uart_num, 100));
                break;
            case SHUTDOWN:
                uart_write_bytes(image_uart_num, (const char*) "s", 1);
                vTaskDelete(NULL);
            case NONE:
                break;
        }
        char buf[1024];
        char final[1024];
        int length = 0;
        int packet_len = 0;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(image_uart_num, (size_t*)&length));
        if(length > 0){
            uart_read_bytes(image_uart_num, buf, length, 100);
            ESP_LOGI(TAG, "Recieved: %s", buf);
            if(buf[0] == 'I'){
                while(length>0){
                    if (xSemaphoreTake(loraMutex, portMAX_DELAY)==pdTRUE) {
                        LoRaSend((uint8_t*)buf, sizeof(buf), SX126x_TXMODE_SYNC);
                        xSemaphoreGive(loraMutex);
                    }
                    ESP_ERROR_CHECK(uart_get_buffered_data_len(image_uart_num, (size_t*)&length));
                }
            }else if(buf[0] == 'G'){
                snprintf(buf, length, final);
                packet_len += length;
                ESP_ERROR_CHECK(uart_get_buffered_data_len(image_uart_num, (size_t*)&length));
                uart_read_bytes(image_uart_num, buf, length, 100);
                while(length>0){
                    snprintf(buf, length, final);
                    packet_len += length;
                    ESP_ERROR_CHECK(uart_get_buffered_data_len(image_uart_num, (size_t*)&length));
                    uart_read_bytes(image_uart_num, buf, length, 100);
                }
                if (xSemaphoreTake(queueMutex, portMAX_DELAY)==pdTRUE) {
                    snprintf(send_queue, packet_len, final);
                    xSemaphoreGive(queueMutex);
                }
            }
            ESP_ERROR_CHECK(uart_get_buffered_data_len(image_uart_num, (size_t*)&length));
        }
        image_state = NONE;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void rx_task(void*pv) {
    while(1) {
        if (xSemaphoreTake(loraMutex, portMAX_DELAY)==pdTRUE) {
            char buf[128];
            uint8_t len = LoRaReceive((uint8_t*)buf, sizeof(buf));
            if (len) {
                buf[len]='\0';
                ESP_LOGI(TAG, "Received: %s", buf);
                // parse uplink command
                if (strncmp(buf, "CMD:STATE:",10)==0) {
                    int s = atoi(buf+10);
                    if (s>=STATE_GROUND && s<=STATE_LANDED) {
                        flight_state = (flight_state_t)s;
                        ESP_LOGI(TAG, "State overridden to %d via CMD", s);
                    }
                }else if (strncmp(buf, "CMD:IMAGE:",10)==0) {
                    ESP_LOGI(TAG, "Save Image");
                    image_state = SAVE;
                }else if (strncmp(buf, "CMD:TIMAGE:",11)==0) {
                    ESP_LOGI(TAG, "Transmit Image Capture");
                    image_state = TRANSMIT;
                }else if (strncmp(buf, "CMD:SIMAGE:",11)==0) {
                    ESP_LOGI(TAG, "Shutdown Image Capture");
                    image_state = SHUTDOWN;
                }
            
            }
            xSemaphoreGive(loraMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}


void app_main() {
    esp_log_level_set("RA01S", ESP_LOG_DEBUG);
    LoRaInit();
    if (LoRaBegin(LORA_FREQUECY,22,3.3,true)!=0) {
        ESP_LOGE(TAG,"LoRa init failed"); while(1) vTaskDelay(1);
    }
    LoRaConfig(7,4,1,8,0,true,false);
    loraMutex = xSemaphoreCreateMutex();
    queueMutex = xSemaphoreCreateMutex();

    nvs_flash_init(); esp_netif_init(); esp_event_loop_create_default();

    // parachute
    gpio_reset_pin(PARACHUTE_PIN);
    gpio_set_direction(PARACHUTE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PARACHUTE_PIN, 1);

    // duo
    gpio_reset_pin(DUO_PIN);
    gpio_set_direction(DUO_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DUO_PIN, 0);

    // I2C bus
    i2c_config_t conf = {.mode=I2C_MODE_MASTER, .sda_io_num=P_I2C_SDA,
        .scl_io_num=P_I2C_SCL, .sda_pullup_en=GPIO_PULLUP_ENABLE,
        .scl_pullup_en=GPIO_PULLUP_ENABLE, .master.clk_speed=100000};
    i2c_param_config(I2C_NUM_0,&conf);
    i2c_driver_install(I2C_NUM_0,I2C_MODE_MASTER,0,0,0);
    i2c_init_config(I2C_NUM_0,P_I2C_SDA,P_I2C_SCL,100000);
    vTaskDelay(pdMS_TO_TICKS(50));

    // sensors
    mpu9250_begin(&imu,I2C_NUM_0,MPU9250_ADDRESS,P_I2C_SDA,P_I2C_SCL);
    bmp180_init(P_I2C_SDA,P_I2C_SCL);
    vTaskDelay(pdMS_TO_TICKS(50));
    float t; 
    uint32_t p;
    if (bmp180_read_temperature(&t) == ESP_OK && bmp180_read_pressure(&p) == ESP_OK) {
        ESP_LOGI(TAG, "Pressure reading: %lu Pa", (uint32_t)p);
        if (p > 8000 && p < 110000) {  // Valid range: roughly 80 mbar to 1100 mbar
            ground_altitude = 44330.0f * (1.0f - powf((float)p / 102300.0f, 0.1903f));
        } else {
            ground_altitude = 0;  // Fallback if the sensor gives garbage
            ESP_LOGE(TAG, "Pressure reading out of range: %lu Pa, setting ground_altitude to 0!", (uint32_t)p);
        }
        last_altitude = 0;
        ESP_LOGI(TAG, "Baseline altitude: %.2f", ground_altitude);
    } else {
        ground_altitude = 0;
        ESP_LOGE(TAG, "Failed to read from BMP180! Setting ground_altitude to 0.");
    }

    // ToF init
    uint16_t id; ESP_LOGI(TAG,"Probing ToF...");
    int8_t id_ret=VL53L1X_GetSensorId(TOF_I2C_ADDR,&id);
    if(id_ret) {ESP_LOGE(TAG,"GetSensorId:%d",id_ret);} else { 
        ESP_LOGI(TAG,"ID:0x%04X",id);
        if(id==0xEEAC||id==0xEACC) {
            VL53L1X_SensorInit(TOF_I2C_ADDR);
            VL53L1X_SetTimingBudgetInMs(TOF_I2C_ADDR,50);
            VL53L1X_SetDistanceMode(TOF_I2C_ADDR,2);
            VL53L1X_SetInterMeasurementInMs(TOF_I2C_ADDR,50);
            VL53L1X_StartRanging(TOF_I2C_ADDR);
        }
    }

    uart_config_t image_uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure image UART parameters
    ESP_ERROR_CHECK(uart_driver_install(image_uart_num, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(image_uart_num, &image_uart_config));
    ESP_ERROR_CHECK(uart_set_pin(image_uart_num, 48, 47, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)); //image tx: 48, rx: 47

    xTaskCreate(rx_task,"rx",4096,NULL,3,&rx_task_handle);
    xTaskCreate(transmit_loop_task,"tx",4096,NULL,4,NULL);
    xTaskCreate(duo_comm_task, "duo comm",4096,NULL,3,NULL);
}