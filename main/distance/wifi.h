#ifndef __WIFI_H_
#define __WIFI_H_

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

void wifi_init(void);

/**
 * @brief 等待 WiFi 连接成功并获取到 IP 地址
 * @param ticks_to_wait 等待的超时时间（Tick），portMAX_DELAY 表示一直等待
 * @return pdTRUE 连接成功，pdFALSE 超时
 */
BaseType_t wifi_wait_connected(TickType_t ticks_to_wait);

#endif
