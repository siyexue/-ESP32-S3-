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
/*
for (int i = 0; i < 7; i++) {
    ESP_LOGI(TAG, "sym[%d]: L%d=%duS  L%d=%duS",
             i,
             symbols[i].level0, symbols[i].duration0,
             symbols[i].level1, symbols[i].duration1);
}
*/

    return (dht11_data_analyse(symbols, rx_sym_count, data));

    
}

/*
void dht11_task(void *x)
{
    dht11_data_t data;
    dht11_init(GPIO_NUM_4);
    //oled_show_string(0, 1, "Tem:");
    //oled_show_string(0, 2, "Hum:");
    while (1) 
    {
        if (dht11_read(&data) == ESP_OK)
        {
            printf("温度：%f\n",data.tem);
            printf("湿度：%f\n",data.hum);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
*/
