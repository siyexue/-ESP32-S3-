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
