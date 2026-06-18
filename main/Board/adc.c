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