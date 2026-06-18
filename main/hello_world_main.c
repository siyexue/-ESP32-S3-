/*
 * @Author: 鎬濆闆?
 * @Date: 2026-06-10 18:27:41
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-16 22:58:17
 * @Description: 
 * @FilePath: \hello_world\main\hello_world_main.c
 */
/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GPIO.h"
#include "soc/gpio_num.h"
#include "Board/pwm.h"
#include "Board/adc.h"
#include "Board/oled.h"
#include "Board/dht11.h"
#include "distance/wifi.h"
#include "distance/mqtt.h"
#include "distance/sensor_task.h"
#include "Board/ble.h"

static TaskHandle_t breath_handle = NULL;
static TaskHandle_t adc_handle = NULL;
//static TaskHandle_t dht11_handle = NULL;
static TaskHandle_t sensor_handle = NULL;

void app_main(void)
{
    oled_init();
    oled_clear();
    wifi_init();

    /* 等待 WiFi 连接成功并获取到 IP（最长等待 30 秒） */
    if (wifi_wait_connected(pdMS_TO_TICKS(30000)) == pdTRUE) {
        ESP_LOGI("MAIN", "WiFi connected, starting MQTT...");
        mqtt_init();
    } else {
        ESP_LOGE("MAIN", "WiFi timeout, MQTT not started");
    }

    ble_init();

    xTaskCreate(pwm_breath_task, "pwm_breath_led", 4096,NULL, 5,&breath_handle);
    xTaskCreate(adc_get_task, "adc_get_task", 4096, NULL, 6, &adc_handle);
    //xTaskCreate(dht11_task, "dht11_task", 4096, NULL, 4, &dht11_handle);
    xTaskCreate(sensor_task,"sensor_task", 4096, NULL,4, &sensor_handle);
}
