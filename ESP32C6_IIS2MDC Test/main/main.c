#include "iis2mdc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_MASTER_BUS          I2C_NUM_0
#define I2C_MASTER_SDA          23
#define I2C_MASTER_SCL          22
#define I2C_MASTER_CLK_SPEED    400000

static const char *TAG = "MAIN";



void app_main() {
    iis2mdc_handle_t mag;
    iis2mdc_init(&mag, I2C_MASTER_BUS, I2C_MASTER_SDA, I2C_MASTER_SCL, I2C_MASTER_CLK_SPEED);

    uint8_t id;
    iis2mdc_read_who_am_i(&mag, &id);
    ESP_LOGI("TEST", "WHO_AM_I = 0x%02X", id);

    iis2mdc_raw_magnetometer_t raw_data;
    float x_mg, y_mg, z_mg;

    int16_t temp_raw;
    float temp_celsius;

    while (1) {
        iis2mdc_read_magnetic_raw(&mag, &raw_data);
        iis2mdc_convert_magnetic_raw_to_mg(&raw_data, &x_mg, &y_mg, &z_mg);

        iis2mdc_read_temperature_raw(&mag, &temp_raw);
        iis2mdc_convert_temperature_raw_to_celsius(temp_raw, &temp_celsius);

        ESP_LOGI("MAG", "X=%d, Y=%d, Z=%d", raw_data.x, raw_data.y, raw_data.z);
        ESP_LOGI("-->MAG_MG", "X=%.2f mg, Y=%.2f mg, Z=%.2f mg", x_mg, y_mg, z_mg);
        ESP_LOGI("TEMP", "Raw Temp=%d, Celsius=%.2f", temp_raw, temp_celsius);

        ESP_LOGI(TAG, "-----------------------------");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
