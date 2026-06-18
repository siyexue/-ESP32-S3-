# ESP32-S3 MQTT 连 OneNET 断连问题调试记录

> 日期：2026-06-16
> 开发板：ESP32-S3 | 传感器：DHT11 | 平台：OneNET（OneJSON 协议）
> IDE：VS Code + ESP-IDF 扩展

---

## 目录

1. [问题现象](#一问题现象)
2. [调试过程](#二调试过程)
3. [根因定位过程](#三根因定位过程)
4. [解决方案汇总](#四解决方案汇总)
5. [最终代码状态](#五最终代码状态)
6. [经验总结](#六经验总结)

---

## 一、问题现象

烧录后串口日志如下：

```
I (2511) MQTT: MQTT CONNECTED               ← MQTT 连上了
I (3211) sensor: publish successful          ← 发布 "成功"
I (3211) sensor: publish json: {"id":2832...}
E (3531) mqtt_client: transport_read(): EOF  ← OneNET 断开连接
I (3541) MQTT: MQTT ERROR
I (3551) MQTT: MQTT DISCONNECT
```

**特征：**
- MQTT 能连上 OneNET
- 发布一次数据后，~300ms OneNET 主动断开连接
- 断开后不再重连（后续 publish 只在本地排队，实际未发出）
- OneNET 控制台显示「登录 → 注销」，无错误信息
- 数据流为空（平台未收到数据）

---

## 二、调试过程

### 第 1 步：编译错误 — 头文件路径

**错误信息：**

```
Board/dht11.h: No such file or directory
```

**分析：**
- `sensor_task.c` 在 `main/distance/` 目录
- `dht11.h` 在 `main/Board/` 目录
- 源文件用 `#include "Board/dht11.h"`，编译器优先从**源文件所在目录**搜索
- 实际搜索路径：`main/distance/Board/dht11.h` → 不存在

**修复：** 用相对路径 `../Board/dht11.h`

---

### 第 2 步：编译错误 — 内部私有头文件

**错误信息：**

```
esp_timer_impl.h: No such file or directory
```

**分析：**
- `sensor_task.c` 包含了 `esp_timer_impl.h`
- 这是 ESP-IDF `esp_timer` 组件的**内部私有头文件**，用户代码不该引用
- 代码只用到了 `esp_timer_get_time()`，已经在 `esp_timer.h` 中声明

**修复：** 删除 `#include "esp_timer_impl.h"`

---

### 第 3 步：编译错误 — 函数未声明 / static 不可见

**错误信息：**

```
'dht11_init' undeclared (first use in this function)
'GPIO_NUM_4' undeclared
```

**分析：**

`dht11.c` 中两个关键函数被声明为 `static`：

```c
static esp_err_t dht11_init(gpio_num_t gpio)   // ← static，外部不可见
static esp_err_t dht11_read(dht11_data_t *data) // ← static，外部不可见
```

`dht11.h` 也只声明了 `dht11_task()`，没有 `dht11_init` 和 `dht11_read`。

**修复方案（待执行）：**
1. `dht11.h` — 添加 `dht11_init` 和 `dht11_read` 声明
2. `dht11.c` — 去掉 `static`
3. `sensor_task.c` — 添加 GPIO 头文件

---

### 第 4 步：代码设计 — 重复结构体

```c
// dht11.h
typedef struct { float tem; float hum; } dht11_data_t;

// sensor_task.h
typedef struct { float hum; float tem; } sensor_data_t;  // 字段顺序不同！
```

两个结构体存同样的数据（两个 float），但 **字段顺序不同**。用 `{ dht.tem, dht.hum }` 初始化时会导致**温度湿度赋值反了**。

**当前处理：** 使用 `#define sensor_data_t dht11_data_t` 别名统一类型。

---

### 第 5 步：运行时 MQTT 连接后断开（核心问题）

这是最耗时的一步。从 Topic 到 JSON 格式，来回尝试了多种组合。

#### 5.1 原始状态 — `dp/post/json` + 简单 JSON

```
Topic: $sys/.../dp/post/json
JSON: {"id":2832,"temperature":24.0,"humidity":62}
```
→ ❌ OneNET 断开连接

#### 5.2 改 JSON 加 `dp` 包装

```json
{"id":2832,"dp":{"temperature":[{"v":24.0}],"humidity":[{"v":62}]}}
```
→ ❌ 仍然断开

#### 5.3 改物模型标识符

| 功能 | 代码里用的 | 平台实际标识符 |
|------|-----------|--------------|
| 温度 | `temperature` | `temp_value` |
| 湿度 | `humidity` | `humidity_value` |

```json
{"id":2832,"dp":{"temp_value":[{"v":24.0}],"humidity_value":[{"v":62}]}}
```
→ ❌ 仍然断开

#### 5.4 试 `custome/up` Topic（自定义格式）

```
Topic: $sys/.../custome/up
```
→ ✅ 连接稳定，OneNET 回复 `up_reply`，数据成功到达
→ ⚠️ 但 OneNET 控制台看不到数据（自定义格式不自动解析）

#### 5.5 确认产品协议是 OneJSON

查 OneNET 产品概况 → 数据协议：**OneJSON**（不是自定义格式，也不是普通物模型）

根据 OneJSON 文档：

```
Topic: $sys/{pid}/{device-name}/thing/property/post
JSON:
{
  "id": "123",
  "version": "1.0",
  "params": {
    "temp_value": { "value": 24.5 },
    "humidity_value": { "value": 62 }
  }
}
```

```
响应 Topic: $sys/{pid}/{device-name}/thing/property/post/reply
响应:
{
  "id": "123",
  "code": 200,
  "msg": "xxxx"
}
```

→ ✅ 连接稳定，OneNET 返回了**明确的错误信息**：
```json
{"code":2415,"msg":"invalid time:identifier:temp_value"}
```

#### 5.6 去掉无效的 time 字段

JSON 中包含了 `time` 字段，但传入的是 ESP32 启动毫秒数（如 `2812`），不是 Unix 时间戳。OneNET 校验不通过。`time` 是可选字段，直接删除。

```json
{"id":"2812","version":"1.0","params":{"temp_value":{"value":24.0},"humidity_value":{"value":63}}}
```
→ ✅ 连接稳定，OneNET 回复 `{"code":200}`，数据出现在数据流

---

## 三、根因定位过程

```
问题现象：MQTT 发布一次后断开
    ↓
第 1 层：JSON 格式不对 → 加了 dp 包装 → 无效
    ↓
第 2 层：物模型标识符不匹配 → 改成 temp_value/humidity_value → 无效
    ↓
第 3 层：Topic 不对
    ├─ 试 custome/up → 连接稳定了！数据能发出去
    ├─ 但数据在平台看不到（自定义格式不解析）
    └─ 确认协议是 OneJSON（不是自定义格式）
    ↓
第 4 层：OneJSON 用 thing/property/post Topic
    ├─ 连接稳定，OneNET 返回错误码
    └─ "invalid time" → time 字段值不对
    ↓
最终：去掉 time 字段 → code:200 → 数据流可见 ✅
```

---

## 四、解决方案汇总

### 文件：`main/distance/sensor_task.c`

| 问题 | 修改 |
|------|------|
| include 路径错误 | `"Board/dht11.h"` → `"../Board/dht11.h"` |
| 包含私有头文件 | 删除 `#include "esp_timer_impl.h"` |
| JSON 标识符不匹配 | `temperature` → `temp_value`，`humidity` → `humidity_value` |
| Topic 与协议不匹配 | `$sys/.../dp/post/json` → `$sys/.../thing/property/post` |
| 重复结构体 | `#define sensor_data_t dht11_data_t` 统一类型 |
| JSON 格式错误 | `dp` + `[{ "v": X }]` → `params` + `{"value": X}` |
| `id` 类型错误 | 数字 → 字符串 `"\"%lu\""` |
| `time` 字段错误 | 移除了 `time` 字段（需要 Unix 时间戳，不能用运行时长） |

### 文件：`main/Board/dht11.h`（待执行）

- 添加 `dht11_init` / `dht11_read` 函数声明
- 包含 `esp_err.h` 和 `hal/gpio_types.h`

### 文件：`main/Board/dht11.c`（待执行）

- 去掉 `dht11_init` 和 `dht11_read` 的 `static`

### 文件：`main/distance/mqtt.c`

- 订阅 Topic：`$sys/.../dp/post/json/accepted` → `$sys/.../thing/property/post/reply`

---

## 五、最终代码状态

### sensor_task.c（关键片段）

```c
#define TOPIC  "$sys/d17QeNOY7J/ESP32/thing/property/post"

static void sensor_data_to_json(const sensor_data_t *d, char *buf, size_t size)
{
    unsigned long now_ms = (unsigned long)(esp_timer_get_time() / 1000);
    snprintf(buf, size,
             "{"
             "\"id\":\"%lu\","
             "\"version\":\"1.0\","
             "\"params\":{"
             "\"temp_value\":{\"value\":%.1f},"
             "\"humidity_value\":{\"value\":%d}"
             "}"
             "}",
             now_ms,
             d->tem,
             (int)d->hum);
}
```

**最终 JSON 示例：**
```json
{"id":"2812","version":"1.0","params":{"temp_value":{"value":24.0},"humidity_value":{"value":63}}}
```

### mqtt.c（关键片段）

```c
case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG,"MQTT CONNECTED");
    esp_mqtt_client_subscribe(mqtt_handle,
        "$sys/d17QeNOY7J/ESP32/thing/property/post/reply", 0);
    break;

case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "topic=%.*s  data=%.*s",
             evt->topic_len, evt->topic,
             evt->data_len,  evt->data);
    break;
```

---

## 六、经验总结

1. **先确认产品协议类型再写代码**
   - OneNET 有多种协议：OneJSON、自定义数据格式、旧版 MQTT
   - Topic 和 JSON 格式完全不同，混用会导致连接断开

2. **OneJSON 协议要点**
   - Topic：`$sys/.../thing/property/post`
   - JSON 用 `params` 包装，属性用 `{"value": X}`
   - `id` 必须是字符串
   - `time` 字段可选，传入则必须是 Unix 毫秒时间戳
   - 订阅响应 Topic：`thing/property/post/reply`

3. **`esp_mqtt_client_publish` 返回正数 ≠ 发布成功**
   - 返回值只是本地消息队列 ID，不代表服务器已收到
   - 要确认发布成功，需要订阅响应 Topic 看 `code`

4. **OneNET 的响应 Topic 会返回具体错误**
   - 如 `{"code":2415,"msg":"invalid time:identifier:temp_value"}`
   - 比单纯看「是否断开连接」有用得多

5. **ESP-IDF 内部头文件（`_impl.h`、`_priv.h`）不应被用户代码引用**

6. **头文件搜索规则：** `#include "xxx.h"` 优先从源文件所在目录搜索

7. **物模型标识符必须和 JSON 键名完全一致**，大小写和下划线敏感