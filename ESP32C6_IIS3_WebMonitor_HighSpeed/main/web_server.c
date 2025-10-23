#include "web_server.h"
#include "data_buffer.h"
#include "imu_manager.h"
#include "led_status.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static httpd_handle_t ws_server = NULL;

#define WS_RECENT_MAX_SAMPLES      IMU_MANAGER_MAX_SAMPLES

// WebSocket connection tracking
typedef struct {
    int fd;
    bool active;
} ws_connection_t;

static ws_connection_t ws_connections[WEBSOCKET_MAX_CONNECTIONS];
static SemaphoreHandle_t ws_mutex = NULL;
static volatile float ws_msg_rate = 0.0f;
static volatile float ws_samples_rate = 0.0f;
static volatile uint32_t ws_total_messages = 0;
static volatile bool ws_streaming_paused = false; // Pause/Resume control

// Forward declarations
static esp_err_t api_data_handler(httpd_req_t *req);
static esp_err_t api_stats_handler(httpd_req_t *req);
static esp_err_t api_config_handler(httpd_req_t *req);
static esp_err_t api_download_handler(httpd_req_t *req);
static esp_err_t ws_data_handler(httpd_req_t *req);
static esp_err_t ws_control_handler(httpd_req_t *req);
static esp_err_t file_handler(httpd_req_t *req);
static void ws_register_connection(int fd);
static void ws_unregister_connection(int fd);
static esp_err_t ws_send_to_all(const char *data, size_t len);
static void ws_broadcast_task(void *arg);
static esp_err_t root_handler(httpd_req_t *req);

// API Data endpoint - returns latest sensor data
static esp_err_t api_data_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Data request");
    
    imu_data_t data;
    esp_err_t ret = data_buffer_get_latest(&data);
    
    if (ret != ESP_OK || !data.accelerometer.valid) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "No accelerometer data available", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "timestamp_us", data.timestamp_us);
    cJSON *accel_g = cJSON_CreateObject();
    cJSON_AddNumberToObject(accel_g, "x_g", data.accelerometer.x_g);
    cJSON_AddNumberToObject(accel_g, "y_g", data.accelerometer.y_g);
    cJSON_AddNumberToObject(accel_g, "z_g", data.accelerometer.z_g);
    cJSON_AddNumberToObject(accel_g, "magnitude_g", data.accelerometer.magnitude_g);
    cJSON_AddItemToObject(json, "accelerometer_g", accel_g);

    const float g_to_ms2 = 9.80665f;
    cJSON *accel_ms2 = cJSON_CreateObject();
    cJSON_AddNumberToObject(accel_ms2, "x_ms2", data.accelerometer.x_g * g_to_ms2);
    cJSON_AddNumberToObject(accel_ms2, "y_ms2", data.accelerometer.y_g * g_to_ms2);
    cJSON_AddNumberToObject(accel_ms2, "z_ms2", data.accelerometer.z_g * g_to_ms2);
    cJSON_AddNumberToObject(accel_ms2, "magnitude_ms2", data.accelerometer.magnitude_g * g_to_ms2);
    cJSON_AddItemToObject(json, "accelerometer_ms2", accel_ms2);

    cJSON *stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "samples_read", data.stats.samples_read);
    cJSON_AddNumberToObject(stats, "batch_interval_us", data.stats.batch_interval_us);
    cJSON_AddNumberToObject(stats, "samples_per_second", data.stats.samples_per_second);
    cJSON_AddNumberToObject(stats, "plot_samples_per_second", ws_samples_rate);
    cJSON_AddNumberToObject(stats, "msg_per_second", ws_msg_rate);
    cJSON_AddNumberToObject(stats, "websocket_total_messages", ws_total_messages);
    cJSON_AddItemToObject(json, "stats", stats);
    
    char *json_string = cJSON_Print(json);
    if (json_string != NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "JSON generation failed", HTTPD_RESP_USE_STRLEN);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// API Stats endpoint - returns buffer statistics
static esp_err_t api_stats_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Stats request");
    
    buffer_stats_t stats;
    esp_err_t ret = data_buffer_get_stats(&stats);
    
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to get stats", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "total_samples", stats.total_samples);
    cJSON_AddNumberToObject(json, "dropped_samples", stats.dropped_samples);
    cJSON_AddNumberToObject(json, "buffer_overflows", stats.buffer_overflows);
    cJSON_AddNumberToObject(json, "last_timestamp_us", stats.last_timestamp_us);
    cJSON_AddNumberToObject(json, "avg_processing_time_us", stats.avg_processing_time_us);
    cJSON_AddNumberToObject(json, "buffer_count", data_buffer_get_count());
    cJSON_AddBoolToObject(json, "buffer_full", data_buffer_is_full());
    cJSON_AddBoolToObject(json, "buffer_empty", data_buffer_is_empty());
    /* imu_odr_hz intentionally omitted from API to avoid confusion with actual plotted points/sec */
    cJSON_AddNumberToObject(json, "imu_fifo_watermark", imu_manager_get_fifo_watermark());
    cJSON_AddNumberToObject(json, "ws_msg_per_sec", ws_msg_rate);
    cJSON_AddNumberToObject(json, "ws_samples_per_sec", ws_samples_rate);
    cJSON_AddNumberToObject(json, "ws_total_messages", ws_total_messages);
    
    char *json_string = cJSON_Print(json);
    if (json_string != NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "JSON generation failed", HTTPD_RESP_USE_STRLEN);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

// API Config endpoint - handles configuration changes
static esp_err_t api_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Config request");
    
    if (req->method == HTTP_POST) {
        char buf[128];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
            return ESP_FAIL;
        }
        buf[ret] = '\0';
        
        cJSON *json = cJSON_Parse(buf);
        if (json == NULL) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            return ESP_FAIL;
        }
        
        cJSON *response = cJSON_CreateObject();
        bool changed = false;
        
        // Handle pause/resume
        cJSON *pause = cJSON_GetObjectItem(json, "pause");
        if (pause && cJSON_IsBool(pause)) {
            ws_streaming_paused = cJSON_IsTrue(pause);
            cJSON_AddBoolToObject(response, "paused", ws_streaming_paused);
            ESP_LOGI(TAG, "Streaming %s", ws_streaming_paused ? "PAUSED" : "RESUMED");
            changed = true;
        }
        
        // Handle full scale change
        cJSON *fs = cJSON_GetObjectItem(json, "full_scale");
        if (fs && cJSON_IsNumber(fs)) {
            uint8_t fs_code = (uint8_t)fs->valueint;
            if (fs_code <= 3) {
                esp_err_t err = imu_manager_set_full_scale(fs_code);
                if (err == ESP_OK) {
                    cJSON_AddNumberToObject(response, "full_scale", fs_code);
                    changed = true;
                } else {
                    cJSON_AddStringToObject(response, "error", "Failed to set full scale");
                }
            } else {
                cJSON_AddStringToObject(response, "error", "Invalid full scale value");
            }
        }
        
        if (!changed) {
            cJSON_AddStringToObject(response, "status", "no_changes");
        } else {
            cJSON_AddStringToObject(response, "status", "ok");
        }
        
        char *json_string = cJSON_Print(response);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        
        free(json_string);
        cJSON_Delete(response);
        cJSON_Delete(json);
        return ESP_OK;
    }

    // GET request - return current config
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "imu_fifo_watermark", imu_manager_get_fifo_watermark());
    cJSON_AddNumberToObject(json, "full_scale", imu_manager_get_full_scale());
    cJSON_AddBoolToObject(json, "paused", ws_streaming_paused);

    char *json_string = cJSON_Print(json);
    if (json_string != NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}// API Download endpoint - returns data in various formats
static esp_err_t api_download_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Download request");
    
    // Get format parameter
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char *buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char format[16];
            if (httpd_query_key_value(buf, "format", format, sizeof(format)) == ESP_OK) {
                if (strcmp(format, "csv") == 0) {
                    // Return CSV data
                    char csv_buffer[8192];
                    esp_err_t ret = data_buffer_export_csv(csv_buffer, sizeof(csv_buffer), 100);
                    if (ret == ESP_OK) {
                        httpd_resp_set_type(req, "text/csv");
                        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=imu_data.csv");
                        httpd_resp_send(req, csv_buffer, strlen(csv_buffer));
                    } else {
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        httpd_resp_send(req, "Failed to export CSV", HTTPD_RESP_USE_STRLEN);
                    }
                } else if (strcmp(format, "json") == 0) {
                    // Return JSON data
                    char json_buffer[8192];
                    esp_err_t ret = data_buffer_export_json(json_buffer, sizeof(json_buffer), 100);
                    if (ret == ESP_OK) {
                        httpd_resp_set_type(req, "application/json");
                        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=imu_data.json");
                        httpd_resp_send(req, json_buffer, strlen(json_buffer));
                    } else {
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        httpd_resp_send(req, "Failed to export JSON", HTTPD_RESP_USE_STRLEN);
                    }
                } else {
                    httpd_resp_set_status(req, "400 Bad Request");
                    httpd_resp_send(req, "Unsupported format", HTTPD_RESP_USE_STRLEN);
                }
            } else {
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Missing format parameter", HTTPD_RESP_USE_STRLEN);
            }
        }
        free(buf);
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing query parameters", HTTPD_RESP_USE_STRLEN);
    }
    
    return ESP_OK;
}

// WebSocket data handler
static esp_err_t ws_data_handler(httpd_req_t *req)
{
    // On initial GET upgrade, register the connection
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);
        ws_register_connection(fd);
        ESP_LOGI(TAG, "WebSocket connected fd=%d", fd);
        return ESP_OK;
    }

    // For incoming messages (optional control), just consume and ack
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &ws_pkt, 0) == ESP_OK && ws_pkt.len) {
        uint8_t tmp_buf[64];
        ws_pkt.payload = tmp_buf;
        size_t to_read = ws_pkt.len < sizeof(tmp_buf) ? ws_pkt.len : sizeof(tmp_buf);
        httpd_ws_recv_frame(req, &ws_pkt, to_read);
        // No-op; could parse control messages here
    }
    return ESP_OK;
}

// WebSocket control handler
static esp_err_t ws_control_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket control connection request");
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

// File handler for serving static files from SPIFFS/web directory
static esp_err_t file_handler(httpd_req_t *req)
{
    const char *filepath = req->uri + 1; // Skip leading '/'
    
    // Security check - prevent directory traversal
    if (strstr(filepath, "..") != NULL) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "Access denied", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Default to index.html if no file specified
    if (strlen(filepath) == 0) {
        filepath = "index.html";
    }
    
    // Try web/ directory first (for separated HTML/CSS/JS files)
    char full_path[96];
    snprintf(full_path, sizeof(full_path), "/spiffs/web/%s", filepath);
    
    FILE *file = fopen(full_path, "r");
    if (file == NULL) {
        // Fallback to /spiffs/ root
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", filepath);
        file = fopen(full_path, "r");
    }
    
    if (file == NULL) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "File not found", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Set content type based on file extension
    if (strstr(filepath, ".html") != NULL) {
        httpd_resp_set_type(req, "text/html");
    } else if (strstr(filepath, ".css") != NULL) {
        httpd_resp_set_type(req, "text/css");
    } else if (strstr(filepath, ".js") != NULL) {
        httpd_resp_set_type(req, "application/javascript");
    } else if (strstr(filepath, ".json") != NULL) {
        httpd_resp_set_type(req, "application/json");
    }
    
    // Send file content
    char buffer[512];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK) {
            break;
        }
    }
    
    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0); // End chunked response
    return ESP_OK;
}

// WebSocket connection management
static void ws_register_connection(int fd)
{
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (!ws_connections[i].active) {
                ws_connections[i].fd = fd;
                ws_connections[i].active = true;
                ESP_LOGI(TAG, "WebSocket connection registered: fd=%d", fd);
                break;
            }
        }
        xSemaphoreGive(ws_mutex);
    }
}

static void ws_unregister_connection(int fd)
{
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (ws_connections[i].active && ws_connections[i].fd == fd) {
                ws_connections[i].active = false;
                ESP_LOGI(TAG, "WebSocket connection unregistered: fd=%d", fd);
                break;
            }
        }
        xSemaphoreGive(ws_mutex);
    }
}

static esp_err_t ws_send_to_all(const char *data, size_t len)
{
    static uint32_t total_sends = 0;
    int active_connections = 0;
    
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = len
    };
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (ws_connections[i].active) {
                httpd_ws_send_frame_async(server, ws_connections[i].fd, &frame);
                active_connections++;
            }
        }
        xSemaphoreGive(ws_mutex);
        
        total_sends++;
        if (total_sends % 500 == 0) {
            ESP_LOGI(TAG, "WS broadcast: %lu total sends, %d active connections", total_sends, active_connections);
        }
    }
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    ESP_LOGI(TAG, "Starting web server...");
    
    // Create WebSocket mutex
    ws_mutex = xSemaphoreCreateMutex();
    if (ws_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WebSocket mutex");
        return ESP_FAIL;
    }
    
    // Initialize WebSocket connections
    memset(ws_connections, 0, sizeof(ws_connections));
    
    // HTTP server configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = WEB_SERVER_MAX_URI_HANDLERS;
    config.stack_size = WEB_SERVER_STACK_SIZE;
    
    // Start HTTP server
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started on port %d", WEB_SERVER_PORT);
        
        // Register URI handlers
        httpd_uri_t api_data_uri = {
            .uri = API_DATA_PATH,
            .method = HTTP_GET,
            .handler = api_data_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_data_uri);
        
        httpd_uri_t api_stats_uri = {
            .uri = API_STATS_PATH,
            .method = HTTP_GET,
            .handler = api_stats_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_stats_uri);
        
        httpd_uri_t api_config_uri = {
            .uri = API_CONFIG_PATH,
            .method = HTTP_GET | HTTP_POST,
            .handler = api_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_config_uri);
        
        httpd_uri_t api_download_uri = {
            .uri = API_DOWNLOAD_PATH,
            .method = HTTP_GET,
            .handler = api_download_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &api_download_uri);
        
        // Root handler serves an embedded dashboard when SPIFFS is not populated
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        // WebSocket endpoint for realtime data
        httpd_uri_t ws_data_uri = {
            .uri = WS_DATA_PATH,
            .method = HTTP_GET,
            .handler = ws_data_handler,
            .user_ctx = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_data_uri);

        // File handler for static content under /spiffs
        httpd_uri_t file_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = file_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &file_uri);
        
        ESP_LOGI(TAG, "Web server started successfully");
        xTaskCreatePinnedToCore(ws_broadcast_task, "ws_broadcast", 4096, NULL, 4, NULL, 0);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
}

esp_err_t web_server_stop(void)
{
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
    }
    
    if (ws_mutex != NULL) {
        vSemaphoreDelete(ws_mutex);
        ws_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

esp_err_t web_server_broadcast_data(const char *data, size_t len)
{
    return ws_send_to_all(data, len);
}

// Broadcast latest sample periodically as compact JSON (polling mode - direct send)
static void ws_broadcast_task(void *arg)
{
    (void)arg;
    static float temp_x[WS_RECENT_MAX_SAMPLES];
    static float temp_y[WS_RECENT_MAX_SAMPLES];
    static float temp_z[WS_RECENT_MAX_SAMPLES];
    static char json_buf[4096];

    uint32_t window_msgs = 0;
    uint32_t window_samples = 0;
    uint64_t window_start_us = esp_timer_get_time();
    uint16_t last_batch_samples = 0;
    uint64_t last_timestamp = 0;

    TickType_t last_wake = xTaskGetTickCount();
    TickType_t broadcast_period = pdMS_TO_TICKS(10);
    if (broadcast_period == 0) {
        broadcast_period = 1;
    }

    ESP_LOGI(TAG, "WebSocket broadcast task started (polling mode)");

    for (;;) {
        uint64_t batch_ts = 0;
        uint16_t fifo_level = 0;  // Not used in polling mode, but kept for API compatibility
        uint32_t seq_id = 0;
        
        led_status_data_pulse_start();
        
        uint16_t fetched = imu_manager_copy_recent_samples(temp_x, temp_y, temp_z,
                                                           WS_RECENT_MAX_SAMPLES,
                                                           &batch_ts, &fifo_level, &seq_id);
        
        if (fetched == 0) {
            led_status_data_pulse_end();
            vTaskDelayUntil(&last_wake, broadcast_period);
            continue;
        }
        
        last_batch_samples = fetched;
        last_timestamp = batch_ts;

        // Get sensor stats
        imu_data_t d;
        bool have_stats = (data_buffer_get_latest(&d) == ESP_OK) && d.accelerometer.valid;
        float sensor_sps = have_stats ? d.stats.samples_per_second : 0.0f;

        // Update window-based rate calculation
        uint64_t now_us = esp_timer_get_time();
        if (window_start_us == 0 || now_us <= window_start_us) {
            window_start_us = now_us;
            window_msgs = 0;
            window_samples = 0;
        }

        window_msgs++;
        window_samples += fetched;

        uint64_t window_span = now_us - window_start_us;
        if (window_span >= 500000ULL) {
            float new_msg_rate = (window_msgs * 1000000.0f) / (float)window_span;
            float new_samples_rate = (window_samples * 1000000.0f) / (float)window_span;
            
            // Exponential moving average (EMA) for smooth display
            if (ws_msg_rate == 0.0f) {
                ws_msg_rate = new_msg_rate;
                ws_samples_rate = new_samples_rate;
            } else {
                ws_msg_rate = ws_msg_rate * 0.7f + new_msg_rate * 0.3f;
                ws_samples_rate = ws_samples_rate * 0.7f + new_samples_rate * 0.3f;
            }
            
            window_msgs = 0;
            window_samples = 0;
            window_start_us = now_us;
            ESP_LOGI(TAG, "WS metrics: %.2f msg/s, %.0f points/s", 
                     ws_msg_rate, ws_samples_rate);
        }

        // Build JSON payload - send fetched samples directly (no ring buffer)
        float last_chunk_x = 0.0f;
        float last_chunk_y = 0.0f;
        float last_chunk_z = 0.0f;

        int n = snprintf(json_buf, sizeof(json_buf), "{\"t\":%llu,\"chunks\":{\"x\":[",
                         (unsigned long long)(last_timestamp ? last_timestamp : now_us));

        for (uint16_t i = 0; i < fetched && n > 0 && n < (int)sizeof(json_buf); ++i) {
            last_chunk_x = temp_x[i];
            n += snprintf(json_buf + n, sizeof(json_buf) - n, i ? ",%.5f" : "%.5f", temp_x[i]);
        }
        n += snprintf(json_buf + n, sizeof(json_buf) - n, "],\"y\":[");
        for (uint16_t i = 0; i < fetched && n > 0 && n < (int)sizeof(json_buf); ++i) {
            last_chunk_y = temp_y[i];
            n += snprintf(json_buf + n, sizeof(json_buf) - n, i ? ",%.5f" : "%.5f", temp_y[i]);
        }
        n += snprintf(json_buf + n, sizeof(json_buf) - n, "],\"z\":[");
        for (uint16_t i = 0; i < fetched && n > 0 && n < (int)sizeof(json_buf); ++i) {
            last_chunk_z = temp_z[i];
            n += snprintf(json_buf + n, sizeof(json_buf) - n, i ? ",%.5f" : "%.5f", temp_z[i]);
        }

        float chunk_mag = sqrtf(last_chunk_x * last_chunk_x +
                                 last_chunk_y * last_chunk_y +
                                 last_chunk_z * last_chunk_z);

        n += snprintf(json_buf + n, sizeof(json_buf) - n,
                  "]},\"mag\":%.5f,\"s\":{\"batch\":%u,"
                          "\"sps\":%.2f,\"pps\":%.2f,\"mps\":%.2f,\"chunk\":%u}}",
                  chunk_mag, last_batch_samples,
                  sensor_sps, ws_samples_rate, ws_msg_rate, fetched);

        // Send WebSocket message
        if (!ws_streaming_paused && n > 0 && n < (int)sizeof(json_buf)) {
            ws_send_to_all(json_buf, (size_t)n);
            ws_total_messages++;
        }
        
        led_status_data_pulse_end();

        vTaskDelayUntil(&last_wake, broadcast_period);
    }
}

// Root handler - serves embedded dashboard HTML
static esp_err_t root_handler(httpd_req_t *req)
{
    // Try to serve index.html from SPIFFS first (if configured)
    FILE *file = fopen("/spiffs/web/index.html", "r");
    if (file == NULL) {
        file = fopen("/spiffs/index.html", "r");
    }
    
    if (file != NULL) {
        // Found index.html - serve it
        httpd_resp_set_type(req, "text/html");
        char buffer[512];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
            if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK) {
                break;
            }
        }
        fclose(file);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
    
    // No SPIFFS - serve embedded HTML dashboard
    static const char *html_page = 
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>IIS3DWB Monitor</title>"
        "<script src='https://cdn.jsdelivr.net/npm/chart.js@4'></script>"
        "<style>"
        "body{font-family:Arial,sans-serif;margin:20px;background:#f8fafc;color:#1e293b}"
        "h2{color:#0369a1;margin-bottom:12px}"
        ".status{padding:12px;border-left:4px solid #10b981;background:#fff;margin:12px 0;font-weight:600;box-shadow:0 1px 3px rgba(0,0,0,0.1)}"
        ".metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px;margin:20px 0}"
        ".metric{background:#fff;padding:12px;border-radius:6px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}"
        ".metric .label{display:block;font-size:12px;text-transform:uppercase;color:#64748b;letter-spacing:0.05em}"
        ".metric .value{display:block;font-size:20px;font-weight:600;margin-top:6px;color:#0f172a}"
        ".chart-box{background:#fff;padding:12px;border-radius:6px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}"
        ".chart-box h3{margin:0 0 10px 0;color:#0284c7;font-size:15px}"
        "canvas{width:100%!important;height:240px!important;background:#f8fafc;border:1px solid #e2e8f0;border-radius:4px}"
        ".log{background:#fff;padding:12px;height:160px;overflow:auto;border:1px solid #e2e8f0;border-radius:6px;font-family:monospace;font-size:11px;color:#334155}"
        ".controls{background:#fff;padding:16px;margin:12px 0;border-radius:6px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}"
        ".control-group{margin-bottom:12px}"
        ".control-group:last-child{margin-bottom:0}"
        ".control-group h4{margin:0 0 8px 0;color:#475569;font-size:13px;text-transform:uppercase;letter-spacing:0.05em}"
        "button{padding:8px 16px;margin:4px;border:1px solid #cbd5e1;border-radius:4px;background:#fff;color:#1e293b;cursor:pointer;font-size:13px;transition:all 0.2s}"
        "button:hover{background:#f1f5f9;border-color:#94a3b8}"
        "button.active{background:#0ea5e9;color:#fff;border-color:#0284c7}"
        "button.paused{background:#ef4444;color:#fff;border-color:#dc2626}"
        "</style></head><body>"
        "<h2>ESP32-C6 IIS3DWB High-Speed Monitor</h2>"
        "<div class='status' id='status'>Connecting...</div>"
        "<div class='controls'><div class='control-group'><h4>Streaming Control</h4>"
        "<button id='btnPause' onclick='togglePause()'>Pause</button></div>"
        "<div class='control-group'><h4>Full Scale Range (Sensor + Chart)</h4>"
        "<button id='fs0' onclick='setFullScale(0,2)'>±2g</button>"
        "<button id='fs1' onclick='setFullScale(1,4)' class='active'>±4g</button>"
        "<button id='fs2' onclick='setFullScale(2,8)'>±8g</button>"
        "<button id='fs3' onclick='setFullScale(3,16)'>±16g</button>"
        "<button id='fsAuto' onclick='setFullScale(3,null)'>Auto Scale</button></div></div>"
        "<div class='metrics'>"
        "<div class='metric'><span class='label'>Hz</span><span class='value' id='metricMsg'>-</span></div>"
        "<div class='metric'><span class='label'>Sensor samples/s</span><span class='value' id='metricSamples'>-</span></div>"
        "<div class='metric'><span class='label'>|g|</span><span class='value' id='metricMag'>-</span></div></div>"
        "<div class='chart-box'><h3>IIS3DWB Acceleration (g)</h3><canvas id='chartG'></canvas></div>"
        "<h3 style='color:#38bdf8;margin-top:24px'>Event Log</h3><div class='log' id='log'></div>"
        "<script>"
        "(()=>{let isPaused=false,currentFullScale=0;const statusEl=document.getElementById('status'),logEl=document.getElementById('log'),btnPause=document.getElementById('btnPause'),metrics={msg:document.getElementById('metricMsg'),samples:document.getElementById('metricSamples'),mag:document.getElementById('metricMag')};"
        "window.togglePause=()=>{isPaused=!isPaused;fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pause:isPaused})}).then(r=>r.json()).then(d=>{addLog(isPaused?'Streaming PAUSED':'Streaming RESUMED');btnPause.textContent=isPaused?'Resume':'Pause';btnPause.className=isPaused?'paused':'';statusEl.textContent=isPaused?'Paused':'Connected'}).catch(e=>addLog('Pause error: '+e.message))};"
        "window.setFullScale=(fs,chartScale)=>{fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({full_scale:fs})}).then(r=>r.json()).then(d=>{if(d.full_scale!==undefined){currentFullScale=d.full_scale;const labels=['±2g','±4g','±8g','±16g'];addLog('Sensor: '+labels[fs]+', Chart: '+(chartScale===null?'Auto':'±'+chartScale+'g'));for(let i=0;i<4;i++)document.getElementById('fs'+i).className=(i===fs&&chartScale!==null)?'active':'';document.getElementById('fsAuto').className=chartScale===null?'active':'';if(chartScale!==null){chart.options.scales.y.min=-chartScale;chart.options.scales.y.max=chartScale}else{delete chart.options.scales.y.min;delete chart.options.scales.y.max}chart.update()}}).catch(e=>addLog('FS error: '+e.message))};"
        "const maxPoints=500;const chart=new Chart(document.getElementById('chartG'),{type:'line',data:{labels:[],datasets:[{label:'X',data:[],borderColor:'#ef4444',borderWidth:1.5,pointRadius:0},{label:'Y',data:[],borderColor:'#22c55e',borderWidth:1.5,pointRadius:0},{label:'Z',data:[],borderColor:'#3b82f6',borderWidth:1.5,pointRadius:0}]},options:{responsive:true,maintainAspectRatio:false,animation:false,scales:{x:{display:false},y:{beginAtZero:false,grid:{color:'rgba(226,232,240,0.8)'}}},plugins:{legend:{labels:{color:'#1e293b'}}}}});"
        "let sampleIndex=0;function addLog(msg){const t=new Date().toLocaleTimeString();logEl.innerHTML='['+t+'] '+msg+'<br>'+logEl.innerHTML;const parts=logEl.innerHTML.split('<br>');if(parts.length>160)logEl.innerHTML=parts.slice(0,160).join('<br>')}"
        "function formatNumber(val,digits){if(val===null||val===undefined||!isFinite(val))return'-';return Number(val).toFixed(digits)}"
        "function pushValues(payload){const chunk=payload&&payload.chunks?payload.chunks:null;if(!chunk||!chunk.x)return;const len=Math.min(chunk.x.length,chunk.y.length,chunk.z.length);for(let i=0;i<len;i++){if(chart.data.labels.length>=maxPoints){chart.data.labels.shift();chart.data.datasets[0].data.shift();chart.data.datasets[1].data.shift();chart.data.datasets[2].data.shift()}chart.data.labels.push(sampleIndex++);chart.data.datasets[0].data.push(chunk.x[i]);chart.data.datasets[1].data.push(chunk.y[i]);chart.data.datasets[2].data.push(chunk.z[i])}chart.update('none');if(payload&&payload.mag!==undefined)metrics.mag.textContent=formatNumber(payload.mag,3)}"
        "function updateMetrics(payload){const stats=payload&&payload.s?payload.s:{};metrics.msg.textContent=formatNumber(stats.pps!==undefined?stats.pps:stats.mps,0);metrics.samples.textContent=formatNumber(stats.sps,0);if(payload&&payload.mag!==undefined)metrics.mag.textContent=formatNumber(payload.mag,3);statusEl.textContent='Connected — '+formatNumber((stats.pps!==undefined?stats.pps:(stats.mps!==undefined?stats.mps:0)),0)+' Hz'}"
        "addLog('Waiting for IIS3DWB data...');const wsUrl=(location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws/data';addLog('Connecting to '+wsUrl);const ws=new WebSocket(wsUrl);"
        "ws.onopen=()=>{addLog('WebSocket connected');statusEl.style.borderLeftColor='#10b981';statusEl.style.background='#fff'};"
        "ws.onerror=()=>{addLog('WebSocket error');statusEl.textContent='Error';statusEl.style.borderLeftColor='#ef4444'};"
        "ws.onclose=()=>{addLog('WebSocket closed');statusEl.textContent='Disconnected';statusEl.style.borderLeftColor='#f59e0b'};"
        "ws.onmessage=(evt)=>{try{const payload=JSON.parse(evt.data);updateMetrics(payload);pushValues(payload)}catch(e){addLog('Parse error: '+e.message)}}"
        "})();</script></body></html>";
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}
