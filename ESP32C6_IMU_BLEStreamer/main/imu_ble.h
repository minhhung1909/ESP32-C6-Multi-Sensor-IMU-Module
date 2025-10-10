#ifndef IMU_BLE_H
#define IMU_BLE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     enable_iis2mdc;
    bool     enable_iis3dwb;
    bool     enable_icm45686;
    bool     enable_scl3300;
    uint16_t iis3dwb_odr_hz;       // BLE-friendly ODR (e.g., 400-800Hz)
    uint16_t icm45686_odr_hz;      // 200-400Hz
    uint16_t packet_interval_ms;   // e.g., 20ms (~50Hz)
} imu_ble_config_t;

esp_err_t imu_ble_init(const imu_ble_config_t *cfg);

#endif // IMU_BLE_H
