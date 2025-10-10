/* main.c — Ví dụ sử dụng ICM45686 qua SPI (ESP-IDF v5.4)
 *
 * Chức năng:
 *  - Khởi tạo SPI bus
 *  - Khởi tạo driver icm45686 (icm45686_begin)
 *  - Bật accelerometer + gyro
 *  - Cấu hình interrupt FIFO (watermark)
 *  - Task chờ semaphore được kích từ ISR và đọc FIFO
 *
 * Chỉnh các define dưới đây để phù hợp board của bạn.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#include "icm45686.h" /* file header bạn đã thêm vào component */

static const char *TAG = "app_main";

/* --- THAY ĐỔI TẠI ĐÂY nếu cần --- */
#define SPI_HOST_USED      SPI2_HOST
#define PIN_NUM_MISO       2
#define PIN_NUM_MOSI       7
#define PIN_NUM_CLK        6
#define PIN_NUM_CS         20    /* CS của ICM45686 */
#define PIN_NUM_INT        4    /* IRQ/INT của ICM45686 */
#define SPI_CLOCK_HZ       6000000
#define FIFO_WATERMARK     32
/* --------------------------------- */

static icm456xx_dev_t imu_dev;
static SemaphoreHandle_t fifo_sem = NULL;

/* ISR được đăng ký cho GPIO INT; gọi FromISR để un-block task */
static void IRAM_ATTR imu_int_isr(void *arg)
{
    BaseType_t hpw = pdFALSE;
    (void)arg;
    if (fifo_sem) {
        xSemaphoreGiveFromISR(fifo_sem, &hpw);
        if (hpw == pdTRUE) portYIELD_FROM_ISR();
    }
}

/* Task đọc FIFO khi có interrupt.
   Hàm icm456xx_get_data_from_fifo trả về cấu trúc inv_imu_fifo_data_t theo driver inv_imu
*/
static void imu_task(void *pv)
{
    ESP_LOGI(TAG, "IMU task started");
    for (;;) {
        /* chờ semaphore (được give từ ISR) */
        if (xSemaphoreTake(fifo_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
            ESP_LOGI(TAG, "FIFO interrupt received, reading frames...");

            /* Lặp đọc FIFO cho đến khi rỗng hoặc lỗi */
            while (1) {
                inv_imu_fifo_data_t fifo_data;
                memset(&fifo_data, 0, sizeof(fifo_data));
                int rc = icm456xx_get_data_from_fifo(&imu_dev, &fifo_data);
                if (rc != 0) {
                    /* nếu không có frame hoặc lỗi, thoát vòng */
                    ESP_LOGW(TAG, "icm456xx_get_data_from_fifo rc=%d (no more frames?)", rc);
                    break;
                }

                /* In thông tin cơ bản — cấu trúc tùy thuộc driver inv_imu */
                ESP_LOGI(TAG, "FIFO frame: sample_count=%u, ts=%u",
                         (unsigned)fifo_data.sample_count, (unsigned)fifo_data.tmst);

                /* Nếu bạn muốn in từng sample accel/gyro (nếu có) */
                for (uint32_t i = 0; i < fifo_data.sample_count; i++) {
                    /* Một số dự đoán trường tên — chỉnh theo inv_imu_fifo_data_t thực tế */
                    int32_t ax = fifo_data.frame[i].accel.x; /* ví dụ trường */
                    int32_t ay = fifo_data.frame[i].accel.y;
                    int32_t az = fifo_data.frame[i].accel.z;
                    int32_t gx = fifo_data.frame[i].gyro.x;
                    int32_t gy = fifo_data.frame[i].gyro.y;
                    int32_t gz = fifo_data.frame[i].gyro.z;
                    ESP_LOGI(TAG, "  sample %u: A=[%ld,%ld,%ld] G=[%ld,%ld,%ld]",
                             (unsigned)i, (long)ax, (long)ay, (long)az, (long)gx, (long)gy, (long)gz);
                }
            }

        } else {
            /* timeout: bạn vẫn có thể poll sensor / heartbeat */
            ESP_LOGI(TAG, "No FIFO event in 5s, still alive");
        }
    }
}

/* Hàm init SPI bus và device, khởi tạo IMU */
static esp_err_t imu_init_board(void)
{
    esp_err_t ret;

    /* 1) Init SPI bus */
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

    /* 2) Prepare icm45686_dev_t và add device */
    icm456xx_init_spi(&imu_dev, SPI_HOST_USED, PIN_NUM_CS, SPI_CLOCK_HZ);

    ret = icm456xx_begin(&imu_dev);
    if (ret != 0) {
        ESP_LOGE(TAG, "icm456xx_begin failed: %d", ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "icm45686 driver initialized");

    /* 3) Start accel + gyro (ví dụ: 100 Hz, ±16g; gyro 100 Hz ±2000 dps) */
    icm456xx_start_accel(&imu_dev, 100, 16);
    icm456xx_start_gyro(&imu_dev, 100, 2000);

    /* 4) Tạo semaphore để sync từ ISR -> task */
    fifo_sem = xSemaphoreCreateBinary();
    if (!fifo_sem) {
        ESP_LOGE(TAG, "Cannot create semaphore");
        return ESP_FAIL;
    }

    /* 5) Cấu hình GPIO INT (input + pullup + isr) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_INT),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&io_conf);

    /* install isr service (once) */
    gpio_install_isr_service(0);

    /* đăng ký handler ISR - ở code thư viện mình gọi gpio_isr_handler_add(user_isr) trực tiếp
       nên ở đây chúng ta đăng ký callback của ứng dụng (imu_int_isr) */
    gpio_isr_handler_add(PIN_NUM_INT, imu_int_isr, NULL);

    /* 6) Kích hoạt FIFO interrupt trong driver (watermark). 
       Thư viện icm45686 có hàm icm456xx_enable_fifo_interrupt để cấu hình driver + GPIO.
       Ở ví dụ này chúng ta đã setup GPIO/ISR thủ công; nhưng vẫn gọi để cấu hình interrupt trong device.
    */
    int rc = icm456xx_enable_fifo_interrupt(&imu_dev, PIN_NUM_INT, imu_int_isr, FIFO_WATERMARK);
    if (rc != 0) {
        ESP_LOGW(TAG, "icm456xx_enable_fifo_interrupt returned %d (may be OK if already configured)", rc);
    }

    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ICM45686 example app");

    esp_err_t r = imu_init_board();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "imu_init_board failed");
        return;
    }

    /* Tạo task đọc FIFO */
    xTaskCreatePinnedToCore(imu_task, "imu_task", 4096, NULL, 5, NULL, tskNO_AFFINITY);

    /* Nếu muốn: đọc register mẫu định kỳ (debug) */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        inv_imu_sensor_data_t regdata;
        memset(&regdata, 0, sizeof(regdata));
        int rc = icm456xx_get_data_from_registers(&imu_dev, &regdata);
        if (rc == 0) {
            ESP_LOGI(TAG, "REG sample: accel_raw=[%ld,%ld,%ld] gyro_raw=[%ld,%ld,%ld]",
                     (long)regdata.accel_raw[0], (long)regdata.accel_raw[1], (long)regdata.accel_raw[2],
                     (long)regdata.gyro_raw[0], (long)regdata.gyro_raw[1], (long)regdata.gyro_raw[2]);
        } else {
            ESP_LOGW(TAG, "icm456xx_get_data_from_registers rc=%d", rc);
        }
    }
}
