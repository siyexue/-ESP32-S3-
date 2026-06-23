#pragma once
#include "sdkconfig.h"

/* =====================================================
 * 统一配置中转头文件
 * 将 Kconfig menuconfig 生成的 CONFIG_* 宏
 * 映射回代码中原有的宏名称，减少改动量
 * ===================================================== */

/* Wi-Fi */
#define WIFI_SSID       CONFIG_WIFI_SSID
#define WIFI_PASSWORD   CONFIG_WIFI_PASSWORD

/* OneNET MQTT */
#define ONENET_PRODUCT_ID   CONFIG_ONENET_PRODUCT_ID
#define ONENET_DEVICE_NAME  CONFIG_ONENET_DEVICE_NAME
#define ONENET_TOKEN        CONFIG_ONENET_TOKEN

/* 自动拼接上报 Topic，不再需要手动写死 */
#define ONENET_TOPIC_POST \
    "$sys/" CONFIG_ONENET_PRODUCT_ID "/" CONFIG_ONENET_DEVICE_NAME "/thing/property/post"

#define ONENET_TOPIC_REPLY \
    "$sys/" CONFIG_ONENET_PRODUCT_ID "/" CONFIG_ONENET_DEVICE_NAME "/thing/property/post/reply"

