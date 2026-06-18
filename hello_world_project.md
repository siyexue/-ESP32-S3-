# ESP32-S3 Hello World 项目代码

> 基于 ESP-IDF 5.3.5 框架
> 作者：思夜雪 | 日期：2026-06-16

---

## 项目结构

```
E:\ESP32-S3\Hello_World\hello_world\
├── CMakeLists.txt              # 顶层 CMake 构建
├── README.md                   # 官方 README
├── sdkconfig                   # ESP-IDF 配置
├── pytest_hello_world.py       # 自动化测试
├── main/
│   ├── CMakeLists.txt           # 组件构建
│   ├── hello_world_main.c      # 主程序入口
│   ├── GPIO.c / GPIO.h         # GPIO 初始化
│   ├── Board/
│   │   ├── pwm.c / pwm.h       # PWM 呼吸灯
│   │   ├── adc.c / adc.h       # ADC 电压采集
│   │   ├── oled.c / oled.h     # SSD1306 OLED 驱动
│   │   ├── oled_font.h         # 8x16 ASCII 字库
│   │   ├── dht11.c / dht11.h   # DHT11 温湿度传感器 (RMT)
│   └── distance/
│       ├── wifi.c / wifi.h     # WiFi STA 连接
│       ├── mqtt.c / mqtt.h     # MQTT 连接 OneNET
│       └── sensor_task.c / sensor_task.h  # 传感器数据上报任务
├── docs/
│   ├── DHT11_RMT驱动详解.md
│   └── WiFi_MQTT物联网详解.md
└── managed_components/         # 托管组件 (ssd1306 等)
```

---

## 1. 顶层构建

### `CMakeLists.txt`

```cmake
# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_world)
```

---

## 2. main 组件

### `main/CMakeLists.txt`

```cmake
idf_component_register(SRCS "GPIO.c" "hello_world_main.c" "Board/pwm.c" "Board/adc.c" "Board/oled.c" "Board/dht11.c" "distance/wifi.c" "distance/mqtt.c" "distance/sensor_task.c"
                    INCLUDE_DIRS ""
                    REQUIRES esp_driver_ledc esp_driver_gpio esp_adc esp_driver_i2c esp_lcd esp_driver_rmt esp_wifi esp_netif nvs_flash esp_event mqtt)
```

### `main/hello_world_main.c` — 主程序入口

```c
/*
 * @Author: 思夜雪
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

    xTaskCreate(pwm_breath_task, "pwm_breath_led", 4096,NULL, 5,&breath_handle);
    xTaskCreate(adc_get_task, "adc_get_task", 4096, NULL, 6, &adc_handle);
    //xTaskCreate(dht11_task, "dht11_task", 4096, NULL, 4, &dht11_handle);
    xTaskCreate(sensor_task,"sensor_task", 4096, NULL,4, &sensor_handle);
}
```

**功能说明：**
1. 初始化 OLED 显示屏并清屏
2. 初始化 WiFi 并等待连接（最长 30 秒）
3. WiFi 连接成功后初始化 MQTT（连接 OneNET 平台）
4. 创建三个 FreeRTOS 任务：
   - `pwm_breath_led` — PWM 呼吸灯效果
   - `adc_get_task` — ADC 电压采集并显示在 OLED 上
   - `sensor_task` — 读取 DHT11 温湿度并通过 MQTT 上报

---

## 3. GPIO

### `GPIO.h`

```c
#ifndef __GPIO_H_
#define __GPIO_H_

void gpio_init(void);

#endif
```

### `GPIO.c`

```c
/*
 * @Author: 思夜雪
 * @Date: 2026-06-10 21:56:37
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-10 22:14:41
 * @Description: 
 * @FilePath: \hello_world\main\GPIO.c
 */
#include <stdio.h>
#include "driver/gpio.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"


  void gpio_init(void)
  {
    gpio_config_t gpio_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_4),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&gpio_conf);
  }
```

---

## 4. Board 层 — 板级外设驱动

### 4.1 PWM 呼吸灯

#### `pwm.h`

```c
#ifndef __PWM_H_
#define __PWM_H_

void pwm_Breathled_init(void);
void pwm_breath_task(void *pvParam);

#endif
```

#### `pwm.c`

```c
/*
 * @Author: 思夜雪
 * @Date: 2026-06-11 19:37:58
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-12 14:35:26
 * @Description: 
 * @FilePath: \hello_world\main\Board\pwm.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "soc/gpio_num.h"

void pwm_Breathled_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .deconfigure     = false,
        .timer_num       = LEDC_TIMER_0,
        .clk_cfg         = LEDC_AUTO_CLK,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .freq_hz         = 1000
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = 0,
        .gpio_num   = GPIO_NUM_1,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_sel  = LEDC_TIMER_0,
    };
    ledc_channel_config(&ch_cfg);
}

void pwm_breath_task(void *pvParam)
{
    pwm_Breathled_init();

    while (1) {
        for (int duty = 0; duty < 256; duty += 2) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        for (int duty = 255; duty >= 0; duty -= 2) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
```

**功能说明：** 使用 LEDC 控制器，GPIO1 输出 PWM，占空比从 0→255→0 循环，实现呼吸灯效果。

---

### 4.2 ADC 电压采集

#### `adc.h`

```c
#ifndef __ADC_H_
#define __ADC_H_

void adc_get_task(void *pvParam);

#endif
```

#### `adc.c`

```c
/*
 * @Author: 思夜雪
 * @Date: 2026-06-12 19:38:09
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-12 22:14:13
 * @Description: 
 * @FilePath: \hello_world\main\Board\adc.c
 */
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled.h"
#include "soc/clk_tree_defs.h"

#define ADC_CHAN  ADC_CHANNEL_1   // GPIO2
#define ADC_UNIT  ADC_UNIT_1

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t cali_handle;

static void adc_init(void)
{   
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHAN, &chan_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .chan = ADC_CHAN,
        .unit_id = ADC_UNIT
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle));
}


static int adc_read_raw(void)
{
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHAN, &raw));
    return raw;
}
/*通道映射函数
static void adc_log_channel_io(adc_unit_t unit, adc_channel_t chan)
{
    int io;
    ESP_ERROR_CHECK(adc_oneshot_channel_to_io(unit, chan, &io));
    ESP_LOGI("ADC", "ADC%d_CH%d = GPIO%d", unit, chan, io);
}
*/
static int adc_read_voltage(void)
{
    int raw = adc_read_raw();
    int v ;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &v));
    return v;
}

void adc_get_task(void *x)
{
    adc_init();
    while(1)
    {
        int v = adc_read_voltage();
        oled_show_num(0, 0, v);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

**功能说明：** ADC1_CH1（GPIO2）采集电压，通过曲线校准转为 mV，每 500ms 刷新显示在 OLED 上。

---

### 4.3 OLED 显示屏 (SSD1306, I2C)

#### `oled.h`

```c
#ifndef __OLED_H_
#define __OLED_H_

#include <stdbool.h>

#define OLED_W       128
#define OLED_H       64

void oled_init(void);
void oled_clear(void);
void oled_show_char(int x, int y, char c);
void oled_show_string(int x, int y, const char *str);
void oled_show_num(int x, int y, int num);

#endif
```

#### `oled.c`

```c
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"
#include "oled.h"
#include "oled_font.h"

static const char *TAG = "oled";

#define I2C_PORT      I2C_NUM_1
#define OLED_SCL      GPIO_NUM_17
#define OLED_SDA      GPIO_NUM_18
#define OLED_ADDR     0x3C

static uint8_t fb[OLED_W * OLED_H / 8];

/* ========== 同步 I2C 底层 (旧版驱动, 阻塞式, 无异步队列) ========== */

static void oled_write_cmd(uint8_t cmd)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    i2c_master_write_byte(link, cmd, true);
    i2c_master_stop(link);
    ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_PORT, link, pdMS_TO_TICKS(100)));
    i2c_cmd_link_delete(link);
}

static void oled_write_cmd1(uint8_t cmd, uint8_t p1)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    i2c_master_write_byte(link, cmd, true);
    i2c_master_write_byte(link, p1, true);
    i2c_master_stop(link);
    ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_PORT, link, pdMS_TO_TICKS(100)));
    i2c_cmd_link_delete(link);
}

static void oled_write_cmd2(uint8_t cmd, uint8_t p1, uint8_t p2)
{
    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x00, true);
    i2c_master_write_byte(link, cmd, true);
    i2c_master_write_byte(link, p1, true);
    i2c_master_write_byte(link, p2, true);
    i2c_master_stop(link);
    ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_PORT, link, pdMS_TO_TICKS(100)));
    i2c_cmd_link_delete(link);
}

/* ========== 帧缓冲刷新 ========== */

static void oled_flush(void)
{
    oled_write_cmd2(0x21, 0x00, 0x7F);  // 列地址 0~127
    oled_write_cmd2(0x22, 0x00, 0x07);  // 页地址 0~7

    i2c_cmd_handle_t link = i2c_cmd_link_create();
    i2c_master_start(link);
    i2c_master_write_byte(link, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(link, 0x40, true);  // 控制字节: 数据模式
    i2c_master_write(link, fb, sizeof(fb), true);
    i2c_master_stop(link);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, link, pdMS_TO_TICKS(500));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(ret));
    }
    i2c_cmd_link_delete(link);
}

/* ========== 字符写入 (仅改 fb, 不刷新) ========== */

static void oled_write_char(int x, int y, char c)
{
    if (x < 0 || x >= MAX_COL || y < 0 || y >= MAX_ROW) return;
    if (c < 32 || c > 126) c = ' ';
    int idx = c - 32;
    int x0 = x * FONT_W;
    int page = y * 2;

    for (int col = 0; col < 8; col++) {
        fb[page * OLED_W + x0 + col] = font_8x16[idx][col];
        fb[(page + 1) * OLED_W + x0 + col] = font_8x16[idx][col + 8];
    }
}

/* ========== 初始化 ========== */

void oled_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    // SSD1306 初始化序列
    oled_write_cmd(0xAE);
    oled_write_cmd1(0xD5, 0x80);
    oled_write_cmd1(0xA8, 0x3F);
    oled_write_cmd1(0xD3, 0x00);
    oled_write_cmd(0x40);
    oled_write_cmd1(0x8D, 0x14);
    oled_write_cmd1(0x20, 0x00);
    oled_write_cmd(0xA1);
    oled_write_cmd(0xC8);
    oled_write_cmd1(0xDA, 0x12);
    oled_write_cmd1(0x81, 0x7F);
    oled_write_cmd1(0xD9, 0xF1);
    oled_write_cmd1(0xDB, 0x40);
    oled_write_cmd(0xA4);
    oled_write_cmd(0xA6);
    oled_write_cmd(0xAF);

    vTaskDelay(pdMS_TO_TICKS(10));
    memset(fb, 0, sizeof(fb));
    oled_flush();
    ESP_LOGI(TAG, "init done");
}

/* ========== 公开接口 ========== */

void oled_clear(void)
{
    memset(fb, 0, sizeof(fb));
    oled_flush();
}

void oled_show_char(int x, int y, char c)
{
    oled_write_char(x, y, c);
    oled_flush();
}

void oled_show_string(int x, int y, const char *str)
{
    if (!str) return;
    int col = x, row = y;
    while (*str) {
        if (*str == '\n') {
            col = 0;
            row++;
            str++;
            continue;
        }
        oled_write_char(col, row, *str);
        col++;
        if (col >= MAX_COL) { col = 0; row++; }
        if (row >= MAX_ROW) break;
        str++;
    }
    oled_flush();
}

void oled_show_num(int x, int y, int num)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", num);
    /* 用空格填充尾部, 清除上一次的残留字符 */
    int len = strlen(buf);
    if (len < (int)sizeof(buf) - 1) {
        memset(buf + len, ' ', sizeof(buf) - len - 1);
        buf[sizeof(buf) - 1] = '\0';
    }
    oled_show_string(x, y, buf);
}
```

**功能说明：**
- 使用 I2C_NUM_1，SCL=GPIO17, SDA=GPIO18
- 基于帧缓冲机制（1KB），修改后一次性刷新
- 支持字符、字符串、数字显示
- 8x16 像素字体，屏幕分 16 列 × 4 行

---

#### `oled_font.h` — 8x16 ASCII 字库

```c
/* 8x16 ASCII 字库 (32~126), 每字符 16 字节, 前 8 字节 = 上半, 后 8 = 下半, LSB = 顶部 */
#ifndef __OLED_FONT_H_
#define __OLED_FONT_H_

#include <stdint.h>

#define FONT_W   8       // 字符宽
#define FONT_H  16       // 字符高
#define COL_W   FONT_W   // 列间距 = 8
#define ROW_H   FONT_H        // 行间距 = 16
#define MAX_COL (OLED_W / COL_W)  // 16
#define MAX_ROW (OLED_H / ROW_H)  // =4

static const uint8_t font_8x16[95][16] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*  32 ' ' */
    {0x00,0x00,0x00,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x33,0x30,0x00,0x00,0x00}, /*  33 '!' */
    {0x00,0x10,0x0C,0x06,0x10,0x0C,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*  34 '"' */
    {0x40,0xC0,0x78,0x40,0xC0,0x78,0x40,0x00,0x04,0x3F,0x04,0x04,0x3F,0x04,0x04,0x00}, /*  35 '#' */
    {0x00,0x70,0x88,0xFC,0x08,0x30,0x00,0x00,0x00,0x18,0x20,0xFF,0x21,0x1E,0x00,0x00}, /*  36 '$' */
    {0xF0,0x08,0xF0,0x00,0xE0,0x18,0x00,0x00,0x00,0x21,0x1C,0x03,0x1E,0x21,0x1E,0x00}, /*  37 '%' */
    {0x00,0xF0,0x08,0x88,0x70,0x00,0x00,0x00,0x1E,0x21,0x23,0x24,0x19,0x27,0x21,0x10}, /*  38 '&' */
    {0x10,0x16,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*  39 ''' */
    {0x00,0x00,0x00,0xE0,0x18,0x04,0x02,0x00,0x00,0x00,0x00,0x07,0x18,0x20,0x40,0x00}, /*  40 '(' */
    {0x00,0x02,0x04,0x18,0xE0,0x00,0x00,0x00,0x00,0x40,0x20,0x18,0x07,0x00,0x00,0x00}, /*  41 ')' */
    {0x40,0x40,0x80,0xF0,0x80,0x40,0x40,0x00,0x02,0x02,0x01,0x0F,0x01,0x02,0x02,0x00}, /*  42 '*' */
    {0x00,0x00,0x00,0xF0,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x1F,0x01,0x01,0x00,0x00}, /*  43 '+' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xB0,0x70,0x00,0x00,0x00,0x00}, /*  44 ',' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01}, /*  45 '-' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x00,0x00}, /*  46 '.' */
    {0x00,0x00,0x00,0x00,0x80,0x60,0x18,0x04,0x00,0x60,0x18,0x06,0x01,0x00,0x00,0x00}, /*  47 '/' */
    {0x00,0xE0,0x10,0x08,0x08,0x10,0xE0,0x00,0x00,0x0F,0x10,0x20,0x20,0x10,0x0F,0x00}, /*  48 '0' */
    {0x00,0x10,0x10,0xF8,0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x3F,0x20,0x20,0x00,0x00}, /*  49 '1' */
    {0x00,0x70,0x08,0x08,0x08,0x88,0x70,0x00,0x00,0x30,0x28,0x24,0x22,0x21,0x30,0x00}, /*  50 '2' */
    {0x00,0x30,0x08,0x88,0x88,0x48,0x30,0x00,0x00,0x18,0x20,0x20,0x20,0x11,0x0E,0x00}, /*  51 '3' */
    {0x00,0x00,0xC0,0x20,0x10,0xF8,0x00,0x00,0x00,0x07,0x04,0x24,0x24,0x3F,0x24,0x00}, /*  52 '4' */
    {0x00,0xF8,0x08,0x88,0x88,0x08,0x08,0x00,0x00,0x19,0x21,0x20,0x20,0x11,0x0E,0x00}, /*  53 '5' */
    {0x00,0xE0,0x10,0x88,0x88,0x18,0x00,0x00,0x00,0x0F,0x11,0x20,0x20,0x11,0x0E,0x00}, /*  54 '6' */
    {0x00,0x38,0x08,0x08,0xC8,0x38,0x08,0x00,0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /*  55 '7' */
    {0x00,0x70,0x88,0x08,0x08,0x88,0x70,0x00,0x00,0x1C,0x22,0x21,0x21,0x22,0x1C,0x00}, /*  56 '8' */
    {0x00,0xE0,0x10,0x08,0x08,0x10,0xE0,0x00,0x00,0x00,0x31,0x22,0x22,0x11,0x0F,0x00}, /*  57 '9' */
    {0x00,0x00,0x00,0xC0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x30,0x00,0x00,0x00}, /*  58 ':' */
    {0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xE0,0x60,0x00,0x00,0x00}, /*  59 ';' */
    {0x00,0x00,0x80,0x40,0x20,0x10,0x08,0x00,0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x00}, /*  60 '<' */
    {0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x00}, /*  61 '=' */
    {0x00,0x08,0x10,0x20,0x40,0x80,0x00,0x00,0x00,0x20,0x10,0x08,0x04,0x02,0x01,0x00}, /*  62 '>' */
    {0x00,0x70,0x48,0x08,0x08,0x08,0xF0,0x00,0x00,0x00,0x00,0x30,0x36,0x01,0x00,0x00}, /*  63 '?' */
    {0xC0,0x30,0xC8,0x28,0xE8,0x10,0xE0,0x00,0x07,0x18,0x27,0x24,0x23,0x14,0x0B,0x00}, /*  64 '@' */
    {0x00,0x00,0xC0,0x38,0xE0,0x00,0x00,0x00,0x20,0x3C,0x23,0x02,0x02,0x27,0x38,0x20}, /*  65 'A' */
    {0x08,0xF8,0x88,0x88,0x88,0x70,0x00,0x00,0x20,0x3F,0x20,0x20,0x20,0x11,0x0E,0x00}, /*  66 'B' */
    {0xC0,0x30,0x08,0x08,0x08,0x08,0x38,0x00,0x07,0x18,0x20,0x20,0x20,0x10,0x08,0x00}, /*  67 'C' */
    {0x08,0xF8,0x08,0x08,0x08,0x10,0xE0,0x00,0x20,0x3F,0x20,0x20,0x20,0x10,0x0F,0x00}, /*  68 'D' */
    {0x08,0xF8,0x88,0x88,0xE8,0x08,0x10,0x00,0x20,0x3F,0x20,0x20,0x23,0x20,0x18,0x00}, /*  69 'E' */
    {0x08,0xF8,0x88,0x88,0xE8,0x08,0x10,0x00,0x20,0x3F,0x20,0x00,0x03,0x00,0x00,0x00}, /*  70 'F' */
    {0xC0,0x30,0x08,0x08,0x08,0x38,0x00,0x00,0x07,0x18,0x20,0x20,0x22,0x1E,0x02,0x00}, /*  71 'G' */
    {0x08,0xF8,0x08,0x00,0x00,0x08,0xF8,0x08,0x20,0x3F,0x21,0x01,0x01,0x21,0x3F,0x20}, /*  72 'H' */
    {0x00,0x08,0x08,0xF8,0x08,0x08,0x00,0x00,0x00,0x20,0x20,0x3F,0x20,0x20,0x00,0x00}, /*  73 'I' */
    {0x00,0x00,0x08,0x08,0xF8,0x08,0x08,0x00,0xC0,0x80,0x80,0x80,0x7F,0x00,0x00,0x00}, /*  74 'J' */
    {0x08,0xF8,0x88,0xC0,0x28,0x18,0x08,0x00,0x20,0x3F,0x20,0x01,0x26,0x38,0x20,0x00}, /*  75 'K' */
    {0x08,0xF8,0x08,0x00,0x00,0x00,0x00,0x00,0x20,0x3F,0x20,0x20,0x20,0x20,0x30,0x00}, /*  76 'L' */
    {0x08,0xF8,0xF8,0x00,0xF8,0xF8,0x08,0x00,0x20,0x3F,0x00,0x3F,0x00,0x3F,0x20,0x00}, /*  77 'M' */
    {0x08,0xF8,0x30,0xC0,0x00,0xF8,0x08,0x00,0x20,0x3F,0x20,0x00,0x07,0x18,0x3F,0x00}, /*  78 'N' */
    {0xE0,0x10,0x08,0x08,0x08,0x10,0xE0,0x00,0x0F,0x10,0x20,0x20,0x20,0x10,0x0F,0x00}, /*  79 'O' */
    {0x08,0xF8,0x08,0x08,0x08,0x08,0xF0,0x00,0x20,0x3F,0x21,0x01,0x01,0x01,0x00,0x00}, /*  80 'P' */
    {0xE0,0x10,0x08,0x08,0x08,0x10,0xE0,0x00,0x0F,0x18,0x24,0x24,0x38,0x50,0x4F,0x00}, /*  81 'Q' */
    {0x08,0xF8,0x88,0x88,0x88,0x88,0x70,0x00,0x20,0x3F,0x20,0x00,0x03,0x0C,0x30,0x20}, /*  82 'R' */
    {0x00,0x70,0x88,0x08,0x08,0x08,0x38,0x00,0x00,0x38,0x20,0x21,0x21,0x22,0x1C,0x00}, /*  83 'S' */
    {0x18,0x08,0x08,0xF8,0x08,0x08,0x18,0x00,0x00,0x00,0x20,0x3F,0x20,0x00,0x00,0x00}, /*  84 'T' */
    {0x08,0xF8,0x08,0x00,0x00,0x08,0xF8,0x08,0x00,0x1F,0x20,0x20,0x20,0x20,0x1F,0x00}, /*  85 'U' */
    {0x08,0x78,0x88,0x00,0x00,0xC8,0x38,0x08,0x00,0x00,0x07,0x38,0x3E,0x07,0x00,0x00}, /*  86 'V' */
    {0xF8,0x08,0x00,0xF8,0x00,0x08,0xF8,0x00,0x03,0x3C,0x07,0x00,0x07,0x3C,0x03,0x00}, /*  87 'W' */
    {0x08,0x18,0x68,0x80,0x80,0x68,0x18,0x08,0x20,0x30,0x2C,0x03,0x03,0x2C,0x30,0x20}, /*  88 'X' */
    {0x08,0x38,0xC8,0x00,0xC8,0x38,0x08,0x00,0x00,0x00,0x20,0x3F,0x20,0x00,0x00,0x00}, /*  89 'Y' */
    {0x10,0x08,0x08,0x08,0xC8,0x38,0x08,0x00,0x20,0x38,0x26,0x21,0x20,0x20,0x18,0x00}, /*  90 'Z' */
    {0x00,0x00,0x00,0xFE,0x02,0x02,0x02,0x00,0x00,0x00,0x00,0x7F,0x40,0x40,0x40,0x00}, /*  91 '[' */
    {0x00,0x0C,0x30,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x06,0x38,0xC0,0x00}, /*  92 '\' */
    {0x00,0x02,0x02,0x02,0xFE,0x00,0x00,0x00,0x00,0x40,0x40,0x40,0x7F,0x00,0x00,0x00}, /*  93 ']' */
    {0x00,0x00,0x04,0x02,0x02,0x02,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*  94 '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80}, /*  95 '_' */
    {0x00,0x02,0x02,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*  96 '`' */
    {0x00,0x00,0x80,0x80,0x80,0x80,0x00,0x00,0x00,0x19,0x24,0x22,0x22,0x22,0x3F,0x20}, /*  97 'a' */
    {0x08,0xF8,0x00,0x80,0x80,0x00,0x00,0x00,0x00,0x3F,0x11,0x20,0x20,0x11,0x0E,0x00}, /*  98 'b' */
    {0x00,0x00,0x00,0x80,0x80,0x80,0x00,0x00,0x00,0x0E,0x11,0x20,0x20,0x20,0x11,0x00}, /*  99 'c' */
    {0x00,0x00,0x00,0x80,0x80,0x88,0xF8,0x00,0x00,0x0E,0x11,0x20,0x20,0x10,0x3F,0x20}, /* 100 'd' */
    {0x00,0x00,0x80,0x80,0x80,0x80,0x00,0x00,0x00,0x1F,0x22,0x22,0x22,0x22,0x13,0x00}, /* 101 'e' */
    {0x00,0x80,0xF0,0x88,0x88,0x88,0x18,0x00,0x00,0x20,0x3F,0x20,0x20,0x00,0x00,0x00}, /* 102 'f' */
    {0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x00,0x00,0x6B,0x94,0x94,0x94,0x93,0x60,0x00}, /* 103 'g' */
    {0x08,0xF8,0x00,0x80,0x80,0x80,0x00,0x00,0x20,0x3F,0x21,0x00,0x00,0x20,0x3F,0x20}, /* 104 'h' */
    {0x00,0x80,0x98,0x98,0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x3F,0x20,0x20,0x00,0x00}, /* 105 'i' */
    {0x00,0x00,0x00,0x80,0x98,0x98,0x00,0x00,0x00,0xC0,0x80,0x80,0x80,0x7F,0x00,0x00}, /* 106 'j' */
    {0x08,0xF8,0x00,0x00,0x80,0x80,0x80,0x00,0x20,0x3F,0x24,0x02,0x2D,0x30,0x20,0x00}, /* 107 'k' */
    {0x00,0x08,0x08,0xF8,0x00,0x00,0x00,0x00,0x00,0x20,0x20,0x3F,0x20,0x20,0x00,0x00}, /* 108 'l' */
    {0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x00,0x20,0x3F,0x20,0x00,0x3F,0x20,0x00,0x3F}, /* 109 'm' */
    {0x80,0x80,0x00,0x80,0x80,0x80,0x00,0x00,0x20,0x3F,0x21,0x00,0x00,0x20,0x3F,0x20}, /* 110 'n' */
    {0x00,0x00,0x80,0x80,0x80,0x80,0x00,0x00,0x00,0x0E,0x11,0x20,0x20,0x11,0x0E,0x00}, /* 111 'o' */
    {0x80,0x80,0x00,0x80,0x80,0x00,0x00,0x00,0x80,0xFF,0x91,0x20,0x20,0x11,0x0E,0x00}, /* 112 'p' */
    {0x00,0x00,0x00,0x80,0x80,0x00,0x80,0x80,0x00,0x0E,0x11,0x20,0x20,0x91,0xFF,0x80}, /* 113 'q' */
    {0x80,0x80,0x80,0x00,0x80,0x80,0x80,0x00,0x20,0x20,0x3F,0x21,0x20,0x00,0x01,0x00}, /* 114 'r' */
    {0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x00,0x00,0x33,0x24,0x24,0x24,0x24,0x19,0x00}, /* 115 's' */
    {0x00,0x80,0xE0,0x80,0x80,0x00,0x00,0x00,0x00,0x00,0x1F,0x20,0x20,0x00,0x00,0x00}, /* 116 't' */
    {0x80,0x80,0x00,0x00,0x00,0x80,0x80,0x00,0x00,0x1F,0x20,0x20,0x20,0x10,0x3F,0x20}, /* 117 'u' */
    {0x80,0x80,0x80,0x00,0x00,0x80,0x80,0x80,0x00,0x01,0x0E,0x30,0x3C,0x06,0x01,0x00}, /* 118 'v' */
    {0x80,0x80,0x00,0x80,0x00,0x80,0x80,0x80,0x00,0x0F,0x30,0x0C,0x03,0x0C,0x30,0x0F}, /* 119 'w' */
    {0x80,0x80,0x80,0x00,0x80,0x80,0x80,0x00,0x20,0x31,0x2A,0x04,0x2A,0x31,0x20,0x00}, /* 120 'x' */
    {0x80,0x80,0x80,0x00,0x00,0x80,0x80,0x80,0x80,0x81,0x8E,0x70,0x18,0x06,0x01,0x00}, /* 121 'y' */
    {0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x00,0x00,0x21,0x30,0x2C,0x22,0x21,0x30,0x00}, /* 122 'z' */
    {0x00,0x00,0x00,0x00,0x80,0x7C,0x02,0x02,0x00,0x00,0x00,0x00,0x00,0x3F,0x40,0x40}, /* 123 '{' */
    {0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00}, /* 124 '|' */
    {0x00,0x02,0x02,0x7C,0x80,0x00,0x00,0x00,0x00,0x40,0x40,0x3F,0x00,0x00,0x00,0x00}, /* 125 '}' */
    {0x00,0x06,0x01,0x01,0x02,0x02,0x04,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 '~' */
};

#endif
```

---

### 4.4 DHT11 温湿度传感器 (RMT 驱动)

#### `dht11.h`

```c
#ifndef __DHT11_H_
#define __DHT11_H_

#include "driver/gpio.h"
#include "driver/rmt_common.h"
#include "driver/rmt_rx.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "hal/gpio_types.h"
#include "portmacro.h"
#include "soc/clk_tree_defs.h"
#include "soc/gpio_num.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "oled.h"

typedef struct {
    float tem;
    float hum;
} dht11_data_t;

void dht11_task(void *x);
esp_err_t dht11_init(gpio_num_t gpio);
esp_err_t dht11_read(dht11_data_t *data);

#endif
```

#### `dht11.c`

```c
/*
 * @Author: 思夜雪
 * @Date: 2026-06-12 23:04:55
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-16 22:06:30
 * @Description: 
 * @FilePath: \hello_world\main\Board\dht11.c
 */
#include "dht11.h"

static const char *TAG = "dht11";

static rmt_channel_handle_t  rx_chan      = NULL;
static rmt_symbol_word_t     symbols[128];
static SemaphoreHandle_t     rx_sem       = NULL;
static volatile size_t       rx_sym_count = 0;
static gpio_num_t            dht_gpio     = GPIO_NUM_NC;
static bool                  installed    = false;

static bool rmt_rx_done_callback(rmt_channel_handle_t rx_chan, const rmt_rx_done_event_data_t *edata, void *user_ctx)
{
    rx_sym_count = edata->num_symbols;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(rx_sem, &wake);
    return (wake == pdTRUE);
}

#define DHT11_us 45 //高低电平判断阈值
static esp_err_t dht11_data_analyse(const rmt_symbol_word_t *sym,size_t count,dht11_data_t *out)
{
    if(count < 41)
    {
        ESP_LOGW(TAG, "DHT11采集数据数量错误");
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t byte[5] = {0};

    for(int i =0; i<40; i++)
    {
        uint16_t dur = sym[i+1].duration0;
        if(dur > DHT11_us)
        {
            byte[i / 8] |=  (1 << (7 - (i % 8)));
        }
    }

    if(byte[0] + byte[1] + byte[2] + byte[3] != byte[4])
    {
        ESP_LOGE(TAG,"校验和错误");
        return ESP_ERR_INVALID_CRC;
    }

    out -> hum = (float)byte[0];
    out -> tem = (float)byte[2];

    return ESP_OK; 
}

esp_err_t dht11_init(gpio_num_t gpio)
{
    if(installed) return ESP_OK;
    dht_gpio = gpio;

    rx_sem = xSemaphoreCreateBinary();
    if(!rx_sem) return ESP_ERR_NO_MEM;

    rmt_rx_channel_config_t rx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = dht_gpio,
        .intr_priority = 0,
        .mem_block_symbols = 128,
        .resolution_hz = 1*1000*1000,
        .flags.invert_in = 0,
        .flags.io_loop_back = 0,
        .flags.with_dma = 0
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx_chan));

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, NULL));

    ESP_ERROR_CHECK(rmt_enable(rx_chan)); 

    installed = true;
    ESP_LOGI(TAG,"init gpio%d",gpio);
    return ESP_OK;
}

esp_err_t dht11_read(dht11_data_t *data)
{
    if(!installed) return ESP_ERR_INVALID_STATE;
    rmt_disable(rx_chan);

    /* 用 set_direction 而非 gpio_config, 避免破坏 RMT 的 GPIO 矩阵连接 */
    gpio_set_direction(dht_gpio, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(dht_gpio, GPIO_PULLUP_ONLY);

    gpio_set_level(dht_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(18));
    gpio_set_level(dht_gpio, 1);
    esp_rom_delay_us(30);

    
    /* 启动 RMT*/
    xSemaphoreTake(rx_sem, 0);
    rmt_enable(rx_chan);

    rmt_receive_config_t rec_cfg = {
        .signal_range_min_ns = 2000,
        .signal_range_max_ns = 100000
    };
    ESP_ERROR_CHECK(rmt_receive(rx_chan, symbols, sizeof(symbols), &rec_cfg));

    /* 释放总线，DHT11 开始回应时 RMT 未就绪 */
    gpio_set_direction(dht_gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(dht_gpio, GPIO_PULLUP_ONLY);
    
    if(xSemaphoreTake(rx_sem, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "超时未取得信号量");
        return ESP_ERR_TIMEOUT;
    }

    return (dht11_data_analyse(symbols, rx_sym_count, data));
}
```

**功能说明：**
- 使用 RMT 外设读取 DHT11 单总线协议时序
- 分辨率为 1μs，通过脉冲宽度判断数据位 0/1
- 包含校验和验证
- 中断服务中使用信号量通知读取完成

---

## 5. Network 层 — WiFi & MQTT 物联网

### 5.1 WiFi STA 连接

#### `wifi.h`

```c
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
```

#### `wifi.c`

```c
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
```

**功能说明：**
- 连接 WiFi 热点 `jinse`（密码：`20040625`）
- 断线自动重连（指数退避，最多 10 次）
- 通过事件组通知连接状态，支持超时等待

---

### 5.2 MQTT 连接 OneNET

#### `mqtt.h`

```c
#ifndef __MQTT_H_
#define __MQTT_H_

void mqtt_init(void);
int mqtt_publish(const char *topic,const char *data);

#endif
```

#### `mqtt.c`

```c
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

#define MQTT_BROKER_URI    "mqtt://218.201.45.2:1883"

static void mqtt_event_handle (void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
{
    esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG,"MQTT CONNECTED");
            /* 订阅响应主题，看 OneNET 返回什么 */
            esp_mqtt_client_subscribe(mqtt_handle, "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/dp/post/json/accepted", 0);
            esp_mqtt_client_subscribe(mqtt_handle, "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/dp/post/json/rejected", 0);
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
```

---

### 5.3 传感器数据上报任务

#### `sensor_task.h`

```c
#ifndef __SENSOR_TASK_H_
#define __SENSOR_TASK_H_

void sensor_task(void *pvParam);

#endif
```

#### `sensor_task.c`

```c
/*
 * @Author: 思夜雪
 * @Date: 2026-06-16 20:55:19
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-16 23:13:34
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

#define sensor_data_t dht11_data_t
#define TOPIC  "$sys/d17QeNOY7J/ESP32/dp/post/json"

static const char * TAG = "sensor";

static void sensor_data_to_json(const sensor_data_t *d, char *buf, size_t size)
{
    snprintf(buf, size,
             "{"
             "\"id\":%lu,"
             "\"dp\":{"
             "\"temp_value\":[{\"v\":%.1f}],"
             "\"humidity_value\":[{\"v\":%d}]"
             "}"
             "}",
             (unsigned long)(esp_timer_get_time() / 1000),
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
```

**功能说明：**
- 每 2 秒读取一次 DHT11（GPIO4）
- 将温湿度数据打包为 JSON
- 通过 MQTT 发布到 OneNET 平台
- JSON 格式：`{"id":时间戳,"dp":{"temp_value":[{"v":温度}],"humidity_value":[{"v":湿度}]}}`

---

## 6. 测试

### `pytest_hello_world.py`

```python
# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0

import hashlib
import logging
from typing import Callable

import pytest
from pytest_embedded_idf.dut import IdfDut
from pytest_embedded_qemu.app import QemuApp
from pytest_embedded_qemu.dut import QemuDut


@pytest.mark.supported_targets
@pytest.mark.preview_targets
@pytest.mark.generic
def test_hello_world(
    dut: IdfDut, log_minimum_free_heap_size: Callable[..., None]
) -> None:
    dut.expect('Hello world!')
    log_minimum_free_heap_size()


@pytest.mark.linux
@pytest.mark.host_test
def test_hello_world_linux(dut: IdfDut) -> None:
    dut.expect('Hello world!')


def verify_elf_sha256_embedding(app: QemuApp, sha256_reported: str) -> None:
    sha256 = hashlib.sha256()
    with open(app.elf_file, 'rb') as f:
        sha256.update(f.read())
    sha256_expected = sha256.hexdigest()

    logging.info(f'ELF file SHA256: {sha256_expected}')
    logging.info(f'ELF file SHA256 (reported by the app): {sha256_reported}')

    # the app reports only the first several hex characters of the SHA256, check that they match
    if not sha256_expected.startswith(sha256_reported):
        raise ValueError('ELF file SHA256 mismatch')


@pytest.mark.esp32  # we only support qemu on esp32 for now
@pytest.mark.host_test
@pytest.mark.qemu
def test_hello_world_host(app: QemuApp, dut: QemuDut) -> None:
    sha256_reported = (
        dut.expect(r'ELF file SHA256:\s+([a-f0-9]+)').group(1).decode('utf-8')
    )
    verify_elf_sha256_embedding(app, sha256_reported)

    dut.expect('Hello world!')
```

---

## 硬件引脚定义

| 功能 | GPIO | 外设 |
|------|------|------|
| PWM 呼吸灯 | GPIO1 | LEDC_CH0 |
| ADC 输入 | GPIO2 | ADC1_CH1 |
| DHT11 数据 | GPIO4 | RMT RX |
| OLED SCL | GPIO17 | I2C_NUM_1 |
| OLED SDA | GPIO18 | I2C_NUM_1 |

---

## 依赖组件

```
REQUIRES esp_driver_ledc esp_driver_gpio esp_adc esp_driver_i2c esp_lcd
         esp_driver_rmt esp_wifi esp_netif nvs_flash esp_event mqtt
```
