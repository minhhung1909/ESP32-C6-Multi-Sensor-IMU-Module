idf_component_register(SRCS "main.c" 
                              "web_server.c" 
                              "imu_manager.c"
                              "data_buffer.c"
                              "sensors/iis3dwb.c"
                    INCLUDE_DIRS "." "sensors"
                    REQUIRES esp_http_server esp_wifi nvs_flash spiffs json driver esp_timer)
