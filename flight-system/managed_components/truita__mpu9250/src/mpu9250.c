// SPDX-License-Identifier: MIT
#include "mpu9250.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <math.h>  // For trigonometric functions

#define TAG "MPU9250"
#define TIMEOUT pdMS_TO_TICKS(1000)

#define MPU9250_MAG_ADDR 0x0C
#define MPU9250_MAG_REG_ST1 0x02
#define MPU9250_MAG_REG_DATA 0x03
#define MPU9250_MAG_REG_ST2 0x09
#define MPU9250_MAG_REG_CNTL 0x0A

#define GYRO_SCALE_FACTOR 131.0  // For ±250 dps range

#define ERR_CHECK(x)                                                                 \
    do {                                                                             \
        esp_err_t err = (x);                                                       \
        if (err != ESP_OK) {                                                       \
            ESP_LOGE(TAG, "%s failed: %s", #x, esp_err_to_name(err));             \
            return err;                                                             \
        }                                                                            \
    } while (0)

#define ERR_CHECK_VOID(x)                                                            \
    do {                                                                             \
        esp_err_t err = (x);                                                       \
        if (err != ESP_OK) {                                                       \
            ESP_LOGE(TAG, "%s failed: %s", #x, esp_err_to_name(err));             \
            return;                                                                  \
        }                                                                            \
    } while (0)

void mpu_enable_bypass(i2c_port_t i2c_num) {
    uint8_t cmd[] = {0x37, 0x02};  // INT_PIN_CFG register, Enable bypass mode
    ERR_CHECK_VOID(i2c_master_write_to_device(i2c_num, 0x68, cmd, sizeof(cmd), TIMEOUT));
}

void mpu_disable_bypass(i2c_port_t i2c_num) {
    uint8_t cmd[] = {0x37, 0x00};  // Disable bypass mode
    ESP_ERROR_CHECK(i2c_master_write_to_device(i2c_num, 0x68, cmd, sizeof(cmd), TIMEOUT));
}

// Function to set the AK8963 (magnetometer) to continuous measurement mode
void mpu9250_enable_magnetometer(i2c_port_t i2c_num) {
    uint8_t data[2];

    // Write to the CNTL1 register of the AK8963 to set it to continuous measurement mode
    data[0] = 0x0A;  // CNTL1 register address
    data[1] = 0x06;  // Continuous measurement mode

    esp_err_t ret = i2c_master_write_to_device(i2c_num, 0x0C, data, sizeof(data), TIMEOUT);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to AK8963 CNTL1 register. Error code: %d", ret);
        return;
    }

    ESP_LOGI(TAG, "Magnetometer set to continuous mode.");
}
int mpu9250_begin(mpu9250_t *mpu, i2c_port_t i2c_num, uint8_t address, gpio_num_t sda, gpio_num_t scl) {
    mpu->_address = address;
    mpu->_i2c_num = i2c_num;

    // Wake up MPU9250
    uint8_t wake_cmd[] = {0x6B, 0x00};  // Power management register
    ERR_CHECK(i2c_master_write_to_device(i2c_num, address, wake_cmd, sizeof(wake_cmd), TIMEOUT));
	
	mpu_disable_bypass(i2c_num);
    // Enable bypass mode to access the magnetometer
    mpu_enable_bypass(i2c_num);
    
    //~ mpu9250_enable_magnetometer(i2c_num);

    
    uint8_t bypass_reg_value;
    ERR_CHECK(i2c_master_write_read_device(i2c_num, 0x68, (uint8_t[]){0x37}, 1, &bypass_reg_value, 1, TIMEOUT));
    ESP_LOGI(TAG, "Bypass mode register value: 0x%X", bypass_reg_value);

    //~ // Reset the magnetometer
    //~ uint8_t reset_cmd[] = {MPU9250_MAG_REG_CNTL, 0x01};  // Reset command for the magnetometer
    //~ ERR_CHECK(i2c_master_write_to_device(i2c_num, MPU9250_MAG_ADDR, reset_cmd, sizeof(reset_cmd), TIMEOUT));
    //~ vTaskDelay(pdMS_TO_TICKS(10));  // Wait for reset to complete

    //~ // Power up the magnetometer again
    //~ uint8_t power_up_cmd[] = {MPU9250_MAG_REG_CNTL, 0x16};  // Continuous measurement mode
    //~ ERR_CHECK(i2c_master_write_to_device(i2c_num, MPU9250_MAG_ADDR, power_up_cmd, sizeof(power_up_cmd), TIMEOUT));

    //~ uint8_t who_am_i;
    //~ esp_err_t err = i2c_master_write_read_device(i2c_num, MPU9250_MAG_ADDR, (uint8_t[]){0x00}, 1, &who_am_i, 1, TIMEOUT);
    //~ if (err == ESP_OK) {
        //~ ESP_LOGI(TAG, "Magnetometer WHO_AM_I: 0x%02X", who_am_i);
    //~ } else {
        //~ ESP_LOGE(TAG, "Failed to read magnetometer WHO_AM_I: %s", esp_err_to_name(err));
    //~ }

    return ESP_OK;
}

int mpu9250_update(mpu9250_t *mpu) {
    uint8_t address = mpu->_address;
    i2c_port_t i2c_num = mpu->_i2c_num;

    uint8_t reg = 0x3B;  // ACCEL_XOUT_H
    uint8_t data[14];

    // Read 14 bytes from accelerometer and gyroscope registers
    ERR_CHECK(i2c_master_write_read_device(i2c_num, address, &reg, 1, data, 14, TIMEOUT));

    // Raw accelerometer and gyroscope values
    mpu->accel.x = (int16_t)(data[0] << 8 | data[1]);
    mpu->accel.y = (int16_t)(data[2] << 8 | data[3]);
    mpu->accel.z = (int16_t)(data[4] << 8 | data[5]);
    mpu->temp    = (int16_t)(data[6] << 8 | data[7]);
    mpu->gyro.x  = (int16_t)(data[8] << 8 | data[9]);
    mpu->gyro.y  = (int16_t)(data[10] << 8 | data[11]);
    mpu->gyro.z  = (int16_t)(data[12] << 8 | data[13]);

    // Convert accelerometer data to pitch and roll in degrees
    float accel_x_g = mpu->accel.x / 16384.0;  // Convert to g (assuming ±2g scale)
    float accel_y_g = mpu->accel.y / 16384.0;
    float accel_z_g = mpu->accel.z / 16384.0;

    // Calculate pitch and roll in radians
    float pitch_radians = atan(accel_x_g / sqrt(accel_y_g * accel_y_g + accel_z_g * accel_z_g));
    float yaw_radians = atan(accel_y_g / sqrt(accel_x_g * accel_x_g + accel_z_g * accel_z_g));

    // Convert radians to degrees
    mpu->pitch = pitch_radians * (180.0 / M_PI);
    mpu->yaw  = yaw_radians * (180.0 / M_PI);

    // Convert gyroscope data to degrees per second
    mpu->gyro.x = mpu->gyro.x / GYRO_SCALE_FACTOR;  // Convert to dps
    mpu->gyro.y = mpu->gyro.y / GYRO_SCALE_FACTOR;
    mpu->gyro.z = mpu->gyro.z / GYRO_SCALE_FACTOR;

    //~ // Read magnetometer data
    //~ uint8_t st1;
    //~ ERR_CHECK(i2c_master_write_read_device(i2c_num, MPU9250_MAG_ADDR, (uint8_t[]){MPU9250_MAG_REG_ST1}, 1, &st1, 1, TIMEOUT));

    //~ if (st1 & 0x01) {  // Data ready
        //~ uint8_t mag_data[7];
        //~ ERR_CHECK(i2c_master_write_read_device(i2c_num, MPU9250_MAG_ADDR, (uint8_t[]){MPU9250_MAG_REG_DATA}, 1, mag_data, 7, TIMEOUT));

        //~ if (!(mag_data[6] & 0x08)) {  // Check for magnetic overflow
            //~ mpu->mag.x = (int16_t)(mag_data[0] | (mag_data[1] << 8));
            //~ mpu->mag.y = (int16_t)(mag_data[2] | (mag_data[3] << 8));
            //~ mpu->mag.z = (int16_t)(mag_data[4] | (mag_data[5] << 8));
        //~ } else {
            //~ ESP_LOGW(TAG, "Magnetometer overflow");
        //~ }
    //~ }

    // Log or print the data for debugging
    //ESP_LOGI(TAG, "Pitch: %.2f, Yaw: %.2f, GyroX: %.2f, GyroY: %.2f, GyroZ: %.2f",
      //       mpu->pitch, mpu->yaw, (float)mpu->gyro.x, (float)mpu->gyro.y, (float)mpu->gyro.z);

    return ESP_OK;
}
