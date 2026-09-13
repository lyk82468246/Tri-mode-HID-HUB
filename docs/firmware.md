# CH582M 固件工程

## 工程入口

固件使用 MounRiver Studio 工程格式，入口文件为根目录的 `CH582M.wvproj`。工程目标为 WCH CH582M（CH58X / RISC-V / NoneOS），下载配置使用 WCH-Link。

```text
CH582M.wvproj       MounRiver 工程入口
CH582M.launch       调试启动配置
src/Main.c          TMOS 应用入口
src/tmos_app.c      TMOS 任务和 M1/M2/M3/M4 服务周期
src/usb_device.c    USB Device HID/CDC 复合控制器
src/event_router.c  输入事件与输出队列路由
src/static_spsc_ring.c  固定容量 SPSC Ring 实现
src/board_pins.h       开发板/首版 PCB 引脚覆盖层
src/ble_hid_service.c   BLE HOGP HID Service
src/ble_nus_service.c   BLE NUS-compatible Service
src/ble_output.c        BLE Peripheral/TMOS 输出任务
src/ps2_input.c         两路 PS/2 GPIO 边沿采样与解码
src/uart_input.c        UART1 RX Ring 与超时分帧
src/usb_host_hid.c      USB2 Host 非阻塞枚举、HID 轮询与报表解析
BLE/                WCH 官方 BLE/TMOS HAL、头文件和静态库
Startup/            CH583/CH582 系列启动文件
Ld/Link.ld         链接脚本
RVMSIS/             RISC-V CMSIS 兼容头文件
StdPeriphDriver/    WCH 外设驱动、头文件和 ISP 库
```

`.mrs/` 是 MounRiver 的本机工作区状态，可能包含绝对路径，因此不纳入版本控制；重新导入 `CH582M.wvproj` 即可恢复工程。

## Milestone 4 当前状态

M1/M2/M3/M4 已把输入到多路输出的台架链路接入工程：`Main.c` 设置系统时钟后进入 `TMOS_SystemProcess()`；`tmos_app.c` 每 2 ms 运行一次有限预算服务，依次消费 PS/2 edge、UART1 RX、USB2 Host HID 报表、CDC/NUS RX，执行 `EventRouter_Process()`，再尝试提交 USB HID/CDC IN 和 BLE HOGP/NUS 通知。应用层没有 libc `malloc/free`，输入和输出队列均由编译期静态对象提供。

当前 USB Device 描述符采用一个 HID 接口加一个 CDC ACM 功能：

| 功能 | 接口/端点 | 说明 |
|---|---|---|
| HID Keyboard/Mouse/Gamepad | Interface 0，Interrupt IN EP1 `0x81`，16 B | Report ID 1/2/3；线上长度分别为 9/5/9 B（含 Report ID） |
| CDC ACM 控制 | Interface 1，Interrupt IN EP3 `0x83`，8 B | CDC 通知端点，当前只完成标准控制请求 |
| CDC ACM 数据 | Interface 2，Bulk OUT/IN EP2 `0x02/0x82`，64 B | OUT 在 USB ISR 中只复制到静态 Ring；TMOS 中回送到 IN |
| BLE HOGP | HID Service `0x1812`，Report ID 1/2/3 | 键盘、鼠标、手柄输入 Report；含 Boot Keyboard/Mouse、Report Map、Report Reference 和 CCCD |
| BLE NUS-compatible | `6E400001/2/3-B5A3-F393-E0A9-E50E24DCCA9E` | RX Write/Write Without Response；TX Notification；应用数据按 20 B 分片 |
| PS/2 keyboard | PA0 CLK / PA1 DATA | GPIOA 下降沿采样；Set 2 键盘帧校验、扩展码、按下/释放快照；鼠标初始化命令独立走状态机 |
| PS/2 mouse | PA2 CLK / PA3 DATA | 3 字节标准鼠标包；按钮、X/Y 增量转换为统一 4 B Mouse Report |
| RS232/UART | UART1 PA8 RX / PA9 TX | MAX3232 后的 115200 8N1 RX；20 B 满帧或 6 ms 空闲分帧为 `STREAM_DATA` |
| USB Host HID | USB2 Host（首版 PCB 为 PB13/PB12，VBUS 由 PB6/HOST_EN 控制；开发板可覆盖宏） | 非阻塞总线复位、EP0 控制传输、配置/HID/Report Descriptor 解析；最多 2 个 HID 中断 IN 接口，单包上限 64 B |

USB Host M4 的控制传输不调用 WCH 示例中的阻塞式高层 helper，而是由 `src/usb_host_hid.c` 逐阶段推进 SETUP、DATA、STATUS。设备描述符先取 8 B 再按 `bMaxPacketSize0` 取完整描述符；随后读取配置描述符、选择 HID interrupt IN endpoint、设置 configuration，并按 HID descriptor 中的 Report Descriptor 长度取固定缓存。Boot Keyboard/Mouse 发送 `SET_PROTOCOL(0)`/`SET_IDLE(0)`；通用 `protocol=0` HID 设备使用固定容量字段表解析，支持 Report ID、键盘数组/修饰键、鼠标按钮/X/Y/Wheel 和相对轴。

USB Host 报表先复制到 `UsbHostHidRawReport[4]` 静态 Ring，再在 TMOS 上下文转换为与 PS/2 相同的 `HidKeyboardReport`/`HidMouseReport`，不会把 USB DMA 地址交给路由器。当前 M4 只支持全速、单根 Hub 直连的 USB HID keyboard/mouse 子集；Hub、Bulk/ISO、超过固定缓存上限的描述符和其他 HID 类型明确拒绝或等待下一次枚举。

HID 的统一中间格式由 [`src/event_router_types.h`](../src/event_router_types.h) 定义：键盘为完整 8 字节 Boot Keyboard payload，鼠标为 `buttons/dx/dy/wheel` 4 字节，手柄为 8 字节槽位，数据流按不超过 20 字节分片。M2/M3 联调默认启用 USB 与 BLE 双输出掩码；后续 M5 再实现按连接状态自动选择活跃上行链路。

USB VID/PID `0x1209:0x5820` 仅为开发阶段占位值，发布前必须更换为项目合法拥有的 VID/PID。当前交叉编译已验证描述符、TMOS、静态队列、GATT 属性表和链接依赖；尚未在实际开发板上完成 PC/BLE 枚举与物理收发测试，需连接 CH582M 开发板后按下面的验收步骤执行。

系统级数据流、静态内存布局、Ring Buffer 所有权和六阶段开发顺序见 [`docs/firmware-architecture.md`](firmware-architecture.md)。后续实现严格按 M1 至 M6 逐阶段推进。

## M4 静态内存与任务边界

M1/M2/M3/M4 的主要静态分配如下，所有可变 Buffer 均使用 4 字节对齐：

| 对象 | 容量 |
|---|---:|
| WCH BLE 协议栈堆 `MEM_BUF` | 6144 B |
| USB EP0 DMA 区 | 192 B |
| USB EP1/EP2/EP3 DMA 区 | 128 B × 3 |
| 路由输入事件 | `RouterEvent[16]` |
| USB/BLE HID 输出 | `HidTxFrame[4]` × 2 |
| USB/BLE 数据流输出 | `StreamTxFrame[8]` × 2 |
| CDC RX | 64 B 数据包 × 4（含元数据） |
| BLE HOGP Report Map/属性/CCCD | Report Map 193 B；32 个静态属性；5 组 CCCD |
| BLE NUS RX | `BleNusRxFrame[4]`，每帧最多 20 B |
| PS/2 edge | `Ps2EdgeSample[64]` × 2 |
| UART1 RX | `UartRxItem[128]`，每项含字节与线路状态 |
| UART1 分帧工作区 | 20 B |
| USB2 Host RX/TX DMA | 64 B × 2 |
| USB Host 设备/配置描述符 | 18 B + 256 B |
| USB Host HID Report Descriptor | 256 B × 2 |
| USB Host HID parser/interface 状态 | `UsbHostHidInterface[2]`，含 24 个固定字段槽位/接口 |
| USB Host 原始报表 Ring | `UsbHostHidRawReport[4]`，每帧 64 B 有效数据 |

ISR 不解析 HID、不调用路由器，也不等待发送完成。PS/2 GPIOA ISR 只读取对应 DATA 电平并入 edge Ring，UART1 ISR 只排空 FIFO 并保存线路状态，EP2 OUT ISR 只完成有限长度复制和入队；GATT 写回调只复制 NUS RX 数据到静态 Ring。PS/2 鼠标发 `F4` 时，ISR 只推进数据位/ACK 的时序状态，协议解析、UART/PS/2 解码、CDC 回送、HOGP/NUS 通知和输出端点提交都在 TMOS 上下文中执行。Ring 为 SPSC，满时返回资源错误或增加对应计数，不能把 DMA 地址或局部变量指针交给异步消费者。

## 在 MounRiver Studio 中使用

1. 打开 `CH582M.wvproj`，确认芯片选择为 CH582M。
2. 检查工程中的 WCH SDK/工具链路径已由本机 MounRiver Studio 正确解析。
3. 选择 `obj` 配置并执行 Build；生成物目录 `obj/` 已被 Git 忽略。
4. 连接 WCH-Link 后按工程的下载/调试配置烧录或启动调试。

若要启用 M1 的台架按键注入，在工程 C 预处理宏中临时加入 `CH582M_M1_TEST_PATTERN=1` 后重新 Build；默认值为 0，不会自动向主机发送按键。CDC 验收可从主机向 CDC OUT 写入最多 64 字节，设备应在下一个 TMOS 周期通过 CDC IN 回送。

本机 MRS 自带 RISC-V GCC 8.2.0 的 M4 交叉编译结果为：代码 Flash 使用 166,832 B / 448 KB，RAM 使用 24,572 B / 32 KB（含 WCH BLE 库、外设驱动和应用，最终仍以 MRS 生成的 map 为准）。若 MRS GUI 重新生成工程配置，应确认 `BLE/HAL/include`、`BLE/LIB`、`CH58xBLE`、UART1/GPIOA 中断入口、USB2 Host 源文件和上述预处理宏没有丢失。

不同版本的 MounRiver Studio 可能使用不同的 SDK 安装路径；工程文件保留了芯片、编译器、链接脚本和下载目标配置，但不把本机 SDK 安装目录写入仓库。
