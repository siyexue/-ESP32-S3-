# DHT11 RMT 驱动详解

> 从原理到代码，逐行拆解。学会后可以自己写任何单线协议外设。

---

## 一、先理解 DHT11 的"语言"

DHT11 只有一根 DATA 线，靠**拉低拉高的时间长度**传递信息，像摩斯电码：

```
┌─ 主机发起 ──────────────────────────────────────────────┐
│                                                         │
│  拉低 18ms+          拉高 30µs         释放总线           │
│  ████████████████    ██                                 │
│                     (等 DHT11 回应)                     │
└─────────────────────────────────────────────────────────┘

┌─ DHT11 回应 ────────────────────────────────────────────┐
│                                                         │
│  拉低 80µs    拉高 80µs    40个数据bit ...               │
│  ████████    ████████    ██  ██  ████  ██  ...          │
│  "我听到了"  "准备发数据"                                │
└─────────────────────────────────────────────────────────┘

┌─ 每个 bit 的含义 ───────────────────────────────────────┐
│                                                         │
│  都是: 拉低 50µs 开头                                    │
│    然后拉高 ~27µs → 表示 bit=0                           │
│    或者拉高 ~70µs → 表示 bit=1                           │
│                                                         │
│     ████████░░                                          │
│     │ 50µs │27µs│  = 0                                  │
│                                                         │
│     ████████████████                                    │
│     │ 50µs │   70µs   │  = 1                            │
└─────────────────────────────────────────────────────────┘

40 个 bit = 5 字节:
  Byte0: 湿度整数  Byte1: 湿度小数(始终0)
  Byte2: 温度整数  Byte3: 温度小数(始终0)
  Byte4: 校验 = Byte0+1+2+3 的低8位
```

**核心难题：如何精确测量 µs 级脉冲？** 用 RMT。

---

## 二、RMT 是什么，为什么用它

RMT 全称 Remote Control，本来是给红外遥控设计的，但它本质上是一个**硬件脉冲计数器**——自动记录"高电平持续了多久，低电平持续了多久"，完全不需要 CPU 参与。

```
你接上 GPIO，启动 RMT 接收：

实际信号:  ──┐      ┌──────┐      ┌──┐      ┌──────
             └──────┘      └──────┘  └──────┘

RMT 内存里:  [低 80µs] [高 80µs] [低 50µs] [高 27µs] ...
             symbol[0]           symbol[1]
              ├─dur0─┤├─dur1─┤   ├─dur0─┤├─dur1─┤

每个 symbol = 一对边沿（低电平+高电平）
duration 单位 = 1 / resolution_hz 秒
```

**为什么不用 GPIO 中断 + 定时器？**

DHT11 的 µs 级时序太精细，CPU 中断延迟抖动（RTOS 调度、ISR 嵌套）就会漏脉冲。RMT 是**纯硬件**记录，零误差。

**和其他方案对比：**

| 方案 | CPU 占用 | 精度 | 多任务友好 |
|------|----------|------|------------|
| GPIO 中断 + 定时器 | 高（每个脉冲都进中断） | 差（ISR 延迟抖动） | ❌ |
| 关中断 + 忙等 | 极高（全程占 CPU） | 中 | ❌ |
| **RMT 硬件** | 零（纯硬件捕获） | 高 | ✅ |

---

## 三、文件设计思路

```
dht11.h ─ 对外接口，只管两件事：初始化 + 读取
dht11.c ─ 对内实现，分四层：
  ① 静态变量区  ─ 保存状态（句柄、缓冲区、信号量）
  ② 回调函数    ─ RMT 收完数据时被硬件中断触发
  ③ 解析函数    ─ 把 RMT 原始数据（脉冲时长）翻译成温湿度
  ④ 对外 API    ─ init() 供用户调用一次，read() 可反复调用
```

**为什么全部用 `static`？**

这些变量和函数只在 dht11.c 内部使用，外部看不到。好处：

- 避免和其他文件的全局变量/函数重名
- 明确"这是内部实现细节，不要依赖它"
- 编译器可以做更激进的优化

**为什么 init 和 read 分开？**

初始化只做一次（分配 RMT 通道、注册回调），read 可能调用很多次。分开避免重复开销。实际项目中，很可能在 `app_main()` 调 init，在任务循环里反复调 read。

---

## 四、逐行拆解

### 4.1 静态变量区

```c
static rmt_channel_handle_t  rx_chan      = NULL;   // RMT 通道句柄
static rmt_symbol_word_t     symbols[128];          // 接收缓冲区
static SemaphoreHandle_t     rx_sem       = NULL;   // 通知信号量
static volatile size_t       rx_sym_count = 0;      // 实际收到几个 symbol
static gpio_num_t            dht_gpio     = GPIO_NUM_NC;  // 用哪个引脚
static bool                  installed    = false;  // 是否已初始化
```

逐个解释：

| 变量 | 为什么需要 | 注意事项 |
|------|-----------|---------|
| `rx_chan` | RMT 的一切操作（enable/disable/receive）都要传这个句柄 | 类似文件描述符，由 `rmt_new_rx_channel()` 分配，用完后应 `rmt_del_channel()` |
| `symbols[128]` | RMT 硬件把脉冲数据写到这里 | 大小 = 预期 symbol 数 × 2，DHT11 最多 42 个，128 足够且必须是偶数 |
| `rx_sem` | `rmt_receive()` 是非阻塞的，需要信号量通知"数据收好了" | 二值信号量，可以理解为"通知牌"：放上 = 有数据，拿走 = 等待中 |
| `rx_sym_count` | 回调里记录实际 symbol 数，解析时要知道几个有效 | `volatile` 因为 ISR 写、主任务读，告诉编译器"别优化这个变量" |
| `dht_gpio` | 初始化时记住引脚号，后续 read 要用 | 为什么不用宏？允许多实例扩展。虽然当前只支持一个，但保留扩展性 |
| `installed` | 防止重复初始化，read 时校验是否已 init | 防御性编程：如果用户忘了 init 就 read，返回明确的错误码而不是 crash |

---

### 4.2 RMT 回调函数

```c
static bool rmt_rx_done_cb(rmt_channel_handle_t chan,
                           const rmt_rx_done_event_data_t *edata,
                           void *user_ctx)
{
    rx_sym_count = edata->num_symbols;  // ← 记录实际 symbol 数
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(rx_sem, &wake);  // ← 发出"收完了"信号
    return (wake == pdTRUE);  // ← 告诉 RTOS：需要立即任务切换
}
```

**参数说明：**

| 参数 | 含义 | 谁传的 |
|------|------|--------|
| `chan` | 哪个 RMT 通道收完了 | RMT 驱动 |
| `edata` | 事件数据，包含 `received_symbols`（指向 buffer）和 `num_symbols`（实际数量） | RMT 驱动 |
| `user_ctx` | 注册回调时传入的自定义数据 | 你（本驱动传 NULL） |

**关键细节：**

- **为什么是 `FromISR`？** 回调在 RMT 中断服务程序里执行，必须用 ISR 版本的 FreeRTOS API。普通 `xSemaphoreGive()` 在 ISR 上下文里会直接 crash。
- **`wake` 参数：** ISR 结束时 RTOS 会检查这个值。如果 ISR 把它设成了 `pdTRUE`，RTOS 会立刻切换到等待信号量的任务，不用等到下一个 tick（最多省 1ms）。
- **返回值：** 返回 `true` 等同于 `wake = pdTRUE` 的效果，都是告诉 RTOS 有高优先级任务需要调度。
- **`edata->num_symbols`：** RMT 驱动告诉回调"收到了几个 symbol"。**不一定是 buffer 大小**——比如只收到 42 个 symbol，symbols[42] 到 symbols[127] 就是垃圾数据，解析时不能碰。
- **`edata->received_symbols`：** 指向同一个 buffer（就是你传给 `rmt_receive()` 的那个），所以不用拷贝数据，直接记个数就行。

---

### 4.3 解析函数

```c
#define DHT11_THRESHOLD_US  45  // 判 0/1 的阈值

static esp_err_t dht11_parse(const rmt_symbol_word_t *sym, size_t count,
                             dht11_data_t *out)
```

**为什么阈值取 45？**

```
bit 0: 高电平 ~27µs  ←  <45µs
bit 1: 高电平 ~70µs  ←  >45µs

45 = (27+70)/2 ≈ 48，取 45 留点余量偏 bit 0 方向
因为实际电路上电容可能拉长脉冲，但不会缩短
```

**参数设计：**

| 参数 | 含义 | 为什么这样设计 |
|------|------|---------------|
| `sym` | RMT 捕获的 symbol 数组 | 作为参数传入而不是用全局变量 → 函数可以独立测试 |
| `count` | 实际 symbol 个数 | 用回调解出的 `rx_sym_count`，不解析垃圾数据 |
| `out` | 输出参数，温湿度结果写这里 | 让 `return` 用来返回错误码，符合 ESP-IDF 惯例 |

**解析逻辑逐句解释：**

```c
if (count < 41) {
    ESP_LOGW(TAG, "too few symbols: %zu (need ≥41)", count);
    return ESP_ERR_INVALID_SIZE;
}
```

为什么 ≥41？因为 `sym[0]` 是 DHT11 的响应帧（80µs 低 + 80µs 高），`sym[1]` 到 `sym[40]` 是 40 个数据 bit。如果收到的 symbol 少于 41，说明传输不完整。

```c
uint8_t bytes[5] = {0};

for (int i = 0; i < 40; i++) {
    uint16_t dur = sym[i + 1].duration1;  // +1 跳过响应帧 sym[0]
    if (dur > DHT11_THRESHOLD_US) {
        bytes[i / 8] |= (1 << (7 - (i % 8)));
    }
}
```

逐句：

- `sym[i + 1]`：跳过 `sym[0]`（DHT11 响应帧），从第一个数据 symbol 开始
- `.duration1`：每个 RMT symbol 有两个 duration——`duration0`（低电平长度）和 `duration1`（高电平长度）。解码只看高电平
- `i / 8`：第几个字节（0~4），因为 40 bit = 5 字节
- `7 - (i % 8)`：bit 在字节中的位置。DHT11 先发高位（MSB first），所以第 0 个 bit 对应 bit7，第 7 个 bit 对应 bit0
- `|=` 而不是 `=`：因为一个字节 8 bit，不能覆盖之前的位

**举例：收到 byte[0] = 0b01000101 (0x45)**

```
bit 顺序 (MSB first):  bit7  bit6  bit5  bit4  bit3  bit2  bit1  bit0
                        0     1     0     0     0     1     0     1

i=0: dur=28 → <45 → bit=0, 不改 (byte[0] bit7 保持 0)
i=1: dur=72 → >45 → bit=1, byte[0] bit6 = 1
i=2: dur=27 → <45 → bit=0
i=3: dur=26 → <45 → bit=0
i=4: dur=28 → <45 → bit=0
i=5: dur=71 → >45 → bit=1, byte[0] bit2 = 1
i=6: dur=25 → <45 → bit=0
i=7: dur=73 → >45 → bit=1, byte[0] bit0 = 1

结果: byte[0] = 0b01000101 = 0x45 = 湿度 69%
```

**校验逻辑：**

```c
uint8_t cks = bytes[0] + bytes[1] + bytes[2] + bytes[3];
if (cks != bytes[4]) {
    ESP_LOGE(TAG, "checksum err: %02X%02X%02X%02X%02X (exp=%02X)",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], cks);
    return ESP_ERR_INVALID_CRC;
}
```

前 4 字节求和，低 8 位应该等于第 5 字节。校验不通过说明传输有干扰或接线接触不良。

**输出：**

```c
out->humidity    = (float)bytes[0];   // DHT11 小数部分始终为 0
out->temperature = (float)bytes[2];
```

DHT11 的小数部分（bytes[1] 和 bytes[3]）始终为 0。用 `float` 是为了兼容 DHT22（它真的有小数）。

---

### 4.4 初始化函数

```c
esp_err_t dht11_init(gpio_num_t gpio)
```

**每一步的含义：**

```c
if (installed) return ESP_OK;
dht_gpio = gpio;
```

幂等设计：重复调用不会出错。这在多模块初始化时很有用——你不知道别人是否已经 init 过。

```c
rx_sem = xSemaphoreCreateBinary();
if (!rx_sem) return ESP_ERR_NO_MEM;
```

二值信号量，初始状态是"空"（没有通知）。为什么不用计数信号量？因为只需要两个状态：有数据 / 没数据，二值比计数省内存。

```c
rmt_rx_channel_config_t rx_cfg = {
    .gpio_num          = gpio,
    .clk_src           = RMT_CLK_SRC_DEFAULT,
    .resolution_hz     = 1 * 1000 * 1000,  /* 1 MHz → 1 tick = 1µs */
    .mem_block_symbols = 128,
};
ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx_chan));
```

**RMT 时钟源选择：**

| 参数 | 值 | 为什么 |
|------|-----|--------|
| `clk_src` | `RMT_CLK_SRC_DEFAULT` | 默认时钟源（通常是 80MHz APB），由底层驱动自动选 |
| `resolution_hz` | `1,000,000`（1MHz） | 1 tick = 1µs，duration 值直接就是微秒，不用换算 |
| `mem_block_symbols` | 128 | 每个 RMT 内存块 64 symbol，128 = 2 块。必须是偶数。DHT11 接收最多 42 个，128 有 3 倍余量 |

**为什么选 1MHz 分辨率？** DHT11 最窄脉冲 ~27µs，1µs 精度足够分辨 27 vs 70。设太高（如 40MHz）没必要，而且 16-bit 的 duration 字段也装得下（80µs × 40M = 3200 ticks，远小于 32767 的 int16 max）。

**`ESP_ERROR_CHECK` 的作用：**

```c
ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx_chan));
// 等价于:
// esp_err_t err = rmt_new_rx_channel(&rx_cfg, &rx_chan);
// if (err != ESP_OK) {
//     ESP_LOGE("...", "error: %s", esp_err_to_name(err));
//     esp_restart();  // 重启芯片
// }
```

用于那些"失败就没办法继续"的关键操作（比如分配硬件资源）。非关键操作（比如读取传感器失败）用手动检查。

**回调注册：**

```c
rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_cb };
ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, NULL));
```

只注册 `on_recv_done` 一个回调，最后一个参数 `NULL` 是不需要传递 user_data。如果你需要向回调传自定义数据（比如多实例区分），就把 `NULL` 换成指针。

**使能通道：**

```c
ESP_ERROR_CHECK(rmt_enable(rx_chan));
```

`rmt_new_rx_channel` 只是创建（申请资源），但还没供电。`rmt_enable` 才真正上电、连接 GPIO、启动时钟。

---

### 4.5 读取函数（最核心）

```c
esp_err_t dht11_read(dht11_data_t *data)
```

**完整流程图：**

```
rmt_disable()      → 释放 GPIO
gpio_config()      → 设为输出
拉低 20ms          → 主机起始信号
拉高 30µs          → 释放总线
rmt_enable()       → RMT 重新接管 GPIO
清空信号量         → 防止旧通知干扰
rmt_receive()      → 启动硬件捕获
等信号量 (100ms)   → 阻塞等待 DHT11 回应
     ↓ 超时 → ESP_ERR_TIMEOUT
     ↓ 收到 → dht11_parse()
              ↓ 失败 → ESP_ERR_INVALID_CRC
              ↓ 成功 → return ESP_OK
```

#### 第一步：发送起始信号

```c
rmt_disable(rx_chan);     // 释放 GPIO，RMT 不再控制它

gpio_config_t io_cfg = {
    .pin_bit_mask = (1ULL << dht_gpio),
    .mode         = GPIO_MODE_OUTPUT,
    .pull_up_en   = GPIO_PULLUP_ENABLE,
};
gpio_config(&io_cfg);          // 配成输出模式

gpio_set_level(dht_gpio, 0);  // 拉低
vTaskDelay(pdMS_TO_TICKS(20)); // 保持 20ms
gpio_set_level(dht_gpio, 1);  // 拉高
esp_rom_delay_us(30);          // 保持 30µs
```

**为什么先 `rmt_disable`？** RMT 占用 GPIO 时，你不能同时把它当普通 GPIO 用。disable 后 GPIO 自由了。

**为什么拉低 20ms 用 `vTaskDelay`，拉高 30µs 用 `esp_rom_delay_us`？**

| 函数 | 行为 | 适用场景 |
|------|------|---------|
| `vTaskDelay(20ms)` | 让出 CPU 给其他任务 | 长时间等待（ms 级） |
| `esp_rom_delay_us(30)` | 忙等，不释放 CPU | 短时间等待（µs 级） |

20ms 太长，如果忙等会浪费 CPU，DHT11 又没有时序精度要求（18~30ms 都行）。30µs 太短，上下文切换开销（~10µs）就比等待时间还长，只能忙等。

**为什么 DHT11 需要 18ms 以上的低电平？**

DHT11 内部固件通过检测低电平长度来区分"主机想读数据"还是"线路干扰"。短的低电平（<1ms）会被忽略，长的低电平（>18ms）才触发采样。

#### 第二步：交还 RMT，启动接收

```c
rmt_enable(rx_chan);  // RMT 重新接管 GPIO（自动配成输入+上拉）

xSemaphoreTake(rx_sem, 0);  // 清空信号量残留
```

**为什么先清信号量？** 如果上次读失败（比如超时），回调没触发，但信号量可能已经被其他路径给了。用 timeout=0 非阻塞清掉，保证后续等待的是本次接收完成。

```c
rmt_receive_config_t rcv_cfg = {
    .signal_range_min_ns = 5000,   // <5µs 视为毛刺，忽略
    .signal_range_max_ns = 100000, // >100µs 视为结束，停止接收
};
esp_err_t ret = rmt_receive(rx_chan, symbols, sizeof(symbols), &rcv_cfg);
```

**`signal_range_min_ns` = 5000（5µs）：**
滤除 <5µs 的尖峰干扰（电源噪声、继电器火花等）。DHT11 最短有效脉冲是 27µs，设 5µs 安全。

**`signal_range_max_ns` = 100000（100µs）：**
DHT11 最长脉冲 80µs。当 RMT 检测到一个脉冲超过 100µs，它认为"传输结束了，之后是高电平空闲"，触发 `on_recv_done` 回调。

**如果没有这个上限会怎样？** RMT 会一直等，直到 buffer 填满才触发回调。但 DHT11 发完 40 bit 后释放总线（通过上拉电阻回到高电平），RMT 看不到脉冲就一直不动。所以 max_ns 充当"超时检测"。

#### 第三步：等待完成

```c
if (xSemaphoreTake(rx_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
    ESP_LOGW(TAG, "timeout — no response from DHT11");
    return ESP_ERR_TIMEOUT;
}
```

等待 100ms。DHT11 传输约 5ms，100ms 绰绰有余。如果超时：

- GPIO 接错了？（DATA 不是 init 时传入的引脚）
- DHT11 坏了？或者没供电？
- 上次读取不到 1 秒？（DHT11 需要采样间隔 ≥1 秒）

#### 第四步：解析

```c
return dht11_parse(symbols, rx_sym_count, data);
```

这里 `rx_sym_count` 来自回调（§4.2 第一条语句），是 RMT 硬件实际捕获到的 symbol 数量。

---

## 五、通用套路：用 RMT 驱动任意单线协议

任何"靠脉冲长度编码"的单线协议（DHT11/22、DS18B20、WS2812、红外 NEC 协议），套路完全一样。

### 5.1 通用模板

```
1. 分析数据手册的脉冲时序
   → 最短脉冲多窄？最长脉冲多宽？
   → 根据这个选 RMT resolution_hz
   → 根据这个设置 signal_range_min_ns / max_ns

2. 初始化（只做一次）：
   rmt_new_rx_channel()         → 分配 RMT 通道
   rmt_rx_register_event_callbacks() → 注册接收完成回调
   rmt_enable()                 → 使能通道

3. 读取（每次采样调一次）：
   rmt_disable()   → 释放 GPIO
   GPIO 发起始信号 → 根据协议拉低/拉高特定时间
   rmt_enable()    → RMT 重新接管
   rmt_receive()   → 启动硬件捕获
   等信号量 (回调发) → 阻塞等待传输完成
   解析 symbol[]    → 把脉冲时长翻译成数据
```

### 5.2 各协议参数速查

| 协议 | 最短脉冲 | 最长脉冲 | resolution_hz | signal_range_max_ns | 备注 |
|------|----------|----------|---------------|---------------------|------|
| DHT11 | 27µs | 80µs | 1MHz | 100,000 | 需要主机发起始信号 |
| DHT22 | 27µs | 80µs | 1MHz | 100,000 | 同 DHT11，但数据格式不同 |
| DS18B20 | 1µs | 480µs | 1MHz | 500,000 | OneWire，时序更复杂 |
| WS2812 | 0.35µs | 0.9µs | 40MHz | 5,000 | LED 灯带，用 RMT 编码器模式 |
| NEC 红外 | 560µs | 1690µs | 1MHz | 2,000,000 | 红外遥控，38kHz 载波 |

### 5.3 关键技巧总结

| 技巧 | 为什么 |
|------|--------|
| `resolution_hz = 1MHz` | duration 值直接就是 µs，不用换算，可读性强 |
| `signal_range_max_ns` 稍宽于最长脉冲 | RMT 靠超时判断"传输结束"，设太窄会中途截断 |
| `symbols[]` 缓冲区 ≥ 预期 2 倍 | 防止溢出丢数据，RMT buffer 满了后续脉冲会被丢弃 |
| `volatile` 标记 ISR 共享变量 | 编译器不会把"看起来没变的变量"优化成寄存器缓存 |
| 用信号量而不是忙等 | CPU 空闲时其他任务继续跑（呼吸灯、WiFi 等） |
| 定义阈值宏而不是写死数字 | `DHT11_THRESHOLD_US 45` 比 `45` 可读性好得多 |
| `ESP_ERROR_CHECK` 只用于致命错误 | 初始化失败重启，读数失败重试——不同级别 |
| 长延时用 `vTaskDelay`，短延时用 `esp_rom_delay_us` | 长延时让 CPU，短延时精度高 |

### 5.4 多实例扩展

当前驱动只支持一个 DHT11。如果以后要接多个，改动点：

```c
// 把所有状态封进一个结构体
typedef struct {
    rmt_channel_handle_t  rx_chan;
    rmt_symbol_word_t     symbols[128];
    SemaphoreHandle_t     rx_sem;
    volatile size_t       rx_sym_count;
    gpio_num_t            gpio;
} dht11_ctx_t;

// init 返回 context 指针
dht11_ctx_t *dht11_init(gpio_num_t gpio);

// read 接受 context 指针
esp_err_t dht11_read(dht11_ctx_t *ctx, dht11_data_t *data);

// 回调里通过 user_ctx 拿到 context
static bool rmt_rx_done_cb(..., void *user_ctx) {
    dht11_ctx_t *ctx = (dht11_ctx_t *)user_ctx;
    ctx->rx_sym_count = edata->num_symbols;
    xSemaphoreGiveFromISR(ctx->rx_sem, &wake);
    ...
}
```

---

## 六、常见问题排查

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 第一次读到全 0 | DHT11 上电后需要 ≥1 秒稳定 | 首次读之前加 `vTaskDelay(pdMS_TO_TICKS(1100))` |
| 经常校验失败 | 没有外部上拉电阻或上拉太弱 | 在 DATA 和 VCC 间接 4.7kΩ~10kΩ 电阻 |
| 总是超时 | GPIO 接错了 | 用 `adc_oneshot_channel_to_io()` 类似的 API 确认引脚号 |
| `rmt_receive()` 返回错误 | 该 GPIO 的 RMT 功能冲突（其他外设已占用） | 换一个 GPIO 试试 |
| 编译报 `rmt_symbol_word_t` 未定义 | CMakeLists.txt 没 require `esp_driver_rmt` | 在 `idf_component_register` 里加 `REQUIRES esp_driver_rmt` |
| 采样间隔 <1s 时第二次总失败 | DHT11 内部限制，至少 1 秒冷却 | 在 read 之间 `vTaskDelay(1000ms)` |
| 呼吸灯被卡住 | 不是 DHT11 的问题，是 I2C OLED 阻塞 | 参考 OLED 时序分析文档 |

---

## 七、相关文件

- 驱动源码: `main/Board/dht11.h`, `main/Board/dht11.c`
- ADC 驱动: `main/Board/adc.c` — 同样是 RMT 思路，但用的是 ADC OneShot API
- PWM 呼吸灯: `main/Board/pwm.c` — LEDC 外设，纯硬件，不受 DHT11 影响
- ESP-IDF RMT 官方文档: [RMT Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/rmt.html)

dht11.c
#include "dht11.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "dht11";

static rmt_channel_handle_t  rx_chan      = NULL;
static rmt_symbol_word_t     symbols[128];
static SemaphoreHandle_t     rx_sem       = NULL;
static volatile size_t       rx_sym_count = 0;
static gpio_num_t            dht_gpio     = GPIO_NUM_NC;
static bool                  installed    = false;

/* ==================== RMT 回调 ==================== */

static bool rmt_rx_done_cb(rmt_channel_handle_t chan,
                           const rmt_rx_done_event_data_t *edata,
                           void *user_ctx)
{
    rx_sym_count = edata->num_symbols;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(rx_sem, &wake);
    return (wake == pdTRUE);
}

/* ==================== 位解析 ==================== */

/**
 * DHT11 单 bit 时序:
 *  - 起始: 低 50µs + 高 26~28µs → bit=0
 *  - 起始: 低 50µs + 高 70µs    → bit=1
 *
 * RMT 每个 symbol 记录两个边沿:
 *   duration0 = 低电平持续 (µs),  level0 = 0
 *   duration1 = 高电平持续 (µs),  level1 = 1
 *
 * symbol[0]:    DHT11 响应帧 (低 80µs + 高 80µs), 跳过
 * symbol[1~40]: 40 个数据 bit
 *
 * 解码: duration1 > 45µs → 1
 */
#define DHT11_THRESHOLD_US  45

static esp_err_t dht11_parse(const rmt_symbol_word_t *sym, size_t count,
                             dht11_data_t *out)
{
    if (count < 41) {
        ESP_LOGW(TAG, "too few symbols: %zu (need ≥41)", count);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t bytes[5] = {0};

    for (int i = 0; i < 40; i++) {
        uint16_t dur = sym[i + 1].duration1;  /* +1 跳过响应帧 */
        if (dur > DHT11_THRESHOLD_US) {
            bytes[i / 8] |= (1 << (7 - (i % 8)));
        }
    }

    uint8_t cks = bytes[0] + bytes[1] + bytes[2] + bytes[3];
    if (cks != bytes[4]) {
        ESP_LOGE(TAG, "checksum err: %02X%02X%02X%02X%02X (exp=%02X)",
                 bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], cks);
        return ESP_ERR_INVALID_CRC;
    }

    out->humidity    = (float)bytes[0];
    out->temperature = (float)bytes[2];
    return ESP_OK;
}

/* ==================== 初始化 ==================== */

esp_err_t dht11_init(gpio_num_t gpio)
{
    if (installed) return ESP_OK;
    dht_gpio = gpio;

    /* 创建二值信号量 (RMT 回调中使用) */
    rx_sem = xSemaphoreCreateBinary();
    if (!rx_sem) return ESP_ERR_NO_MEM;

    /* RMT RX 通道 */
    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num          = gpio,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 1 * 1000 * 1000,  /* 1 MHz → 1 tick = 1µs */
        .mem_block_symbols = 128,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &rx_chan));

    /* 注册回调 */
    rmt_rx_event_callbacks_t cbs = { .on_recv_done = rmt_rx_done_cb };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx_chan, &cbs, NULL));

    /* 使能通道 */
    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    installed = true;
    ESP_LOGI(TAG, "init GPIO%d", gpio);
    return ESP_OK;
}

/* ==================== 读取 ==================== */

esp_err_t dht11_read(dht11_data_t *data)
{
    if (!installed) return ESP_ERR_INVALID_STATE;

    /* ---- 1. 释放 GPIO，发送主机起始信号 ---- */
    rmt_disable(rx_chan);

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << dht_gpio),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_cfg);

    /* 拉低 >18ms */
    gpio_set_level(dht_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 拉高 30µs */
    gpio_set_level(dht_gpio, 1);
    esp_rom_delay_us(30);

    /* ---- 2. 交还 RMT，启动接收 ---- */
    rmt_enable(rx_chan);  /* 会重新配置 GPIO 为 RMT 输入 */

    /* 清空信号量（可能残留上次的值） */
    xSemaphoreTake(rx_sem, 0);

    rmt_receive_config_t rcv_cfg = {
        .signal_range_min_ns = 5000,   /* 滤除 <5µs 毛刺 */
        .signal_range_max_ns = 100000, /* 最大脉宽 100µs */
    };
    esp_err_t ret = rmt_receive(rx_chan, symbols, sizeof(symbols), &rcv_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "rmt_receive failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 等待接收完成 (DHT11 一次传输约 5ms, 超时设 100ms 足够) */
    if (xSemaphoreTake(rx_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "timeout — no response from DHT11");
        return ESP_ERR_TIMEOUT;
    }

    /* ---- 3. 解析 ---- */
    return dht11_parse(symbols, rx_sym_count, data);
}
dht11.h
#ifndef __DHT11_H_
#define __DHT11_H_

#include <stdbool.h>
#include "hal/gpio_types.h"
#include "esp_err.h"

typedef struct {
    float temperature;  // °C
    float humidity;     // %
} dht11_data_t;

/**
 * @brief  Initialize the DHT11 driver (RMT-based, non-blocking)
 *
 * @param gpio  GPIO pin connected to DHT11 DATA line
 * @return      ESP_OK on success
 */
esp_err_t dht11_init(gpio_num_t gpio);

/**
 * @brief  Read temperature and humidity from DHT11
 *
 * @param data  Output struct, populated with temperature (°C) and humidity (%)
 * @return      ESP_OK on success, ESP_ERR_TIMEOUT if sensor not responding
 */
esp_err_t dht11_read(dht11_data_t *data);

#endif
