#include "web_server.h"
#include "data_buffer.h"
#include "imu_manager.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t server = NULL;
static httpd_handle_t ws_server = NULL;

// WebSocket connection tracking
typedef struct {
    int fd;
    bool active;
} ws_connection_t;

static ws_connection_t ws_connections[WEBSOCKET_MAX_CONNECTIONS];
static SemaphoreHandle_t ws_mutex = NULL;

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

// API Data endpoint - returns latest sensor data
static esp_err_t api_data_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API Data request");
    
    imu_data_t data;
    esp_err_t ret = data_buffer_get_latest(&data);
    
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "No data available", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Convert to JSON
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "timestamp_us", data.timestamp_us);
    
    // Magnetometer
    if (data.magnetometer.valid) {
        cJSON *mag = cJSON_CreateObject();
        cJSON_AddNumberToObject(mag, "x_mg", data.magnetometer.x_mg);
        cJSON_AddNumberToObject(mag, "y_mg", data.magnetometer.y_mg);
        cJSON_AddNumberToObject(mag, "z_mg", data.magnetometer.z_mg);
        cJSON_AddNumberToObject(mag, "temperature_c", data.magnetometer.temperature_c);
        cJSON_AddItemToObject(json, "magnetometer", mag);
    }
    
    // Accelerometer
    if (data.accelerometer.valid) {
        cJSON *accel = cJSON_CreateObject();
        cJSON_AddNumberToObject(accel, "x_g", data.accelerometer.x_g);
        cJSON_AddNumberToObject(accel, "y_g", data.accelerometer.y_g);
        cJSON_AddNumberToObject(accel, "z_g", data.accelerometer.z_g);
        cJSON_AddItemToObject(json, "accelerometer", accel);
    }
    
    // IMU 6-axis
    if (data.imu_6axis.valid) {
        cJSON *imu = cJSON_CreateObject();
        cJSON *accel = cJSON_CreateObject();
        cJSON *gyro = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(accel, "x_g", data.imu_6axis.accel_x_g);
        cJSON_AddNumberToObject(accel, "y_g", data.imu_6axis.accel_y_g);
        cJSON_AddNumberToObject(accel, "z_g", data.imu_6axis.accel_z_g);
        cJSON_AddNumberToObject(gyro, "x_dps", data.imu_6axis.gyro_x_dps);
        cJSON_AddNumberToObject(gyro, "y_dps", data.imu_6axis.gyro_y_dps);
        cJSON_AddNumberToObject(gyro, "z_dps", data.imu_6axis.gyro_z_dps);
        cJSON_AddNumberToObject(imu, "temperature_c", data.imu_6axis.temperature_c);
        
        cJSON_AddItemToObject(imu, "accelerometer", accel);
        cJSON_AddItemToObject(imu, "gyroscope", gyro);
        cJSON_AddItemToObject(json, "imu_6axis", imu);
    }
    
    // Inclinometer
    if (data.inclinometer.valid) {
        cJSON *incl = cJSON_CreateObject();
        cJSON *angles = cJSON_CreateObject();
        cJSON *accel = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(angles, "x_deg", data.inclinometer.angle_x_deg);
        cJSON_AddNumberToObject(angles, "y_deg", data.inclinometer.angle_y_deg);
        cJSON_AddNumberToObject(angles, "z_deg", data.inclinometer.angle_z_deg);
        cJSON_AddNumberToObject(accel, "x_g", data.inclinometer.accel_x_g);
        cJSON_AddNumberToObject(accel, "y_g", data.inclinometer.accel_y_g);
        cJSON_AddNumberToObject(accel, "z_g", data.inclinometer.accel_z_g);
        cJSON_AddNumberToObject(incl, "temperature_c", data.inclinometer.temperature_c);
        
        cJSON_AddItemToObject(incl, "angles", angles);
        cJSON_AddItemToObject(incl, "accelerometer", accel);
        cJSON_AddItemToObject(json, "inclinometer", incl);
    }
    
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
        // Handle POST request for configuration
        char content[256];
        int ret = httpd_req_recv(req, content, sizeof(content) - 1);
        if (ret <= 0) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "No data received", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        content[ret] = '\0';
        
        // Parse JSON configuration
        cJSON *json = cJSON_Parse(content);
        if (json == NULL) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Invalid JSON", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        
        // Process configuration
        cJSON *sampling_rate = cJSON_GetObjectItem(json, "sampling_rate");
        if (cJSON_IsNumber(sampling_rate)) {
            imu_manager_set_sampling_rate(sampling_rate->valueint);
        }
        
        cJSON *watermark = cJSON_GetObjectItem(json, "fifo_watermark");
        if (cJSON_IsNumber(watermark)) {
            imu_manager_set_fifo_watermark(watermark->valueint);
        }
        
        cJSON_Delete(json);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    } else {
        // Handle GET request - return current configuration
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "sampling_rate", 100); // TODO: Get from IMU manager
        cJSON_AddNumberToObject(json, "fifo_watermark", 32); // TODO: Get from IMU manager
        
        char *json_string = cJSON_Print(json);
        if (json_string != NULL) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_send(req, json_string, strlen(json_string));
            free(json_string);
        }
        
        cJSON_Delete(json);
    }
    
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
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket data connection request");
        return ESP_OK;
    }
    
    // WebSocket frame handling
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    if (req->method == HTTP_POST) {
        // Register connection
        ws_register_connection(httpd_req_to_sockfd(req));
        
        // Send initial data
        imu_data_t data;
        if (data_buffer_get_latest(&data) == ESP_OK) {
            char json_buffer[1024];
            // TODO: Convert to JSON and send
            ws_pkt.payload = (uint8_t*)json_buffer;
            ws_pkt.len = strlen(json_buffer);
            httpd_ws_send_frame(req, &ws_pkt);
        }
        
        return ESP_OK;
    }
    
    return ESP_FAIL;
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
    if (xSemaphoreTake(ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < WEBSOCKET_MAX_CONNECTIONS; i++) {
            if (ws_connections[i].active) {
                // TODO: Send WebSocket frame to connection
                // This would require implementing WebSocket frame sending
            }
        }
        xSemaphoreGive(ws_mutex);
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
        
        // File handler for static content
        httpd_uri_t file_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = file_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &file_uri);
        
        ESP_LOGI(TAG, "Web server started successfully");
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

esp_err_t web_server_broadcast_stats(const char *stats, size_t len)
{
    return ws_send_to_all(stats, len);
}

esp_err_t websocket_send_data(httpd_handle_t hd, int fd, const char *data, size_t len)
{
    // TODO: Implement WebSocket frame sending
    return ESP_OK;
}

esp_err_t websocket_send_json(httpd_handle_t hd, int fd, const char *json_data)
{
    return websocket_send_data(hd, fd, json_data, strlen(json_data));
}

esp_err_t web_server_set_sampling_rate(uint32_t rate_hz)
{
    return imu_manager_set_sampling_rate(rate_hz);
}

esp_err_t web_server_set_fifo_watermark(uint16_t watermark)
{
    return imu_manager_set_fifo_watermark(watermark);
}

esp_err_t web_server_enable_sensor(uint8_t sensor_id, bool enable)
{
    return imu_manager_enable_sensor(sensor_id, enable);
}
