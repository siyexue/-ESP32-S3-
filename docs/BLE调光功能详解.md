# ESP32-S3 BLE 调光功能 —— 手把手从零实现

> 日期：2026-06-17
> 开发板：ESP32-S3 | IDE：VS Code + ESP-IDF v5.3.5 | 手机工具：nRF Connect
> 前置条件：已完成温湿度+WiFi+MQTT上报的工程

---

## 目录

1. [你要做什么](#一你要做什么)
2. [先搞懂 BLE 几个概念](#二先搞懂-ble-几个概念)
3. [代码架构](#三代码架构)
4. [Step 1：修改 pwm.c — 增加调光接口](#四step-1修改-pwmc--增加调光接口)
5. [Step 2：新建 ble.h — BLE 头文件](#五step-2新建-bleh--ble-头文件)
6. [Step 3：新建 ble.c — BLE 完整实现（可直接复制）](#六step-3新建-blesc--ble-完整实现可直接复制)
7. [Step 4：修改 hello_world_main.c](#七step-4修改-helloworldmainc)
8. [Step 5：修改 CMakeLists.txt](#八step-5修改-cmakeliststxt)
9. [Step 6：menuconfig 配置](#九step-6-menuconfig-配置)
10. [编译烧录 & 用手机测试](#十编译烧录--用手机测试)
11. [调试过程 & 踩坑记录（必读）](#十一调试过程--踩坑记录必读)
12. [常见问题排查](#十二常见问题排查)
13. [移植到其他项目](#十三移植到其他项目)
14. [完整代码一览](#十四完整代码一览)

---

## 一、你要做什么

在现有的 ESP32-S3 工程上增加一个功能：

```
手机 (nRF Connect)
    ↓ BLE 无线
ESP32-S3
    ├── 收到亮度值 (0-100)
    ├── 控制 GPIO1 上的 LED 亮度
    └── 写入时暂停呼吸灯
```

最终效果：打开手机 App，连上 ESP32，写一个数字就能调灯光亮度。

---

## 二、先搞懂 BLE 几个概念

BLE 就像一个小型文件系统，设备把自己能读/写的数据公开出来。

| BLE 概念 | 类比为文件系统 | 在这个项目里 |
|---------|--------------|------------|
| **GATT** | 整个文件系统框架 | BLE 设备的数据组织结构 |
| **Service（服务）** | 一个文件夹 | "调光服务"，UUID = `0xFFE0` |
| **Characteristic（特征）** | 一个文件 | "亮度值"，UUID = `0xFFE1` |
| **UUID** | 文件名/文件夹名 | 用 16 位或 128 位唯一标识 |
| **Property** | 文件权限 | 这个文件可读、可写 |
| **广播** | 举着牌子喊"我在这" | ESP32 不断喊"我叫 ESP32-S3-LED，我有调光服务" |

**工作流程：**

```
手机扫描 → 看到 "ESP32-S3-LED" → 点击连接
    → 发现 Service 0xFFE0
        → 发现 Characteristic 0xFFE1
            → 写入 0x32 (十进制50) → ESP32 收到 → LED 亮度变 50%
```

---

## 三、代码架构

新增 `Board/ble.c` + `Board/ble.h`，遵循工程现有的模块风格：

```
main/
├── hello_world_main.c          ← 入口：加一行 ble_init()
├── CMakeLists.txt              ← 加 Board/ble.c + bt 组件
├── Board/
│   ├── pwm.c / pwm.h           ← 改造：增加 pwm_set_duty()
│   ├── ble.c / ble.h           ← ★ 新增：BLE 完整实现
│   ├── dht11.c / dht11.h
│   ├── adc.c / adc.h
│   └── oled.c / oled.h
└── distance/
    ├── sensor_task.c
    ├── mqtt.c
    └── wifi.c
```

---

## 四、Step 1：修改 pwm.c — 增加调光接口

### pwm.h — 加函数声明

```c
#ifndef __PWM_H_
#define __PWM_H_

#include <stdint.h>

void pwm_breath_led_init(void);
void pwm_breath_task(void *pvParam);
void pwm_set_duty(uint8_t duty);     // ★ 新增：BLE 调光时调用
uint8_t pwm_get_duty(void);          // ★ 新增：获取当前亮度
void pwm_enable_breath(uint8_t enable);  // ★ 新增：0=暂停呼吸，1=恢复呼吸

#endif
```

### pwm.c — 增加调光控制函数

```c
/* pwm.c 头部新增 static 变量 */
static uint8_t s_ble_mode = 0;     /* 0=呼吸循环，1=BLE固定亮度 */
static uint8_t s_current_duty = 0;

/* 新增调光控制函数，放在呼吸灯初始化函数后面 */
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
```

**改动说明：**
- `pwm_set_duty()` — BLE 回调里调用它，直接设置 LED 亮度
- `pwm_get_duty()` — 手机读取当前亮度时返回
- `pwm_enable_breath(0)` — BLE 写入时暂停呼吸灯循环

---

## 五、Step 2：新建 ble.h — BLE 头文件

创建 `main/Board/ble.h`：

```c
/*
 * BLE 调光模块 — 手机通过 nRF Connect 写入亮度值控制 LED
 */

#ifndef __BLE_H_
#define __BLE_H_

#include <stdint.h>

void ble_init(void);
uint8_t ble_get_brightness(void);

#endif
```

`ble_init()` 一调用，ESP32 就开始广播「ESP32-S3-LED」，手机就能搜到。

---

## 六、Step 3：新建 ble.c — BLE 完整实现（可直接复制）

这是核心文件。创建 `main/Board/ble.c`：

```c
/*
 * BLE 调光模块完整实现
 * 使用 ESP-IDF Bluedroid API（create_service + add_char 方式）
 *
 * GATT 结构：
 *   Service UUID: 0xFFE0 (Primary Service)
 *     └── Characteristic UUID: 0xFFE1
 *         ├── Properties: Read + Write
 *         └── Value: 1 byte (0=灭, 255=全亮)
 *
 * 手机 nRF Connect 操作：
 *   扫描 → 连接 "ESP32-S3-LED" → 找 0xFFE0 Service
 *   → 向 0xFFE1 写入 0~100 (百分制) → LED 亮度变化
 *
 * ★ 重要：此代码适配 ESP-IDF v5.3.5，v4.x 用户需查阅下方「踩坑记录」
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

/* ========== GAP 回调（广播相关事件） ========== */

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                               esp_ble_gap_cb_param_t *param)
{
    switch (event) {

    /*
     * 广播数据设置完成 → 开始广播
     * 注意：raw API 触发的是 ADV_DATA_RAW_SET_COMPLETE_EVT（值=4）
     *       结构体 API 触发的是 ADV_DATA_SET_COMPLETE_EVT（值=0）
     *       两个都要处理，因为代码用了 raw API
     */
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
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
        ESP_LOGI(TAG, "advertising: %s", GATTS_DEVICE_NAME);
        break;
    }

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "advertise start failed");
        }
        break;

    default:
        break;
    }
}

/* ========== GATT 回调（服务注册/读写请求） ========== */

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
        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid.uuid16 = GATTS_CHAR_UUID,
        };

        /* Characteristic 初始值 */
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
            &char_val, &ctrl);
        break;
    }

    /* ③ Characteristic 添加完成 → 启动 Service + 配置广播 */
    case ESP_GATTS_ADD_CHAR_EVT:
    {
        char_handle = param->add_char.attr_handle;
        esp_ble_gatts_start_service(param->add_char.service_handle);
        ESP_LOGI(TAG, "service started, char_handle=%d", char_handle);

        /*
         * 配置广播数据（用原始字节方式，避免结构体兼容性问题）
         *
         * BLE 广播包最大 31 字节，放不了设备名。
         * 把设备名放到「扫描响应包」里。
         */
        uint8_t adv_data[] = {
            0x02, 0x01, 0x06,                   /* Flags: BLE General Discoverable */
            0x03, 0x03, 0xE0, 0xFF,             /* Service UUID: 0xFFE0 */
        };
        esp_ble_gap_config_adv_data_raw(adv_data, sizeof(adv_data));

        /* 扫描响应包：放设备名 */
        const char *name = GATTS_DEVICE_NAME;
        uint8_t name_len = strlen(name);
        uint8_t scan_rsp[32];
        scan_rsp[0] = name_len + 1;             /* AD 长度 */
        scan_rsp[1] = 0x09;                     /* AD 类型: Complete Local Name */
        memcpy(&scan_rsp[2], name, name_len);
        esp_ble_gap_config_scan_rsp_data_raw(scan_rsp, name_len + 2);
        break;
    }

    /* ④ 手机连接 */
    case ESP_GATTS_CONNECT_EVT:
        conn_id = param->connect.conn_id;
        ESP_LOGI(TAG, "connected, conn_id=%d", conn_id);
        esp_ble_gap_stop_advertising();
        break;

    /* ⑤ 手机断开 → 重新广播 */
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

    /* ⑥ ★ 手机写入 → 控制 LED 亮度 */
    case ESP_GATTS_WRITE_EVT:
    {
        uint8_t value = param->write.value[0];

        /* 手机写 0-100（百分制），映射到 PWM 0-255 */
        if (value <= 100) {
            brightness = (uint8_t)(value * 255 / 100);
        } else {
            brightness = value;        /* 直接当作占空比 */
        }

        pwm_enable_breath(0);          /* BLE 控制时暂停呼吸灯 */
        pwm_set_duty(brightness);
        ESP_LOGI(TAG, "write: %d%% → duty=%d", value, brightness);
        break;
    }

    /* ⑦ 手机读取 */
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

    /* 1. 初始化 BT 控制器（注意用 BLE only，不是 BTDM！） */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(TAG, "controller init fail"); return; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);  /* ← 必须 BLE 模式 */
    if (ret) { ESP_LOGE(TAG, "controller enable fail"); return; }

    /* 2. 初始化 Bluedroid 协议栈 */
    ret = esp_bluedroid_init();
    if (ret) { ESP_LOGE(TAG, "bluedroid init fail"); return; }
    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(TAG, "bluedroid enable fail"); return; }

    /* 3. 注册 GAP + GATT 回调 */
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) { ESP_LOGE(TAG, "gap register fail"); return; }
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) { ESP_LOGE(TAG, "gatts register fail"); return; }

    /* 4. 设置设备名 + 注册应用 */
    esp_ble_gap_set_device_name(GATTS_DEVICE_NAME);
    esp_ble_gatts_app_register(0);
    /*   ↑ 触发 REG_EVT → CREATE_EVT → ADD_CHAR_EVT → 广播 */

    ESP_LOGI(TAG, "init done, name=%s", GATTS_DEVICE_NAME);
}

uint8_t ble_get_brightness(void)
{
    return brightness;
}
```

### 代码执行流程

```
app_main() → ble_init()
    ↓
esp_ble_gatts_app_register(0)
    ↓ (异步回调)
ESP_GATTS_REG_EVT      → 创建 Service
    ↓
ESP_GATTS_CREATE_EVT   → 添加 Characteristic
    ↓
ESP_GATTS_ADD_CHAR_EVT → 启动 Service + 配置广播数据
    ↓
ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT → 开始广播
    ↓
手机扫描到 "ESP32-S3-LED" → 连接
    ↓
ESP_GATTS_CONNECT_EVT  → 停止广播（省电）
    ↓
手机写入 0x32 → ESP_GATTS_WRITE_EVT → pwm_set_duty(128)
    ↓
LED 亮度变化
```

---

## 七、Step 4：修改 hello_world_main.c

在文件头增加 `#include "Board/ble.h"`，在 `app_main()` 中加一行 `ble_init()`：

```c
#include "Board/ble.h"              // ★ 新增

// ...

void app_main(void)
{
    oled_init();
    oled_clear();
    wifi_init();

    // ...

    ble_init();                                         // ★ 新增

    xTaskCreate(pwm_breath_task, ...);
    // ...
}
```

---

## 八、Step 5：修改 CMakeLists.txt

两处改动：SRCS 加 `"Board/ble.c"`，REQUIRES 加 `bt`：

```cmake
idf_component_register(SRCS "GPIO.c" "hello_world_main.c"
                            "Board/pwm.c" "Board/adc.c" "Board/oled.c"
                            "Board/dht11.c"
                            "Board/ble.c"                  # ★ 新增
                            "distance/wifi.c" "distance/mqtt.c"
                            "distance/sensor_task.c"
                    INCLUDE_DIRS ""
                    REQUIRES esp_driver_ledc esp_driver_gpio esp_adc
                             esp_driver_i2c esp_lcd esp_driver_rmt
                             esp_wifi esp_netif nvs_flash esp_event mqtt
                             bt)                            # ★ 新增
```

---

## 九、Step 6：menuconfig 配置

蓝牙相关的配置必须手动打开，否则编译/链接会报错。

```bash
idf.py menuconfig
```

### 必开选项（按顺序操作）

**① 主开关：**
```
Component config → Bluetooth → [*] Bluetooth（按空格勾选）
```

**② Bluetooth host（回车进入）：**
```
Component config → Bluetooth → Bluetooth host
    → [*] Bluedroid（按空格勾选，然后回车进入子菜单）
```

**③ Bluedroid Options（回车进入子菜单）：**
```
Component config → Bluetooth → Bluedroid Options
    → [*] Enable BLE 4.2 advertising（勾上！否则链接报 undefined reference）
```

**④ Partition Table（固件太大，必须加大分区）：**
```
Partition Table → Partition Table → Single factory app (large)
```

> BLE 协议栈体积大，加上后固件超过 1MB，默认分区装不下。

### 保存退出

`S` → `Enter` → `Q`

---

## 十、编译烧录 & 用手机测试

### 编译

```bash
idf.py build
```

成功的话最后会显示 `Project build complete.`

### 烧录 + 看串口

```bash
idf.py flash monitor
```

串口出现下面几行就说明 BLE 正常工作：

```
BLE: REG_EVT
BLE: service started, char_handle=42
BLE: adv configured, name=ESP32-S3-LED
BLE: init done, name=ESP32-S3-LED
```

### 手机操作（nRF Connect）

| 步骤 | 操作 |
|------|------|
| ① | 打开 nRF Connect，点击 **SCAN** |
| ② | 列表中找到 **ESP32-S3-LED**，点击 **CONNECT** |
| ③ | 连上后往下滑，找到 Service **0xFFE0** |
| ④ | 点击展开，看到 Characteristic **0xFFE1** |
| ⑤ | 点右边的 **→** 箭头（或向上的箭头） |
| ⑥ | 选择 **Write value** 选项 |
| ⑦ | 输入 `64` → 点 **WRITE** → LED 全亮 |
| ⑧ | 输入 `00` → LED 熄灭，输入 `32` → 半亮 |

**亮度值对照：**

| 写入值（十六进制） | 含义 | PWM 占空比 |
|------------------|------|-----------|
| `00` | 关闭 | 0/255 |
| `32`（= 50） | 50% 亮度 | ~128/255 |
| `64`（= 100） | 全亮 | 255/255 |

> 公式：`PWM_duty = 写入值 × 255 ÷ 100`

---

## 十一、调试过程 & 踩坑记录（必读）

这段记录了我们实际调试中遇到的所有坑，从编译到跑通踩了 **7 个问题**。

### 坑 1：`esp_gatts_attr_db_t` 结构体找不到 `.attrcfg` 成员

**现象：**

```
error: 'esp_gatts_attr_db_t' has no member named 'attrcfg'
```

**原因：** 网上教程多为 ESP-IDF v4.x 的写法，v5.3.5 的结构体成员名变了。

**解决：** 
- v4.x 用 `.attrcfg` 直接写属性
- v5.x 用 `.attr_control` + `.att_desc` 两层嵌套
- 更简单的办法：不写属性表，改用 `create_service + add_char` 方式（本代码采用）

### 坑 2：`esp_ble_gatts_create_service` 参数不对

**现象：**

```
error: too many arguments to function
note: expected 'esp_gatt_srvc_id_t *' but argument is of type 'esp_bt_uuid_t *'
```

**原因：** v5.x 把参数从 `esp_bt_uuid_t` 改成了 `esp_gatt_srvc_id_t` 结构体（包含 is_primary 等字段）。

**解决：** 用 `esp_gatt_srvc_id_t` 结构体创建服务。

### 坑 3：事件名变了

**现象：**

```
error: 'ESP_GATTS_CREATE_SERVICE_EVT' undeclared; did you mean 'ESP_GATTS_CREAT_ATTR_TAB_EVT'?
```

**原因：** v5.x 把事件名改了：

| 功能 | v4.x 事件名 | v5.x 事件名 |
|------|------------|------------|
| 服务创建完成 | `ESP_GATTS_CREATE_SERVICE_EVT` | `ESP_GATTS_CREATE_EVT` |
| 属性表创建完成 | `ESP_GATTS_CREAT_ATTR_TAB_EVT` | 不变（本代码没用） |

**解决：** 用 `ESP_GATTS_CREATE_EVT` 替代。

### 坑 4：链接报 undefined reference

**现象：**

```
undefined reference to 'esp_ble_gap_start_advertising'
undefined reference to 'esp_ble_gap_config_adv_data'
```

**原因：** menuconfig 里有个隐藏选项 **Enable BLE 4.2 advertising** 没有开，导致编译时把 GAP 函数跳过了。

**解决：** menuconfig → `Bluetooth → Bluedroid Options → [*] Enable BLE 4.2 advertising`

### 坑 5：控制器模式不对

**现象：**

```
BLE_INIT: invalid mode 3, controller support mode is 1
BLE: controller enable fail
```

**原因：** 代码用的 `ESP_BT_MODE_BTDM`（双模，支持经典蓝牙+BLE），但 ESP32-S3 的控制器只支持纯 BLE（mode 1）。

**解决：** 改成 `ESP_BT_MODE_BLE`。

### 坑 6：广播数据超 31 字节

**现象：**

```
config_adv_data ret=258
```

（`258` = `ESP_ERR_INVALID_SIZE`）

**原因：** BLE 广播包最大 **31 字节**，设备名 `ESP32-S3-LED` 就占了 13 字节，加上 Flags 和 Service UUID 就装不下了。

**解决：** 
- 广播包里只放 Flags + Service UUID（共 7 字节，远小于 31）
- 设备名放到**扫描响应包**（Scan Response）里
- 手机扫描时会先收广播包（判断是什么设备），再收扫描响应包（获取设备名）

### 坑 7：广播数据配置完成后的事件编号变了

**现象：** 日志显示 `GAP event: 4` 但没有触发广播启动。

**原因：** 代码中用了 `esp_ble_gap_config_adv_data_raw()` ，它触发的是 `ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT`（值=4），不是 `ADV_DATA_SET_COMPLETE_EVT`（值=0）。旧代码只处理了事件 0。

**解决：** 两个事件都处理（case 合并）。

---

## 调试方法论：怎么一步步找到问题

以上 7 个坑是怎么找到的？不是靠猜，而是靠一套系统的调试方法。这一节把「查问题的思路」说清楚，之后遇到类似问题你也能自己查。

### 方法 1：**看编译器错误信息**

编译器给你的错误不是乱码，每行都告诉你具体问题。

```
error: 'esp_gatts_attr_db_t' has no member named 'attrcfg'
```

→ 这说明结构体里没有 `attrcfg` 这个成员。接下来要做的是：

```bash
# 1. 找到这个结构体在哪定义的
grep -rn "esp_gatts_attr_db_t" components/bt/ --include="*.h"
# 2. 打开文件看它到底有什么成员
```

> **核心思路：** 不要猜 API 怎么用，去头文件里看它到底长什么样。

### 方法 2：**查 API 签名**

函数调不对时，编译器会告诉你「你传的参数类型不对」：

```
note: expected 'esp_gatt_srvc_id_t *' but argument is of type 'esp_bt_uuid_t *'
```

第一反应不是乱改参数，而是去头文件看这个函数声明：

```bash
grep -A 5 "esp_ble_gatts_create_service" components/bt/.../esp_gatts_api.h
```

看到原型：

```c
esp_err_t esp_ble_gatts_create_service(esp_gatt_if_t gatts_if,
                                       esp_gatt_srvc_id_t *service_id,
                                       uint16_t num_handle);
```

→ 原来要传 `esp_gatt_srvc_id_t *`，再去查这个结构体怎么初始化。

> **核心思路：** 所有 API 的用法都在头文件里有定义，编译器指向哪你就去查哪。

### 方法 3：**grep 查宏定义和编译开关**

链接报 `undefined reference` 时，说明函数声明了但没被编译进去。用 grep 找它被什么 `#if` 包裹：

```bash
# 查这个函数在哪实现
grep -rn "esp_ble_gap_start_advertising" components/bt/ --include="*.c"

# 看它被什么条件编译控制
grep -B 5 "esp_ble_gap_start_advertising" components/bt/.../esp_gap_ble_api.c
```

输出：

```c
#if (BLE_42_ADV_EN == TRUE)    ← 就是这个开关！
esp_err_t esp_ble_gap_start_advertising(esp_ble_adv_params_t *adv_params)
```

然后再查 `BLE_42_ADV_EN` 对应的 menuconfig 选项是什么：

```bash
grep "BLE_42_ADV_EN" components/bt/.../Kconfig.in
```

→ 发现 `config BT_BLE_42_ADV_EN`，去 menuconfig 里勾上就解决了。

> **核心思路：** 链接报错 → 查编译开关 → 查 menuconfig 选项。

### 方法 4：**查枚举值编号**

事件名不对时，直接去头文件看枚举定义：

```bash
grep -A 15 "gap_ble_cb_event_t" components/bt/.../esp_gap_ble_api.h | grep "="
```

输出：

```c
ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT = 0,
ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT,  // = 1
ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT,     // = 2
ESP_GAP_BLE_SCAN_RESULT_EVT,                 // = 3
ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT,   // = 4  ← 哦！原来事件 4 是这个
ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT, // = 5
ESP_GAP_BLE_ADV_START_COMPLETE_EVT,          // = 6
```

→ 原来 `GAP event: 4` 是 `ADV_DATA_RAW_SET_COMPLETE_EVT`，不是 `ADV_START_COMPLETE_EVT`。

> **核心思路：** 看到日志里的数字（如 event: 4），就去枚举定义里查这个数字代表什么。

### 方法 5：**加日志看返回值**

很多函数返回错误码，但代码里没检查返回值就过去了。加一行日志就能发现：

```c
esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
ESP_LOGI(TAG, "config_adv_data ret=%d", ret);  // ← 加了这行
```

→ 看到 `ret=258`，查一下错误码含义：

```c
// 在 esp_err.h 里
#define ESP_ERR_INVALID_SIZE   0x102  // 258
```

→ 知道是「大小不对」，再查 BLE 广播包最大 31 字节的限制。

> **核心思路：** 所有函数都有可能失败，检查返回值能发现隐藏的问题。

### 方法 6：**grep 查 sdkconfig**

蓝牙选项很多，不确定哪个开了哪个没开，直接搜索：

```bash
grep "^CONFIG_BT_" sdkconfig
```

看到 `CONFIG_BT_BLE_42_ADV_EN` 没出现 → 说明没开 → 去 menuconfig 里找。

> **核心思路：** 与其在 menuconfig 里翻来翻去找选项，不如直接 grep 配置文件。

### 方法 7：**搜 ESP-IDF 官方示例**

当不确定某个 API 在 v5.x 里怎么用时，直接找官方示例：

```bash
find examples/bluetooth/ -name "*.c" | xargs grep "esp_ble_gatts_create_service"
```

看官方示例里怎么用这个函数，直接复制过来改。

---

## 总结：调试流程

```
编译器报错
    ↓
看错误信息 → 去头文件查结构体/函数签名（方法1、2）
    ↓
编译通过了
    ↓
链接报 undefined reference
    ↓
查编译开关（方法3）→ 开 menuconfig 补选项
    ↓
链接通过
    ↓
烧录跑起来但工作不正常
    ↓
加日志看返回值（方法5）→ 看日志里的数字含义（方法4）
    ↓
→ 查文档/查代码 → 修复 → 验证
```

这套流程不只适用于 BLE，ESP-IDF 所有的外设（WiFi、I2C、SPI、ADC 等）遇到问题时，都可以按同样思路排查。

## 十二、常见问题排查

### Q：编译报 `esp_bt.h: No such file or directory`

没开蓝牙。运行 menuconfig，勾选 `Component config → Bluetooth → [*] Bluetooth`。

### Q：链接报 `undefined reference to esp_ble_gap_*`

menuconfig 里的 `Enable BLE 4.2 advertising` 没开。路径见 [Step 6](#九step-6-menuconfig-配置)。

### Q：固件太大装不下

默认分区只有 1MB，加了蓝牙后固件超过 1MB。改为 `Single factory app (large)` 分区表。

### Q：手机搜不到 `ESP32-S3-LED`

检查串口有没有出现 `advertising: ESP32-S3-LED`。没有的话查一下 GAP event 日志看卡在哪一步。

### Q：写入了亮度但又被呼吸灯覆盖

呼吸灯任务还在跑。需要 BLE 写入时调用 `pwm_enable_breath(0)` 暂停呼吸灯，本代码已经做了。

### Q：连上 nRF Connect 后看不到 Service

断开重连，或者清空 nRF Connect 的设备缓存重新扫描。

---

## 十三、移植到其他项目

### 要改的文件

| 文件 | 改动 |
|------|------|
| `Board/ble.c` | 复制，改 `DEVICE_NAME` 和 UUID（可选） |
| `Board/ble.h` | 复制，不用改 |
| `Board/pwm.c` | 加 `pwm_set_duty()` 函数 |
| `Board/pwm.h` | 加对应声明 |
| 你的 `main.c` | 加 `#include "Board/ble.h"` + `ble_init()` |
| `CMakeLists.txt` | SRCS 加 `Board/ble.c`，REQUIRES 加 `bt` |

### 也要跑 menuconfig

移植到新项目时，别忘了重新配置：
1. 打开蓝牙 + Bluedroid + BLE 4.2 advertising
2. 分区表改为 `Single factory app (large)`

### 如果不用 PWM 调光

`ble.c` 里把 `pwm_set_duty()` 换成你自己的控制函数即可，比如控制继电器、电机等。

---

## 十四、完整代码一览

### 新增文件

| 文件 | 行数 | 作用 |
|------|------|------|
| `main/Board/ble.h` | ~15 | BLE 接口声明 |
| `main/Board/ble.c` | ~220 | BLE GATT Server + 广播 + 读写回调 |

### 修改文件

| 文件 | 改动 |
|------|------|
| `main/Board/pwm.h` | 加 `pwm_set_duty()` / `pwm_get_duty()` / `pwm_enable_breath()` |
| `main/Board/pwm.c` | 实现调光接口，呼吸灯兼容 BLE 模式 |
| `main/hello_world_main.c` | 加 `#include "Board/ble.h"` + `ble_init()` |
| `main/CMakeLists.txt` | SRCS 加 `ble.c`，REQUIRES 加 `bt` |

### 自定义配置

如果你想修改 Service/Characteristic 的 UUID 或设备名，改 `ble.c` 顶部的宏：

```c
#define GATTS_SERVICE_UUID      0xFFE0     // Service 标识
#define GATTS_CHAR_UUID         0xFFE1     // Characteristic 标识
#define GATTS_DEVICE_NAME       "ESP32-S3-LED"  // 手机搜索到的名字
```