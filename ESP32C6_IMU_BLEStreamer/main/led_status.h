/**
 * @file led_status.h
 * @brief LED Status Indicator Module
 * 
 * Manages LED states based on BLE connection status:
 * - Disconnected: LED ON (solid)
 * - Connected: LED BLINKING (0.5s period)
 * - Streaming: LED ON during data transmission
 */

#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief LED status states
 */
typedef enum {
    LED_STATUS_DISCONNECTED,    ///< BLE not connected - LED solid ON
    LED_STATUS_CONNECTED,       ///< BLE connected - LED blinking 0.5s
    LED_STATUS_STREAMING        ///< Data streaming - LED ON during TX
} led_status_state_t;

/**
 * @brief LED configuration
 */
typedef struct {
    int gpio_num;               ///< GPIO pin number (e.g., 18)
    bool active_low;            ///< true if LED is active-low
    uint32_t blink_period_ms;   ///< Blink period for CONNECTED state (default 500ms)
} led_status_config_t;

/**
 * @brief Initialize LED status indicator
 * 
 * @param config LED configuration (NULL = use defaults: GPIO18, active-low, 500ms)
 * @return ESP_OK on success
 */
esp_err_t led_status_init(const led_status_config_t *config);

/**
 * @brief Set LED status state
 * 
 * @param state New LED state
 * @return ESP_OK on success
 */
esp_err_t led_status_set_state(led_status_state_t state);

/**
 * @brief Trigger streaming pulse (brief LED flash)
 * 
 * Called when data is transmitted. LED will turn ON briefly.
 * Only effective in STREAMING state.
 */
void led_status_streaming_pulse(void);

/**
 * @brief Deinitialize LED status module
 */
void led_status_deinit(void);

#endif // LED_STATUS_H
