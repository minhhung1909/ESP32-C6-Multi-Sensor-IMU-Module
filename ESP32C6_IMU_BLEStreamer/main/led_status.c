/**
 * @file led_status.c
 * @brief LED Status Indicator Implementation
 */

#include "led_status.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include <inttypes.h>  // For PRIu32

static const char *TAG = "LED_STATUS";

// Default configuration
#define LED_DEFAULT_GPIO        18
#define LED_DEFAULT_ACTIVE_LOW  true
#define LED_DEFAULT_BLINK_MS    500
#define LED_TX_PULSE_MS         20  // TX pulse duration (like UART TX LED)

// Module state
typedef struct {
    led_status_config_t config;
    led_status_state_t state;
    TaskHandle_t blink_task;
    TimerHandle_t tx_pulse_timer;
    volatile bool streaming_active;
    bool initialized;
} led_status_context_t;

static led_status_context_t s_ctx = {0};

/**
 * @brief Set physical LED level
 * @param on true = LED ON, false = LED OFF
 */
static inline void led_set_level(bool on)
{
    uint32_t level = s_ctx.config.active_low ? !on : on;
    gpio_set_level(s_ctx.config.gpio_num, level);
}

/**
 * @brief Timer callback to turn off TX pulse LED
 */
static void tx_pulse_timer_callback(TimerHandle_t timer)
{
    (void)timer;
    
    // Turn off LED after TX pulse
    if (s_ctx.state == LED_STATUS_STREAMING) {
        led_set_level(false);
        s_ctx.streaming_active = false;
    }
}

/**
 * @brief LED blink task (for CONNECTED state)
 */
static void led_blink_task(void *arg)
{
    const TickType_t half_period = pdMS_TO_TICKS(s_ctx.config.blink_period_ms / 2);
    
    ESP_LOGI(TAG, "Blink task started (period=%" PRIu32 "ms)", s_ctx.config.blink_period_ms);
    
    while (1) {
        // Only blink if in CONNECTED state
        if (s_ctx.state == LED_STATUS_CONNECTED) {
            led_set_level(true);
            vTaskDelay(half_period);
            
            led_set_level(false);
            vTaskDelay(half_period);
        } else {
            // Not blinking - just sleep
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

/**
 * @brief Update LED based on current state
 */
static void led_update_state(void)
{
    switch (s_ctx.state) {
        case LED_STATUS_DISCONNECTED:
            // Solid ON
            led_set_level(true);
            ESP_LOGD(TAG, "State: DISCONNECTED (LED ON)");
            break;
        
        case LED_STATUS_CONNECTED:
            // Blinking handled by task
            ESP_LOGD(TAG, "State: CONNECTED (LED BLINKING)");
            break;
        
        case LED_STATUS_STREAMING:
            // LED controlled by streaming_pulse()
            ESP_LOGD(TAG, "State: STREAMING (LED pulse on TX)");
            s_ctx.streaming_active = false;
            led_set_level(false);  // Default OFF, pulse when TX
            break;
        
        default:
            ESP_LOGW(TAG, "Unknown state: %d", s_ctx.state);
            break;
    }
}

// Public API implementation

esp_err_t led_status_init(const led_status_config_t *config)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    // Use provided config or defaults
    if (config) {
        s_ctx.config = *config;
    } else {
        s_ctx.config.gpio_num = LED_DEFAULT_GPIO;
        s_ctx.config.active_low = LED_DEFAULT_ACTIVE_LOW;
        s_ctx.config.blink_period_ms = LED_DEFAULT_BLINK_MS;
    }
    
    // Validate blink period
    if (s_ctx.config.blink_period_ms < 100) {
        ESP_LOGW(TAG, "Blink period too short (%" PRIu32 "ms), using 100ms", 
                 s_ctx.config.blink_period_ms);
        s_ctx.config.blink_period_ms = 100;
    }
    
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_ctx.config.gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", 
                 s_ctx.config.gpio_num, esp_err_to_name(ret));
        return ret;
    }
    
    // Initial state: DISCONNECTED
    s_ctx.state = LED_STATUS_DISCONNECTED;
    s_ctx.streaming_active = false;
    led_set_level(true);  // Start with LED ON
    
    // Create TX pulse timer (one-shot)
    s_ctx.tx_pulse_timer = xTimerCreate(
        "led_tx_pulse",
        pdMS_TO_TICKS(LED_TX_PULSE_MS),
        pdFALSE,  // One-shot timer
        NULL,
        tx_pulse_timer_callback
    );
    
    if (s_ctx.tx_pulse_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create TX pulse timer");
        return ESP_ERR_NO_MEM;
    }
    
    // Create blink task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        led_blink_task,
        "led_blink",
        2048,
        NULL,
        3,  // Lower priority than BLE/IMU
        &s_ctx.blink_task,
        0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create blink task");
        return ESP_ERR_NO_MEM;
    }
    
    s_ctx.initialized = true;
    
    ESP_LOGI(TAG, "Initialized: GPIO%d, active-%s, blink=%" PRIu32 "ms",
             s_ctx.config.gpio_num,
             s_ctx.config.active_low ? "low" : "high",
             s_ctx.config.blink_period_ms);
    
    return ESP_OK;
}

esp_err_t led_status_set_state(led_status_state_t state)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (state == s_ctx.state) {
        return ESP_OK;  // No change
    }
    
    led_status_state_t old_state = s_ctx.state;
    s_ctx.state = state;
    
    led_update_state();
    
    ESP_LOGI(TAG, "State changed: %d -> %d", old_state, state);
    
    return ESP_OK;
}

void led_status_streaming_pulse(void)
{
    if (!s_ctx.initialized) {
        return;
    }
    
    // Only pulse in STREAMING state
    if (s_ctx.state != LED_STATUS_STREAMING) {
        return;
    }
    
    // Turn ON LED immediately (TX indicator)
    led_set_level(true);
    s_ctx.streaming_active = true;
    
    // Schedule automatic turn-off after LED_TX_PULSE_MS
    // Reset timer if already running (rapid successive transmissions)
    if (xTimerIsTimerActive(s_ctx.tx_pulse_timer)) {
        xTimerReset(s_ctx.tx_pulse_timer, 0);
    } else {
        xTimerStart(s_ctx.tx_pulse_timer, 0);
    }
}

void led_status_deinit(void)
{
    if (!s_ctx.initialized) {
        return;
    }
    
    // Stop and delete TX pulse timer
    if (s_ctx.tx_pulse_timer) {
        xTimerStop(s_ctx.tx_pulse_timer, 0);
        xTimerDelete(s_ctx.tx_pulse_timer, 0);
        s_ctx.tx_pulse_timer = NULL;
    }
    
    // Delete blink task
    if (s_ctx.blink_task) {
        vTaskDelete(s_ctx.blink_task);
        s_ctx.blink_task = NULL;
    }
    
    // Turn off LED
    led_set_level(false);
    
    // Reset GPIO to input
    gpio_reset_pin(s_ctx.config.gpio_num);
    
    s_ctx.initialized = false;
    
    ESP_LOGI(TAG, "Deinitialized");
}
