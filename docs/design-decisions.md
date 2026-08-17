# 首版设计取舍与风险

日期：2026-08-17  
状态：Rev A 结构重排版，待 ERC、器件数据手册和 PCB 复核

## 1. 主控与 USB 分工

原理图按 CH582M 的两个 USB 数据对分别建模：

- `PB11/UD+`、`PB10/UD-` → `USB_DEV_DP_MCU` / `USB_DEV_DN_MCU`：连接 USB-C，作为电脑侧 USB Device。
- `PB13/U2D+`、`PB12/U2D-` → `USB_HOST_DP_MCU` / `USB_HOST_DN_MCU`：连接 USB-A，作为外设侧 USB Host。

这是本项目首版的硬件假设；固件移植前必须以 CH582M 官方数据手册、SDK 示例和实际 USB 控制器初始化要求确认 PA/PB 的主从角色、上拉/下拉配置及 Host VBUS 时序。

USB 数据线上预留 22 Ω 串联电阻，USB-C 与 USB-A 各有一组高速数据 ESD 器件；USB-A VBUS TVS 接在受控的 `VBUS_HOST` 侧，而不是升压芯片输出的未开关侧。

## 2. 电源树

- `VBUS_RAW`：USB-C 输入，进入 BQ24074 的输入端，同时给输入去耦和 TVS。
- `BAT`：J6 的带保护板 1S 锂电池。
- `SYS`：BQ24074 power-path 输出，作为主系统电源。
- `3V3`：TPS63031 buck-boost，供 CH582M、MAX3232、OLED、逻辑上拉和 BLE/RF。
- `5V_HOST`：TPS61023 boost 输出，作为 PS/2 和 USB-A 外设的 5 V 来源。
- `VBUS_HOST`：SY6280 受控输出，连接 USB-A VBUS；`HOST_EN` 由 CH582M 控制。

当前 `R31` 的 `HOST_ISET` 标为 TBD，意味着 USB-A 限流值不能在未核对 SY6280 具体料号/公式前视为最终值。TPS61023 的峰值电流、5 V 输出纹波、USB-A 负载和电池放电能力也需要单独做热/电流预算。

## 3. 充电器参数

U2 使用 BQ24074 power-path 充电器：

- CE# 接地，EN1 接 3V3，EN2 接地，形成首版固定输入限流/USB 供电配置。
- `ISET`、`ILIM`、`TS`、`CHG#`、`PGOOD#` 均保留外部网络。
- `TMR`、`ITERM` 暂按不使用处理并标 NC；这必须结合电池保护板、电池厂商允许的充电终止策略再确认。

不应在未确认电池 NTC 和充电电流前直接生产。

## 4. PS/2 插座与电平

J3/J4 已从错误的 9 针长条封装替换为 6 针圆形 DIN-6 候选 C23689424（`PS2-TH_DIN-603`）。按 PS/2 常见针脚定义建模：pin 1 DATA、pin 2 NC、pin 3 GND、pin 4 +5V、pin 5 CLK、pin 6 NC；连接器外壳/机械 pin 0 归 GND。器件页与数据手册仍需在下单前复核：

- [LCSC C23689424](https://www.lcsc.com/product-detail/C23689424.html)
- [C23689424 datasheet](https://atta.szlcsc.com/upload/public/pdf/source/20240702/0160C5086853E535507D1610542C9395.pdf)

键盘和鼠标各用两颗 BSS138 构成四路双向电平转换，低压侧上拉到 3V3，高压侧上拉到 5V_HOST。

这一部分仍必须用最终采购的 PS/2 插座数据手册核对 pin 1/3/4/5；不能仅依据封装库名称。

## 5. RS232

U6 使用 3.3 V 供电的 MAX3232，并只启用通道 1：

- MCU `UART_TX` → DIN1 → DOUT1 → DB9 pin 3 `RS232_TX`。
- DB9 pin 2 `RS232_RX` → RIN1 → ROUT1 → MCU `UART_RX`。
- DB9 pin 5 及连接器外壳按 GND 处理。

DB9 的 DTE/DCE 角色会改变外部线缆的交叉关系；当前图按常见 DTE 标注，不代表所有设备都能直连。

## 6. BLE/RF

U1 的 `ANT` 接 `RF_ANT`，再接板载 2.4 GHz 天线；32 MHz 晶振及两个 12 pF 负载电容已纳入。RF 区域仍需依据 CH582M 官方参考 layout、天线厂商 keep-out 和匹配网络建议重画 PCB。

当前原理图没有额外 π 匹配网络；如果天线厂商或 WCH 参考设计要求，PCB 阶段应预留可装配的匹配焊盘。

## 7. 调试、按键和扩展

- J9：3V3、GND、WCH_TCK、WCH_TIO、RESET、GND。
- SW1/SW2/SW3：BOOT、RESET、USER，均为按下接 GND 的按键并配 10 kΩ 上拉。
- J8：3V3、GND、SPI_MISO、SPI_MOSI、SPI_SCK、SPI_CS。
- J7：4 线 OLED 接口，按 pin 1 GND、pin 2 3V3、pin 3 SCL、pin 4 SDA 建模，额外机械脚接 GND。

J8 只使用已规划的 SPI 引脚；其余 MCU GPIO 在首版标 NC，后续若扩展 UART/I²C，需要移除相应 NC 标记并重新审查复用冲突。

## 8. 不作为首版承诺的内容

- 2.4 GHz 专用接收端的硬件和软件。
- USB 高速模式、USB Hub、多设备并发带宽保证。
- 所有触摸屏/手柄的任意 HID 报表自动转换。
- BLE 端的完整多路 HID/串口协议实现。
- 免驱 WinUSB 厂商通道的最终描述符。

## 9. 器件可追溯性与装配约束

- 电阻、电容已统一压缩为短显示值；默认电阻/小电容为 0603，储能和输入/输出电容为 0805，便于手焊和控制尺寸。
- J1/J2/J5/J6/J7/J8/J9、X1、ANT1、SW1/SW2/SW3 仍保留为 C990 Extended Part 机械候选，已在图中明确标注 `[TBD]`，不能把它们视为已经核实的生产料号。
- J3/J4 使用可查询的 C23689424，但它仍属于 Extended Part；最终采购前应核实库存、机械尺寸、引脚编号和 3D/PCB 封装。
