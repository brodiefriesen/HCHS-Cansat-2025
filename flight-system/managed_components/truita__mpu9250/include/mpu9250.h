// SPDX-License-Identifier: MIT
#pragma once

#include "driver/i2c.h"
#include <stdint.h>

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} vec3i16_t;

typedef struct {
    i2c_port_t _i2c_num;
    uint8_t _address;
    vec3i16_t accel;
    vec3i16_t gyro;
    vec3i16_t mag;
    int16_t temp;
    float pitch;  // Add pitch field for storing pitch value
    float yaw;
    float roll;   // Add roll field for storing roll value
} mpu9250_t;

/**
 * @brief Initialize the MPU9250
 * 
 * @param mpu Pointer to the mpu9250_t struct to initialize
 * @param i2c_num I2C port number (e.g., I2C_NUM_0)
 * @param address I2C address of the MPU9250 (usually 0x68 or 0x69)
 * @return esp_err_t ESP_OK on success, or an error code
 */
int mpu9250_begin(mpu9250_t *mpu, i2c_port_t i2c_num, uint8_t address, gpio_num_t sda, gpio_num_t scl);

/**
 * @brief Update all sensor readings from the MPU9250
 * 
 * Fills accel, gyro, temp, mag, pitch, and roll fields in the struct if available.
 * 
 * @param mpu Pointer to the mpu9250_t struct to update
 * @return esp_err_t ESP_OK on success, or an error code
 */
int mpu9250_update(mpu9250_t *mpu);
