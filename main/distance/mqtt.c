/*
 * @Author: 思夜雪
 * @Date: 2026-06-14 23:46:57
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-16 15:01:44
 * @Description: 
 * @FilePath: \hello_world\main\distance\mqtt.c
 */

#include "esp_event_base.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include <stdint.h>
#include <string.h>
#include "mbedtls/md.h"
#include "mbedtls/base64.h"

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t mqtt_handle = NULL;

#define ONENET_PRODUCT_ID "d17QeNOY7J"
#define ONENET_DEVICE_NAME "ESP32"
#define ONENET_TOKEN "version=2018-10-31&res=products%2Fd17QeNOY7J%2Fdevices%2FESP32&et=4102444799&method=md5&sign=EXGF869KC2DDqjqq4KntvA%3D%3D"

//#define MQTT_BROKER_URI    "mqtt://218.201.45.2:1883"

#define MQTT_BROKER_URI "mqtt://studio-mqtt.heclouds.com:1883"

static void mqtt_event_handle (void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
{
    esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG,"MQTT CONNECTED");
            /* 订阅属性上报响应 */
            esp_mqtt_client_subscribe(mqtt_handle, "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post/reply", 0);
        break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT DISCONNECT");
        break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT PUBLISHED");
        break;

        case MQTT_EVENT_DATA:
        /* 收到服务器下发的数据时触发 (本教程暂时不订阅) */
        ESP_LOGI(TAG, "topic=%.*s  data=%.*s",
                 evt->topic_len, evt->topic,
                 evt->data_len,  evt->data);
        break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT ERROR");
        break;
    }
}

void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials = {
            .authentication.password = ONENET_TOKEN,
            .client_id = ONENET_DEVICE_NAME,
            .username = ONENET_PRODUCT_ID
        }
    };

    mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handle, NULL);

    esp_mqtt_client_start(mqtt_handle);

    ESP_LOGI(TAG, "MQTT starting");
}

int mqtt_publish(const char *topic,const char *data)
{
    if(!mqtt_handle)
    {
        ESP_LOGE(TAG, "MQTT未初始化");
        return -1;
    }

     /*
     * esp_mqtt_client_publish 的参数:
     *   client  → MQTT 客户端句柄
     *   topic   → 发到哪个主题
     *   data    → 消息内容
     *   0       → data_len=0 表示自动用 strlen(data)
     *   1       → QoS=1 (至少送达一次)
     *   0       → retain=0 (不保留最后一条消息)
     */ 
    int i = esp_mqtt_client_publish(mqtt_handle, topic, data, 0, 1, 0);
    return i;
}
