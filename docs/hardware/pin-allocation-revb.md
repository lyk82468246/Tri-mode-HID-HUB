# CH582M Rev B-IR 引脚分配提案

日期：2026-09-30。配套布局：`pcb-concept-revb.svg`；底层用户界面：`pcb-bottom-revb.svg`；物理剖面：`pcb-stack-revb.svg`；顶视放大图：`pinout-revb.svg`。
这是单光电收发模块的 Rev B-IR 硬件基线；当前固件仍按旧的 MCP2120/TSOP 分立原型，最终电气行为需在开发板/PCB 上实测确认。

0° 定义：数据手册第 111 页 Top View，Pin 1 在左下。1–10 南侧从左到右；11–24 东侧从下到上；25–34 北侧从右到左；35–48 西侧从上到下。EP 在手册标 0，本提案 CSV 以库常用的 49 表示。

| Pin | 芯片功能 | 物理侧 | Rev B-IR 网络 | 用途/约束 |
|---:|---|---|---|---|
| 1 | VDCID | 南 | `CH_VDCI` | 内部电源节点；连接 VDCIA；按 DCDC/LDO 模式去耦 |
| 2 | VSW | 南 | `CH_VSW` | 到本地电感再到 VDCID；是否启用内部 DCDC 与固件一致 |
| 3 | VIO33/VDD33 | 南 | `3V3` | 电源输入；贴近引脚去耦 |
| 4 | PA7/AIN11 | 南 | `USB_C_VBUS_ADC` | 新增：VBUS_RAW 分压/RC；禁止 5 V 直连 |
| 5 | PA8/RXD1 | 南 | `RS232_RX_TTL` | UART1 默认映射；MAX3232 ROUT1 到 MCU |
| 6 | PA9/TXD1 | 南 | `RS232_TX_TTL` | UART1 默认映射；MCU 到 MAX3232 DIN1 |
| 7 | PB9 | 南 | `STAT_CHG_N` | ETA9697 STAT；开漏，外部约 10 kΩ 上拉到 3V3，低电平表示充电中 |
| 8 | PB8 | 南 | `POWER_AUX_EN` | 高电流分支运行时控制/监测预留；硬件总开关必须取 SW_SYS 后节点，ETA9697 不使用该脚，外部下拉保持安全态 |
| 9 | PB17 | 南 | `POWER_AUX_FAULT_N` | 预留给高电流升压/电源监测故障输入；ETA9697 不使用该脚，外部上拉或 NC 按最终分支处理 |
| 10 | PB16 | 南 | `POWER_RESERVED` | ETA9697 没有 PGOOD 输出；保留数字测试点，禁止当 ADC 使用 |
| 11 | PB15/TCK | 东 | `WCH_TCK` | 保留 WCH 两线调试，不复用 SPI |
| 12 | PB14/TIO | 东 | `WCH_TIO` | 保留 WCH 两线调试，不复用 SPI |
| 13 | PB13/U2D+ | 东 | `USB_HOST_DP_MCU` | USB2 Host，固定数据引脚 |
| 14 | PB12/U2D- | 东 | `USB_HOST_DN_MCU` | USB2 Host；禁止 I2C 默认映射占用 |
| 15 | PB11/UD+ | 东 | `USB_DEV_DP_MCU` | USB Device，固定数据引脚 |
| 16 | PB10/UD- | 东 | `USB_DEV_DN_MCU` | USB Device，固定数据引脚 |
| 17 | PB7/TXD0/PWM9 | 东 | `IR_TXD_U9` | 直接接 U9 TXD；IrDA 用 UART0 TX，遥控用同一脚的 PWM9/定时器载波与包络；不放外部 TX 多路器 |
| 18 | PB6 | 东 | `HOST_EN` | TPS2553 EN，高有效；外部下拉，上电关断 |
| 19 | PB5 | 东 | `HOST_FAULT_N` | TPS2553 FAULT#；外部上拉到 3V3；不是电压合格指示 |
| 20 | PB4/RXD0 | 东 | `IR_RX_RAW` | 直接接 U9 RXD；UART0 IrDA 接收；同一高阻节点可选 0 Ω/串阻分支到 PB1 学习捕获 |
| 21 | PB3 | 东 | `RESERVED_GPIO` | 不再承担外部红外模式选择；保留为普通 GPIO/测试点，原理图不连接 U12 |
| 22 | PB2 | 东 | `RESERVED_GPIO` | 不再承担 MCP2120 MODE；可作为未来扩展 GPIO，默认 NC/测试点 |
| 23 | PB1 | 东 | `IR_RX_CAPTURE` | 可选高阻输入捕获 U9 RXD；遥控学习记录原始载波脉冲，不放 TSOP |
| 24 | PB0/PWM6 | 东 | `IR_EXT_SINK_GATE` | 仅在可选 DNP IREDC/Q_IR 支路装配时作栅极控制；直接 TXD 基线不使用 |
| 25 | PB23/RST# | 北 | `RESET_N` | 低有效复位；底层 RST 触摸电极与调试口共用，不重映射 UART2 |
| 26 | PB22 | 北 | `BOOT_N` | 底层 BOOT 触摸电极；保留 ISP 配置 |
| 27 | PB21/SCL_ | 北 | `I2C_SCL` | I2C 必须重映射 RB_PIN_I2C=1 |
| 28 | PB20/SDA_ | 北 | `I2C_SDA` | 板边直角 J7 I2C/OLED 排母，外部上拉到 3V3 |
| 29 | PB19 | 北 | `IRDA_SD` | U9 pin5 SD，高关断；10 kΩ 左右下拉到 GND，PB19 拉高才关断 |
| 30 | PB18 | 北 | `USER_TOUCH` | 底层 USER 电容触摸通道；按 WCH touch 配置使用，不默认放 GPIO 上拉 |
| 31 | X32MO | 北 | `X32MO` | 32 MHz 晶振；负载由晶体 CL 与寄生计算 |
| 32 | X32MI | 北 | `X32MI` | 32 MHz 晶振；最短回路，与 RF 馈线分开 |
| 33 | VINTA | 北 | `CH_VINTA` | 内部模拟节点；只按手册去耦，不作为外部负载电源 |
| 34 | ANT | 北 | `RF_ANT` | 向北，50 ohm 馈线；可选调谐焊盘贴近馈点 |
| 35 | VDCIA | 西 | `CH_VDCI` | 按 WCH 参考与 VDCID 相连并本地去耦 |
| 36 | PA4/RXD3 | 西 | `UART_TTL_RX` | 新增 J10 直角排母；UART3 默认映射 |
| 37 | PA5/TXD3 | 西 | `UART_TTL_TX` | 新增 J10 直角排母；MCU 视角命名，3.3 V |
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
| J1 USB-C | A4/A9/B4/B9 = VBUS_RAW → 入口保护 → ETA_VIN；A6/B6 = D+；A7/B7 = D−；CC1/CC2 各自独立 5.1 kΩ 到地；SBU NC；GND 与壳体接法按 ESD 设计 |
| J2 USB-A | 1 VBUS_HOST；2 D−；3 D+；4 GND；壳体接屏蔽/地方案 |
| J3/J4 Mini-DIN-6 | 1 DATA；2 NC；3 GND；4 受保护的 5V_PS2_K/M；5 CLK；6 NC；外壳地；必须核对实际座子的 mating-face 与 PCB-side 图 |
| J5 DB9 公座 DTE | 2 RX（到 MAX3232 RIN1）；3 TX（来自 DOUT1）；5 GND；1/4/6/7/8/9 NC；不提供 RTS/CTS 硬件握手 |
| J6 电池 | 1 BAT+；2 GND（本板自定义，不能假定成品电池线颜色/极性一致）；改为中央盆地内的带保护 1S 电池连接，NTC 另用焊盘连接；电芯不得压到 U1/电感/焊点 |
| J7 I2C/OLED 1×4 | 1 GND；2 3V3_OUT；3 SCL；4 SDA。右角弯插排母，向板外插拔；OLED 本体在 PCB 底层 |
| J8 SPI0 2×4 | 1 3V3_OUT；2 GND；3 SCK；4 GND；5 MOSI；6 MISO；7 CS#；8 GND。左列 1/3/5/7、右列 2/4/6/8；底边弯插排母 |
| J9 WCH-Link 2×3 | 1 3V3_VTref；2 GND；3 TCK；4 TIO；5 RESET#；6 GND。底边弯插排母；目标板自行供电，VTref 不与调试器电源硬并联 |
| J10 UART3 1×4 | 1 GND；2 3V3_OUT；3 TX（MCU 输出）；4 RX（MCU 输入）。顶边弯插排母，不是 RS232 电平 |

J7/J8/J9/J10 均是向板外插拔的直角排母；正式 footprint 应以方焊盘、丝印、mating-face 和 3D 模型再次确认。底层 OLED/触摸电极的镜像、FPC 和螺丝孔避让必须在 EDA 中单独检查。

## Rev B-M 物理层叠约束

- PCB 顶层器件在装配姿态中朝向盆地；CH582M 的封装标记面朝下。底层 OLED 与三块 USER/RST/BOOT 触摸电极朝用户面。这个“顶/底”是 PCB 层名，不等于观察图的朝向。
- 中央盆地只定义电池空间，不豁免 U1、晶振、DC/DC 电感、焊点与电池之间的绝缘/压力/温升验证；先锁定电芯厚度、泡棉和支撑高度，再确定 pocket。
- H1–H4 为四角贯穿安装孔候选，当前按 3.2 mm 通孔的 M3 起点绘制；螺丝、垫片、支柱、热熔螺母和孔环形焊盘需作为一组重新收口。
- 3D 打印底板与四面墙建议采用非导电、非碳纤材料；墙体为 USB/DB9/PS2/共用光学头/直角排母开窗，BLE 天线保留净空。

## 外设资源与复用

| 资源 | 选择 | 冲突处理 |
|---|---|---|
| USB / USB2 | Device PB11/PB10；Host PB13/PB12 | 不占用作 UART1、SPI0 或 I2C 默认脚 |
| UART0 | PB7 TX / PB4 RX，标准 IrDA 数据通道 | RB_PIN_UART0=0；同一 PB7 也可切换 PWM9 生成遥控载波 |
| UART1 | PA9 TX / PA8 RX，RS232 | RB_PIN_UART1=0；现有 UART1 debug 输出需防止污染业务串口 |
| UART2 | 不启用 | 默认 PA6/PA7 用作 ADC；重映射 PB22/PB23 保留给 BOOT/RESET |
| UART3 | PA5 TX / PA4 RX，板边直角排母 | RB_PIN_UART3=0；PB20/PB21 留给 I2C |
| SPI0 | PA12–PA15 默认组 | RB_PIN_SPI0=0；SPI1 禁用（其 PA0–PA2 已是 PS/2） |
| I2C | PB21 SCL / PB20 SDA | RB_PIN_I2C=1，否则与 USB2 冲突 |
| PB7/PWM9 | 同一 PB7/TXD0 产生 38 kHz 载波和包络 | 由 MCU 内部复用选择；不放外部 TX 多路器 |
| PB0/PWM6 | 可选 IREDC/Q_IR 栅极 | 仅装配 DNP 电流汇增强支路时使用；基线 NC/测试点 |
| GPIO 中断 / 时间基准 | PS/2 GPIOA；共用光模块 RXD 可送 PB1 | 遥控学习记录 RXD 原始脉冲；不在 ISR 解码整帧 |
| ADC | AIN10=PA6 电池；AIN11=PA7 上行 VBUS | 采样前关闭数字输入/上拉，按 ADC PGA 范围核算分压 |
| LSE | PA10/PA11 可选晶振 | 与现有 LSI 模式二选一；没有分配给其他信号 |

PB16 不支持 ADC。Rev A 的 USB-A 电压模拟采样不再占用 PA4：PA4/PA5 用于独立 UART3；本提案使用 HOST_FAULT_N 报告开关故障，FAULT# 不能代替 5 V 电压测量。ETA9697 的 STAT 接 PB9；PB8/PB17 预留给高电流分支 EN/故障，不能假定为 BQ24074 的 EN1/EN2；若还要求 USB-A 电压数值，需加模拟开关复用 ADC 或外置 ADC，不能把 PB16 标成 ADC。

## Rev B-IR 红外电路连接

- U9 基线改为带 `IREDC` 阴极引出的 TFBS4650（侧视封装）：一个器件内仍同时包含 IRED、PIN 光电二极管和接收 ASIC，只保留一个朝外光窗。基线把 U9 `TXD` 直接接到 PB7；`IREDA`、`IREDC`、VCC、去耦、SD、RXD 按制造商资料画出。
- **不放 U12 外部多路器**：PB7/TXD0 是同一根物理线，标准 IrDA 时配置为 UART0 TX，家电遥控时配置为 PWM9/定时器输出载波与包络；这是 CH582M 内部复用和固件状态机完成的互斥，不需要 PB3 选择脚。
- `IREDC`/Q_IR/R_IR 只保留为可选 DNP 增强支路。若实测直接 TXD 的光强、脉宽或外部 IRED 电流不足，再用 PB0/PWM6 通过栅极电阻驱动 Q_IR；装配该支路时，固件必须先让 PB7/TXD0 进入安全低电平，避免两种发射路径叠加。
- U9 RXD 为共用接收节点 `IR_RX_RAW`：直接送 PB4/RXD0，另以高阻/0 Ω 可选分支送 PB1 捕获。遥控学习记录 U9 的原始载波脉冲串，不需要 TSOP；不能让两个推挽输出同时驱动该节点。
- 标准 IrDA 和遥控发射都由 MCU 软件/外设完成；MCP2120 只作为可选 DNP 物理层编码器，若装配则用 0 Ω 选择 TX 源，不能与 PB7 推挽输出硬并联。
- TFBS4650 的 TXD 脉冲宽度窗口覆盖常见 38 kHz、1/3–1/2 占空比载波高电平，但最终光强、距离、学习灵敏度仍需实物验证；同一光窗在物理层仍是半双工。
- U9 的 TX/RX 不能同时当作无回声全双工链路；发射期间 RXD 会回显/饱和，固件须屏蔽回显并在发射结束后按数据手册留出接收恢复时间。
- TFBS4650 内置 IRED 峰值约 870–910 nm；若产品必须使用 940 nm 外部发射器，应在 IREDC 支路改接经验证的外部 IRED，并重新做光学窗口、限流、热和眼安全评估。TFBS4711 可作为紧凑的 IrDA-only 备选，但因无 IREDC 不能作为本 Rev B-IR 遥控发射基线。

## 与当前固件/Rev A 的差异

- 保留 USB 两组固定引脚、PS/2 PA0–PA3、SPI0 PA12–PA15、RS232 UART1、调试与 BOOT/RESET。
- VBAT_SENSE：PA6/AIN10；PA7/AIN11 新增上行 VBUS 检测；PA4/PA5 改为 UART3 板边直角排母。
- USER：PB18；ETA9697 STAT：PB9；PB8/PB17 预留给高电流分支 EN/故障；PB16 保留数字测试点，不作为 ADC/PGOOD。
- U5 计划由 SY6280 改为 TPS2553，新增 PB5 FAULT#，需要新封装/参数，绝不是直接替换料号。
- 当前运行固件仍是旧的 UART0/MCP2120/TFBS4711 + PB1 TSOP/PB0 独立 LED 原型；单模块方案需要迁移到 PB7 直连 TXD、PB4 直连 RXD、可选 PB1 捕获和 MCU 内部 UART/PWM 模式状态机后，才能宣称软件与硬件一致。

依据：[WCH CH583/CH582 官方资料仓库](https://github.com/openwch/ch583)及 CH582M 数据手册引脚复用；[Vishay TFBS4650 数据手册](https://www.vishay.com/docs/84672/tfbs4650.pdf)；[Vishay IrDA 收发器应用笔记](https://www.vishay.com/doc/?82610=)；[Microchip MCP2120 数据手册](https://ww1.microchip.com/downloads/en/devicedoc/21618b.pdf)。其余元件资料见 `pcb-design-study.md`。
