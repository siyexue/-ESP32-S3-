/*
 * @Author: 思夜雪
 * @Date: 2026-06-17
 * @Description: BLE 调光模块完整实现
 *               GATT: Service 0xFFE0 → Char 0xFFE1 (Read+Write)
 *               手机写入 0-100 → 控制 LED 亮度
 * @FilePath: \hello_world\main\Board\ble.c
 */

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_device.h"
#include "pwm.h"
#include <string.h>

static const char *TAG = "BLE";

/* ========== 用户可配置参数 ========== */
#define GATTS_SERVICE_UUID      0xFFE0
#define GATTS_CHAR_UUID         0xFFE1
#define GATTS_DEVICE_NAME       "ESP32-S3-LED"

/* ========== 内部状态 ========== */
static uint16_t conn_id     = 0;
static uint16_t char_handle = 0;
static uint8_t  brightness  = 0;

/* ========== 前向声明 ========== */
static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param);
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param);

/* ========== GAP 回调 ========== */

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    ESP_LOGI(TAG, "GAP event: %d", event);

    switch (event) {

    /* 广播数据配置完成（结构体方式） */
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    /* 广播数据配置完成（原始字节方式） */
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
    {
        ESP_LOGI(TAG, "adv data ready, starting adv...");

        esp_ble_adv_params_t adv = {
            .adv_int_min         = 0x100,
            .adv_int_max         = 0x200,
            .adv_type            = ADV_TYPE_IND,
            .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
            .channel_map         = ADV_CHNL_ALL,
            .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
        };
        esp_err_t ret = esp_ble_gap_start_advertising(&adv);
        ESP_LOGI(TAG, "start adv ret=%d", ret);
        break;
    }

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "adv start complete, status=%d", param->adv_start_cmpl.status);
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "advertise start failed");
        }
        break;

    default:
        break;
    }
}

/* ========== GATT 回调 ========== */

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                 esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    /* ① 应用注册完成 → 创建 Service */
    case ESP_GATTS_REG_EVT:
    {
        ESP_LOGI(TAG, "REG_EVT");
        esp_gatt_srvc_id_t srvc_id = {
            .id = {
                .uuid = {
                    .len = ESP_UUID_LEN_16,
                    .uuid.uuid16 = GATTS_SERVICE_UUID,
                },
                .inst_id = 0,
            },
            .is_primary = true,
        };
        esp_ble_gatts_create_service(gatts_if, &srvc_id, 4);
        break;
    }

    /* ② Service 创建完成 → 添加 Characteristic */
    case ESP_GATTS_CREATE_EVT:
    {
        if (param->create.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "create service failed");
            break;
        }

        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid.uuid16 = GATTS_CHAR_UUID,
        };

        /* Characteristic 初始值 = 0 */
        uint8_t init_val = 0;
        esp_attr_value_t char_val = {
            .attr_max_len = 1,
            .attr_len     = 1,
            .attr_value   = &init_val,
        };

        esp_attr_control_t ctrl = {
            .auto_rsp = ESP_GATT_AUTO_RSP,
        };

        esp_ble_gatts_add_char(
            param->create.service_handle,
            &char_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
            &char_val,
            &ctrl);
        break;
    }

    /* ③ Characteristic 添加完成 → 启动 Service + 广播 */
    case ESP_GATTS_ADD_CHAR_EVT:
    {
        char_handle = param->add_char.attr_handle;
        esp_ble_gatts_start_service(param->add_char.service_handle);
        ESP_LOGI(TAG, "service started, char_handle=%d", char_handle);

        /* 手动构造广播包（原始字节，绕过结构体兼容性问题） */
        /* 格式: [长度][AD类型][数据]... */
        uint8_t adv_data[] = {
            0x02, 0x01, 0x06,                         /* Flags: BLE General Discoverable */
            0x03, 0x03, 0xE0, 0xFF,                   /* Service UUID: 0xFFE0 (小端序) */
        };
        esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));

        /* 扫描响应包：设备名 */
        const char *name = GATTS_DEVICE_NAME;
        uint8_t name_len = strlen(name);
        uint8_t scan_rsp[32];
        scan_rsp[0] = name_len + 1;                   /* AD 长度 */
        scan_rsp[1] = 0x09;                           /* AD 类型: Complete Local Name */
        memcpy(&scan_rsp[2], name, name_len);
        esp_ble_gap_config_scan_rsp_data_raw(scan_rsp, name_len + 2);
        ESP_LOGI(TAG, "adv configured, name=%s", name);
        break;
    }

    /* ④ Service 启动完成（备用，万一 ③ 的广播配置没触发） */
    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG, "START_EVT received");
        break;

    /* ⑤ 手机连接 */
    case ESP_GATTS_CONNECT_EVT:
        conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "connected, conn_id=%d", conn_id);
        esp_ble_gap_stop_advertising();
        break;

    /* ⑥ 手机断开 → 重新广播 */
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG, "disconnected, restart advertising");
        {
            esp_ble_adv_params_t adv = {
                .adv_int_min         = 0x100,
                .adv_int_max         = 0x200,
                .adv_type            = ADV_TYPE_IND,
                .own_addr_type       = BLE_ADDR_TYPE_PUBLIC,
                .channel_map         = ADV_CHNL_ALL,
                .adv_filter_policy   = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
            };
            esp_ble_gap_start_advertising(&adv);
        }
        break;

    /* ⑦ ★ 手机写入 → 控制 LED 亮度 */
    case ESP_GATTS_WRITE_EVT:
    {
        uint8_t value = param->write.value[0];
        if (value <= 100) {
            brightness = (uint8_t)(value * 255 / 100);
        } else {
            brightness = value;
        }

        pwm_enable_breath(0);    /* BLE 控制时暂停呼吸灯 */
        pwm_set_duty(brightness);
        ESP_LOGI(TAG, "write: %d%% → duty=%d", value, brightness);
        break;
    }

    /* ⑧ 手机读取 */
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG, "read: duty=%d", brightness);
        break;

    default:
        break;
    }
}

/* ========== 对外接口 ========== */

void ble_init(void)
{
    esp_err_t ret;

    /* 1. 初始化 BT 控制器（BLE only） */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(TAG, "controller init fail"); return; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) { ESP_LOGE(TAG, "controller enable fail"); return; }

    /* 2. 初始化 Bluedroid 协议栈 */
    ret = esp_bluedroid_init();
    if (ret) { ESP_LOGE(TAG, "bluedroid init fail"); return; }
    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(TAG, "bluedroid enable fail"); return; }

    /* 3. 注册回调 */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) { ESP_LOGE(TAG, "gap register fail: %d", ret); return; }
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) { ESP_LOGE(TAG, "gatts register fail: %d", ret); return; }

    /* 4. 设置设备名 + 注册应用 */
    esp_ble_gap_set_device_name(GATTS_DEVICE_NAME);
    esp_ble_gatts_app_register(0);
    /*   ↑ 触发 REG_EVT → 创建 Service → 添加 Char → 启服务 → 广播 */

    ESP_LOGI(TAG, "init done, name=%s", GATTS_DEVICE_NAME);
}

uint8_t ble_get_brightness(void)
{
    return brightness;
}
