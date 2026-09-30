# 原理图绘制与审计记录

## 2026-08-17

### 环境与连接

1. 确认嘉立创 EDA 专业版已安装 Run API Gateway，并已启用“允许外部交互”。
2. 通过本地 Gateway 连接到当前 EDA 窗口，打开工程 `Tri-mode HID Hub`。
3. 记录工程 UUID `6631163b415d4b76b85d6c91ef5d43f5`、原理图 UUID `160ff6e2b76d4104`、页面 UUID `21fbca2f7877cf86`。
4. 本仓库此前为空，仅保留 Git 元数据；本次先加入文档，未提交或推送远端。

### 绘制内容

- 放置 U1 CH582M、32 MHz 晶振、RF 天线、WCH-Link 调试口、BOOT/RESET/USER 按键。
- 放置 USB-C、USB-A、两路 USB 数据 ESD、两侧 22 Ω 串联电阻和 USB-A 受控 VBUS。
- 放置 BQ24074、TPS63031、TPS61023、SY6280AAC、电池接口、去耦电容、采样/使能/状态网络。
- 放置 MAX3232、DB9、RS232 charge-pump 电容。
- 放置两路 PS/2 接口、四颗 BSS138 双向电平转换 MOS、5 V/3.3 V 两侧上拉。
- 放置 OLED I²C 接口及上拉、SPI 扩展排针。
- 电阻默认使用 0603，储能/输入输出电容使用 0805；12 pF、100 nF、1 µF、2.2 µF 等小电容使用 0603。

### 网表级修订

在 API 读回并按引脚坐标核对时，修订了以下问题：

- USB-A VBUS TVS 从 `5V_HOST` 调整到受控 `VBUS_HOST`。
- 删除 USB D−、USB-C CC、I²C 上拉等因器件引脚坐标重叠而产生的错误隐藏网络端口。
- 将 R15 的 MCU 侧恢复为 `USB_DEV_DN_MCU`，避免与 USB-C VBUS 合并。
- 将 R29 的下端恢复为 `RESET`，与 R30 的 3V3 上拉分离。
- 将 U1 pin 41 恢复为 `MOUSE_CLK_MCU`，并将 C5 正端独立恢复到 `VDCIA`。
- 将 L3 的第二端从误接的 GND 改为 `BOOST_SW`。
- 将 DB9 pin 2/3 修正为常见 DTE 标注：pin 2 `RS232_RX`、pin 3 `RS232_TX`。
- 恢复 U1 裸露焊盘 pin 49 到 GND。
- 将首版未使用 MCU GPIO 标记为 NC。

### 最终读回结果

最后一次保存前的 API 审计结果：

- 元件/网络端口读回对象：298
- 原理图线段对象：278
- 隐藏网络端口：213
- 引脚多网络短接：0
- 隐藏网络端口与引脚线网不一致：0
- 未连接且未标 NC 的引脚：0
- 关键功能映射（U1/U2/U3/U4/U5/U6、USB、PS/2、RS232、OLED、调试、扩展、电源）已按预期网络名通过核对。

这些数字是 API 层网名/引脚连接审计，不等同于嘉立创 EDA ERC 通过，也不替代 PCB DRC、封装焊盘核对和实际电气测试。

### 尚未完成

- 尚未导出或提交嘉立创 EDA 工程文件；当前以云端工程为唯一原理图源。
- 尚未做 PCB 封装布局、USB 差分线、DCDC 回路、RF keep-out、天线匹配和地平面设计。
- 尚未锁定电池型号、NTC、充电电流、USB-A 最大输出电流及 5 V boost 热设计。
- 尚未编写固件、USB 描述符、BLE GATT/HOGP 或 HID 报表转换逻辑。

## 2026-08-17 二次整改：布局、PS/2 器件与网络显示

用户复核指出首版存在三类问题：整页布局不可读、部分库器件显示为未分类/不可追溯、PS/2 插座封装错误。针对这些问题，Rev A 进行了以下处理：

1. 删除首版全部导线、隐藏网络端口和重叠说明文字，按电源/USB-C、3V3/5V、USB-A/OLED、CH582M/RF、PS/2、RS232、调试/扩展分区重新摆放。
2. 删除错误的 `TH_9P-P3.90_PS2`，J3/J4 改为 `PS2-TH_DIN-603`、LCSC C23689424 的 6 针圆形 DIN-6 候选，并按 1 DATA / 3 GND / 4 +5V / 5 CLK / 2,6 NC 重新建网。
3. 修正 R4、C4/C5、C6、R11、R13、R14、R18/R19 的错误或带 `.1` 后缀的供应商编号；这些编号现使用可查询的 C 号。
4. 将仍未能确认生产料号的 C990 Extended Part 连接器、晶振、天线和按键名称改为 `[TBD]`/`[MECHANICAL CANDIDATE]`，不再把它们伪装成已经完成选型的 BOM。
5. 原理图采用 293 个引脚处网络端口维护网名，并隐藏端口文字；另保留 289 条不带网络名的短引线作为连接方向提示，避免在每个线头重复绘制 `GND`、`3V3` 等文字。

### Rev A API 审计结果

- 有设计编号的实际器件：84
- 网络端口：293；期望连接引脚：293；按引脚坐标/网络名核对缺失：0
- 网络端口名称可见数：0（网络仍保留在端口对象中）
- 无网络名短引线：289；带显式网络名的导线：0
- 同一坐标出现多个不同网络的端口：0
- 当前页面仍是 A4 单页，标题栏已隐藏以释放绘图区；这不是 ERC/DRC 通过证明。

API 可以取得当前页 PNG 渲染用于复查；当前 Gateway 下 PDF 导出调用曾超过请求时限，因此未把一个未经确认的 PDF 当作仓库交付物。EDA 云端工程仍是原理图唯一来源，仓库只保存设计记录。

### 后续审核重点

- 打开 EDA 后确认隐藏网络端口确实在 GUI 中保持连接，并运行原生 ERC。
- 对 C990 候选逐一替换为最终可采购、可查数据手册的具体料号；特别是 USB-C、USB-A、DB9、OLED/排针、晶振、天线和按键。
- 重新核对 BQ24074、TPS63031、TPS61023、SY6280 的参数网络和电流预算，再进入 PCB。

## 2026-09-12 固件工程纳入仓库

1. 将本地 MounRiver Studio CH582M 空工程接入已有 GitHub 仓库，而不是创建一个与远端无关的新历史：本地 `main` 跟踪 `origin/main`，远端地址为 `https://github.com/lyk82468246/Tri-mode-HID-HUB.git`。
2. 保留远端已有的 PCB/原理图设计记录，并把 `CH582M.wvproj`、`src/`、`Startup/`、`Ld/`、`RVMSIS/` 和 `StdPeriphDriver/` 作为固件工程加入同一仓库。
3. 新增仓库级 `.gitignore` 和 `.gitattributes`：忽略 MounRiver 本机工作区及构建输出，保留可复现工程配置、源码、启动文件、链接脚本和随工程使用的 ISP 库。
4. 当前加入的 `src/Main.c` 是 WCH 的 UART1 收发模板，尚不宣称已经实现三模 HID；后续固件功能必须继续以 `docs/pin-plan.md` 和实际芯片/SDK 文档为准。

## 2026-09-12 固件系统架构与迭代 Roadmap

1. 在未实现底层驱动前，定义 `Event_Router`、统一键鼠/手柄/数据流中间格式、静态 SPSC Ring Buffer、ISR/TMOS/输出后端的所有权边界。
2. 规定 PS/2/USB Host 键盘都先转换为完整 8 字节 Boot Keyboard Report 快照，再进入 `router_input_ring`；输出端根据 Report ID 映射到 USB HID 或 BLE HOGP。
3. 规划六个严格串行里程碑：USB Device 复合输出、BLE HOGP/NUS、PS/2/UART 输入、USB Host HID、Event_Router 全链路合并、资源与可靠性收口。
4. 根据当前 `Ld/Link.ld` 将可执行代码区按 448K、SRAM 按 32K 预算；WCH 官方 `openwch/ch583` README 列出该系列 Flash 为 512KB，DataFlash/具体料号容量仍待数据手册确认，1MB 说法暂不作为链接依据。
5. 记录 WCH BLE 报文缓冲 API 的动态所有权审计为 M2 前置条件，应用层不使用 libc `malloc/free`。

## 2026-09-13 Milestone 1 实现与交叉编译验证

1. 将 WCH 官方 CH58x BLE/TMOS 运行时文件纳入 `BLE/`：HAL 时基、TMOS 初始化头文件、匹配的 `CH58xBLE` 静态库；工程配置同步加入头文件路径、库路径和 `-lCH58xBLE`。
2. 将 `src/Main.c` 改为 TMOS 主循环，新增 `tmos_app.c/.h`，以 2 ms reload event 驱动有限预算服务；没有引入 FreeRTOS，应用层没有 `malloc/free`。
3. 新增固定容量 SPSC Ring、`Event_Router` 中间格式和 M1 路由实现。键盘/鼠标/手柄分别使用 8/4/8 字节 payload，数据流切成不超过 20 字节的静态队列项。
4. 新增 USB Device 复合描述符和控制器：一个多 Report ID HID 接口（Keyboard/Mouse/Gamepad）加 CDC ACM；EP2 OUT ISR 只复制并入队，TMOS 任务负责 CDC 回送和 HID IN 提交。
5. 保留 WCH `CH58x_usbdev.h` 接口，但排除原始 `CH58x_usbdev.c`，由 `src/usb_device.c` 提供复合控制器所需符号，避免同名初始化/中断处理函数冲突。
6. 使用 MounRiver Studio 自带 RISC-V Embedded GCC 8.2.0 完成交叉编译和链接验证：Flash 26,460 B / 448 KB，RAM 15,388 B / 32 KB；尚未连接开发板执行真实 USB 枚举、HID 主机识别或 CDC 收发验收。
7. M1 仅预留 BLE HID/数据队列，不启动 BLE 广播、HOGP 或 NUS；下一步进入 M2 前需先审查 BLE SDK 报文 Buffer 所有权和 MTU 分片策略。

## 2026-09-13 Milestone 2：BLE HOGP/NUS-compatible 输出

1. 对照本仓库随工程保存的 WCH CH58x BLE 头文件、静态库和官方示例，确认外设初始化顺序、GATT 属性回调、CCCD、连接角色回调、TMOS 消息处理以及 `GATT_bm_alloc/free` 的报文所有权。
2. 新增 `src/ble_hid_service.c/.h`：实现标准 HID Service `0x1812`，包含 193 B Report Map、Keyboard/Mouse/Gamepad Report ID 1/2/3、Boot Keyboard/Mouse、Report Reference、Protocol Mode、HID Control Point 和输入/输出 CCCD。
3. 新增 `src/ble_nus_service.c/.h`：实现 Nordic UART Service UUID-compatible 的 RX Write/Write Without Response 与 TX Notification；RX 使用 `BleNusRxFrame[4]` 固定 Ring，按 ATT 默认 MTU 预算限制为每帧最多 20 B。
4. 新增 `src/ble_output.c/.h`：注册 BLE Peripheral/TMOS 任务，配置广播、配对、连接状态、HOGP/NUS 服务和有限预算通知发送。每个 2 ms 周期最多尝试一帧 BLE HID 和一帧 NUS；连接/CCCD/发送资源暂不可用时保留队首，不忙等。
5. 修改 `Event_Router`：HID 和数据流都按输出掩码分别复制到 USB、BLE 两套队列，避免 USB 与 BLE 竞争同一队列；M2 默认使用 `ROUTER_OUTPUT_BOTH`，动态活跃链路策略留到 M5。
6. 应用层未发现 libc `malloc/free/calloc/realloc` 调用。BLE 通知使用 WCH SDK 要求的协议栈报文池：`GATT_bm_alloc()` 成功后由协议栈接管，失败立即 `GATT_bm_free()`；这不等同于应用层动态分配，边界已写入架构文档。
7. 使用 MounRiver Studio 自带 RISC-V GCC 8.2.0 完成交叉编译和链接：Flash `155,436 B / 448 KB`，RAM `20,204 B / 32 KB`。`-Wall -Wextra -fsyntax-only`、工程 JSON/XML 解析、ELF 符号和 Report Map/属性表大小检查均通过。
8. 尚未接入 CH582M 开发板进行真实 BLE 配对、HID 主机识别、CCCD 写入、NUS 收发、MTU 协商和断连重连验证；下一阶段按顺序进入 M3，实现两路 PS/2 与 UART 输入适配器。

## 2026-09-13 Milestone 3：PS/2 与 UART 输入适配器

1. 新增 `src/board_pins.h`，把开发板/首版 PCB 规划中的 PS/2 键盘 PA0/PA1、PS/2 鼠标 PA2/PA3、UART1 PA8/PA9 和 115200 波特率集中管理；后续 PCB 复用只需覆盖 board/pin 宏，不改变路由器中间格式。
2. 新增 `src/ps2_input.c/.h`：GPIOA ISR 在时钟下降沿只采样 DATA 并写入两个独立的 `Ps2EdgeSample[64]` 静态 SPSC Ring；TMOS 侧完成 11 位帧的 start/8-bit/odd-parity/stop 校验、10 ms 无边沿超时、溢出统计和解码器 resync。
3. PS/2 键盘适配器实现 Set 2 常用键、`F0` break、`E0` extended、`E1` Pause、修饰键和 ErrorRollOver 处理，统一输出完整 8 字节 Keyboard Report；鼠标适配器解析标准 3 字节包，处理按钮、X/Y 饱和和 PS/2 到 USB 的 Y 轴方向转换。
4. 鼠标上电初始化通过非阻塞 GPIOA 中断状态机发送 `F4`（enable data reporting），包含主机 inhibit、数据位/奇偶校验、设备 ACK、超时和 1 s 重试；ISR 不执行协议解析、路由或阻塞等待。
5. 新增 `src/uart_input.c/.h`：UART1 ISR 仅排空 FIFO 并将字节与线路状态写入 `UartRxItem[128]` 静态 Ring；TMOS 侧按 20 B 满帧、CR/LF 或 6 ms 空闲分帧，路由器背压时保留当前帧，线路错误字节丢弃并计数。
6. 修改 `tmos_app.c`，在每个 2 ms 服务周期按固定顺序消费 PS/2、UART、CDC/NUS 输入，再运行路由器和 USB/BLE 输出；未引入 FreeRTOS、运行时 `malloc/free` 或阻塞式外设等待。
7. 使用 MounRiver Studio 自带 RISC-V Embedded GCC 8.2.0 完成全工程 `-Wall -Wextra -fsyntax-only` 与交叉链接验证：Flash `159,692 B / 448 KB`，RAM `21,876 B / 32 KB`；ELF 静态对象大小、工程 JSON/XML 解析和动态分配调用审计通过。
8. 尚未接入 CH582M 开发板执行 PS/2 电平/时序、鼠标 ACK、UART MAX3232 收发及 USB/BLE 端到端验收；下一阶段按顺序进入 M4，实现下行 USB Host HID 枚举与固定上限报表解析。

## 2026-09-13 Milestone 4：USB Host HID 枚举与报表解析

1. 对照本仓库随工程保存的 WCH `CH58x_usbhost.h`、`CH58x_usb2hostBase.c` 和官方 `HostU2Enum` 例程，确认 USB2 Host 使用独立 RX/TX DMA、USB2 控制器寄存器和 `USB2_HostInit()`；上行 USB Device 继续使用另一套控制器。
2. 明确禁止在 TMOS 中调用 WCH 官方阻塞式 Host 事务/控制传输 helper；新增 `src/usb_host_hid.c/.h`，以每 2 ms 轮询的事务对象实现 SETUP、DATA、STATUS、NAK 重试、40 ms 单事务超时和 200 ms 单控制请求超时。
3. 新增固定 4 字节对齐的 USB Host DMA、18 B 设备描述符、256 B 配置描述符、两个 256 B Report Descriptor 缓存、两个 HID 接口状态和 `UsbHostHidRawReport[4]` 原始报表 Ring；应用层无 `malloc/free`。
4. 完成 PB6/HOST_EN 高电平开启 USB-A VBUS、attach/16 ms bus reset/EP0 包长探测/SET_ADDRESS/配置枚举/HID Report Descriptor/Boot `SET_PROTOCOL` 与 `SET_IDLE`/interrupt IN 轮询；只接受最多两个全速 HID interrupt IN 接口，端点报表单包上限 64 B，开发板可覆盖 Host 电源宏。
5. 完成固定容量 HID Report Descriptor 解析器：支持 Report ID、全局 Push/Pop、键盘修饰键/数组、鼠标按钮/X/Y/Wheel、相对轴、短报表和错误边界；Boot Keyboard/Mouse 统一转换为 8 B/4 B 中间报告并注入 `EventRouter`。
6. 完成拔出释放快照、事务超时/STALL/描述符错误统计和 100 ms 后非阻塞重新枚举；`tmos_app.c` 将 Host 服务放在 PS/2/UART 之后、`EventRouter_Process()` 之前，保证原始报表在同一 TMOS 周期进入统一路由。
7. 使用 MounRiver Studio 自带 RISC-V Embedded GCC 8.2.0 完成全工程语法检查和交叉链接：Flash `166,832 B / 448 KB`，RAM `24,572 B / 32 KB`；工程 XML/JSON 解析、ELF 静态对象大小和动态分配调用审计通过。
8. 尚未接入 CH582M 开发板执行 USB-A VBUS、PB13/PB12、键鼠枚举、不同 Report ID、短报表、热插拔和 USB Device/BLE 并发物理验收；代码完成后按顺序进入 M5，实施活跃上行链路与多源状态合并。

## 2026-09-13 Milestone 5：Event_Router 全链路合并与活跃上行策略

1. 将 `Event_Router` 从“事件直接复制到输出队列”改为统一状态源：每个输入源保存键盘、鼠标和手柄状态，键盘按 Usage ID 去重合并，鼠标按钮按源 OR 合并，手柄采用最后更新的有效源快照。
2. 为 HID 与 stream 分别维护 USB/BLE 可用性和活跃掩码。USB 由 configured 且非 suspend 判定；BLE HID 按每个 Report ID 的 HOGP 输入 CCCD 判定，BLE stream 由连接与 NUS TX CCCD 判定；单个未订阅 BLE HID Report 不进入共用队列，避免 HOGP/NUS 或不同 HID Report 之间队首阻塞。
3. 实现 `USB_ONLY`、`BLE_ONLY`、`BOTH`、`USB_PREFERRED`、`BLE_PREFERRED` 和 `NONE` 策略；默认使用 `USB_PREFERRED`，策略变化和输出断链会清理该输出的旧队列，恢复时重新投递当前完整键盘/鼠标/手柄快照。
4. HID 队列满时不等待、不丢弃最新状态：键盘/手柄保留 pending 位，鼠标按输出保留饱和后的增量和按钮状态；数据流保持固定 20 B 分片，输出不可用或 stream 队列满时增加对应统计量。
5. 增加 CDC/NUS 显式控制帧 `[0xA5, 0x5A, command, argument]`：`0x01` 设置策略，`0x02` 设置输出掩码；只有来源为 CDC/NUS 且帧头/命令有效时才解释，普通串口数据继续走 `STREAM_DATA`。
6. 增加 USB `IsReady`、BLE HOGP/NUS ready 状态接口，并在 `tmos_app.c` 每个 2 ms 周期将状态送入路由器；路由器不直接调用 USB/BLE 发送 API，仍由各自后端消费独立 Ring。
7. 使用本机 MounRiver Studio 自带 RISC-V GCC 8.2.0 完成 M5 全工程交叉链接：Flash `170,000 B / 448 KB`，RAM `24,636 B / 32 KB`；`Event_Router` ELF 静态对象为 `0x184 B`，应用层无 libc `malloc/free`，语法检查、链接和 `git diff --check` 通过。
8. 尚未接入 CH582M 开发板执行多源同时按键、鼠标增量、CDC/NUS 控制帧、USB/BLE 切换、HID/NUS CCCD 独立状态和断链恢复物理验收；下一阶段进入 M6，进行资源、长期可靠性和 PCB 收口。

## 2026-09-13 Milestone 6：资源、可靠性和开发板收口准备

1. 新增 `src/firmware_diagnostics.c/.h`，使用 44 B 固定运行态和 184 B 缓存快照（合计 228 B）记录 2 ms TMOS 服务周期的最近/最大执行周期、超时次数、复位原因、系统时钟和 USB/BLE/Router Ring 高水位，并通过 `FirmwareDiagnosticsSnapshot` 汇总 Router、PS/2、UART、USB Host 和 BLE NUS 统计。
2. 看门狗支持通过 `FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE=1` 显式开启，默认关闭；启用后只在完整固件服务周期结束时喂狗，避免服务卡死后继续运行。实际超时和复位行为必须在开发板上测量。
3. 强化 `EventRouter_Post()` 的报表长度校验，禁止异常长度进入固定格式解析器；USB Host 枚举/传输进入错误恢复时先投递键盘/鼠标释放快照，避免重枚举期间遗留 stuck key/button。
4. 明确 `StaticSpscRing_Clear()` 的协同复位前提：只有在生产者/消费者静默时清理；TMOS 路由器在输出后端消费前执行 failover 清队列，符合当前单任务执行顺序。
5. 新增 [`docs/m6-validation.md`](m6-validation.md)，固定代码验收、复位/USB/BLE/多源/溢出/8 h 长稳测试矩阵、诊断快照记录方法和开发板到首版 PCB 的引脚/电源/电平/RF 复核条件。
6. 使用本机 MounRiver Studio 自带 RISC-V GCC 8.2.0 完成 M6 全工程交叉链接：Flash `170,796 B / 448 KB`，RAM `24,868 B / 32 KB`；全量 `src/` 语法检查、应用层动态分配审计、USB Host 阻塞 helper 审计和工程 XML/JSON 解析通过。
7. 尚未执行开发板物理验收和 PCB 迁移；M6 代码验收后，下一步只执行 `docs/m6-validation.md` 中的实测、缺陷修复和硬件收口，不新增协议功能。

## 2026-09-21 Rev B PCB 概念布局与文档整理

1. 核对 WCH 官方引脚和 QFN48 封装图，以及连接器、IrDA、遥控红外的制造商资料，形成 85.60 × 53.98 mm 信用卡尺寸 PCB 提案；正放 MCU，接口围绕中央布置。
2. 用户确认保留完整 DB9 公座、两个独立 Mini-DIN-6 插座，以及标准 IrDA 和 38 kHz 遥控收发两套功能。新增 UART0 IrDA 和 UART3 裸串口规划，保留 UART1 RS232。
3. 新增 `docs/hardware/` 的设计说明、布局和引脚 SVG/PNG、49 行引脚表、CSV、毫米坐标 JSON、生成脚本和几何检查结果。PB16 的错误 ADC 分配在提案中改为 PA6/AIN10，并加入上行 VBUS 检测。
4. 整理根 README 与硬件索引，明确 Rev A 历史记录、Rev B 提案和当前固件的边界；在历史文档顶部注明已知错误，修正 README 电源树中 BAT/SYS 供电意图不一致的说明。
5. 已核对 GPIO 分配、复用限制、预留矩形的边界/重叠、SVG/表格生成及本地文档链接，图形预览已渲染检查。下载的参考 PDF 和临时研究文件不纳入版本控制。
6. 云端原理图、生产 BOM、实际 PCB 布局布线与运行固件尚未按 Rev B 更新。ERC/DRC、实际封装、连接器插拔、RF、热设计及硬件实测仍属于下一阶段。

## 2026-09-28 Rev B-M 物理层叠与机械约束

1. 根据用户新的装配设想，把 Rev B 扩展为 Rev B-M：PCB 顶层器件面向中央盆地，CH582M 封装标记面朝盆地；底层放置 OLED 和三块电容触摸电极，面向用户。
2. I2C、SPI、UART3、WCH-Link 的排针改为板边直角排母，J7/J8/J9/J10 在布局图中用向外插拔的机械包络表示；顶层不再绘制 USER/RST/BOOT 实体按键。
3. 新增中央 1S 电池 pocket、H1–H4 四角安装孔、3D 打印底板/四面墙的剖面 SVG，并在 `placement-revb.json` / `concept-checks.json` 中记录机械覆盖层和自动边界检查。
4. 文档明确电池与 CH582M、QFN 焊点、晶振、DC/DC 电感的 z 向绝缘/压力/温升风险，以及非导电墙体、光窗、排母弯折和天线净空要求。以上仍是机械概念，不替代最终 footprint、3D 模型、螺丝/电芯选择或 ERC/DRC。

## 2026-09-28 图形复核与拆分

1. 复核后确认上一版概念图存在机械覆盖层、底层用户区、信号线和文字互相遮挡的问题，不能作为可靠的评审图。
2. 重画顶层布局图：只保留顶层器件、侧插排母、天线净空和安装孔；把底层 OLED/触摸界面独立为 `pcb-bottom-revb.svg`，把电池/墙体关系保留在独立剖面图。
3. 重新渲染并目视检查顶层、底层和剖面 PNG；同步更新硬件索引、根 README、引脚表和生成检查，使四张 SVG 都通过 XML 校验，电气包络和孔环 keep-out 检查仍无冲突。

## 2026-09-28 Rev B-IR 单光电收发修订

1. 联网核对 Vishay TFBS4650/TFBS4711 数据手册与 IrDA 收发器应用笔记：一个 TXD/RXD 光电收发模块内部已经包含 IRED、PIN 光电二极管和接收 ASIC，载波遥控学习不必额外放置 TSOP；IrDA 脉冲编码也可以由 MCU 完成，MCP2120 不再作为必选器件。
2. 将红外硬件提案从“TFBS4711 + MCP2120 + TSOP38438 + 独立 940 nm LED”改为“带 IRED 阴极的 TFBS4650 级单个共用光模块 + Q_IR/R_IR 遥控电流汇 + 可选 DNP MCP2120 + U12 模式选择器”。PB0/PWM6 负责遥控载波，PB1 捕获共用 RXD 原始脉冲，PB7/PB4 保留 MCU-only IrDA 旁路。
3. 同步更新 `docs/hardware/` 的引脚表、坐标 JSON、顶层布局图、生成脚本和设计说明；明确 TFBS4650 内置 IRED 峰值约 870–910 nm，不能把它误标成 940 nm，若需要 940 nm 必须在 IREDC 支路另选经验证的外部发光器并重新做限流、热、光窗和眼安全评估。TFBS4711 仅保留为 IrDA-only 紧凑备选。
4. 当前运行固件仍是旧的 MCP2120/TSOP/独立 LED 原型；单模块硬件需要后续迁移 RXD 原始脉冲学习、U12/Q_IR 模式仲裁和 MCU-only IrDA 状态机后再做开发板实测。

## 2026-09-29 Rev B 原理图绘制指南

1. 新增 `docs/hardware/schematic-design-guide.md`，将 Rev B-IR 提案拆成 P00–P11 多页原理图绘制顺序，并为电源、USB-C/USB-A、两路 PS/2、RS232、共用红外光头、I²C/SPI/UART3/WCH-Link、OLED/触摸和测试点分别列出网络名、引脚连接、默认状态与待确认项。
2. 指南明确 BQ24074、TPS63031、TPS61023、TPS2553、MAX3232E、BSS138、TFBS4650、SN74LVC2G157 和可选 MCP2120 不能只用方框替代，必须按最终料号数据手册补齐电容、限流、使能、开漏上拉、DNP 和安全状态。
3. 更新硬件索引、根 README 和阅读顺序；固件源文件及此前尚未完成的 Rev B 原理图/PCB 工作保持不变。

## 2026-09-29 原理图指南 SVG 示意图

1. 在 `docs/hardware/schematic-guide/` 新增六张 SVG：P00–P11 分页、电源树、USB/RS232、PS/2 双向电平转换、共用红外光头、I²C/SPI/UART3/WCH-Link/触摸。
2. 将六张图嵌入原理图绘制指南对应章节，并通过 SVG XML 解析和本地 PNG 渲染复核文字、模块边界、箭头和网络标签；修正了红外控制线、PS/2 5 V 上拉和扩展排针图中的重叠。

## 2026-09-30 Rev B-IR 直接 TXD/PWM9 复核与总线规则修订

1. 根据早期手机用同一 IrDA 光头发送家电遥控波形的实际工作方式，撤销 SN74LVC2G157 作为基线 TX 多路器的设计；PB7/TXD0/PWM9 直接连接 TFBS4650 TXD，由 CH582M 内部 UART/PWM 功能和固件在标准 IrDA、38 kHz 遥控之间互斥切换。
2. U9 RXD 直接连接 PB4/RXD0，并以高阻/0 Ω 可选分支送 PB1 学习捕获；IREDC/Q_IR/R_IR 仅保留为直接 TXD 光强不足时的 DNP 增强支路，MCP2120 只用装配时 0 Ω 选择 TX 源。
3. I²C 规则改为“板级每线一组 2.2–4.7 kΩ 上拉，内部上拉只作短线低速后备”；SPI 只保留 CS 默认上拉，SCK/MOSI 不默认加上拉，MISO 仅按空闲电平需求处理。PB18 改为 WCH 电容触摸通道，不默认放 GPIO 上拉，触摸与红外完全分离。
4. 重新生成引脚表、坐标、顶层/底层/剖面/引脚 SVG 与 PNG，更新 P08 直接连接示意图和分页图；49 个引脚、40 个 GPIO、预留区边界/重叠检查仍通过，未触碰用户已有固件修改。

## 2026-09-30 IP5305T + AMS1117 电源候选评估

1. 联网核对 Injoinic IP5305T 原厂数据表和 Advanced Monolithic Systems AMS1117-3.3 数据表；确认 IP5305T 是 1S 充电、单路 5 V 升压的充电宝 SOC，典型充电 1.2 A、升压 1.0 A，并集成 power-path。
2. 评估发现 IP5305T 在 VOUT 负载持续低于约 45 mA 时约 32 s 后轻载关机，ESOP8 没有 CHG#/PGOOD/EN1/EN2/TS，且 USB-A/PS/2 共用一条 1 A 5 V 总线；AMS1117 从 5 V 降到 3.3 V 还要承担压差和热耗散。
3. 新增 `docs/hardware/power-management-evaluation.md` 和 `schematic-guide/power-candidate-ip5305t.svg`，把候选接法、总电流/电池电流公式、原型测试和晋级条件写清楚；正式 Rev B 电源树不改，仍为 BQ24074/TPS63031/TPS61023/TPS2553。
4. 同步更新原理图绘制指南、硬件索引和 README，明确 `5V_IP5` 不得冒充 `SYS`，候选方案若要继续只能另开 `Rev B-P` 并保留 USB-A 独立限流开关。

## 2026-09-30 IP5306-CK / AMS1117 再评估

1. 根据标准 IP5306 资料核对 2.1 A 充电、2.4 A 级 5 V 升压、集成 power-path 和低于约 45 mA 持续约 32 s 的轻载检测；用户提出的 1 A 限流在 USB-A 限 0.5 A、两路 PS/2 各 0.1 A、3V3 约 0.1 A 的约 0.8 A 连续预算下不再作为主要否决点。
2. 检索发现 `IP5306-CK` 的“常开/5V 常开 2A”主要出现在供应商型号页，未找到英集芯公开的 `-CK` 独立数据表；因此把它定为条件性小样候选，要求厂家资料、完整料号/批次和 0/10/50/100 mA 低负载实测通过后才可替换电源树。
3. 明确 3V3 不必强制使用 Buck：`5V_IP53 → AMS1117-3.3` 在 `I3V3≤100–150 mA`、铺铜和温升合格时可以成立；200–300 mA 或封闭电池盆地时改低 IQ LDO/Buck，且 AMS1117 不能从 BAT 直接产生稳定 3V3。
4. 新增 `schematic-guide/power-candidate-ip5306ck.svg`，并同步更新电源评估、原理图绘制指南、PCB 设计说明、硬件索引和 README；正式基线仍为 BQ24074/TPS63031/TPS61023/TPS2553，候选网络使用 `IP53_VIN`、`5V_IP53`，不复用 `SYS`。
5. 参考用户提供的 51hei 页面及对应的 IP5306CK 开源电源板资料，补充 CK 版本的冷启动 `KEY` 要求、约 3 mA 常开待机电流、AMS1117 约 5 mA 静态电流、EPAD 接地/热过孔、1 µH 电感起点和 `B−/P−/GND` 负端隔离规则；底层触摸电极不能代替断电后的第一次激活。

## 2026-09-30 IP5306-CK 总开关与 USB Host 控制

1. 将维持型总开关与 IP5306-CK `KEY` 分离：推荐 DPST `SW_PWR` 同时切断受保护电池 `P+` 和 USB-C VBUS，确保 OFF 是真正断电；`PWR_KEY` 只使用板边瞬时按键负责冷启动/软关断。
2. USB-A Host 继续由 TPS2553 供电和限流；新增 `SW_HOST` 只控制 `HOST_EN`，MCU 通过开漏 `HOST_KILL` 下拉实现软件禁止，机械开关不承载 USB 大电流。两路 PS/2 如需同步关闭，必须另加受控 5 V 分支。
3. 新增 `schematic-guide/power-switch-control.svg` 并将上述网络、测试点和反向供电注意事项写入原理图绘制指南 4.7。

## 2026-09-30 电源开关拓扑按电池拔插方案修订

1. 用户确认电池与系统通过带保护的 XH2.54 连接，不用时直接拔出；因此撤销 DPST `SW_PWR` 作为当前基线，电池侧硬断电由 `J_BAT` 拔插实现。
2. USB-C 输入不再被总开关切断；电池拔出时若 USB-C 仍连接，允许 IP5306-CK 按其 power-path 行为供电/充电，图纸必须把它标成独立的 USB 供电状态。
3. `PWR_KEY` 保留为板边瞬时按键，负责 IP5306-CK 冷启动和按键事件；不把维持型拨动/船型开关长期接到 `KEY`，也不让底层触摸承担断电后的第一次启动。
4. USB-A Host 的 `SW_HOST`/`HOST_KILL` 拓扑不变，只控制 TPS2553 `EN`；OLED 的正常工作熄屏改为软件 Display OFF，独立电源开关只保留为可选 DNP 测试位。

## 2026-09-30 总电源状态转换边界补充

1. 明确当前基线不是维持型总电源开关，而是 `J_BAT` 电池硬断开加 `SW_KEY` 瞬时 KEY。电池接入后的冷启动有明确路径，但 IP5306-CK 的关断手势需按最终变体确认。
2. USB-C 插入时 power-path 可能继续给系统供电，因此不能宣称任意状态都能通过 `SW_KEY` 得到无电关机。
3. 如果产品必须在电池保持连接、USB-C 任意插拔时实现确定的开/关，后续硬件应改为带明确 `EN` 的升压器，或增加电池侧低功耗 KEY 脉冲/锁存控制；单独增加 5 V 负载开关不能替代该功能。
