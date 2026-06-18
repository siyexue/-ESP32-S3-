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
