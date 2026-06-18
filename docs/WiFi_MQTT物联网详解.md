# ESP32-S3 WiFi + MQTT 物联网接入详解

> 从零到手机查看温湿度，每一步都有解释。先回答三个致命疑问，再看代码。

---

## 第〇章：先搞懂"数据是怎么到手机的"

这是新手最容易搞混的地方，放最前面讲清楚。

### 0.1 物理连接图

不管你有没有路由器，最终链路都长这样：

```
┌──────────┐     WiFi      ┌──────────────┐      4G/宽带     ┌──────────────┐     4G/5G     ┌──────────┐
│  ESP32   │···············→  手机热点/    │···············→   OneNET 云   │·············→ │  手机 App │
│  +DHT11  │  连上 WiFi     │  家里的路由器  │   通过互联网     │  MQTT 服务器  │   App 去取数据  │  (同一台    │
│          │               │              │                │              │              │  或者另一台)│
└──────────┘               └──────────────┘                └──────────────┘              └──────────┘
    送信人                        邮筒                         邮局/信箱                    收件人
```

**ESP32 和手机从来没有直接通信。** 它们之间靠 OneNET 中转——就像你寄快递，你和收件人不会在包裹转运中心见面。

### 0.2 每个"角色"干什么

打个比方：你在家测了室温，想告诉你远方的朋友。

| 角色 | 比喻 | 在这个项目里是 |
|------|------|---------------|
| **ESP32** | 你 | 采集温湿度，发送数据 |
| **WiFi 热点/路由器** | 邮筒 | 给 ESP32 提供上网通道 |
| **MQTT** | 邮政协议（小包裹格式） | ESP32 和 OneNET 之间通信的语言 |
| **OneNET 平台** | 邮局 + 信箱 | 收下数据，存好，等你来看 |
| **手机 App** | 你朋友 | 打开 App，看到你发的温湿度 |

### 0.3 "没路由器怎么办？"

**用手机热点，完全没问题：**

```
┌──────────┐     WiFi      ┌──────────┐      4G        ┌──────────┐      4G        ┌──────────┐
│  ESP32   │···············→ 手机开热点 │··············→ 互联网   │··············→ │ OneNET   │
│          │               │ (邮筒)   │               │          │               │ (邮局)   │
└──────────┘               └──────────┘               └──────────┘               └──────────┘
                                  │                                                        │
                                  │                  同一台手机                              │
                                  └──────────── App 读数据 ←─────────────────────────────────┘
```

手机同时干两件事：
1. **开热点** → 给 ESP32 当"邮筒"，让它能上网发数据
2. **装 App** → 从 OneNET "取信"，看温湿度

**注意**：部分安卓手机开热点后，手机自身的 App 可能无法同时走 4G 外网。如果 App 连不上，换 iPhone 开热点，或者用两台手机（一台开热点给 ESP32，另一台装 App 看数据）。

### 0.4 "为什么不能 ESP32 直接发数据给手机？"

ESP32 连上手机热点后，拿到的是一个**内网 IP**（比如 `192.168.1.100`）。这个 IP 只在热点的小局域网内有效，**互联网上找不到**。

```
热点局域网内:  192.168.1.100  ← 有效
从百度搜:      192.168.1.100  ← 不存在 (因为世界上有几百万个设备都是这个 IP)
```

所以 ESP32 不能直接说"来连我的 `192.168.x.x`"，必须推数据到一个有**公网固定地址**的服务器——OneNET 就是这个服务器。

### 0.5 一句话总结

> **WiFi 让 ESP32 上网，MQTT 让数据和云端说同一种语言，OneNET 帮你在任何地方都能看到数据。**

---

## 目录

1. [代码架构：四个模块怎么协作](#一代码架构)
2. [WiFi 模块：连上网还能自动重连](#二wifi-模块-wifi_appc)
3. [MQTT 模块：把数据发到云端](#三mqtt-模块-mqtt_appc)
4. [传感器采集模块：定时采 + 组 JSON + 上报](#四传感器采集模块-sensor_taskc)
5. [OneNET 平台注册 + App 绑定](#五onenet-平台注册)
6. [项目集成与编译](#六项目集成)

---

## 一、代码架构

```
┌──────────────────────────────────────────────────────┐
│                    app_main()                         │
│                                                       │
│  ① OLED 初始化                                        │
│  ② wifi_init_sta()   ← 启动 WiFi，自动重连            │
│  ③ mqtt_app_start()  ← 连接 OneNET MQTT 服务器       │
│  ④ 创建 3 个任务:                                     │
│     - pwm_breath_task (优先级5)  LED 呼吸灯            │
│     - adc_get_task    (优先级6)  ADC 采样              │
│     - sensor_task     (优先级4)  DHT11 + MQTT 上报    │
└──────────────────────────────────────────────────────┘

数据流向:
  DHT11 每 2 秒采集
    → 组装 OneNET JSON
      → mqtt_publish() 发送
        → OneNET 服务器
          → 手机 App 实时查看
```

**为什么拆成独立文件而不是全写在一起？**

- WiFi、MQTT、传感器是三个独立功能，拆开方便调试和复用
- 以后换云平台（比如从 OneNET 换阿里云），只改 `mqtt_app.c`，WiFi 和传感器不动
- 每个模块只暴露 1~2 个函数，调用关系清晰

---

## 二、WiFi 模块 (`wifi_app.c`)

### 2.1 它做了什么

```
wifi_init_sta()
  ├── 初始化 NVS (WiFi 库需要)
  ├── 初始化 TCP/IP 协议栈
  ├── 注册事件回调 (连上/断开/拿到IP 时自动处理)
  ├── 配置 WiFi SSID + 密码
  └── 启动 WiFi，开始连接
```

### 2.2 逐段解释

#### 头文件和常量

```c
#include "esp_wifi.h"       // ESP-IDF WiFi 驱动
#include "esp_event.h"      // 事件系统 (WiFi 状态变化通过事件通知)
#include "nvs_flash.h"      // 非易失存储 (WiFi 库用它在 Flash 里存校准数据)

#define WIFI_SSID      "你的WiFi名"    // ← 改成你的
#define WIFI_PASSWORD  "你的WiFi密码"   // ← 改成你的
```

**为什么用 `#define` 而不是 `menuconfig`？** 代码里直接改快，正式产品再用 menuconfig。

#### 事件组

```c
static EventGroupHandle_t wifi_evt;
#define WIFI_CONNECTED  BIT0
```

**事件组是什么？** 一个 32 位的"标记牌"，每个 bit 代表一件事。这里只用 BIT0：1 = WiFi 已连接，0 = 断开。`sensor_task` 可以通过检查这个位来决定是否上报数据，不需要自己轮询。

#### 事件回调 — 核心逻辑

```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
```

ESP-IDF 的 WiFi 驱动在状态变化时自动调用这个函数，传入不同的事件 ID：

| 事件 ID | 何时触发 | 做什么 |
|---------|---------|--------|
| `WIFI_EVENT_STA_START` | WiFi 芯片启动完毕 | 调用 `esp_wifi_connect()` 开始连接 |
| `WIFI_EVENT_STA_CONNECTED` | 已连上路由器 | 复位重试计数器 |
| `WIFI_EVENT_STA_DISCONNECTED` | 断开了 | **自动重连（核心）** |
| `IP_EVENT_STA_GOT_IP` | DHCP 拿到 IP | 设置事件组 BIT0，通知其他任务"网络通了" |

**断开重连的指数退避逻辑：**

```c
case WIFI_EVENT_STA_DISCONNECTED:
    s_connected = false;
    xEventGroupClearBits(wifi_evt, WIFI_CONNECTED);

    int delay = (1 << s_retry_cnt) * 1000;  // 1s, 2s, 4s, 8s, 16s, ...
    if (delay > 30000) delay = 30000;       // 最长 30 秒
    vTaskDelay(pdMS_TO_TICKS(delay));
    esp_wifi_connect();
    s_retry_cnt++;
```

**为什么要指数退避？** 如果路由器暂时挂了，每秒重连一次只会浪费 CPU。1→2→4→8 秒越来越慢，路由器恢复后最多等 30 秒就好。

#### 初始化函数

```c
void wifi_init_sta(void)
{
    // 1. NVS — WiFi 驱动用它在 Flash 里存数据
    nvs_flash_init();

    // 2. TCP/IP 协议栈 — 不初始化这个，拿到 IP 也没用
    esp_netif_init();
    esp_event_loop_create_default();           // 事件循环
    esp_netif_create_default_wifi_sta();       // 创建 STA 网络接口

    // 3. WiFi 初始化
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 4. 注册回调 (ESP_EVENT_ANY_ID = 所有 WiFi 事件都通知我)
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, ...);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, ...);

    // 5. 设置 SSID/密码并启动
    wifi_config_t wifi_cfg = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASSWORD }
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();
}
```

---

## 三、MQTT 模块 (`mqtt_app.c`)

### 3.1 先理解"为什么需要 MQTT"

WiFi 连上之后，ESP32 已经能上网了。但你得想一个问题：**ESP32 说"我温度 24.5°C"，谁听？**

直接发 HTTP 请求？也行。但 HTTP 每次要握手、发头、等响应——对 ESP32 这种资源少的设备太浪费了。而且手机 App 也没法主动找 ESP32（内网 IP 外网不可达）。

**MQTT 解决的就是这个问题：**

```
HTTP (一问一答):
  ESP32: "我连你了吗？没有。好，先三次握手..."
         "我要 POST /data  HTTP/1.1"
         "Content-Type: application/json..."
         "..."（几百字节的头）
  Server: "HTTP/1.1 200 OK"
         "..."（几百字节的响应头）
  ESP32: "终于说完了，断开吧"
  → 每次发 24.5 这个数字，可能要几百字节的开销

MQTT (小纸条投递):
  ESP32: "温度=24.5"（几个字节）
  Broker: "收到"(几个字节)
  → 极致精简，专为传感器设计
```

### 3.2 MQTT 三个核心概念

MQTT 就像一个报刊订阅系统：

- **Broker（邮局/报刊亭）**：负责收信、存信、转发。OneNET 提供。
- **Topic（栏目名称）**：比如 `$sys/产品ID/设备名/dp/post/json`，就像订阅"ESP32温湿度快报"。发布和订阅都用 Topic 来匹配。
- **QoS（送达保证）**：
  - QoS0：发出去不管（可能丢）
  - QoS1：至少送达一次（我们用的，可靠）
  - QoS2：恰好送达一次（最可靠但最慢）

```
┌──────────┐  publish("温度", 24.5, topic="/sensor/temp")  ┌──────────┐
│  ESP32   │ ────────────────────────────────────────────→ │  OneNET  │
│ (发布者) │                                               │ (Broker) │
└──────────┘                                               └────┬─────┘
                                                                │
                                          这个 Topic 有人订阅吗？  │
                                                                ↓
                                                         ┌──────────┐
                                                         │ 手机 App  │
                                                         │ (订阅者) │
                                                         └──────────┘
```

### 3.3 OneNET 的 MQTT 怎么连

OneNET 是一个公网的 MQTT Broker，地址是 `183.230.40.96:6002`。

连接需要**三个身份证明**（相当于"你是谁"）：

| 参数 | 写法 | 去哪找 |
|------|------|--------|
| `client_id` | 设备名称，如 `esp32-s3-01` | OneNET 控制台 → 设备列表 |
| `username` | 产品 ID，如 `Ab123CDefg` | OneNET 控制台 → 产品概况 |
| `password` | Token，如 `version=2018...` | OneNET 控制台 → 设备详情 → Token |

代码里改这三处（`mqtt_app.c` 第 13-15 行）：

```c
#define ONENET_PRODUCT_ID  "你的产品ID"    // ← 产品ID
#define ONENET_DEVICE_NAME "你的设备名"    // ← 设备名称
#define ONENET_TOKEN       "你的设备Token" // ← Token
```

### 3.3 逐段解释



```c
void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://183.230.40.96:6002",  // OneNET 服务器
        .credentials = {
            .client_id     = ONENET_DEVICE_NAME,
            .username      = ONENET_PRODUCT_ID,
            .authentication.password = ONENET_TOKEN,
        },
    };

    client = esp_mqtt_client_init(&mqtt_cfg);          // 创建客户端
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL); // 注册回调
    esp_mqtt_client_start(client);                      // 开始连接
}
```

**`esp_mqtt_client_init` + `start` 是异步的**——调用后立即返回，后台自动连接。连接成功会触发 `MQTT_EVENT_CONNECTED` 回调。

#### 发布函数

```c
int mqtt_publish(const char *topic, const char *data)
{
    return esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
    //                                               ↑  ↑  ↑
    //                                          data_len=0 QoS=1 retain=0
    //                                          (0=自动strlen)
}
```

**QoS=1**：消息至少到达一次。Broker 收到后会回复一个确认，比 QoS=0 可靠，比 QoS=2 省流量。

---

## 四、传感器采集模块 (`sensor_task.c`)

### 4.1 它做了什么

```
while(1):
  ① dht11_read()          ← 读温湿度
  ② sensor_data_to_json()  ← 转成 OneNET 规定的 JSON
  ③ mqtt_publish()         ← 发到云端
  ④ vTaskDelay(2000ms)     ← 等 2 秒 (DHT11 要求 ≥1 秒间隔)
```

### 4.2 JSON 格式

OneNET 物模型要求的数据格式：

```json
{
  "id": 1718123456789,
  "dp": {
    "temperature": [{ "v": 24.5 }],
    "humidity":    [{ "v": 68.0 }]
  }
}
```

| 字段 | 含义 |
|------|------|
| `id` | 消息 ID（用时间戳，唯一即可） |
| `dp` | datapoints，数据点 |
| `temperature` / `humidity` | OneNET 物模型里定义的功能标识 |
| `v` | value，当前值 |

#### 组包函数

```c
/*printf 输出到串口 / 控制台，snprintf 输出到内存缓冲区（你的 char buf）
        snprintf 防缓冲区溢出（嵌入式致命问题）
        sprintf（无长度限制，危险）
        函数	输出目标	是否有长度保护	适用场景
        printf	串口 / 终端控制台	无	单纯调试打印，不缓存文本
        sprintf	内存 char 数组	无（危险）	禁止在嵌入式工程使用，溢出风险高
        snprintf	内存 char 数组	有第二个参数限制长度	生成字符串缓存、网络报文，工程标准用法*/
static void sensor_data_to_json(const sensor_data_t *d, char *buf, size_t size)
{   
    snprintf(buf, size,
             "{"
             "\"id\":%lu,"                              // ← 反斜杠是转义引号
             "\"temperature\":%.1f,"          // %.1f = 保留 1 位小数
             "\"humidity\":%.1f"
             "}",
             (unsigned long)(esp_timer_get_time() / 1000),  // 微秒→毫秒
             d->temperature,
             d->humidity);
}
```

#### 任务入口

```c
void sensor_task(void *pvParam)
{
    dht11_data_t dht;
    dht11_init(GPIO_NUM_4);                        // 初始化 DHT11
    vTaskDelay(pdMS_TO_TICKS(1200));               // 首读前等 1.2 秒

    while (1) {
        esp_err_t ret = dht11_read(&dht);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "DHT11 read failed, retry...");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;                               // 失败了重试，不死等
        }

        char json[256];
        sensor_data_t sd = { dht.tem, dht.hum };
        sensor_data_to_json(&sd, json, sizeof(json));

        mqtt_publish(TOPIC, json);                  // 发到云端

        vTaskDelay(pdMS_TO_TICKS(2000));            // 2 秒一次
    }
}
```

**为什么首读前等 1.2 秒？** DHT11 上电后需要 ≥1 秒的内部校准时间，马上读会得到乱码。

**为什么失败后 `continue` 而不是 `ESP_ERROR_CHECK`？** 传感器偶尔读数失败是正常的（电磁干扰、时序抖动），重试就好，不需要重启芯片。

---

## 五、OneNET 平台注册

### 5.1 注册账号

1. 打开 [open.iot.10086.cn](https://open.iot.10086.cn)
2. 手机号注册（免费）

### 5.2 创建产品

1. 控制台 → 全部产品 → 创建产品
2. 产品名称：`ESP32温湿度`
3. 协议：**MQTT**
4. 其余默认

### 5.3 添加设备 + 获取 Token

1. 进入产品 → 设备列表 → 添加设备
2. 设备名称：`esp32-s3-01`（自定义，记住它）
3. 添加后点进设备 → 查看详情 → 复制 **产品ID**、**设备名称**、**Token**
4. 填入 `mqtt_app.c` 和 `sensor_task.c` 的三个宏

### 5.4 创建物模型（让平台认识温湿度）

1. 产品 → 物模型 → 添加功能
2. 功能 1：名称 `temperature`，标识 `temperature`，类型 `浮点型`，范围 0~100，单位 °C
3. 功能 2：名称 `humidity`，标识 `humidity`，类型 `浮点型`，范围 0~100，单位 %
4. 保存

### 5.5 手机 App 查看

1. 应用商店搜索 **"设备云"**（OneNET 官方 App）
2. 登录 → 添加设备 → 扫描设备二维码（控制台有）
3. 设备上线后即可看到温湿度曲线

---

## 六、项目集成

### 6.1 文件结构

```
main/
├── hello_world_main.c          ← 启动入口 (需修改)
├── CMakeLists.txt              ← 编译配置 (需修改)
├── GPIO.c / GPIO.h
└── Board/
    ├── adc.c / adc.h           ← ADC 驱动 (不动)
    ├── dht11.c / dht11.h       ← DHT11 驱动 (删 #include "oled.h")
    ├── oled.c / oled.h         ← OLED 驱动 (不动)
    ├── pwm.c / pwm.h           ← 呼吸灯 (不动)
    ├── wifi_app.h / wifi_app.c ← ★ 新增
    ├── mqtt_app.h / mqtt_app.c  ← ★ 新增
    └── sensor_task.h / sensor_task.c ← ★ 新增
```

### 6.2 `hello_world_main.c` 最终版本

```c
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Board/pwm.h"
#include "Board/adc.h"
#include "Board/oled.h"
#include "Board/wifi_app.h"
#include "Board/mqtt_app.h"
#include "Board/sensor_task.h"

void app_main(void)
{
    oled_init();
    oled_clear();
    oled_show_string(0, 0, "WiFi...");

    wifi_init_sta();          // 启动 WiFi

    oled_show_string(0, 1, "MQTT...");
    mqtt_app_start();         // 启动 MQTT

    xTaskCreate(pwm_breath_task, "breath", 4096, NULL, 5, NULL);
    xTaskCreate(adc_get_task,   "adc",    4096, NULL, 6, NULL);
    xTaskCreate(sensor_task,    "sensor", 4096, NULL, 4, NULL);

    oled_show_string(0, 1, "Running");
}
```

### 6.3 `CMakeLists.txt` 最终版本

```cmake
idf_component_register(SRCS "GPIO.c" "hello_world_main.c"
                            "Board/pwm.c" "Board/adc.c" "Board/oled.c"
                            "Board/dht11.c"
                            "Board/wifi_app.c" "Board/mqtt_app.c"
                            "Board/sensor_task.c"
                    INCLUDE_DIRS ""
                    REQUIRES esp_driver_ledc esp_driver_gpio esp_adc
                             esp_driver_i2c esp_lcd esp_driver_rmt
                             esp_wifi esp_netif nvs_flash esp_event mqtt)
```

### 6.4 编译 + 烧录

```bash
# 1. 确保 menuconfig 里 WiFi SSID/密码已设
#    (本方案用代码宏，不需要 menuconfig)

# 2. 配置 OneNET 认证信息
#    修改 mqtt_app.c 和 sensor_task.c 里的 ONENET_* 宏

# 3. 编译烧录
idf.py build flash monitor

# 期望输出:
# I (xxx) wifi: STA started, connecting...
# I (xxx) wifi: got IP: 192.168.1.100
# I (xxx) mqtt: MQTT connected to broker
# I (xxx) sensor: temp=24.5°C  hum=68.0%
```

### 6.5 验证清单

- [ ] 串口看到 `WiFi connected`
- [ ] 串口看到 `MQTT connected to broker`
- [ ] OneNET 控制台设备状态变为"在线"
- [ ] OneNET 数据流中出现 `temperature` 和 `humidity` 数据点
- [ ] 手机 App 绑定后看到温湿度曲线

---

## 七、常见问题

| 现象 | 原因 | 解决 |
|------|------|------|
| `esp_wifi_connect` 失败 | SSID 或密码错误 | 检查拼写，注意大小写和空格 |
| MQTT 连不上 | 产品ID/设备名/Token 不对 | 去 OneNET 控制台核对三个值 |
| MQTT 连上但没数据 | 物模型标识和 JSON 里的键不一致 | 物模型的"功能标识"必须和 JSON 键名完全一致（比如都是 `temperature`） |
| 串口只看到 DHT11 数据，没看到 MQTT | `sensor_task.c` 里的 TOPIC 宏还是默认值 | 把 `你的产品ID` / `你的设备名` 换成实际值 |
| 手机看不到设备 | 没在 App 里添加设备 | 用控制台生成的二维码在 App 里扫码绑定 |

---

## 八、下一步扩展

- **ADC 电压上报**：在 `sensor_task` 里加 `adc_read_voltage()`，多写一个 JSON 字段
- **OLED 显示状态**：在 `mqtt_event_handler` 的 CONNECTED 事件里写 OLED
- **OTA 远程升级**：OneNET 支持 OTA，后续可以远程更新固件
- **换阿里云**：只需改 `mqtt_app.c` 的 Broker URI 和认证方式，其他文件不动

---

## 附录：跟着敲一遍（可移植完整代码）

> 从空目录开始，按顺序创建文件。需要你改的地方用 `← ✏️ 改成你的` 标记。

### A.1 文件清单

```
要新建 6 个文件:
  main/Board/wifi_app.h
  main/Board/wifi_app.c
  main/Board/mqtt_app.h
  main/Board/mqtt_app.c
  main/Board/sensor_task.h
  main/Board/sensor_task.c

要修改 2 个文件:
  main/hello_world_main.c
  main/CMakeLists.txt
```

### A.2 第 1 步：WiFi 头文件

**创建 `main/Board/wifi_app.h`** — 只声明一个函数：

```c
#ifndef __WIFI_APP_H_
#define __WIFI_APP_H_

#include "esp_err.h"

/**
 * @brief  初始化 WiFi STA 模式并连接
 *         内部自动注册重连回调，断线指数退避重连
 */
void wifi_init_sta(void);

#endif
```

| 行 | 解释 |
|----|------|
| `#ifndef` / `#define` | 防止被重复 include |
| `void wifi_init_sta(void)` | 调用一次就启动 WiFi，之后的一切（连接、断线重连）都在后台自动处理 |

---

### A.3 第 2 步：WiFi 实现文件

**创建 `main/Board/wifi_app.c`** — 这是整个 WiFi 连接的完整实现：

```c
#include "wifi_app.h"
#include "esp_event.h"        // WiFi 状态变化通过事件通知
#include "esp_log.h"
#include "esp_wifi.h"         // ESP-IDF WiFi 驱动
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"        // WiFi 库需要非易失存储
#include <string.h>

static const char *TAG = "wifi";

/* ========== ✏️ 改成你的 WiFi 名和密码 ========== */
#define WIFI_SSID      "你的WiFi名"
#define WIFI_PASSWORD  "你的WiFi密码"

/* ========== 事件组 (通知其他任务"网络通了") ========== */
static EventGroupHandle_t wifi_evt;
#define WIFI_CONNECTED  BIT0

static int  s_retry_cnt = 0;

/* ========== WiFi 事件回调 ========== */
/*
 * ESP-IDF 的 WiFi 驱动在状态变化时自动调用此函数。
 * 参数 event_id 告诉我们发生了什么：
 *   WIFI_EVENT_STA_START       → WiFi 芯片就绪了，开始连接
 *   WIFI_EVENT_STA_CONNECTED   → 已连上路由器
 *   WIFI_EVENT_STA_DISCONNECTED → 断开了，需要重连
 *   IP_EVENT_STA_GOT_IP        → DHCP 拿到 IP，真正能上网了
 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();          // 真正发起连接
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            s_retry_cnt = 0;             // 复位重试计数
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d =
                (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "disconnected, reason=%d", d->reason);
            xEventGroupClearBits(wifi_evt, WIFI_CONNECTED);

            /* ---------- 指数退避重连 ---------- */
            if (s_retry_cnt < 10) {
                int delay = (1 << s_retry_cnt) * 1000;   // 1s→2s→4s→8s→...
                if (delay > 30000) delay = 30000;        // 封顶 30 秒
                ESP_LOGI(TAG, "reconnect in %d ms", delay);
                vTaskDelay(pdMS_TO_TICKS(delay));
                esp_wifi_connect();
                s_retry_cnt++;
            } else {
                ESP_LOGE(TAG, "max retries reached, giving up");
            }
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* 拿到 IP 才算真正能上网 */
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_retry_cnt = 0;
        xEventGroupSetBits(wifi_evt, WIFI_CONNECTED);
    }
}

/* ========== 公开接口 ========== */

void wifi_init_sta(void)
{
    wifi_evt = xEventGroupCreate();

    /* ---- ① NVS 初始化 (WiFi 库依赖) ---- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* ---- ② TCP/IP 协议栈 ---- */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    /* ---- ③ WiFi 初始化 ---- */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* ---- ④ 注册事件回调 ---- */
    esp_event_handler_instance_t ctx;
    esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,      // 所有 WiFi 事件都通知我
        &wifi_event_handler, NULL, &ctx);
    esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,      // 顺便监听拿 IP 事件
        &wifi_event_handler, NULL, &ctx);

    /* ---- ⑤ 配置 SSID/密码并启动 ---- */
    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();                       // 启动后会触发 WIFI_EVENT_STA_START

    ESP_LOGI(TAG, "WiFi init done, SSID=%s", WIFI_SSID);
}
```

**关键知识点：**

| 概念 | 解释 |
|------|------|
| 事件回调 | 你不用轮询"连上了没"，WiFi 驱动主动通知你 |
| 事件组 | 一个 32-bit 标记牌，`BIT0=1` 表示"网络通了"。其他任务可以 `xEventGroupWaitBits()` 等这个标记 |
| 指数退避 | 1s→2s→4s→8s→...→30s，断线后越来越慢地重试，不浪费 CPU |
| `nvs_flash_init` | WiFi 驱动在 Flash 里存 PHY 校准数据，必须先初始化 NVS |
| `esp_netif_init` | 不初始化这个，拿到 IP 地址也不知道怎么用 |

---

### A.4 第 3 步：MQTT 头文件

**创建 `main/Board/mqtt_app.h`**：

```c
#ifndef __MQTT_APP_H_
#define __MQTT_APP_H_

/**
 * @brief  启动 MQTT 客户端 (连接 OneNET)
 *         调用后后台自动连接，断线自动重连
 */
void mqtt_app_start(void);

/**
 * @brief  发布 MQTT 消息
 * @param  topic  主题 (如 "$sys/产品ID/设备名/dp/post/json")
 * @param  data   JSON 字符串
 * @return 消息 ID，-1 表示失败
 */
int mqtt_publish(const char *topic, const char *data);

#endif
```

---

### A.5 第 4 步：MQTT 实现文件

**创建 `main/Board/mqtt_app.c`**：

```c
#include "mqtt_app.h"
#include "esp_log.h"
#include "mqtt_client.h"        // ESP-IDF 内置 MQTT 库
#include <string.h>

static const char *TAG = "mqtt";

/* ========== ✏️ 去 OneNET 控制台找到你的这三个值，填进去 ========== */
#define ONENET_PRODUCT_ID  "你的产品ID"      // 产品概况页
#define ONENET_DEVICE_NAME "你的设备名"      // 设备列表 → 设备名称
#define ONENET_TOKEN       "你的设备Token"   // 设备详情 → 查看 Token

/* OneNET MQTT 服务器地址 (全国统一) */
#define MQTT_BROKER_URI    "mqtt://183.230.40.96:6002"

static esp_mqtt_client_handle_t client = NULL;

/* ========== MQTT 事件回调 ========== */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to broker!");
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected, auto-reconnecting...");
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "msg id=%d published OK", evt->msg_id);
        break;

    case MQTT_EVENT_DATA:
        /* 收到服务器下发的数据时触发 (本教程暂时不订阅) */
        ESP_LOGI(TAG, "topic=%.*s  data=%.*s",
                 evt->topic_len, evt->topic,
                 evt->data_len,  evt->data);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    }
}

/* ========== 公开接口 ========== */

void mqtt_app_start(void)
{
    /* ---- MQTT 连接配置 ---- */
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,          // 连哪个服务器
        .credentials = {
            .client_id     = ONENET_DEVICE_NAME,         // 设备标识
            .username      = ONENET_PRODUCT_ID,           // 产品标识
            .authentication.password = ONENET_TOKEN,      // 鉴权密钥
        },
    };

    /* 创建客户端 */
    client = esp_mqtt_client_init(&mqtt_cfg);

    /* 注册事件回调 */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);

    /* 启动 (异步 — 后台自动连接，不阻塞) */
    esp_mqtt_client_start(client);

    ESP_LOGI(TAG, "MQTT starting, broker=%s", MQTT_BROKER_URI);
}

int mqtt_publish(const char *topic, const char *data)
{
    if (!client) {
        ESP_LOGW(TAG, "MQTT client not initialized");
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
    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
    return msg_id;
}
```

**关键知识点：**

| 概念 | 解释 |
|------|------|
| `esp_mqtt_client_start()` | **异步** — 调用后立即返回，连接在后台进行。成功时触发 `MQTT_EVENT_CONNECTED` |
| `esp_mqtt_client_publish` | **非阻塞** — 消息放入发送队列立即返回，不等待 Broker 确认 |
| QoS=1 | "至少送达一次" — Broker 收到后回一个确认，没收到会自动重发 |
| `mqtt://` 前缀 | 标准 MQTT 协议，端口 6002 (OneNET 的非 TLS 端口) |
| 三个认证参数 | OneNET 要求在 CONNECT 包里带上产品ID/设备名/Token，缺一不可 |

---

### A.6 第 5 步：传感器采集头文件

**创建 `main/Board/sensor_task.h`**：

```c
#ifndef __SENSOR_TASK_H_
#define __SENSOR_TASK_H_

/** 传感器数据结构 */
typedef struct {
    float temperature;   // 温度 (°C)
    float humidity;      // 湿度 (%)
} sensor_data_t;

/**
 * @brief  传感器采集 + MQTT 上报任务
 *         初始化 DHT11 → 每 2 秒采集 → 组 JSON → mqtt_publish
 */
void sensor_task(void *pvParam);

#endif
```

---

### A.7 第 6 步：传感器采集实现文件

**创建 `main/Board/sensor_task.c`**：

```c
#include "sensor_task.h"
#include "dht11.h"            // 你的 DHT11 驱动 (之前写的)
#include "mqtt_app.h"         // mqtt_publish()
#include "esp_log.h"
#include "esp_timer.h"        // esp_timer_get_time() 用于消息 ID
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "sensor";

/* ========== ✏️ 把 产品ID 和 设备名 换成你自己的 ========== */
#define TOPIC  "$sys/你的产品ID/你的设备名/dp/post/json"

/* ========== 数据 → OneNET JSON ========== */
/*
 * OneNET 物模型规定的数据格式:
 * {
 *   "id": 消息ID(用时间戳),
 *   "dp": {
 *     "temperature": [{ "v": 24.5 }],
 *     "humidity":    [{ "v": 68.0 }]
 *   }
 * }
 *
 * "temperature" 和 "humidity" 必须和你在 OneNET 物模型里定义的
 * 功能标识完全一致 (大小写敏感)
 */
static void data_to_json(const sensor_data_t *d, char *buf, size_t size)
{
    snprintf(buf, size,
             "{"
             "\"id\":%lu,"                              // ← \" 是转义引号
             "\"dp\":{"
             "\"temperature\":[{\"v\":%.1f}],"           // %.1f = 保留1位小数
             "\"humidity\":[{\"v\":%.1f}]"
             "}"
             "}",
             (unsigned long)(esp_timer_get_time() / 1000),  // us→ms 当消息ID
             d->temperature,
             d->humidity);
}

/* ========== 任务入口 ========== */

void sensor_task(void *pvParam)
{
    dht11_data_t dht_data;

    /* 初始化 DHT11 (GPIO4) */
    dht11_init(GPIO_NUM_4);

    /* DHT11 上电后需要 ≥1 秒校准，首次读之前等 1.2 秒 */
    vTaskDelay(pdMS_TO_TICKS(1200));

    ESP_LOGI(TAG, "sensor task started, publishing to: %s", TOPIC);

    while (1) {
        /* ---- ① 采集 ---- */
        esp_err_t ret = dht11_read(&dht_data);
        if (ret != ESP_OK) {
            /* 传感器偶尔读数失败是正常的 (噪声/时序抖动)，
             * 稍等重试，不要 restart 整个芯片 */
            ESP_LOGW(TAG, "DHT11 read failed (%s), retry...",
                     esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ESP_LOGI(TAG, "temp=%.1f°C  hum=%.1f%%",
                 dht_data.tem, dht_data.hum);

        /* ---- ② 组包 ---- */
        char json[256];
        sensor_data_t sd = {
            .temperature = dht_data.tem,
            .humidity    = dht_data.hum,
        };
        data_to_json(&sd, json, sizeof(json));

        /* ---- ③ 上报 ---- */
        int msg_id = mqtt_publish(TOPIC, json);
        if (msg_id < 0) {
            /* 可能在 WiFi 还没连上时就发了，忽略 */
            ESP_LOGW(TAG, "mqtt publish skipped (WiFi/not ready?)");
        } else {
            ESP_LOGI(TAG, "published, msg_id=%d", msg_id);
        }

        /* ---- ④ DHT11 要求采样间隔 ≥1 秒，用 2 秒更稳 ---- */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

**每一步的逻辑链：**

```
① dht11_read()          采集当前温湿度
     ↓ 失败 → 等 100ms → continue (重试)
     ↓ 成功
② data_to_json()         转成 OneNET 要求的 JSON 格式
     ↓
③ mqtt_publish()         通过 MQTT 发到 OneNET 云
     ↓ 失败 → 日志告警，继续下一轮
     ↓ 成功
④ vTaskDelay(2000)       等 2 秒，下一轮
```

---

### A.8 第 7 步：修改 `hello_world_main.c`

你原来的 `main.c` 已经有 OLED、PWM 呼吸灯、ADC、DHT11 任务。**保留这些**，加上 WiFi 和 MQTT，把原来的 `dht11_task` 换成新的 `sensor_task`：

```c
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Board/pwm.h"
#include "Board/adc.h"
#include "Board/oled.h"
#include "Board/wifi_app.h"         // ★ 新增
#include "Board/mqtt_app.h"          // ★ 新增
#include "Board/sensor_task.h"       // ★ 新增

void app_main(void)
{
    /* ---- ① OLED ---- */
    oled_init();
    oled_clear();

    /* ---- ② WiFi 连接 ---- */
    oled_show_string(0, 0, "WiFi...");
    wifi_init_sta();                          // ★ 启动 WiFi

    /* ---- ③ MQTT 连接 ---- */
    oled_show_string(0, 1, "MQTT...");
    mqtt_app_start();                         // ★ 启动 MQTT (异步, 后台自动连)

    /* ---- ④ 创建任务 ---- */
    xTaskCreate(pwm_breath_task, "breath", 4096, NULL, 5, NULL);
    xTaskCreate(adc_get_task,    "adc",   4096, NULL, 6, NULL);
    xTaskCreate(sensor_task,     "sensor", 4096, NULL, 4, NULL);  // ★ 代替原来的 dht11_task

    oled_show_string(0, 1, "Running");
    printf("=== System Ready ===\n");
}
```

**改动对照：**

| 原来 | 现在 | 原因 |
|------|------|------|
| 没有 WiFi 初始化 | `wifi_init_sta()` | ESP32 需要连网 |
| 没有 MQTT | `mqtt_app_start()` | 需要上报数据到云端 |
| `xTaskCreate(dht11_task, ...)` | `xTaskCreate(sensor_task, ...)` | `sensor_task` 包含了 DHT11 采集 + JSON 组包 + MQTT 上报 |

---

### A.9 第 8 步：修改 `CMakeLists.txt`

新增 3 个源文件 + 5 个 ESP-IDF 组件：

```cmake
idf_component_register(SRCS "GPIO.c" "hello_world_main.c"
                            "Board/pwm.c" "Board/adc.c" "Board/oled.c"
                            "Board/dht11.c"                       # 你的 DHT11 驱动
                            "Board/wifi_app.c"                    # ★ 新增
                            "Board/mqtt_app.c"                    # ★ 新增
                            "Board/sensor_task.c"                 # ★ 新增
                    INCLUDE_DIRS ""
                    REQUIRES esp_driver_ledc esp_driver_gpio esp_adc
                             esp_driver_i2c esp_lcd esp_driver_rmt
                             esp_wifi      # ★ WiFi 驱动
                             esp_netif      # ★ TCP/IP 协议栈
                             nvs_flash      # ★ 非易失存储
                             esp_event      # ★ 事件系统
                             mqtt)          # ★ MQTT 客户端库
```

**新增的 ESP-IDF 组件说明：**

| 组件 | 作用 | 谁用到 |
|------|------|--------|
| `esp_wifi` | WiFi 驱动 | `wifi_app.c` |
| `esp_netif` | TCP/IP 网络接口 | WiFi 底层 |
| `nvs_flash` | 非易失存储 (Flash) | WiFi 存校准数据 |
| `esp_event` | 事件循环系统 | WiFi 状态通知 |
| `mqtt` | MQTT 客户端协议栈 | `mqtt_app.c` |

---

### A.10 第 9 步：编译前检查清单

在敲 `idf.py build` 之前，确认这几处都改了对的：

```
[ ] wifi_app.c:       WIFI_SSID / WIFI_PASSWORD     → 你的 WiFi
[ ] mqtt_app.c:       ONENET_PRODUCT_ID              → OneNET 控制台
[ ] mqtt_app.c:       ONENET_DEVICE_NAME             → OneNET 控制台
[ ] mqtt_app.c:       ONENET_TOKEN                   → OneNET 控制台
[ ] sensor_task.c:    TOPIC 宏里的 产品ID / 设备名   → 和上面一致
[ ] CMakeLists.txt:   确认 SRCS 列表包含所有 .c 文件
```

### A.11 第 10 步：编译 + 烧录 + 验证

```bash
# 编译
idf.py build

# 烧录 + 看串口
idf.py flash monitor

# 期望的串口输出顺序:
# 1. I (xxx) wifi: WiFi init done, SSID=xxx
# 2. I (xxx) wifi: STA started, connecting...
# 3. I (xxx) wifi: WiFi connected
# 4. I (xxx) wifi: got IP: 192.168.x.x        ← 拿到 IP = 连上了
# 5. I (xxx) mqtt: MQTT starting...
# 6. I (xxx) mqtt: MQTT connected to broker!   ← MQTT 也通了
# 7. I (xxx) sensor: sensor task started
# 8. I (xxx) sensor: temp=24.5°C  hum=68.0%   ← 正在上报
# 9. I (xxx) sensor: published, msg_id=12345   ← 已发送
```

### A.12 移植到其他项目只需要改

要把这套代码搬到另一个 ESP-IDF 项目：

| 要改的 | 改什么 |
|--------|--------|
| `wifi_app.c` | SSID / 密码 |
| `mqtt_app.c` | 产品ID / 设备名 / Token |
| `sensor_task.c` | TOPIC 宏里的 产品ID / 设备名 |
| `sensor_task.c` | 如果你用别的传感器 (如 DHT22 / SHT30)，替换 `dht11_read()` 那一段 |
| `sensor_task.c` | 如果你不用 OneNET，改 `data_to_json()` 的 JSON 格式 |
| `CMakeLists.txt` | 加上这三个 `.c` 文件路径 + 5 个组件名 |

**其他文件完全不用改** — `wifi_app`、`mqtt_app`、`sensor_task` 之间通过函数调用耦合，不依赖具体项目结构。
