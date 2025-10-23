#include "imu_manager.h"
#include "sensors/iis3dwb_wrapper.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

static const char *TAG = "IMU_MANAGER";

static iis3dwb_wrapper_t accel_sensor;

#define IIS3DWB_MAX_ODR_HZ        26670.0f
#define IIS3DWB_SPI_HOST          SPI2_HOST
#define IIS3DWB_SPI_MISO          2
#define IIS3DWB_SPI_MOSI          7
#define IIS3DWB_SPI_CLK           6
#define IIS3DWB_SPI_CS            19
#define IIS3DWB_FIFO_STREAM_MODE  0x06
#define IIS3DWB_MAX_SAMPLES_BATCH 512  // IIS3DWB FIFO max size

static SemaphoreHandle_t sensor_mutex = NULL;
static uint16_t fifo_watermark = 256;  // Increased for 10ms read interval (26.67kHz * 10ms = ~267 samples)
static float configured_odr_hz = IIS3DWB_MAX_ODR_HZ;
static uint64_t last_batch_timestamp_us = 0;
static uint8_t current_full_scale = 0; // 0=±2g, 1=±4g, 2=±8g, 3=±16g
static float current_sensitivity = 0.061f / 1000.0f; // mg/LSB for ±2g
_Static_assert(IMU_MANAGER_MAX_SAMPLES == IIS3DWB_MAX_SAMPLES_BATCH, "IMU manager sample configuration mismatch");
static float recent_ax[IIS3DWB_MAX_SAMPLES_BATCH];
static float recent_ay[IIS3DWB_MAX_SAMPLES_BATCH];
static float recent_az[IIS3DWB_MAX_SAMPLES_BATCH];
static uint16_t recent_samples = 0;
static uint16_t recent_fifo_level = 0;
static uint64_t recent_timestamp_us = 0;
static uint32_t recent_sequence = 0;

static esp_err_t iis3dwb_get_fifo_level(uint16_t *level, bool *overflowed)
{
    uint16_t fifo_level = 0;
    uint8_t overflow = 0;
    
    esp_err_t ret = iis3dwb_wrapper_get_fifo_status(&accel_sensor, &fifo_level, &overflow);
    if (ret != ESP_OK) {
        return ret;
    }

    if (level) {
        *level = fifo_level;
    }
    if (overflowed) {
        *overflowed = (overflow != 0);
    }
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

esp_err_t imu_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing IMU Manager...");

    sensor_mutex = xSemaphoreCreateMutex();
    if (sensor_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor mutex");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    spi_bus_config_t buscfg = {
        .miso_io_num = IIS3DWB_SPI_MISO,
        .mosi_io_num = IIS3DWB_SPI_MOSI,
        .sclk_io_num = IIS3DWB_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    ret = spi_bus_initialize(IIS3DWB_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully (MISO=%d, MOSI=%d, CLK=%d)",
             IIS3DWB_SPI_MISO, IIS3DWB_SPI_MOSI, IIS3DWB_SPI_CLK);

    // Initialize wrapper
    ret = iis3dwb_wrapper_init(&accel_sensor, IIS3DWB_SPI_HOST, IIS3DWB_SPI_CS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIS3DWB wrapper init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure sensor with ±2g and watermark
    ret = iis3dwb_wrapper_configure(&accel_sensor, IIS3DWB_WRAPPER_FS_2G, fifo_watermark);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IIS3DWB configuration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Update current sensitivity from wrapper
    current_sensitivity = iis3dwb_wrapper_get_sensitivity(&accel_sensor) / 1000.0f; // Convert mg/LSB to g/LSB

    recent_samples = 0;
    recent_fifo_level = 0;
    recent_timestamp_us = 0;
    recent_sequence = 0;

    ESP_LOGI(TAG, "IIS3DWB initialized at %.2f Hz ODR (watermark=%u)", configured_odr_hz, fifo_watermark);
    last_batch_timestamp_us = esp_timer_get_time();
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
    imu_manager_read_accelerometer(data);

    xSemaphoreGive(sensor_mutex);
    return ESP_OK;
}

esp_err_t imu_manager_read_accelerometer(imu_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t fifo_level = 0;
    bool overflow = false;
    esp_err_t ret = iis3dwb_get_fifo_level(&fifo_level, &overflow);
    if (ret != ESP_OK) {
        data->accelerometer.valid = false;
        return ret;
    }

    if (overflow) {
        ESP_LOGW(TAG, "IIS3DWB FIFO overflow detected (level=%u) - resetting FIFO", fifo_level);
        iis3dwb_wrapper_reset_fifo(&accel_sensor);
        data->accelerometer.valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t samples_to_read = fifo_level;
    if (samples_to_read == 0) {
        // No samples waiting
        data->accelerometer.valid = false;
        return ESP_ERR_NOT_FOUND;
    }

    if (samples_to_read > IIS3DWB_MAX_SAMPLES_BATCH) {
        ESP_LOGW(TAG, "FIFO level (%u) exceeds max batch (%u), limiting read", 
                 samples_to_read, IIS3DWB_MAX_SAMPLES_BATCH);
        samples_to_read = IIS3DWB_MAX_SAMPLES_BATCH;
    }

    // Read raw data from FIFO using wrapper
    int16_t raw_data[samples_to_read * 3]; // X, Y, Z for each sample
    uint16_t num_read = 0;
    
    ret = iis3dwb_wrapper_read_fifo(&accel_sensor, raw_data, samples_to_read, &num_read);
    if (ret != ESP_OK || num_read == 0) {
        data->accelerometer.valid = false;
        return ret;
    }

    // Convert raw data to g using current sensitivity
    float ax_buf[num_read];
    float ay_buf[num_read];
    float az_buf[num_read];
    
    for (uint16_t i = 0; i < num_read; i++) {
        ax_buf[i] = raw_data[i * 3 + 0] * current_sensitivity;
        ay_buf[i] = raw_data[i * 3 + 1] * current_sensitivity;
        az_buf[i] = raw_data[i * 3 + 2] * current_sensitivity;
    }

    const uint16_t last_index = num_read - 1;
    const float ax = ax_buf[last_index];
    const float ay = ay_buf[last_index];
    const float az = az_buf[last_index];

    data->accelerometer.x_g = ax;
    data->accelerometer.y_g = ay;
    data->accelerometer.z_g = az;
    data->accelerometer.magnitude_g = sqrtf(ax * ax + ay * ay + az * az);
    data->accelerometer.valid = true;

    const uint64_t now_us = data->timestamp_us;
    float samples_per_second = configured_odr_hz;
    if (last_batch_timestamp_us != 0 && now_us > last_batch_timestamp_us) {
        const float elapsed_us = (float)(now_us - last_batch_timestamp_us);
        samples_per_second = (num_read * 1e6f) / elapsed_us;
    }
    last_batch_timestamp_us = now_us;

    data->stats.fifo_level = fifo_level;
    data->stats.samples_read = num_read;
    data->stats.odr_hz = configured_odr_hz;
    data->stats.batch_interval_us = (num_read * 1e6f) / configured_odr_hz;
    data->stats.samples_per_second = samples_per_second;

    if (samples_per_second > configured_odr_hz * 1.1f || samples_per_second < configured_odr_hz * 0.1f) {
        ESP_LOGW(TAG, "Unexpected sample throughput: %.1f sps (expected %.1f)", samples_per_second, configured_odr_hz);
    }

    memcpy(recent_ax, ax_buf, num_read * sizeof(float));
    memcpy(recent_ay, ay_buf, num_read * sizeof(float));
    memcpy(recent_az, az_buf, num_read * sizeof(float));
    recent_samples = num_read;
    recent_fifo_level = fifo_level;
    recent_timestamp_us = data->timestamp_us;
    recent_sequence++;

    return ESP_OK;
}

esp_err_t imu_manager_deinit(void)
{
    if (sensor_mutex) {
        vSemaphoreDelete(sensor_mutex);
        sensor_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "IMU Manager deinitialized");
    return ESP_OK;
}

uint16_t imu_manager_copy_recent_samples(float *x_g, float *y_g, float *z_g,
                                         uint16_t max_samples, uint64_t *timestamp_us,
                                         uint16_t *fifo_level, uint32_t *sequence_id)
{
    if (x_g == NULL || y_g == NULL || z_g == NULL || max_samples == 0) {
        return 0;
    }

    if (sensor_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return 0;
    }

    uint16_t count = recent_samples;
    if (count > max_samples) {
        count = max_samples;
    }

    if (count > 0) {
        memcpy(x_g, recent_ax, count * sizeof(float));
        memcpy(y_g, recent_ay, count * sizeof(float));
        memcpy(z_g, recent_az, count * sizeof(float));
    }

    if (timestamp_us) {
        *timestamp_us = recent_timestamp_us;
    }
    if (fifo_level) {
        *fifo_level = recent_fifo_level;
    }
    if (sequence_id) {
        *sequence_id = recent_sequence;
    }

    xSemaphoreGive(sensor_mutex);
    return count;
}

esp_err_t imu_manager_set_full_scale(uint8_t fs_code)
{
    if (fs_code > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    iis3dwb_wrapper_fs_t wrapper_fs;
    
    switch (fs_code) {
        case 0:
            wrapper_fs = IIS3DWB_WRAPPER_FS_2G;
            break;
        case 1:
            wrapper_fs = IIS3DWB_WRAPPER_FS_4G;
            break;
        case 2:
            wrapper_fs = IIS3DWB_WRAPPER_FS_8G;
            break;
        case 3:
            wrapper_fs = IIS3DWB_WRAPPER_FS_16G;
            break;
        default:
            xSemaphoreGive(sensor_mutex);
            return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = iis3dwb_wrapper_set_full_scale(&accel_sensor, wrapper_fs);
    if (ret == ESP_OK) {
        current_full_scale = fs_code;
        current_sensitivity = iis3dwb_wrapper_get_sensitivity(&accel_sensor) / 1000.0f;
        ESP_LOGI(TAG, "Full scale changed to ±%dg (sensitivity=%.3f mg/LSB)",
                 (fs_code == 0) ? 2 : (fs_code == 1) ? 4 : (fs_code == 2) ? 8 : 16,
                 current_sensitivity * 1000.0f);
    }
    
    xSemaphoreGive(sensor_mutex);
    return ret;
}

uint8_t imu_manager_get_full_scale(void)
{
    return current_full_scale;
}
