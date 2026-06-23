/*
 * @Author: 思夜雪
 * @Date: 2026-06-16 20:55:19
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-23 12:40:28
 * @Description: 
 * @FilePath: \hello_world\main\distance\sensor_task.c
 */
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdio.h>
#include "../Board/dht11.h"
#include "sensor_task.h"
#include "../Board/dht11.h"
#include "mqtt.h"
#include "sdkconfig.h"

#define sensor_data_t dht11_data_t
#define TOPIC  "$sys/" CONFIG_ONENET_PRODUCT_ID "/" CONFIG_ONENET_DEVICE_NAME "/thing/property/post"

static const char * TAG = "sensor";

static void sensor_data_to_json(const sensor_data_t *d, char *buf, size_t size)
{
    unsigned long now_ms = (unsigned long)(esp_timer_get_time() / 1000);
    snprintf(buf, size,
             "{"
             "\"id\":\"%lu\","
             "\"version\":\"1.0\","
             "\"params\":{"
             "\"temp_value\":{\"value\":%.1f},"
             "\"humidity_value\":{\"value\":%d}"
             "}"
             "}",
             now_ms,
             d->tem,
             (int)d->hum);
}

void sensor_task(void *pvParam)
{
    dht11_data_t dht11_data;
    dht11_init(GPIO_NUM_4);
    vTaskDelay(pdMS_TO_TICKS(1200));

    while (1) {
        esp_err_t ert =  dht11_read(&dht11_data);
        if(ert != ESP_OK){
            ESP_LOGE(TAG, "dlt11_read failed,retry...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        char json[256];
        sensor_data_to_json(&dht11_data, json, sizeof(json));
        int count =mqtt_publish(TOPIC, json);
        if(count < 0){
            ESP_LOGE(TAG, "publish failed");
        }else {
            ESP_LOGI(TAG, "publish successful,count = %d",count);
        }
        ESP_LOGI(TAG, "publish json: %s", json);  // 看看实际内容
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
