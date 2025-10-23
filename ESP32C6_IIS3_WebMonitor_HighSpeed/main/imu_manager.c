#include "imu_manager.h"
#include "sensors/iis3dwb_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "IMU_MANAGER";

// SPI and sensor configuration
#define IIS3DWB_SPI_HOST          SPI2_HOST
#define IIS3DWB_SPI_MISO          2
#define IIS3DWB_SPI_MOSI          7
#define IIS3DWB_SPI_CLK           6
#define IIS3DWB_SPI_CS            19

// Sensor context
static stmdev_ctx_t iis3dwb_ctx;
static SemaphoreHandle_t sensor_mutex = NULL;
static uint16_t fifo_watermark = 256;
static float configured_odr_hz = 26670.0f;  // 26.67kHz max for IIS3DWB
static uint8_t current_full_scale = 0;  // 0=±2g, 1=±4g, 2=±8g, 3=±16g

// Recent samples buffer for WebSocket
#define MAX_RECENT_SAMPLES 256
static float recent_x[MAX_RECENT_SAMPLES];
static float recent_y[MAX_RECENT_SAMPLES];
static float recent_z[MAX_RECENT_SAMPLES];
static uint16_t recent_count = 0;
static uint64_t recent_timestamp = 0;
static uint32_t recent_sequence = 0;
static uint16_t recent_fifo_level = 0;

esp_err_t imu_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing IMU Manager...");

    sensor_mutex = xSemaphoreCreateMutex();
    if (sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor mutex");
        return ESP_FAIL;
    }

    // Initialize SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = IIS3DWB_SPI_MISO,
        .mosi_io_num = IIS3DWB_SPI_MOSI,
        .sclk_io_num = IIS3DWB_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(IIS3DWB_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully (MISO=%d, MOSI=%d, CLK=%d)",
             IIS3DWB_SPI_MISO, IIS3DWB_SPI_MOSI, IIS3DWB_SPI_CLK);

    // Initialize IIS3DWB HAL
    ret = iis3dwb_hal_init(&iis3dwb_ctx, IIS3DWB_SPI_HOST, IIS3DWB_SPI_CS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIS3DWB HAL init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Perform self-test
    uint8_t st_result;
    ret = iis3dwb_hal_self_test(&iis3dwb_ctx, &st_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Self-test procedure failed");
        return ret;
    }
    if (st_result != ST_PASS) {
        ESP_LOGW(TAG, "Self-test did not pass, but continuing...");
    }

    // Configure IIS3DWB
    iis3dwb_hal_cfg_t cfg = {
        .bdu = PROPERTY_ENABLE,
        .odr = IIS3DWB_XL_ODR_26k7Hz,
        .fs = IIS3DWB_4g,  // Start with ±4g for better range
        .filter = IIS3DWB_LP_ODR_DIV_100,
#if FIFO_MODE
        .fifo_mode = IIS3DWB_STREAM_MODE,
        .fifo_watermark = fifo_watermark,
        .fifo_xl_batch = IIS3DWB_XL_BATCHED_AT_26k7Hz,
        .fifo_temp_batch = IIS3DWB_TEMP_BATCHED_AT_104Hz,
        .fifo_timestamp_batch = IIS3DWB_DEC_32,
        .fifo_timestamp_en = PROPERTY_ENABLE,
#endif
    };

    ret = iis3dwb_hal_configure(&iis3dwb_ctx, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIS3DWB configuration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    current_full_scale = 1;  // Set to ±4g
    ESP_LOGI(TAG, "IIS3DWB initialized successfully at %.2f Hz (watermark=%u, fs=±4g)",
             configured_odr_hz, fifo_watermark);

    return ESP_OK;
}

esp_err_t imu_manager_read_all(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    data->timestamp_us = esp_timer_get_time();
    
    // Polling mode: read sensor data with samples array
    static iis3dwb_sample_t sample_buffer[IIS3DWB_HAL_MAX_SAMPLES];
    iis3dwb_hal_data_t hal_data = {
        .samples = sample_buffer,
        .sample_count = 0
    };
    
    esp_err_t ret = iis3dwb_hal_read_data(&iis3dwb_ctx, &hal_data);
    if (ret != ESP_OK) {
        data->accelerometer.valid = false;
        xSemaphoreGive(sensor_mutex);
        return ESP_FAIL;
    }
    
    if (hal_data.sample_count == 0) {
        data->accelerometer.valid = false;
        xSemaphoreGive(sensor_mutex);
        return ESP_OK;
    }
    
    // Convert sensitivity based on full scale
    float sensitivity_mg_lsb;
    switch (current_full_scale) {
        case 0: sensitivity_mg_lsb = 0.061f; break;  // ±2g
        case 1: sensitivity_mg_lsb = 0.122f; break;  // ±4g
        case 2: sensitivity_mg_lsb = 0.244f; break;  // ±8g
        case 3: sensitivity_mg_lsb = 0.488f; break;  // ±16g
        default: sensitivity_mg_lsb = 0.122f;
    }
    
    // Store all samples in recent buffer for WebSocket
    uint16_t samples_to_store = hal_data.sample_count;
    if (samples_to_store > MAX_RECENT_SAMPLES - recent_count) {
        samples_to_store = MAX_RECENT_SAMPLES - recent_count;
    }
    
    int16_t last_x = 0, last_y = 0, last_z = 0;
    for (uint16_t i = 0; i < samples_to_store; i++) {
        uint16_t idx = recent_count + i;
        recent_x[idx] = hal_data.samples[i].x_raw * sensitivity_mg_lsb / 1000.0f;
        recent_y[idx] = hal_data.samples[i].y_raw * sensitivity_mg_lsb / 1000.0f;
        recent_z[idx] = hal_data.samples[i].z_raw * sensitivity_mg_lsb / 1000.0f;
        
        // Keep last sample for display
        last_x = hal_data.samples[i].x_raw;
        last_y = hal_data.samples[i].y_raw;
        last_z = hal_data.samples[i].z_raw;
    }
    
    recent_count += samples_to_store;
    recent_timestamp = data->timestamp_us;
    recent_sequence++;
    recent_fifo_level = hal_data.sample_count;
    
    // Use last sample for current reading (or average from hal_data)
    data->accelerometer.x_g = hal_data.x_mg / 1000.0f;
    data->accelerometer.y_g = hal_data.y_mg / 1000.0f;
    data->accelerometer.z_g = hal_data.z_mg / 1000.0f;
    data->accelerometer.magnitude_g = sqrtf(
        data->accelerometer.x_g * data->accelerometer.x_g +
        data->accelerometer.y_g * data->accelerometer.y_g +
        data->accelerometer.z_g * data->accelerometer.z_g
    );
    data->accelerometer.valid = true;

    // Fill in stats
    data->stats.fifo_level = hal_data.sample_count;
    data->stats.samples_read = hal_data.sample_count;
    data->stats.odr_hz = configured_odr_hz;
    data->stats.batch_interval_us = (hal_data.sample_count * 1e6f) / configured_odr_hz;
    data->stats.samples_per_second = configured_odr_hz;

    xSemaphoreGive(sensor_mutex);
    return ESP_OK;
}

esp_err_t imu_manager_read_accelerometer(imu_data_t *data)
{
    // For simplified version, same as read_all
    return imu_manager_read_all(data);
}

esp_err_t imu_manager_deinit(void)
{
    if (sensor_mutex) {
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
    }

    iis3dwb_hal_deinit(&iis3dwb_ctx);
    ESP_LOGI(TAG, "IMU Manager deinitialized");
    return ESP_OK;
}

float imu_manager_get_configured_odr(void)
{
    return configured_odr_hz;
}

uint16_t imu_manager_get_fifo_watermark(void)
{
    return fifo_watermark;
}

esp_err_t imu_manager_set_full_scale(uint8_t fs_code)
{
    if (fs_code > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    iis3dwb_fs_xl_t fs;
    switch (fs_code) {
        case 0: fs = IIS3DWB_2g; break;
        case 1: fs = IIS3DWB_4g; break;
        case 2: fs = IIS3DWB_8g; break;
        case 3: fs = IIS3DWB_16g; break;
        default:
            xSemaphoreGive(sensor_mutex);
            return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = iis3dwb_xl_full_scale_set(&iis3dwb_ctx, fs);
    if (ret == ESP_OK) {
        current_full_scale = fs_code;
        ESP_LOGI(TAG, "Full scale changed to ±%dg",
                 (fs_code == 0) ? 2 : (fs_code == 1) ? 4 : (fs_code == 2) ? 8 : 16);
    }

    xSemaphoreGive(sensor_mutex);
    return ret;
}

uint8_t imu_manager_get_full_scale(void)
{
    return current_full_scale;
}

uint16_t imu_manager_copy_recent_samples(float *x_g, float *y_g, float *z_g,
                                         uint16_t max_samples, uint64_t *timestamp_us,
                                         uint16_t *fifo_level, uint32_t *sequence_id)
{
    if (!x_g || !y_g || !z_g) {
        return 0;
    }
    
    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }
    
    uint16_t count_to_copy = (recent_count > max_samples) ? max_samples : recent_count;
    
    if (count_to_copy > 0) {
        memcpy(x_g, recent_x, count_to_copy * sizeof(float));
        memcpy(y_g, recent_y, count_to_copy * sizeof(float));
        memcpy(z_g, recent_z, count_to_copy * sizeof(float));
        
        if (timestamp_us) *timestamp_us = recent_timestamp;
        if (fifo_level) *fifo_level = recent_fifo_level;
        if (sequence_id) *sequence_id = recent_sequence;
        
        // Reset count after copy to avoid sending duplicate samples
        recent_count = 0;
    }
    
    xSemaphoreGive(sensor_mutex);
    return count_to_copy;
}
