#include "esp_err.h"
#include "esp_event_base.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "freertos/projdefs.h"
#include "nvs.h"
#include "nvs_flash.h"
static const char *TAG = "WIFI";

#define wifi_id "jinse"
#define wifi_password "20040625"

static EventGroupHandle_t evt_handle;
#define wifi_connect BIT0

static int retry_cnt = 0;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if(event_base == WIFI_EVENT)
    {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WIFI connected");
            retry_cnt = 0;
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            {
                wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "WIFI connect failed,reason = %d",d->reason);
                xEventGroupClearBits(evt_handle, wifi_connect);
                if(retry_cnt < 10)
                {
                    int delay = (1 << retry_cnt) * 1000;
                    if(delay > 30000) delay = 30000;
                    ESP_LOGI(TAG, "reconnect %d ms",delay);
                    vTaskDelay(pdMS_TO_TICKS(delay));
                    esp_wifi_connect();
                    retry_cnt ++;
                }
                else {
                    ESP_LOGE(TAG, "max retries reached, giving up");
                }

                break;
            }

            default:
                break;
        }

    }

    else if (event_base == IP_EVENT  && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *p = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&p->ip_info.ip));
        retry_cnt = 0;
        xEventGroupSetBits(evt_handle, wifi_connect);
    }
}

void wifi_init(void)
{
    evt_handle = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NEW_VERSION_FOUND || ret == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t ctx;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,&ctx);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,&ctx);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = wifi_id,
            .password = wifi_password
        }
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi init done, SSID=%s", wifi_id);
}

BaseType_t wifi_wait_connected(TickType_t ticks_to_wait)
{
    EventBits_t bits = xEventGroupWaitBits(evt_handle, wifi_connect,
                                           pdFALSE, pdFALSE, ticks_to_wait);
    return (bits & wifi_connect) ? pdTRUE : pdFALSE;
}
                              