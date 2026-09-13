# CH582M 固件工程

## 工程入口

固件使用 MounRiver Studio 工程格式，入口文件为根目录的 `CH582M.wvproj`。工程目标为 WCH CH582M（CH58X / RISC-V / NoneOS），下载配置使用 WCH-Link。

```text
CH582M.wvproj       MounRiver 工程入口
CH582M.launch       调试启动配置
src/Main.c          TMOS 应用入口
src/tmos_app.c      TMOS 任务和 M1 服务周期
src/usb_device.c    USB Device HID/CDC 复合控制器
src/event_router.c  输入事件与输出队列路由
src/static_spsc_ring.c  固定容量 SPSC Ring 实现
BLE/                WCH 官方 BLE/TMOS HAL、头文件和静态库
Startup/            CH583/CH582 系列启动文件
Ld/Link.ld         链接脚本
RVMSIS/             RISC-V CMSIS 兼容头文件
StdPeriphDriver/    WCH 外设驱动、头文件和 ISP 库
```

`.mrs/` 是 MounRiver 的本机工作区状态，可能包含绝对路径，因此不纳入版本控制；重新导入 `CH582M.wvproj` 即可恢复工程。

## Milestone 1 当前状态

M1 已把最小可运行链路接入工程：`Main.c` 设置系统时钟后进入 `TMOS_SystemProcess()`；`tmos_app.c` 每 2 ms 运行一次有限预算服务，依次消费 CDC RX、执行 `EventRouter_Process()`，再尝试提交 USB HID/CDC IN。应用层没有 `malloc/free`，输入和输出队列均由编译期静态对象提供。

当前 USB Device 描述符采用一个 HID 接口加一个 CDC ACM 功能：

| 功能 | 接口/端点 | 说明 |
|---|---|---|
| HID Keyboard/Mouse/Gamepad | Interface 0，Interrupt IN EP1 `0x81`，16 B | Report ID 1/2/3；线上长度分别为 9/5/9 B（含 Report ID） |
| CDC ACM 控制 | Interface 1，Interrupt IN EP3 `0x83`，8 B | CDC 通知端点，当前只完成标准控制请求 |
| CDC ACM 数据 | Interface 2，Bulk OUT/IN EP2 `0x02/0x82`，64 B | OUT 在 USB ISR 中只复制到静态 Ring；TMOS 中回送到 IN |

HID 的统一中间格式由 [`src/event_router_types.h`](../src/event_router_types.h) 定义：键盘为完整 8 字节 Boot Keyboard payload，鼠标为 `buttons/dx/dy/wheel` 4 字节，手柄为 8 字节槽位，数据流按不超过 20 字节分片。M1 只开启 USB 输出策略；BLE 输出 Ring 已预留但 HOGP/NUS 业务留到 M2。

USB VID/PID `0x1209:0x5820` 仅为开发阶段占位值，发布前必须更换为项目合法拥有的 VID/PID。当前交叉编译已验证描述符、TMOS、静态队列和链接依赖；尚未在实际开发板上完成 PC 枚举与物理收发测试，需连接 CH582M 开发板后按下面的验收步骤执行。

系统级数据流、静态内存布局、Ring Buffer 所有权和六阶段开发顺序见 [`docs/firmware-architecture.md`](firmware-architecture.md)。后续实现严格按 M1 至 M6 逐阶段推进。

## M1 静态内存与任务边界

M1 的主要静态分配如下，所有可变 Buffer 均使用 4 字节对齐：

| 对象 | 容量 |
|---|---:|
| WCH BLE 协议栈堆 `MEM_BUF` | 6144 B |
| USB EP0 DMA 区 | 192 B |
| USB EP1/EP2/EP3 DMA 区 | 128 B × 3 |
| 路由输入事件 | `RouterEvent[16]` |
| USB/BLE HID 输出 | `HidTxFrame[4]` × 2 |
| CDC/数据流输出 | `StreamTxFrame[8]` |
| CDC RX | 64 B 数据包 × 4（含元数据） |

ISR 不解析 HID、不调用路由器，也不等待发送完成。EP2 OUT ISR 只完成有限长度复制和入队；协议处理、CDC 回送和输出端点提交都在 TMOS 服务周期中执行。Ring 为 SPSC，满时丢弃并增加对应计数，不能把 DMA 地址或局部变量指针交给异步消费者。

## 在 MounRiver Studio 中使用

1. 打开 `CH582M.wvproj`，确认芯片选择为 CH582M。
2. 检查工程中的 WCH SDK/工具链路径已由本机 MounRiver Studio 正确解析。
3. 选择 `obj` 配置并执行 Build；生成物目录 `obj/` 已被 Git 忽略。
4. 连接 WCH-Link 后按工程的下载/调试配置烧录或启动调试。

若要启用 M1 的台架按键注入，在工程 C 预处理宏中临时加入 `CH582M_M1_TEST_PATTERN=1` 后重新 Build；默认值为 0，不会自动向主机发送按键。CDC 验收可从主机向 CDC OUT 写入最多 64 字节，设备应在下一个 TMOS 周期通过 CDC IN 回送。

本机 MRS 自带 RISC-V GCC 8.2.0 的交叉编译结果为：代码 Flash 使用 26,456 B / 448 KB，RAM 使用 15,388 B / 32 KB（含 WCH BLE 库、外设驱动和应用，最终仍以 MRS 生成的 map 为准）。若 MRS GUI 重新生成工程配置，应确认 `BLE/HAL/include`、`BLE/LIB`、`CH58xBLE` 和上述预处理宏没有丢失。

不同版本的 MounRiver Studio 可能使用不同的 SDK 安装路径；工程文件保留了芯片、编译器、链接脚本和下载目标配置，但不把本机 SDK 安装目录写入仓库。
