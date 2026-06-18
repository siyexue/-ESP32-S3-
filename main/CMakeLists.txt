idf_component_register(SRCS "GPIO.c" "hello_world_main.c" "Board/pwm.c" "Board/adc.c" "Board/oled.c" "Board/dht11.c" "Board/ble.c" "distance/wifi.c" "distance/mqtt.c" "distance/sensor_task.c" "Board/ble.c"
                    INCLUDE_DIRS ""
                    REQUIRES esp_driver_ledc esp_driver_gpio esp_adc esp_driver_i2c esp_lcd esp_driver_rmt esp_wifi esp_netif nvs_flash esp_event mqtt bt)
