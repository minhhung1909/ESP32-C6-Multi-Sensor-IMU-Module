#include "web_server.h"
#include "data_buffer.h"
#include "imu_manager.h"
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

#define WS_PLOT_CHUNK_SAMPLES      10
#define WS_PLOT_BUFFER_CAPACITY    6000
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
    cJSON_AddNumberToObject(stats, "fifo_level", data.stats.fifo_level);
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
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"status\":\"read-only\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    cJSON *json = cJSON_CreateObject();
    /* Do not include imu_odr_hz here to keep UI focused on actual plotted points/sec */
    cJSON_AddNumberToObject(json, "imu_fifo_watermark", imu_manager_get_fifo_watermark());

    char *json_string = cJSON_Print(json);
    if (json_string != NULL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, strlen(json_string));
        free(json_string);
    }

    cJSON_Delete(json);
    return ESP_OK;
}

// API Download endpoint - returns data in various formats
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

// File handler for serving static files
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
    
    // Build full path
    char full_path[64];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filepath);
    
    // Open file
    FILE *file = fopen(full_path, "r");
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
        // Start broadcaster task (~100 Hz)
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

// Broadcast latest sample periodically as compact JSON
static void ws_broadcast_task(void *arg)
{
    (void)arg;
    static float ring_x[WS_PLOT_BUFFER_CAPACITY];
    static float ring_y[WS_PLOT_BUFFER_CAPACITY];
    static float ring_z[WS_PLOT_BUFFER_CAPACITY];
    static float temp_x[WS_RECENT_MAX_SAMPLES];
    static float temp_y[WS_RECENT_MAX_SAMPLES];
    static float temp_z[WS_RECENT_MAX_SAMPLES];
    static char json_buf[4096];

    size_t ring_head = 0;
    size_t ring_size = 0;
    uint32_t window_msgs = 0;
    uint32_t window_samples = 0;
    uint64_t window_start_us = esp_timer_get_time();
    uint32_t last_sequence = 0;
    bool sequence_init = false;
    uint16_t last_fifo_level = 0;
    uint16_t last_batch_samples = 0;
    uint64_t last_timestamp = 0;
    uint64_t last_send_time_us = 0;

    TickType_t last_wake = xTaskGetTickCount();
    TickType_t broadcast_period = pdMS_TO_TICKS(10);
    if (broadcast_period == 0) {
        broadcast_period = 1;
    }

    ESP_LOGI(TAG, "WebSocket broadcast task started");

    for (;;) {
        uint64_t batch_ts = 0;
        uint16_t fifo_level = 0;
        uint32_t seq_id = 0;
        uint16_t fetched = imu_manager_copy_recent_samples(temp_x, temp_y, temp_z,
                                                           WS_RECENT_MAX_SAMPLES,
                                                           &batch_ts, &fifo_level, &seq_id);
        if (fetched > 0 && (!sequence_init || seq_id != last_sequence)) {
            sequence_init = true;
            last_sequence = seq_id;
            last_fifo_level = fifo_level;
            last_batch_samples = fetched;
            last_timestamp = batch_ts;

            for (uint16_t i = 0; i < fetched; ++i) {
                size_t idx = (ring_head + ring_size) % WS_PLOT_BUFFER_CAPACITY;
                ring_x[idx] = temp_x[i];
                ring_y[idx] = temp_y[i];
                ring_z[idx] = temp_z[i];
                if (ring_size == WS_PLOT_BUFFER_CAPACITY) {
                    ring_head = (ring_head + 1) % WS_PLOT_BUFFER_CAPACITY;
                } else {
                    ring_size++;
                }
            }
        }

        uint16_t available = (ring_size > UINT16_MAX) ? UINT16_MAX : (uint16_t)ring_size;
        uint16_t chunk = available < WS_PLOT_CHUNK_SAMPLES ? available : WS_PLOT_CHUNK_SAMPLES;
        if (chunk == 0) {
            vTaskDelayUntil(&last_wake, broadcast_period);
            continue;
        }

    imu_data_t d;
    bool have_stats = (data_buffer_get_latest(&d) == ESP_OK) && d.accelerometer.valid;
    float sensor_sps = have_stats ? d.stats.samples_per_second : 0.0f;

        uint64_t now_us = esp_timer_get_time();
        if (window_start_us == 0 || now_us <= window_start_us) {
            window_start_us = now_us;
            window_msgs = 0;
            window_samples = 0;
        }

        window_msgs++;
        window_samples += chunk;

        uint64_t window_span = now_us - window_start_us;
        if (window_span >= 1000000ULL) {
            ws_msg_rate = (window_msgs * 1000000.0f) / (float)window_span;
            ws_samples_rate = (window_samples * 1000000.0f) / (float)window_span;
            window_msgs = 0;
            window_samples = 0;
            window_start_us = now_us;
            ESP_LOGI(TAG, "WS metrics: %.2f msg/s, %.0f points/s", ws_msg_rate, ws_samples_rate);
        }

        uint64_t send_now_us = esp_timer_get_time();
        float delta_us = (last_send_time_us == 0 || send_now_us <= last_send_time_us)
                         ? (float)broadcast_period * 1000.0f * portTICK_PERIOD_MS
                         : (float)(send_now_us - last_send_time_us);
        if (delta_us <= 0.0f) {
            delta_us = (float)broadcast_period * 1000.0f * portTICK_PERIOD_MS;
            if (delta_us <= 0.0f) {
                delta_us = 10000.0f; // fallback 10 ms
            }
        }
        float plot_msg_rate = 1000000.0f / delta_us;
        float plot_point_rate = ((float)chunk * 1000000.0f) / delta_us;
        last_send_time_us = send_now_us;
        if (!have_stats) {
            sensor_sps = plot_point_rate;
        }

    /* keep ws_msg_rate and ws_samples_rate as the window-averaged values
     * (updated above when window_span >= 1s). Do not overwrite them with
     * per-message instantaneous values to ensure displayed pts/s is the
     * measured points-per-second drawn on the graph. */

        size_t head = ring_head;
        float last_chunk_x = 0.0f;
        float last_chunk_y = 0.0f;
        float last_chunk_z = 0.0f;

        int n = snprintf(json_buf, sizeof(json_buf), "{\"t\":%llu,\"chunks\":{\"x\":[",
                         (unsigned long long)(last_timestamp ? last_timestamp : now_us));

        for (uint16_t i = 0; i < chunk && n > 0 && n < (int)sizeof(json_buf); ++i) {
            size_t idx = (head + i) % WS_PLOT_BUFFER_CAPACITY;
            last_chunk_x = ring_x[idx];
            n += snprintf(json_buf + n, sizeof(json_buf) - n, i ? ",%.5f" : "%.5f", ring_x[idx]);
        }
        n += snprintf(json_buf + n, sizeof(json_buf) - n, "],\"y\":[");
        for (uint16_t i = 0; i < chunk && n > 0 && n < (int)sizeof(json_buf); ++i) {
            size_t idx = (head + i) % WS_PLOT_BUFFER_CAPACITY;
            last_chunk_y = ring_y[idx];
            n += snprintf(json_buf + n, sizeof(json_buf) - n, i ? ",%.5f" : "%.5f", ring_y[idx]);
        }
        n += snprintf(json_buf + n, sizeof(json_buf) - n, "],\"z\":[");
        for (uint16_t i = 0; i < chunk && n > 0 && n < (int)sizeof(json_buf); ++i) {
            size_t idx = (head + i) % WS_PLOT_BUFFER_CAPACITY;
            last_chunk_z = ring_z[idx];
            n += snprintf(json_buf + n, sizeof(json_buf) - n, i ? ",%.5f" : "%.5f", ring_z[idx]);
        }

        float chunk_mag = sqrtf(last_chunk_x * last_chunk_x +
                                 last_chunk_y * last_chunk_y +
                                 last_chunk_z * last_chunk_z);

    n += snprintf(json_buf + n, sizeof(json_buf) - n,
              "]},\"mag\":%.5f,\"s\":{\"fifo\":%u,\"batch\":%u,"
                      "\"sps\":%.2f,\"pps\":%.2f,\"mps\":%.2f,\"chunk\":%u}}",
              chunk_mag,
              last_fifo_level,
              last_batch_samples,
              sensor_sps,
                      ws_samples_rate,
                      ws_msg_rate,
              chunk);

        ring_head = (ring_head + chunk) % WS_PLOT_BUFFER_CAPACITY;
        ring_size -= chunk;

        if (n > 0 && n < (int)sizeof(json_buf)) {
            ws_send_to_all(json_buf, (size_t)n);
            ws_total_messages++;
        }

        vTaskDelayUntil(&last_wake, broadcast_period);
    }
}

// Simple embedded HTML dashboard (served at "/")
static esp_err_t root_handler(httpd_req_t *req)
{
    static const char *html =
        "<!DOCTYPE html>"
        "<html><head><meta charset='utf-8'><title>IIS3DWB Web Monitor</title>"
        "<script src='https://cdn.jsdelivr.net/npm/chart.js@4'></script>"
        "<style>"
        "body{font-family:Arial,Helvetica,sans-serif;margin:20px;background:#f8fafc;color:#1e293b}"
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
        "a{color:#0369a1}"
        "</style></head><body>"
        "<h2>ESP32-C6 IIS3DWB High-Speed Monitor</h2>"
        "<div class='status' id='status'>Connecting...</div>"
        "<div class='metrics'>"
        "  <div class='metric'><span class='label'>Plot pts/s</span><span class='value' id='metricMsg'>-</span></div>"
        "  <div class='metric'><span class='label'>Sensor samples/s</span><span class='value' id='metricSamples'>-</span></div>"
        "  <div class='metric'><span class='label'>FIFO level</span><span class='value' id='metricFifo'>-</span></div>"
        "  <div class='metric'><span class='label'>Batch size</span><span class='value' id='metricBatch'>-</span></div>"
        "  <div class='metric'><span class='label'>|g|</span><span class='value' id='metricMag'>-</span></div>"
        "</div>"
        "<div class='chart-box'><h3>IIS3DWB Acceleration (g)</h3><canvas id='chartG'></canvas></div>"
        "<h3 style='color:#38bdf8;margin-top:24px;'>Event Log</h3>"
        "<div class='log' id='log'></div>"
        "<script>"
        "(()=>{"
        "  const statusEl=document.getElementById('status');"
        "  const logEl=document.getElementById('log');"
        "  const metrics={"
        "    msg:document.getElementById('metricMsg'),"
        "    samples:document.getElementById('metricSamples'),"
        "    fifo:document.getElementById('metricFifo'),"
        "    batch:document.getElementById('metricBatch'),"
        "    mag:document.getElementById('metricMag')"
        "  };"
        "  const ctx=document.getElementById('chartG').getContext('2d');"
        "  const maxPoints=200;"
        "  const chart=new Chart(ctx,{"
        "    type:'line',"
        "    data:{labels:[],datasets:["
        "      {label:'X',data:[],borderColor:'#ef4444',borderWidth:1.5,pointRadius:0},"
        "      {label:'Y',data:[],borderColor:'#10b981',borderWidth:1.5,pointRadius:0},"
        "      {label:'Z',data:[],borderColor:'#3b82f6',borderWidth:1.5,pointRadius:0}"
        "    ]},"
        "    options:{responsive:true,maintainAspectRatio:false,animation:false,scales:{x:{display:false},y:{beginAtZero:false,grid:{color:'rgba(226,232,240,0.8)'}}},plugins:{legend:{labels:{color:'#1e293b'}}}}"
        "  });"
        "  let sampleIndex=0;"
        "  function addLog(msg){const t=new Date().toLocaleTimeString();logEl.innerHTML='['+t+'] '+msg+'<br>'+logEl.innerHTML;const parts=logEl.innerHTML.split('<br>');if(parts.length>160){logEl.innerHTML=parts.slice(0,160).join('<br>');}}"
        "  function formatNumber(val, digits){if(val===null||val===undefined||!isFinite(val)) return '-';return Number(val).toFixed(digits);}"
        "  function pushValues(payload){const chunk=payload&&payload.chunks?payload.chunks:null;if(!chunk||!chunk.x)return;const len=Math.min(chunk.x.length, chunk.y.length, chunk.z.length);for(let i=0;i<len;i++){if(chart.data.labels.length>=maxPoints){chart.data.labels.shift();chart.data.datasets[0].data.shift();chart.data.datasets[1].data.shift();chart.data.datasets[2].data.shift();}chart.data.labels.push(sampleIndex++);chart.data.datasets[0].data.push(chunk.x[i]);chart.data.datasets[1].data.push(chunk.y[i]);chart.data.datasets[2].data.push(chunk.z[i]);}chart.update('none');if(payload&&payload.mag!==undefined){metrics.mag.textContent=formatNumber(payload.mag,3);}}"
    "  function updateMetrics(payload){const stats=payload && payload.s ? payload.s : {};metrics.msg.textContent=formatNumber(stats.pps!==undefined?stats.pps:stats.mps,1);metrics.samples.textContent=formatNumber(stats.sps,0);metrics.fifo.textContent=stats.fifo!==undefined?stats.fifo:'-';metrics.batch.textContent=stats.batch!==undefined?stats.batch:'-';if(payload&&payload.mag!==undefined){metrics.mag.textContent=formatNumber(payload.mag,3);}statusEl.textContent='Connected — '+formatNumber((stats.pps!==undefined?stats.pps:(stats.mps!==undefined?stats.mps:0)),1)+' pts/s';}"
        "  addLog('Waiting for IIS3DWB data...');"
        "  const wsUrl=(location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws/data';"
        "  addLog('Connecting to '+wsUrl);"
        "  const ws=new WebSocket(wsUrl);"
        "  ws.onopen=()=>{addLog('WebSocket connected');statusEl.style.borderLeftColor='#10b981';statusEl.style.background='#fff';};"
        "  ws.onerror=()=>{addLog('WebSocket error');statusEl.textContent='Error';statusEl.style.borderLeftColor='#ef4444';statusEl.style.background='#fef2f2';};"
        "  ws.onclose=()=>{addLog('WebSocket closed');statusEl.textContent='Disconnected';statusEl.style.borderLeftColor='#f59e0b';statusEl.style.background='#fffbeb';};"
        "  let msgCounter=0;"
        "  ws.onmessage=(ev)=>{"
        "    try{"
        "      const payload=JSON.parse(ev.data);"
        "      pushValues(payload);"
        "      updateMetrics(payload);"
        "      msgCounter++;"
        "      if(msgCounter%200===0){const stats=payload && payload.s ? payload.s : {};addLog('Plot '+formatNumber(stats.pps!==undefined?stats.pps:(stats.mps||0),1)+' pts/s (msg '+formatNumber(stats.mps||0,1)+'), sensor '+formatNumber(stats.sps||0,0)+' samples/s');}"
        "    }catch(err){addLog('Parse error: '+err.message);}"
        "  };"
        "})();"
        "</script>"
        "</body></html>"
        "";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}
