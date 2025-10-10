#include "imu_ble.h"
#include "ble_stream.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// NOTE: In this standalone example we simulate sensor reads.
// In integration with your drivers, replace sim_read_* with actual reads.

static const char *TAG = "IMU_BLE";
static imu_ble_config_t s_cfg;

static inline int16_t f2q15(float v, float scale)
{
    float q = v * scale;
    if (q > 32767.0f) q = 32767.0f;
    if (q < -32768.0f) q = -32768.0f;
    return (int16_t)q;
}

static void pack_and_notify(float ax, float ay, float az,
                            float gx, float gy, float gz)
{
    // Packet format (little-endian): t32us + 6x int16 (acc xyz, gyr xyz) = 4 + 12 = 16 bytes
    // We can batch multiple frames into a 244B notify payload.
    static uint8_t buf[244];
    static uint16_t off = 0;

    uint32_t t = (uint32_t)(esp_timer_get_time());
    if (off + 16 > sizeof(buf)) {
        ble_stream_notify(buf, off);
        off = 0;
    }
    memcpy(&buf[off], &t, 4); off += 4;
    int16_t iax = f2q15(ax, 16384.0f); // ~1g -> 16384 for ±2g
    int16_t iay = f2q15(ay, 16384.0f);
    int16_t iaz = f2q15(az, 16384.0f);
    int16_t igx = f2q15(gx, 131.072f);  // ~1 dps -> 131 for ±250 dps (example)
    int16_t igy = f2q15(gy, 131.072f);
    int16_t igz = f2q15(gz, 131.072f);
    memcpy(&buf[off], &iax, 2); off += 2;
    memcpy(&buf[off], &iay, 2); off += 2;
    memcpy(&buf[off], &iaz, 2); off += 2;
    memcpy(&buf[off], &igx, 2); off += 2;
    memcpy(&buf[off], &igy, 2); off += 2;
    memcpy(&buf[off], &igz, 2); off += 2;

    // If nearly full, flush now
    if (sizeof(buf) - off < 16) {
        ble_stream_notify(buf, off);
        off = 0;
    }
}

static void producer_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(s_cfg.packet_interval_ms);
    TickType_t last = xTaskGetTickCount();
    ESP_LOGI(TAG, "Producer started: interval=%ums", (unsigned)s_cfg.packet_interval_ms);

    // Simulate: replace with actual sensor reads at configured ODRs
    float t = 0.0f;
    while (1) {
        // Example waveform
        float ax = 0.0f + 0.1f * sinf(t);
        float ay = 0.0f + 0.1f * cosf(t);
        float az = 1.0f; // gravity
        float gx = 0.5f * sinf(t*0.5f);
        float gy = 0.3f * cosf(t*0.4f);
        float gz = 0.0f;
        t += 0.1f;

        pack_and_notify(ax, ay, az, gx, gy, gz);
        vTaskDelayUntil(&last, period);
    }
}

esp_err_t imu_ble_init(const imu_ble_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;

    // In real integration: initialize actual sensors with BLE-friendly ODRs here
    // Example: set IIS3DWB ODR to s_cfg.iis3dwb_odr_hz, etc.

    xTaskCreatePinnedToCore(producer_task, "imu_ble_producer", 4096, NULL, 5, NULL, 0);
    return ESP_OK;
}
