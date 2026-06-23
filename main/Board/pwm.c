/*
 * @Author: 思夜雪
 * @Date: 2026-06-11 19:37:58
 * @LastEditors: Do not edit
 * @LastEditTime: 2026-06-17
 * @Description: PWM 呼吸灯 + BLE 调光控制
 * @FilePath: \hello_world\main\Board\pwm.c
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "soc/gpio_num.h"

/* 呼吸灯模式标志：0=呼吸循环，1=BLE 固定亮度 */
static volatile uint8_t s_ble_mode = 0;
static volatile uint8_t s_current_duty = 0;

void pwm_breath_led_init(void)
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

/* ========== BLE 控制接口 ========== */

void pwm_set_duty(uint8_t duty)
{
    s_current_duty = duty;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

uint8_t pwm_get_duty(void)
{
    return s_current_duty;
}

void pwm_enable_breath(uint8_t enable)
{
    s_ble_mode = (enable == 0) ? 1 : 0;
}

void pwm_breath_task(void *pvParam)
{
    pwm_breath_led_init();

    while (1) {
        /* BLE 模式下跳过呼吸循环，保持固定亮度 */
        if (s_ble_mode) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        for (int duty = 0; duty < 256; duty += 2) {
            if (s_ble_mode) break;  /* BLE 写入时立刻退出呼吸循环 */
            pwm_set_duty(duty);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        for (int duty = 255; duty >= 0; duty -= 2) {
            if (s_ble_mode) break;
            pwm_set_duty(duty);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}