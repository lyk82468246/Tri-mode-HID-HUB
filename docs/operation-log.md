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
