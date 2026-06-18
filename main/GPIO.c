/*
 * @Author: 鎬濆闆?
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
