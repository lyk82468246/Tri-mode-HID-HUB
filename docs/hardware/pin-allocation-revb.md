# CH582M Rev B 引脚分配提案

日期：2026-09-21。配套布局：`pcb-concept-revb.svg`；顶视放大图：`pinout-revb.svg`。
这是新的硬件提案，不覆盖 Rev A 云端工程或当前 `src/board_pins.h`。

0° 定义：数据手册第 111 页 Top View，Pin 1 在左下。1–10 南侧从左到右；11–24 东侧从下到上；25–34 北侧从右到左；35–48 西侧从上到下。EP 在手册标 0，本提案 CSV 以库常用的 49 表示。

| Pin | 芯片功能 | 物理侧 | Rev B 网络 | 用途/约束 |
|---:|---|---|---|---|
| 1 | VDCID | 南 | `CH_VDCI` | 内部电源节点；连接 VDCIA；按 DCDC/LDO 模式去耦 |
| 2 | VSW | 南 | `CH_VSW` | 到本地电感再到 VDCID；是否启用内部 DCDC 与固件一致 |
| 3 | VIO33/VDD33 | 南 | `3V3` | 电源输入；贴近引脚去耦 |
| 4 | PA7/AIN11 | 南 | `USB_C_VBUS_ADC` | 新增：VBUS_RAW 分压/RC；禁止 5 V 直连 |
| 5 | PA8/RXD1 | 南 | `RS232_RX_TTL` | UART1 默认映射；MAX3232 ROUT1 到 MCU |
| 6 | PA9/TXD1 | 南 | `RS232_TX_TTL` | UART1 默认映射；MCU 到 MAX3232 DIN1 |
| 7 | PB9 | 南 | `CHG_N` | BQ24074 CHG#；开漏，外部上拉到 3V3 |
| 8 | PB8 | 南 | `CHG_EN1` | BQ24074 EN1；外部下拉，上电默认 USB100 |
| 9 | PB17 | 南 | `CHG_EN2` | BQ24074 EN2；外部下拉，上电默认 USB100 |
| 10 | PB16 | 南 | `INPUT_PGOOD_N` | BQ24074 PGOOD#；数字输入，非 ADC |
| 11 | PB15/TCK | 东 | `WCH_TCK` | 保留 WCH 两线调试，不复用 SPI |
| 12 | PB14/TIO | 东 | `WCH_TIO` | 保留 WCH 两线调试，不复用 SPI |
| 13 | PB13/U2D+ | 东 | `USB_HOST_DP_MCU` | USB2 Host，固定数据引脚 |
| 14 | PB12/U2D- | 东 | `USB_HOST_DN_MCU` | USB2 Host；禁止 I2C 默认映射占用 |
| 15 | PB11/UD+ | 东 | `USB_DEV_DP_MCU` | USB Device，固定数据引脚 |
| 16 | PB10/UD- | 东 | `USB_DEV_DN_MCU` | USB Device，固定数据引脚 |
| 17 | PB7/TXD0 | 东 | `IRDA_UART_TX` | 到 MCP2120 pin12 TX；UART0 默认映射 |
| 18 | PB6 | 东 | `HOST_EN` | TPS2553 EN，高有效；外部下拉，上电关断 |
| 19 | PB5 | 东 | `HOST_FAULT_N` | TPS2553 FAULT#；外部上拉到 3V3；不是电压合格指示 |
| 20 | PB4/RXD0 | 东 | `IRDA_UART_RX` | 来自 MCP2120 pin11 RX；UART0 默认映射 |
| 21 | PB3 | 东 | `IRDA_CODEC_EN` | MCP2120 pin13 EN，高有效；外部下拉 |
| 22 | PB2 | 东 | `IRDA_MODE` | MCP2120 pin7 MODE；高为数据，低为配置 |
| 23 | PB1 | 东 | `IR_REMOTE_RX` | TSOP38438 OUT；GPIOB 边沿中断，软件切换极性并记时间戳 |
| 24 | PB0/PWM6 | 东 | `IR_REMOTE_TX` | PWM6 约 38 kHz，经 MOSFET 驱动独立 940 nm LED |
| 25 | PB23/RST# | 北 | `RESET_N` | 低有效复位；按键与调试口共用，不重映射 UART2 |
| 26 | PB22 | 北 | `BOOT_N` | 低有效下载按键；保留 ISP 配置 |
| 27 | PB21/SCL_ | 北 | `I2C_SCL` | I2C 必须重映射 RB_PIN_I2C=1 |
| 28 | PB20/SDA_ | 北 | `I2C_SDA` | 裸 I2C/OLED 共用 J7，外部上拉到 3V3 |
| 29 | PB19 | 北 | `IRDA_SD` | TFBS4711 pin4 SD，高关断；外部上拉 |
| 30 | PB18 | 北 | `USER_N` | 用户键迁至此；外部上拉，按下接地 |
| 31 | X32MO | 北 | `X32MO` | 32 MHz 晶振；负载由晶体 CL 与寄生计算 |
| 32 | X32MI | 北 | `X32MI` | 32 MHz 晶振；最短回路，与 RF 馈线分开 |
| 33 | VINTA | 北 | `CH_VINTA` | 内部模拟节点；只按手册去耦，不作为外部负载电源 |
| 34 | ANT | 北 | `RF_ANT` | 向北，50 ohm 馈线；可选调谐焊盘贴近馈点 |
| 35 | VDCIA | 西 | `CH_VDCI` | 按 WCH 参考与 VDCID 相连并本地去耦 |
| 36 | PA4/RXD3 | 西 | `UART_TTL_RX` | 新增 J10 裸串口；UART3 默认映射 |
| 37 | PA5/TXD3 | 西 | `UART_TTL_TX` | 新增 J10 裸串口；MCU 视角命名，3.3 V |
| 38 | PA6/AIN10 | 西 | `VBAT_SENSE` | BAT 分压/RC；从不支持 ADC 的 PB16 迁入 |
| 39 | PA0 | 西 | `KBD_CLK_MCU` | 保留；GPIOA 中断，经 BSS138 开漏电平转换 |
| 40 | PA1 | 西 | `KBD_DATA_MCU` | 保留；仅拉低/释放，经 BSS138 |
| 41 | PA2 | 西 | `MOUSE_CLK_MCU` | 保留；GPIOA 中断，经 BSS138 |
| 42 | PA3 | 西 | `MOUSE_DATA_MCU` | 保留；仅拉低/释放，经 BSS138 |
| 43 | PA15/MISO | 西 | `SPI_MISO` | SPI0 默认映射；禁止 UART0 重映射占用 |
| 44 | PA14/MOSI | 西 | `SPI_MOSI` | SPI0 默认映射 |
| 45 | PA13/SCK0 | 西 | `SPI_SCK` | SPI0 默认映射；不启用 PWM5 输出 |
| 46 | PA12/SCS | 西 | `SPI_CS_N` | SPI0 默认映射；不启用 PWM4 输出 |
| 47 | PA11/X32KO | 西 | `LSE_OUT_RESERVED` | 可选 32.768 kHz 晶振；未装时 NC，不接排针 |
| 48 | PA10/X32KI | 西 | `LSE_IN_RESERVED` | 可选 32.768 kHz 晶振；当前 LSI 方案可不装 |
| 49 | EP / GND (datasheet pin 0) | 底部 EP | `GND` | EP 接完整地平面与接地过孔；核对库的 0/49 编号映射 |

## 接插件定义（均按连接器自身脚号，不能按焊接面视觉猜编号）

| 接口 | 接线定义 |
|---|---|
| J1 USB-C | A4/A9/B4/B9 = VBUS_RAW；A6/B6 = D+；A7/B7 = D−；CC1/CC2 各自独立 5.1 kΩ 到地；SBU NC；GND 与壳体接法按 ESD 设计 |
| J2 USB-A | 1 VBUS_HOST；2 D−；3 D+；4 GND；壳体接屏蔽/地方案 |
| J3/J4 Mini-DIN-6 | 1 DATA；2 NC；3 GND；4 受保护的 5V_PS2_K/M；5 CLK；6 NC；外壳地；必须核对实际座子的 mating-face 与 PCB-side 图 |
| J5 DB9 公座 DTE | 2 RX（到 MAX3232 RIN1）；3 TX（来自 DOUT1）；5 GND；1/4/6/7/8/9 NC；不提供 RTS/CTS 硬件握手 |
| J6 电池 | 1 BAT+；2 GND（本板自定义，不能假定成品电池线颜色/极性一致）；外接带保护 1S 电池，NTC 另用焊盘连接 |
| J7 I2C/OLED 1×4 | 1 GND；2 3V3_OUT；3 SCL；4 SDA。一个共享硬件 I2C 总线 |
| J8 SPI0 2×4 | 1 3V3_OUT；2 GND；3 SCK；4 GND；5 MOSI；6 MISO；7 CS#；8 GND。顶视左列 1/3/5/7、右列 2/4/6/8 |
| J9 WCH-Link 2×3 | 1 3V3_VTref；2 GND；3 TCK；4 TIO；5 RESET#；6 GND。目标板自行供电，VTref 不与调试器电源硬并联 |
| J10 UART3 1×4 | 1 GND；2 3V3_OUT；3 TX（MCU 输出）；4 RX（MCU 输入）。不是 RS232 电平 |

J7/J10 的 pin1 均在图示横排左端；J8/J9 的 pin1 在左上。正式 footprint 应以方焊盘、丝印和 3D 模型再次确认。

## 外设资源与复用

| 资源 | 选择 | 冲突处理 |
|---|---|---|
| USB / USB2 | Device PB11/PB10；Host PB13/PB12 | 不占用作 UART1、SPI0 或 I2C 默认脚 |
| UART0 | PB7 TX / PB4 RX，IrDA 编解码器 | RB_PIN_UART0=0；MODEM 功能关闭，PB0–PB6 按表分配 |
| UART1 | PA9 TX / PA8 RX，RS232 | RB_PIN_UART1=0；现有 UART1 debug 输出需防止污染业务串口 |
| UART2 | 不启用 | 默认 PA6/PA7 用作 ADC；重映射 PB22/PB23 保留给 BOOT/RESET |
| UART3 | PA5 TX / PA4 RX，裸串口 | RB_PIN_UART3=0；PB20/PB21 留给 I2C |
| SPI0 | PA12–PA15 默认组 | RB_PIN_SPI0=0；SPI1 禁用（其 PA0–PA2 已是 PS/2） |
| I2C | PB21 SCL / PB20 SDA | RB_PIN_I2C=1，否则与 USB2 冲突 |
| PWM6 | PB0，38 kHz 遥控载波 | 只使能 PWM6，不开启与 UART/SPI/ADC 重叠的其他 PWM 输出 |
| GPIO 中断 / 时间基准 | PS/2 GPIOA；遥控接收 GPIOB PB1 | 遥控只在边沿记录时间；使用可用自由运行计时器，不在 ISR 解码 |
| ADC | AIN10=PA6 电池；AIN11=PA7 上行 VBUS | 采样前关闭数字输入/上拉，按 ADC PGA 范围核算分压 |
| LSE | PA10/PA11 可选晶振 | 与现有 LSI 模式二选一；没有分配给其他信号 |

PB16 不支持 ADC。Rev A 的 USB-A 电压模拟采样不再占用 PA4：PA4/PA5 用于独立 UART3；本提案使用 HOST_FAULT_N 报告开关故障，FAULT# 不能代替 5 V 电压测量。若还要求 USB-A 电压数值，需加模拟开关复用 ADC 或外置 ADC，不能把 PB16 标成 ADC。

## 新增红外电路连接

- MCP2120：pin12 TX ← PB7；pin11 RX → PB4；pin7 MODE ← PB2；pin13 EN ← PB3；pin4 RESET 接 RESET_N；pin1 VDD=3V3、pin14 GND。
- MCP2120 的 pin2/3 接独立 7.3728 MHz 晶体及计算后的负载；pin8/9/10（BAUD2/1/0）拉高选择软件速率配置，上电先按 9600 bps 初始化，再按数据手册切换。
- MCP2120 pin6 TXIR → TFBS4711 pin2 TXD；TFBS4711 pin3 RXD → MCP2120 pin5 RXIR；TFBS4711 pin4 SD ← PB19。TFBS4711 pin5 用 3V3，pin1 LED 电源按其推荐电路及脉冲负载配置，pin6 GND。
- TFBS4711 是 SIR 光学物理层，MCP2120 是脉冲编解码；完整 IrDA 协议如 IrLAP/IrLMP 不会由这两颗芯片自动提供，需要固件实现。
- TSOP38438：pin1 OUT → PB1，pin2 GND，pin3=3V3 经本地滤波；其已解调输出用于 NEC/RC5 等包络接收。GPIOB 没有原生双边沿模式，ISR 要根据输入电平切换下一边沿极性，并检查竞态/丢沿与最坏中断延迟。
- PB0/PWM6 → 栅极电阻 → 逻辑级 NMOS → 独立 940 nm LED，栅极下拉；LED 串联电阻和电源按允许的脉冲电流计算，禁止 GPIO 直接驱动大电流。
- 两套光学器件彼此分隔并预留遮光隔墙。IrDA 和遥控都能实现，但同一时刻发射需仲裁，不能保证同方向同时光学通信互不干扰。

## 与当前固件/Rev A 的差异

- 保留 USB 两组固定引脚、PS/2 PA0–PA3、SPI0 PA12–PA15、RS232 UART1、调试与 BOOT/RESET。
- VBAT_SENSE：PB16 → PA6/AIN10；PA7/AIN11 新增上行 VBUS 检测；PA4/PA5 改为 UART3 裸排针。
- USER：PB8 → PB18；CHG#：PB19 → PB9；PB8/PB17 新增充电模式控制；PB16 改为输入电源有效状态。
- U5 计划由 SY6280 改为 TPS2553，新增 PB5 FAULT#，需要新封装/参数，绝不是直接替换料号。
- 新增 UART0 + IrDA、PWM6 + 红外收发、UART3/I2C/SPI 初始化及相应协议。当前固件未实现这些新增功能；本次未修改运行固件。

依据：[WCH CH583DS1 v1.9](https://github.com/openwch/ch583/blob/main/Datasheet/CH583DS1_zh.PDF) 第 3–7 页引脚说明、第 111 页封装图及相关外设章节，以及仓库 `StdPeriphDriver/inc/CH583SFR.h` 的复用定义。其余元件资料见 `pcb-design-study.md`。
