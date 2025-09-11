// main.c — Polling FIFO demo tương thích inv_imu_fifo_data_t union bạn cung cấp
// ESP-IDF v5.4
//
// Lưu ý:
// - Mặc định code giả định FIFO trả về 1 frame / lần gọi icm456xx_get_data_from_fifo()
// - Mặc định xử lý theo định dạng 16-bit (byte_16). Thay FIFO_FORMAT_BITS nếu bạn dùng 8 hoặc 20.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include "icm45686.h"    // wrapper bạn đã có (icm456xx_*)

// ---------- TÙY CHỈNH PHẦN CẤU HÌNH ----------
#define SPI_HOST_USED      SPI2_HOST
#define PIN_NUM_MISO       19
#define PIN_NUM_MOSI       23
#define PIN_NUM_CLK        18
#define PIN_NUM_CS         5

#define ACCEL_ODR_HZ       100   // ODR bạn set cho accel
#define GYRO_ODR_HZ        100   // ODR bạn set cho gyro
#define FIFO_WATERMARK     32    // watermark
#define SPI_CLOCK_HZ       6000000

/* Chọn định dạng FIFO hiện tại: 8, 16 hoặc 20 (bit per sample layout) */
#define FIFO_FORMAT_BITS   16
// ------------------------------------------------

static const char *TAG = "main_poll_struct";
static icm456xx_dev_t imu_dev;

/* Xử lý 1 frame FIFO dựa trên union bạn cung cấp.
   In ra raw sample (không chuyển đổi sang g / dps).
*/
static void process_fifo_frame(const inv_imu_fifo_data_t *f)
{
    if (!f) return;

#if FIFO_FORMAT_BITS == 8
    // byte_8: sensor_data[3] (int16_t), temp_data (int8_t)
    int16_t *s = (int16_t*)f->byte_8.sensor_data;
    int8_t  temp = f->byte_8.temp_data;
    ESP_LOGI(TAG, "[8-bit format] sensor_data=%d,%d,%d temp=%d",
             s[0], s[1], s[2], (int)temp);

#elif FIFO_FORMAT_BITS == 16
    // byte_16: accel_data[3] (int16_t), gyro_data[3] (int16_t),
    //         temp_data (int8_t), timestamp (uint16_t)
    int16_t ax = f->byte_16.accel_data[0];
    int16_t ay = f->byte_16.accel_data[1];
    int16_t az = f->byte_16.accel_data[2];
    int16_t gx = f->byte_16.gyro_data[0];
    int16_t gy = f->byte_16.gyro_data[1];
    int16_t gz = f->byte_16.gyro_data[2];
    int8_t temp   = f->byte_16.temp_data;
    uint16_t ts   = f->byte_16.timestamp;

    ESP_LOGI(TAG, "[16-bit] A=[%d,%d,%d] G=[%d,%d,%d] T=%d TS=%u",
             (int)ax, (int)ay, (int)az, (int)gx, (int)gy, (int)gz, (int)temp, (unsigned)ts);

#elif FIFO_FORMAT_BITS == 20
    // byte_20: accel_data[3] (int32_t), gyro_data[3] (int32_t),
    //          temp_data (int16_t), timestamp (uint16_t)
    int32_t ax = f->byte_20.accel_data[0];
    int32_t ay = f->byte_20.accel_data[1];
    int32_t az = f->byte_20.accel_data[2];
    int32_t gx = f->byte_20.gyro_data[0];
    int32_t gy = f->byte_20.gyro_data[1];
    int32_t gz = f->byte_20.gyro_data[2];
    int16_t temp = f->byte_20.temp_data;
    uint16_t ts  = f->byte_20.timestamp;

    ESP_LOGI(TAG, "[20-bit] A=[%ld,%ld,%ld] G=[%ld,%ld,%ld] T=%d TS=%u",
             (long)ax, (long)ay, (long)az, (long)gx, (long)gy, (long)gz, (int)temp, (unsigned)ts);
#else
# error "Please set FIFO_FORMAT_BITS to 8, 16 or 20"
#endif
}

/* Task polling FIFO: gọi icm456xx_get_data_from_fifo() lặp cho đến khi trả về lỗi (không còn frame) */
static void imu_poll_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "FIFO polling task started");

    const float safety_factor = 0.7f;
    uint32_t odr = (ACCEL_ODR_HZ > 0) ? ACCEL_ODR_HZ : 100;
    uint32_t poll_ms = (uint32_t)((FIFO_WATERMARK / (float)odr) * 1000.0f * safety_factor);
    if (poll_ms < 10) poll_ms = 10;
    ESP_LOGI(TAG, "Polling every %u ms (ODR=%u Hz, WM=%u)", (unsigned)poll_ms, (unsigned)odr, (unsigned)FIFO_WATERMARK);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(poll_ms));

        // đọc liên tục cho đến khi fifo rỗng
        while (1) {
            inv_imu_fifo_data_t fifo;
            memset(&fifo, 0, sizeof(fifo));
            int rc = icm456xx_get_data_from_fifo(&imu_dev, &fifo);
            if (rc != 0) {
                // rc != 0: không còn frame / lỗi — thoát vòng read
                break;
            }
            // xử lý frame thu được
            process_fifo_frame(&fifo);
        }
    }
}

/* Khởi tạo SPI bus + driver + bật accel/gyro + set FIFO watermark (không dùng INT) */
static esp_err_t imu_init_board(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    ret = spi_bus_initialize(SPI_HOST_USED, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    icm456xx_init_spi(&imu_dev, SPI_HOST_USED, PIN_NUM_CS, SPI_CLOCK_HZ);
    ret = icm456xx_begin(&imu_dev);
    if (ret != 0) {
        ESP_LOGE(TAG, "icm456xx_begin failed: %d", ret);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ICM45686 initialized");

    // bật accelerometer/gyro
    icm456xx_start_accel(&imu_dev, ACCEL_ODR_HZ, 16);
    icm456xx_start_gyro(&imu_dev, GYRO_ODR_HZ, 2000);

    // cấu hình FIFO watermark trong driver. Gọi hàm wrapper (không dùng INT).
    // Một vài wrapper có thể yêu cầu int_gpio >=0; nếu hàm không cho phép -1,
    // bạn có thể thay bằng gọi API inv_imu_set_fifo_config trực tiếp.
    int rc = icm456xx_enable_fifo_interrupt(&imu_dev, -1, NULL, FIFO_WATERMARK);
    if (rc != 0) {
        ESP_LOGW(TAG, "icm456xx_enable_fifo_interrupt returned %d — nếu wrapper không hỗ trợ -1, ignore or use inv_imu_set_fifo_config()", rc);
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "ICM45686 polling example (struct-aware) starting");

    if (imu_init_board() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init IMU");
        return;
    }

    xTaskCreate(imu_poll_task, "imu_poll", 4096, NULL, 5, NULL);

    // vòng chính chỉ để log trạng thái, bạn có thể bỏ hoặc thêm kiểm tra khác
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        inv_imu_sensor_data_t sample;
        memset(&sample, 0, sizeof(sample));
        if (icm456xx_get_data_from_registers(&imu_dev, &sample) == 0) {
            ESP_LOGI(TAG, "Register snapshot (raw): accel_raw=[%ld,%ld,%ld] gyro_raw=[%ld,%ld,%ld]",
                     (long)sample.accel_data[0], (long)sample.accel_data[1], (long)sample.accel_data[2],
                     (long)sample.gyro_data[0], (long)sample.gyro_data[1], (long)sample.gyro_data[2]);
        }
    }
}
