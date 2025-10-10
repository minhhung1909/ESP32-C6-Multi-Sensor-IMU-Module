idf_component_register(SRCS "main.c" "ble_stream.c" "imu_ble.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_timer driver)
