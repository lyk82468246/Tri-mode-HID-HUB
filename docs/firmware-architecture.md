# 固件系统架构与六阶段 Roadmap

状态：Architecture v0.4。Milestone 1 已落地 TMOS 基础、静态 SPSC Ring、USB Device HID/CDC 复合输出和 CDC 回送；Milestone 2 已落地 BLE HOGP/NUS-compatible 输出；Milestone 3 已落地 PS/2 与 UART 输入适配器。USB Host、活跃链路策略和最终可靠性收口仍待后续里程碑。

## 0. 约束与芯片容量校准

本设计遵循以下硬约束：

- 只使用 WCH 官方 BLE SDK 提供的 TMOS 事件调度模型，不引入 FreeRTOS。
- 应用代码不调用 `malloc/free`；所有应用层缓冲区在编译期静态分配，并使用 `__attribute__((aligned(4)))` 对齐。
- ISR 只采样硬件事件、写入 SPSC Ring Buffer、置位标志或投递 TMOS 事件；协议解析、报表转换和发送都在 TMOS 任务中完成。
- Ring Buffer 存放值而不是指向临时 DMA/端点内存的指针；队列项出队后由消费者拥有，发送完成前不得复用。

当前工程的 `Ld/Link.ld` 将可执行代码区配置为 448K、SRAM 配置为 32K；WCH 官方 `openwch/ch583` README 对 CH582M/CH583M 系列列出的 Flash 规格为 512KB。这里的 448K 是本工程当前可链接代码区，不应继续把用户给出的“1MB”当作未经料号确认的链接依据；DataFlash 容量也暂不假定，必须以实际采购型号数据手册为准。WCH 官方资料还确认该系列为 QingKe V4A / RV32IMAC、BLE 5.3、双全速 USB Host/Device 和多路 UART。[WCH 官方 CH583/CH582 资料仓库](https://github.com/openwch/ch583)

因此本项目暂不按“1MB 代码 Flash”做链接和内存预算：必须以实际采购料号、数据手册和最终 `.map` 文件为准。如果实物确认是不同容量型号，应新增独立的芯片配置，不能静默沿用 CH582M 配置。

WCH BLE 示例中可见 `GATT_bm_alloc()` / `GATT_bm_free()` 这样的协议栈报文缓冲 API。[WCH BLE UART 示例中的使用方式](https://github.com/openwch/ch583/issues/39) M2 已完成审计：应用层禁止 libc `malloc/free`；通知发送只通过 WCH 协议栈提供的固定报文池申请 ATT 发送报文，成功后所有权转移给协议栈，失败路径立即调用 `GATT_bm_free()`。该 API 不属于应用层动态内存，代码和文档均保留这条边界，不能把它替换成未审计的自定义分配器。

## 1. 总体数据流

```text
PS/2 CLK/DATA ── EXTI ISR ──> ps2_edge_ring
                                  │
                                  └─ TMOS PS/2 decoder ──┐
                                                         │
UART RX ─────────────── UART ISR ─> uart_rx_ring ────────┤
                                                         ├─> router_input_ring
USB 下行 Host ── transfer callback ─> usb_host_report_ring ─┘       │
                                                                    │
                                                        Event_Router TMOS task
                                                                    │
                         ┌──────────────────────────────────────────┼────────────────────┐
                         │                                          │                    │
                   usb_hid_tx_ring                            ble_hid_tx_ring
                         │                                          │
                 USB Device task                              BLE HOGP task
                         │                                          │
                   USB 上行 PC                                      BLE 主机

                   usb_stream_tx_ring                         ble_stream_tx_ring
                         │                                          │
                 USB CDC task                                BLE NUS task
                         │                                          │
                   CDC 对端                                      NUS 对端
```

这里的“上行/下行”按数据方向描述，不直接把开发板丝印中的 PA/PB 当作 MCU GPIO 端口名。USB Device 的复合描述符和端点状态机已在 M1 完成；下行物理 USB 口的 Host 控制器、VBUS 和 DP/DN 映射仍由 M4 按开发板原理图和 WCH USB 示例确认。当前 PS/2 与 UART 的首版开发板映射集中在 `src/board_pins.h`，与 [`docs/pin-plan.md`](pin-plan.md) 的 PA0/PA1、PA2/PA3、PA8/PA9 规划一致，迁移 PCB 时只需覆盖 board/pin 配置。

关键原则是：输入适配器先产生统一的 `RouterEvent`，路由器再复制成具体输出队列项；路由器不直接调用 USB 或 BLE 发送函数。

## 2. 统一中间格式

### 2.1 键盘：完整的 8 字节 Boot Keyboard Report

PS/2 或 USB Host 收到一个按键后，不把“按下某键”直接交给发送端，而是先更新该输入源的按键状态，再生成完整快照：

```text
byte 0: modifier bitmap (LeftCtrl ... Right GUI)
byte 1: reserved, always 0
byte 2-7: six USB HID Usage IDs
```

按下和释放都生成完整 8 字节报表。释放键就是从当前源状态中移除 Usage ID 后重新生成报表。这样 USB HID 与 BLE HOGP 都能使用同一份语义，不会因为漏掉一个“释放事件”而产生 stuck key。

PS/2 使用 Set 2 扫描码状态机处理 `F0` break、`E0` extended 和 `E1` 特殊序列，再映射为 USB HID Usage ID。USB Host 先支持 Boot Keyboard，再按固定上限解析 Report Descriptor；两条路径最终都只能向 `router_input_ring` 投递 `ROUTER_EVENT_KEYBOARD_REPORT`。

多键盘源的默认策略是按 Usage ID 去重后合并；超过 6 个非修饰键时进入 `ErrorRollOver`（Usage `0x01`）状态并置 `ROUTER_FLAG_OVERFLOW`。M5 再决定是否增加“单一输入源优先级”策略。源断开、解码溢出或重置时必须生成 release-all/resync，而不是保留旧状态。

### 2.2 鼠标、手柄和数据流

- 鼠标中间报表固定为 4 字节：`buttons, dx, dy, wheel`；位移使用二补码 `int8_t` 语义，线上的字节仍按原始 8 位传输。
- 手柄中间报表先固定 8 字节槽位，具体按钮、Hat 和轴的含义由 M1 的 USB/BLE Report Map 定义；路由器不解析传输层 Report ID。
- UART 数据不是键盘事件，先切成不超过 20 字节的 `ROUTER_EVENT_STREAM_DATA`。M2/M3 的默认 BLE ATT 有效负载预算为 20 字节；M5 完成 MTU/链路策略后才考虑放宽，USB CDC 可在输出端重新分片。
- PS/2 键盘和鼠标都在 GPIOA 时钟下降沿采样 DATA，TMOS 再消费 11 位帧（start、8 个 LSB-first data、odd parity、stop）。键盘解码 Set 2 的 `F0` break、`E0` extended 和 `E1` Pause 序列；鼠标先校验首字节同步位，再按标准 3 字节包生成 `buttons/dx/dy/wheel`，并把 PS/2 的正 Y 方向转换为 USB 的正 Y 方向。鼠标上电后发送 `F4` 进入数据报告模式，发送、ACK 和失败重试均为非阻塞状态机。

## 3. C 语言核心类型定义

以下定义已经落到 [`src/event_router_types.h`](../src/event_router_types.h)，并由 `src/event_router.c`、`src/tmos_app.c` 使用。它们描述中间格式和内存布局，不把 USB/BLE 发送函数塞进路由器；TMOS 任务 ID/事件类型绑定 WCH SDK 的 `tmosTaskID/tmosEvents`，不复制另一套调度器。

```c
#include <stdint.h>

#define EVENT_ROUTER_ALIGN4 __attribute__((aligned(4)))

#define HID_KEYBOARD_REPORT_LEN  8u
#define HID_MOUSE_REPORT_LEN      4u
#define HID_GAMEPAD_REPORT_LEN    8u
#define STREAM_CHUNK_MAX_LEN     20u
#define ROUTER_EVENT_PAYLOAD_LEN 20u

typedef enum
{
    ROUTER_SRC_PS2_KEYBOARD = 0,
    ROUTER_SRC_PS2_MOUSE,
    ROUTER_SRC_USB_KEYBOARD,
    ROUTER_SRC_USB_MOUSE,
    ROUTER_SRC_UART,
    ROUTER_SRC_USB_CDC,
    ROUTER_SRC_BLE_NUS,
    ROUTER_SRC_TEST,
    ROUTER_SOURCE_COUNT
} RouterInputSource;

typedef enum
{
    ROUTER_EVENT_KEYBOARD_REPORT = 0,
    ROUTER_EVENT_MOUSE_REPORT,
    ROUTER_EVENT_GAMEPAD_REPORT,
    ROUTER_EVENT_STREAM_DATA,
    ROUTER_EVENT_SOURCE_UP,
    ROUTER_EVENT_SOURCE_DOWN,
    ROUTER_EVENT_SOURCE_RESYNC
} RouterEventKind;

typedef enum
{
    ROUTER_OUTPUT_NONE = 0,
    ROUTER_OUTPUT_USB  = 1u << 0,
    ROUTER_OUTPUT_BLE  = 1u << 1,
    ROUTER_OUTPUT_BOTH = ROUTER_OUTPUT_USB | ROUTER_OUTPUT_BLE
} RouterOutputMask;

#define ROUTER_FLAG_SNAPSHOT   (1u << 0)
#define ROUTER_FLAG_RELEASE_ALL (1u << 1)
#define ROUTER_FLAG_FROM_ISR   (1u << 2)
#define ROUTER_FLAG_OVERFLOW   (1u << 3)

/* 用 byte array 保证线上报表长度，不依赖编译器 struct padding。 */
typedef struct
{
    uint8_t bytes[HID_KEYBOARD_REPORT_LEN];
} HidKeyboardReport;

typedef struct
{
    uint8_t bytes[HID_MOUSE_REPORT_LEN];
} HidMouseReport;

typedef struct
{
    uint8_t bytes[HID_GAMEPAD_REPORT_LEN];
} HidGamepadReport;

typedef union
{
    HidKeyboardReport keyboard;
    HidMouseReport mouse;
    HidGamepadReport gamepad;
    uint8_t stream[STREAM_CHUNK_MAX_LEN];
    uint8_t raw[ROUTER_EVENT_PAYLOAD_LEN];
} RouterPayload;

typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint8_t source;
    uint8_t kind;
    uint8_t flags;
    uint8_t length;
    RouterPayload payload;
} RouterEvent; /* 32 bytes on RV32 */

typedef struct
{
    uint8_t report_id;
    uint8_t kind;
    uint8_t length;
    uint8_t flags;
    uint8_t bytes[ROUTER_EVENT_PAYLOAD_LEN];
} HidTxFrame; /* 24 bytes */

typedef struct
{
    uint8_t length;
    uint8_t flags;
    uint16_t reserved;
    uint8_t bytes[STREAM_CHUNK_MAX_LEN];
} StreamTxFrame; /* 24 bytes */

typedef struct
{
    uint8_t modifiers;
    uint8_t keycodes[6];
    uint8_t key_count;
    uint8_t overflow;
} KeyboardState;

typedef struct
{
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
    int8_t wheel;
} MouseState;

typedef struct
{
    uint8_t connected;
    uint8_t keyboard_valid;
    uint8_t mouse_valid;
    uint8_t reserved;
    KeyboardState keyboard;
    MouseState mouse;
    HidGamepadReport gamepad;
} InputSourceState;

typedef struct
{
    uint8_t *storage;
    uint16_t item_size;
    uint16_t capacity;       /* 必须是 2 的幂；只支持 SPSC */
    volatile uint16_t head; /* producer only */
    volatile uint16_t tail; /* consumer only */
    volatile uint32_t dropped;
} StaticSpscRing;

typedef struct
{
    uint32_t ps2_overrun;
    uint32_t uart_overrun;
    uint32_t usb_report_drop;
    uint32_t router_event_drop;
    uint32_t hid_tx_drop;
    uint32_t stream_tx_drop;
    uint32_t parser_error;
    uint32_t resync_count;
} RouterStats;

typedef struct
{
    uint8_t task_id;             /* 实际实现绑定 tmosTaskID */
    uint8_t output_policy;
    uint8_t active_output_mask;
    uint8_t flags;
    uint16_t pending_events;     /* 实际实现绑定 tmosEvents */
    uint16_t reserved;
    uint32_t next_sequence;

    InputSourceState source[ROUTER_SOURCE_COUNT];
    RouterStats stats;

    StaticSpscRing input_ring;
    StaticSpscRing usb_hid_tx_ring;
    StaticSpscRing ble_hid_tx_ring;
    StaticSpscRing usb_stream_tx_ring;
    StaticSpscRing ble_stream_tx_ring;
} Event_Router;
```

`RouterEvent` 不携带 USB 或 BLE 的 Report ID；Report ID 属于输出端描述符。建议 USB HID 与 BLE HOGP 先统一使用：`1 = keyboard`、`2 = mouse`、`3 = gamepad`，最终以 M1/M2 的 Report Map 和主机兼容性测试为准。

## 4. 静态 Buffer 与所有权

这些是整个项目的建议容量，定义应只出现在一个 `.c` 文件中；头文件只声明类型和接口。M1/M2/M3 已实现 USB Device、CDC、BLE HOGP/NUS、PS/2 edge 和 UART RX 队列，USB Host raw report 的存储在 M4 再加入。容量不是越大越好，必须让 `.map` 文件证明 BLE/USB 栈、TMOS、应用状态、栈和余量都能放进 32KB SRAM。

```c
typedef struct
{
    uint8_t data_level;
    uint8_t flags;
    uint16_t reserved;
} Ps2EdgeSample; /* 4 bytes; 一个物理 PS/2 口一个 SPSC 队列 */

typedef struct
{
    uint8_t byte;
    uint8_t status;
    uint16_t reserved;
} UartRxItem; /* 4 bytes */

typedef struct
{
    uint8_t device_address;
    uint8_t endpoint;
    uint8_t length;
    uint8_t flags;
    uint8_t bytes[64]; /* USB full-speed interrupt max packet */
} UsbHostReport; /* 68 bytes */

static Ps2EdgeSample g_ps2_keyboard_edge_storage[64] EVENT_ROUTER_ALIGN4;
static Ps2EdgeSample g_ps2_mouse_edge_storage[64]    EVENT_ROUTER_ALIGN4;
static UartRxItem    g_uart_rx_storage[128]           EVENT_ROUTER_ALIGN4;
static UsbHostReport g_usb_host_report_storage[4]     EVENT_ROUTER_ALIGN4;
static uint8_t       g_uart_frame[20]                 EVENT_ROUTER_ALIGN4;

static RouterEvent g_router_input_storage[16] EVENT_ROUTER_ALIGN4;
static HidTxFrame g_usb_hid_tx_storage[4]     EVENT_ROUTER_ALIGN4;
static HidTxFrame g_ble_hid_tx_storage[4]     EVENT_ROUTER_ALIGN4;
static StreamTxFrame g_usb_stream_tx_storage[8] EVENT_ROUTER_ALIGN4;
static StreamTxFrame g_ble_stream_tx_storage[8] EVENT_ROUTER_ALIGN4;

/* WCH USB 驱动若要求应用提供端点/DMA 缓冲，再单独按其规则定义；
 * 若 SDK 已经拥有端点缓冲，不能重复分配一份。 */
static uint8_t g_usb_endpoint_storage[4][64] EVENT_ROUTER_ALIGN4;

static Event_Router g_event_router EVENT_ROUTER_ALIGN4;
```

首版应用自有 Buffer 的粗略预算如下（不含 BLE 协议栈、USB 驱动内部对象和 C 栈）：

| Buffer | 容量 | 约占 SRAM |
|---|---:|---:|
| 两路 PS/2 edge | 64 × 2 × 4 B | 512 B |
| UART RX | 128 × 4 B | 512 B |
| USB Host 原始报表 | 4 × 68 B | 272 B |
| Router 输入事件 | 16 × 32 B | 512 B |
| USB/BLE HID 输出队列 | 4 × 24 B × 2 | 192 B |
| USB/BLE CDC/NUS 数据队列 | 8 × 24 B × 2 | 384 B |
| 端点缓冲实际布局 | 192 B + 128 B × 3 | 576 B |
| UART 分帧工作区 | 20 B | 20 B |
| **M1/M2/M3 规划应用侧合计（不含 BLE 堆）** |  | **约 3.0 KB** |

建议应用层所有静态对象（包括协议状态、固定 Report Descriptor map、统计量和测试注入队列）先控制在 8KB 以内，把剩余 SRAM 留给 BLE/USB/TMOS 和运行栈。最终以链接器 map、启动时栈水位和最坏并发场景为准。

所有权规则：

1. PS/2 EXTI ISR 只写对应 edge ring；UART ISR 只写 UART ring。每个生产者有独立 SPSC ring，禁止多个 ISR 共写一个 MPSC ring。
2. USB transfer callback 只复制有限长度报表并置 TMOS 事件；解析器消费 `UsbHostReport`，不能保存端点 DMA 指针。
3. 适配器消费 raw ring 后生成 `RouterEvent`，由 router task 消费；`Event_Router` 是规范化状态的唯一拥有者。
4. 输出后端从自己的 `HidTxFrame` 队列取值，等待端点空闲/CCCD 开启/连接就绪后发送；不得把 ring 内存地址交给异步协议栈长期保存。
5. 键盘快照可以合并旧帧，UART 数据不能无提示丢弃；每个丢弃路径必须增加统计量并触发 resync/backpressure 策略。

## 5. TMOS 任务边界

建议保留少量任务，并让每次任务执行有明确的处理上限：

| TMOS 任务 | 责任 | 禁止事项 |
|---|---|---|
| `PS2_INPUT_TASK` | 消费 edge ring、Set 2 解码、更新源状态、投递键鼠快照 | ISR 中解析完整帧；死循环等待时钟 |
| `UART_INPUT_TASK` | 消费 UART ring、协议分帧、投递 stream event | 阻塞等待换行/固定长度 |
| `USB_HOST_TASK` | 枚举状态机、固定上限 Report Descriptor 解析、生成输入事件 | 动态建字段链表；在回调里做完整解析 |
| `EVENT_ROUTER_TASK` | 合并源状态、选择输出、填充输出队列、处理 resync | 直接调用 USB/BLE 发送 API |
| `USB_DEVICE_TASK` | 端点状态机、HID/CDC 发送与完成回调 | 从 ISR 直接提交长事务 |
| `BLE_OUTPUT_TASK` | HOGP/NUS 连接状态、CCCD、MTU、通知发送重试 | 阻塞等待连接或动态无界重试 |

每个任务每次只处理固定数量的队列项（例如 4 项），还有数据就重新 `set event`。长事务拆成枚举/解析/发送状态机，不能用阻塞式 `while` 把 TMOS 调度器卡住。TMOS 事件位只表示“有工作”，具体数据始终在静态队列中。

## 6. Milestone 2 已落地的 BLE 输出边界

M2 的 BLE 外设任务已经按 WCH 官方外设流程接入：系统初始化 `CH58X_BLEInit()`、`HAL_Init()`、`GAPRole_PeripheralInit()`；服务注册完成后由 TMOS start event 调用 `GAPRole_PeripheralStartDevice()`。应用层不直接在路由器中调用 GATT API。

- HOGP 使用标准 HID Service `0x1812`，Report Map 为 193 B，Report ID 与 USB 描述符保持一致：`1 = keyboard`、`2 = mouse`、`3 = gamepad`。键盘、鼠标和手柄均有独立输入 Report Characteristic 与 CCCD，并提供 Boot Keyboard/Mouse；Protocol Mode 在 Report/Boot 之间选择对应输入特征。
- NUS-compatible 使用 Nordic UART Service 的标准 128 位 UUID。RX 接收 Write/Write Without Response 后只把最多 20 B 复制进 `BleNusRxFrame[4]` 静态 SPSC Ring；TMOS 再以 `ROUTER_SRC_BLE_NUS` 注入 `EventRouter`。TX 受 CCCD 和 `ATT_GetMTU(conn) - 3` 限制，当前默认分片上限为 20 B。
- 路由器把每个 HID/数据帧分别复制到 USB 与 BLE 队列，BLE 后端用 `Peek` 在连接、CCCD 或发送资源暂不可用时保留队首；每个 2 ms 周期最多尝试一帧 HID 和一帧 NUS，避免忙等和无界重试。M2 联调默认 `ROUTER_OUTPUT_BOTH`，M5 再依据 USB configured、BLE connection/CCCD 状态实现自动活跃链路策略。
- 断链时复位 HID/NUS CCCD 和 BLE 连接状态，不把 DMA 或 Ring 内存地址交给协议栈长期持有。`GATT_bm_alloc()` 成功后由 WCH BLE 栈接管报文，失败则由应用立即 `GATT_bm_free()`；这是 SDK 规定的栈报文池接口，不是应用层 libc `malloc/free`。

M2 的代码级验收已通过 MounRiver 自带 RISC-V GCC 8.2.0 交叉编译、`-Wall -Wextra -fsyntax-only`、工程 JSON/XML 解析和 ELF 静态对象检查：Flash `155,436 B / 448 KB`，RAM `20,204 B / 32 KB`；HOGP Report Map、GATT 属性表、CCCD 数量和静态 NUS RX Ring 均已进入最终镜像。电脑/手机实际 BLE 配对、CCCD 写入、HID 收发和 MTU 协商仍需在 CH582M 开发板上执行，不能由交叉编译替代。

## 7. Milestone 3 已落地的输入适配边界

M3 把开发板阶段的物理输入限制在 `src/board_pins.h`：键盘 PS/2 为 PA0 CLK / PA1 DATA，鼠标 PS/2 为 PA2 CLK / PA3 DATA，RS232 经 MAX3232 后接 UART1 PA8 RX / PA9 TX，默认 115200 8N1。宏均可在工程配置中覆盖，协议层不依赖这些具体 GPIO。

- PS/2 GPIOA ISR 只读取 DATA 电平并向两个独立的 `Ps2EdgeSample[64]` SPSC Ring 入队；TMOS 每个端口每次最多消费 32 个 edge，并按 10 ms 无边沿超时复位。完整帧执行 start/data/parity/stop 校验，错误和 edge 溢出都有统计量。
- 键盘适配器支持 Set 2 常用键、修饰键、扩展键、Pause 序列和自动重复去重，输出完整 8 字节键盘快照；解码错误或溢出会 release-all/resync。鼠标适配器支持标准三字节包、按钮、X/Y 饱和转换和 Y 轴方向修正。
- 鼠标 `F4` 初始化在 TMOS 与 GPIOA ISR 之间以状态机完成：ISR 仅推进时钟边沿、发送位和 ACK 采样，任务上下文负责超时、结果处理和 1 s 重试，不阻塞等待外设。
- UART1 ISR 只排空 FIFO 并把字节及线路状态放入 `UartRxItem[128]` Ring；TMOS 按 20 B 满帧、CR/LF 或 6 ms 空闲分帧，路由器背压时保留当前帧，线路错误字节丢弃并计数。

M3 的代码级验收已通过 RISC-V GCC 8.2.0 全工程语法检查、交叉链接、ELF 静态对象检查和动态分配调用审计；真实开发板的 PS/2 电平、UART 收发、USB/BLE 枚举及端到端报告仍需接线后执行。

## 8. 六个 Milestone

后续实现严格按 M1 → M2 → M3 → M4 → M5 → M6。每个里程碑必须先通过验收、提交 Git，再进入下一个；未通过时只修当前里程碑，不提前并行扩展协议栈。

### Milestone 1：TMOS 基础、静态内存与 USB Device 复合输出（已完成）

范围：

- 从当前 MounRiver 工程确认 CH582M 芯片、WCH-Link、启动文件和链接脚本可用。
- 建立 `event_router_types.h`、`static_spsc_ring.h/.c`、TMOS 应用任务骨架和静态内存审计入口。
- 完成 USB Device 描述符和端点状态机：HID Keyboard、Mouse、Gamepad，加 CDC ACM；建议先使用 Report ID 1/2/3。
- 暂不接真实输入，使用固定按键/鼠标/手柄测试注入，验证 `HidTxFrame` 到端点的非阻塞发送。

验收：交叉编译和链接通过；代码具备 PC 复合设备枚举、HID 报表提交和 CDC 回送路径；实际开发板验收需确认键盘/鼠标/手柄输入报告、CDC 收发、USB reset、端点 busy/NAK 均不阻塞 TMOS。默认测试注入关闭，开启宏后才发送周期性 `a` 按键。

### Milestone 2：BLE HOGP 与 NUS-compatible 输出（代码完成，硬件验收待执行）

范围：

- 接入当前 MRS/官方 SDK 版本对应的 BLE 头文件、ROM 库/静态库和 TMOS 类型。
- 实现 HOGP 键盘/鼠标/手柄 Report Map、广播、配对、连接、CCCD 和通知队列。
- 增加 NUS-compatible 128-bit service，用于 UART ↔ BLE 数据透传；处理默认 MTU 与协商 MTU 的分片。
- 完成 `GATT_bm_alloc/free` 报文池的所有权审计；应用层不接受未记录的 libc 动态内存路径。

代码验收：交叉编译、语法检查、GATT 属性表/Report Map/静态对象检查通过；USB 与 BLE 使用独立 HID/数据队列，NUS 数据不会与 HOGP 报表串线。硬件验收：电脑/手机实际配对为 BLE 键鼠，验证完整键盘按下/释放、鼠标报表、NUS 数据、CCCD、断连重连和 MTU 协商；该部分待开发板接入后执行。

### Milestone 3：PS/2 与 UART 输入适配器（代码完成，硬件验收待执行）

范围：

- 两路 GPIO 外部中断只采样时钟边沿和数据位，分别进入 64 项 edge ring。
- 实现 PS/2 Set 2 非阻塞状态机、超时、奇偶校验、ACK/命令阶段和 HID Usage 映射；先支持标准键盘和三键鼠标。
- 实现 UART RX ring、固定长度/超时分帧和 `STREAM_DATA`；若要把 UART 命令变成 HID，另定义显式上层协议，不把任意字节当按键。

代码验收：两个 PS/2 edge Ring、Set 2/鼠标解码、奇偶校验、超时、溢出 resync、非阻塞 `F4`/ACK 状态机、UART 固定长度/超时分帧和背压路径均已进入工程，并通过交叉编译与静态对象检查。硬件验收：开发板杜邦线接 PS/2 键盘/鼠标可生成 M1 的 USB/BLE 报表；UART 输入可经 CDC/NUS 透传；故意制造 edge Ring 溢出后能统计、复位解码器并发送 release-all；ISR 执行时间保持在采样级别。该硬件验收待开发板接入后执行。

### Milestone 4：USB Host HID 枚举与报表解析

范围：

- 初始化下行 USB Host 端口和 VBUS/连接检测，完成设备枚举、配置、HID 类请求和中断 IN 轮询。
- 先实现 Boot Keyboard/Boot Mouse，再实现有固定上限的 Report Descriptor 字段表；禁止动态字段链表。
- 处理 Report ID、相对/绝对轴、按钮、短报表、异常长度、拔插和设备复位。

验收：外接至少两种键盘、两种鼠标和一个不同 Report ID 的 HID 设备可枚举；报表转成与 PS/2 相同的 `RouterEvent`；热插拔、短报表和不支持的 HID 不会破坏已连接的 USB Device/BLE 输出。

### Milestone 5：Event_Router、活跃上行链路与全链路合并

范围：

- 实现 `Event_Router` 唯一状态源：多输入源键盘合并、鼠标增量、手柄快照、UART stream 分流。
- 实现输出策略：强制 USB、强制 BLE、双发和自动选择；自动模式下根据 USB configured、BLE connected/CCCD 状态选择可用上行。
- 完成 USB/BLE 之间统一 Report ID 映射、队列背压、鼠标帧合并、键盘 resync、源断开 release-all 和错误统计。
- 通过按键或 CDC/NUS 控制命令切换活跃输出，但控制命令本身必须是明确的协议帧。

验收：PS/2、USB Host、UART 的输入可按策略到达 USB Device、BLE HOGP、CDC/NUS；切换链路不会产生 stuck key；上行暂时不可用时不阻塞输入任务；恢复后能发送最新完整快照而不是过期指针。

### Milestone 6：资源、可靠性和开发板到 PCB 的收口

范围：

- 以 map 文件核对代码 Flash、DataFlash、SRAM、C 栈、TMOS/BLE/USB 保留区；做源码和链接产物的动态分配审计。
- 做长时间输入、快速拔插、USB reset、BLE 重连、MTU 变化、队列溢出和同时多源输入测试。
- 记录端到端延迟、丢包/丢帧计数、最大任务执行时间和 SRAM 峰值；补齐故障恢复与诊断日志。
- 最后才把开发板连线映射到自制 PCB，复核 USB 电源、PS/2 电平、WCH-Link、RF 和引脚复用。

验收：所有定义的功能有可复现实验步骤；无应用层 `malloc/free`；错误可恢复或明确报告；在目标容量和最坏并发下有余量；PCB 迁移只改变 board/pin 层，不改变 router 中间格式。

## 9. 后续实现纪律

下一轮只实现 M4，不提前写全链路活跃链路策略。每个新模块先给出：输入/输出队列、静态内存大小、TMOS 事件位、所有权、溢出策略和验收用例，然后再写 `.c/.h`。
