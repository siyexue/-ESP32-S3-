# ESP32-S3 智能物联网综合示例

一个基于 **ESP32-S3 + ESP-IDF** 构建的嵌入式物联网项目，集成 **Wi-Fi 联网、MQTT 云端上报、BLE 调光、OLED 显示、DHT11 温湿度采集、ADC 电压采样、PWM 呼吸灯** 等能力，适合作为智能硬件、课程设计、物联网入门与功能整合实践的参考工程。

## 项目简介

这是一个面向 **ESP32-S3 开发板** 的嵌入式系统项目，通过多任务方式协同管理传感器采集、本地显示、无线通信与蓝牙交互，实现“**本地可观测、近端可控制、远程可上报**”的一体化物联网基础方案。

项目价值在于：它不只是单一外设 Demo，而是一个已经具备**真实项目雏形**的综合示例，适合继续扩展为智能环境监测、家居控制、教学实验或毕业设计项目。

## 项目类型识别

根据仓库结构、构建脚本和源码实现，本项目属于：

- **系统项目**：基于 `ESP-IDF + FreeRTOS` 的嵌入式系统工程
- **工具/设备控制类项目**：面向硬件外设控制与数据采集
- **物联网项目**：具备 `Wi-Fi + MQTT + 云平台` 接入能力

## 项目亮点

- **多模块集成完整**：OLED、DHT11、ADC、PWM、BLE、Wi-Fi、MQTT 已整合到同一工程中
- **基于 FreeRTOS 多任务运行**：各功能模块以任务方式并行执行，结构清晰，便于维护与扩展
- **支持远程物联网接入**：通过 MQTT 接入 OneNET 平台，实现温湿度数据上云
- **支持本地交互显示**：OLED 实时显示 ADC 电压采样值，便于调试与现场观察
- **支持近距离蓝牙控制**：通过 BLE GATT 服务写入亮度值，可直接控制 LED 明暗
- **具备网络重连机制**：Wi-Fi 断连后自动退避重连，提升系统稳定性
- **模块拆分规范**：板级驱动、联网逻辑、业务任务分层明确，适合继续演进为正式项目

## 功能特性

- **Wi-Fi STA 联网**
  - 自动初始化网络栈
  - 获取 IP 后再启动 MQTT
  - 断线自动重连

- **MQTT 云端通信**
  - 接入 OneNET MQTT Broker
  - 周期性上报温湿度数据
  - 支持订阅属性上报回复主题

- **DHT11 温湿度采集**
  - 基于 RMT 接收时序数据
  - 定时采集并封装为 JSON
  - 与 MQTT 发布流程联动

- **OLED 显示**
  - 基于 I2C 驱动 SSD1306
  - 提供字符、字符串、数字显示接口
  - 默认用于显示 ADC 电压值

- **ADC 电压采样**
  - 使用 `ADC One Shot` 模式
  - 启用校准，将原始值转换为毫伏电压
  - 周期刷新 OLED 内容

- **PWM 呼吸灯**
  - 基于 `LEDC` 输出 PWM
  - 支持自动呼吸效果
  - 可被 BLE 控制逻辑打断并切换到固定亮度

- **BLE 调光控制**
  - 自定义 GATT Service/Characteristic
  - 手机端写入 `0-100` 亮度值
  - 实时控制 LED 占空比

## 技术栈说明

### 软件技术栈

- **语言**：C
- **构建系统**：CMake
- **开发框架**：ESP-IDF
- **实时系统**：FreeRTOS
- **联网能力**：
  - `esp_wifi`
  - `esp_netif`
  - `esp_event`
  - `mqtt`
- **硬件驱动能力**：
  - `LEDC` PWM
  - `ADC One Shot + Calibration`
  - `I2C`
  - `RMT`
  - `BLE GATT`

### 云平台

- **OneNET MQTT**

当前代码内已经具备连接 OneNET 的参数结构与上报逻辑，适合直接改造成自己的产品 ID、设备名和 Token。

## 环境依赖

### 开发环境

- `ESP-IDF 5.x` 或与当前工程兼容的版本
- `Python 3.x`
- `CMake 3.16+`
- `Ninja` 或 ESP-IDF 默认构建工具链
- 串口驱动正常可用

### 硬件依赖

- `ESP32-S3` 开发板
- `DHT11` 温湿度传感器
- `SSD1306` I2C OLED 显示屏（默认地址 `0x3C`）
- LED 或板载可控灯珠
- 可选模拟输入设备（用于 ADC 采样）
- 支持 BLE 的手机
- 可连接互联网的 Wi-Fi 网络

### 主要软件组件依赖

从 `main/CMakeLists.txt` 可见，本项目依赖以下 ESP-IDF 组件：

```cmake
REQUIRES esp_driver_ledc esp_driver_gpio esp_adc esp_driver_i2c esp_lcd
         esp_driver_rmt esp_wifi esp_netif nvs_flash esp_event mqtt bt
```

## 硬件连接说明

根据当前源码中的默认定义，主要引脚如下：

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| PWM LED | `GPIO1` | 呼吸灯 / BLE 调光输出 |
| ADC 输入 | `GPIO2` | `ADC_CHANNEL_1` 电压采样 |
| DHT11 数据引脚 | `GPIO4` | 温湿度采集 |
| OLED SCL | `GPIO17` | I2C 时钟 |
| OLED SDA | `GPIO18` | I2C 数据 |

如果你的开发板接线不同，请同步修改对应源码中的 GPIO 定义。

## 快速安装与部署

### 1. 获取项目

```bash
git clone <your-repo-url>
cd hello_world
```

### 2. 配置 ESP-IDF 环境

请先确保本机已正确安装并导出 `ESP-IDF` 环境变量。

示例：

```bash
idf.py --version
```

若命令可正常输出版本号，说明环境已基本可用。

### 3. 修改网络与云平台参数

当前项目中存在**写死在源码中的联网参数**，使用前请先按你的实际环境修改。

#### Wi-Fi 配置

文件位置：`main/distance/wifi.c`

```c
#define wifi_id "你的WiFi名称"
#define wifi_password "你的WiFi密码"
```

#### OneNET MQTT 配置

文件位置：`main/distance/mqtt.c`

```c
#define ONENET_PRODUCT_ID "你的产品ID"
#define ONENET_DEVICE_NAME "你的设备名称"
#define ONENET_TOKEN "你的设备Token"
#define MQTT_BROKER_URI "mqtt://studio-mqtt.heclouds.com:1883"
```

#### MQTT 上报主题

文件位置：`main/distance/sensor_task.c`

```c
#define TOPIC "$sys/你的产品ID/你的设备名/thing/property/post"
```

请确保 `TOPIC` 与 OneNET 产品配置保持一致，否则设备可能能连上 MQTT，但数据无法正确入库。

### 4. 编译工程

```bash
idf.py build
```

### 5. 烧录到开发板

```bash
idf.py -p <PORT> flash
```

Windows 常见串口示例：

```bash
idf.py -p COM5 flash
```

### 6. 查看运行日志

```bash
idf.py -p <PORT> monitor
```

若需要退出串口监视，一般可使用：

```bash
Ctrl + ]
```

## 使用教程

### 启动流程说明

设备上电后，主流程大致如下：

1. 初始化 OLED 并清屏
2. 初始化 Wi-Fi，尝试连接网络
3. 在获取 IP 后启动 MQTT
4. 初始化 BLE 外设广播
5. 创建 PWM 呼吸灯任务
6. 创建 ADC 采样任务
7. 创建传感器采集与 MQTT 上报任务

### 运行后你能看到什么

#### 1. OLED 显示

OLED 会周期显示 ADC 采样换算后的电压值，方便观察模拟输入变化。

#### 2. LED 呼吸效果

系统启动后，LED 默认以呼吸灯方式变化亮度。

#### 3. BLE 控制亮度

使用手机 BLE 调试工具搜索设备名称：

```text
ESP32-S3-LED
```

连接后可向以下 GATT 特征写入亮度值：

- **Service UUID**：`0xFFE0`
- **Characteristic UUID**：`0xFFE1`

写入规则：

- 写入 `0-100`：按百分比映射到 PWM 占空比
- 写入后：自动退出呼吸灯循环，切换到固定亮度模式

#### 4. 云端温湿度上报

设备运行后会周期读取 `DHT11` 的温湿度数据，并以 JSON 格式发布到 OneNET MQTT 主题。

示例数据结构如下：

```json
{
  "id": "1710000000",
  "version": "1.0",
  "params": {
    "temp_value": {
      "value": 26.0
    },
    "humidity_value": {
      "value": 61
    }
  }
}
```

### 日志观察建议

若运行正常，串口日志中通常能看到类似信息：

```text
WiFi init done
got IP: xxx.xxx.xxx.xxx
MQTT CONNECTED
publish successful
```

若 Wi-Fi 未连通，则 MQTT 不会启动，这是当前代码中的保护逻辑。

## 项目目录结构

```text
hello_world/
├─ CMakeLists.txt                 # 工程根构建脚本
├─ sdkconfig                      # ESP-IDF 工程配置
├─ dependencies.lock              # 依赖锁定文件
├─ pytest_hello_world.py          # 示例测试脚本
├─ hello_world_project.md         # 项目过程/说明文档
├─ docs/                          # 补充技术文档
│  ├─ WiFi_MQTT物联网详解.md
│  ├─ DHT11_RMT驱动详解.md
│  ├─ BLE调光功能详解.md
│  └─ 调试记录_MQTT连OneNET断连问题.md
├─ main/
│  ├─ CMakeLists.txt              # 主组件构建配置
│  ├─ hello_world_main.c          # 程序入口 app_main
│  ├─ GPIO.c
│  ├─ GPIO.h
│  ├─ Board/                      # 板级驱动与外设模块
│  │  ├─ pwm.c / pwm.h            # PWM 呼吸灯与调光
│  │  ├─ adc.c / adc.h            # ADC 采样
│  │  ├─ oled.c / oled.h          # OLED 显示驱动
│  │  ├─ oled_font.h              # OLED 字库
│  │  ├─ dht11.c / dht11.h        # DHT11 驱动
│  │  └─ ble.c / ble.h            # BLE GATT 调光
│  └─ distance/
│     ├─ wifi.c / wifi.h          # Wi-Fi 初始化与重连
│     ├─ mqtt.c / mqtt.h          # MQTT 连接与发布
│     └─ sensor_task.c / sensor_task.h  # 传感器采集与上报任务
└─ managed_components/            # 组件管理目录
```

## 核心模块说明

### `main/hello_world_main.c`

项目主入口，负责系统初始化与任务调度，是整个工程的启动核心。

### `main/Board/`

负责板级驱动与本地外设交互，包括：

- OLED 显示
- PWM 灯光控制
- ADC 采样
- DHT11 驱动
- BLE 调光服务

### `main/distance/`

负责联网与业务上报逻辑，包括：

- Wi-Fi 连接管理
- MQTT 客户端初始化
- 传感器采集与云端数据发布

## 常见问题 FAQ

### 1. 编译时报找不到 `idf.py` 怎么办？

说明 `ESP-IDF` 环境尚未正确安装或未激活，请先完成官方环境配置，并确保终端中可直接执行 `idf.py`。

### 2. 烧录成功但设备没有联网？

优先检查以下内容：

- `main/distance/wifi.c` 中的 Wi-Fi 名称和密码是否正确
- 路由器是否开启 2.4GHz Wi-Fi
- 开发板天线与供电是否稳定
- 串口日志中是否出现 `got IP`

### 3. MQTT 一直连接失败怎么办？

请重点检查：

- OneNET `Product ID / Device Name / Token` 是否填写正确
- MQTT Broker 地址是否与你的平台接入信息一致
- 设备所在网络是否可访问公网
- 上报主题是否与平台物模型配置一致

仓库中的 [docs/调试记录_MQTT连OneNET断连问题.md](/E:/ESP32-S3/Hello_World/hello_world/docs/调试记录_MQTT连OneNET断连问题.md) 也可作为排查参考。

### 4. DHT11 读数失败或不稳定怎么办？

建议排查：

- 数据线是否接到 `GPIO4`
- 电源与地线连接是否可靠
- 是否加了合适上拉
- 采样间隔是否过短

当前代码已经在任务中加入重试逻辑，但如果硬件接线不稳定，仍可能采集失败。

### 5. BLE 能搜索到设备，但写入后没有效果？

请确认：

- 是否连接到了设备 `ESP32-S3-LED`
- 是否写入到 `0xFFE1`
- 写入值是否在 `0-100` 范围内
- LED 是否接在 `GPIO1`

### 6. OLED 没有显示内容怎么办？

优先检查：

- 屏幕是否为 `SSD1306`
- I2C 地址是否为 `0x3C`
- `SCL/SDA` 是否分别连接到 `GPIO17/GPIO18`
- 电压供电是否匹配

### 7. 这个项目适合做什么扩展？

比较适合继续扩展为：

- 智能环境监测终端
- 家居控制节点
- 本地显示 + 云平台联动设备
- 课程设计 / 毕业设计
- 物联网开发练手项目

## 开发建议

如果你计划把这个项目继续做成更正式的开源工程，建议下一步优先优化：

1. 将 Wi-Fi、Token、Topic 等敏感配置迁移到 `menuconfig` 或独立配置文件
2. 在 README 中加入实物图、接线图和运行效果图
3. 补充平台接入流程图和 BLE 使用截图
4. 增加更明确的错误码说明与日志排障表
5. 为不同开发板整理可选引脚配置


