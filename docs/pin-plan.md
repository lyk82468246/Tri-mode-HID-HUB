# CH582M 首版引脚规划

> 版本：Rev A 历史记录，对应首版云端原理图设计意图，不是已验证的生产引脚表。下表 PB16 电池 ADC 分配已确认不可用；请参阅 [Rev B 引脚提案](hardware/pin-allocation-revb.md)及[硬件文档索引](hardware/README.md)。Rev B 尚未同步到云端原理图或运行固件。

## MCU U1

| Pin | 芯片功能 | 首版网络 | 用途 |
|---:|---|---|---|
| 1 | VDCID | CH_VDCI | DCDC 电容节点 |
| 2 | VSW | CH_VSW | CH582M 内置 DCDC 开关节点 |
| 3 | VIO33/VDD33 | 3V3 | 主电源 |
| 5 | PA8/RXD1 | UART_RX | MAX3232 接收 |
| 6 | PA9/TXD1 | UART_TX | MAX3232 发送 |
| 8 | PB8 | USER | 用户按键 |
| 10 | PB16 | VBAT_SENSE | 电池电压采样 |
| 11 | PB15/TCK | WCH_TCK | WCH-Link 调试 |
| 12 | PB14/TIO | WCH_TIO | WCH-Link 调试 |
| 13 | PB13/U2D+ | USB_HOST_DP_MCU | USB-A Host D+ |
| 14 | PB12/U2D- | USB_HOST_DN_MCU | USB-A Host D- |
| 15 | PB11/UD+ | USB_DEV_DP_MCU | USB-C Device D+ |
| 16 | PB10/UD- | USB_DEV_DN_MCU | USB-C Device D- |
| 18 | PB6/RTS | HOST_EN | USB-A VBUS 负载开关使能 |
| 25 | PB23/RST | RESET | 复位按键/调试 |
| 26 | PB22 | BOOT | 下载/启动按键 |
| 27 | PB21/SCL | I2C_SCL | OLED SCL |
| 28 | PB20/SDA | I2C_SDA | OLED SDA |
| 29 | PB19 | CHG# | 充电状态 |
| 31 | X32MO | X32MO | 32 MHz 晶振 |
| 32 | X32MI | X32MI | 32 MHz 晶振 |
| 33 | VINTA | VINTA_TEST | 按参考设计保留 |
| 34 | ANT | RF_ANT | BLE/RF 天线 |
| 35 | VDCIA | VDCIA | 模拟电源去耦 |
| 36 | PA4/AIN0 | VBUS_SENSE | USB-A VBUS 采样 |
| 39 | PA0 | KBD_CLK_MCU | PS/2 键盘时钟低压侧 |
| 40 | PA1 | KBD_DATA_MCU | PS/2 键盘数据低压侧 |
| 41 | PA2 | MOUSE_CLK_MCU | PS/2 鼠标时钟低压侧 |
| 42 | PA3 | MOUSE_DATA_MCU | PS/2 鼠标数据低压侧 |
| 43 | PA15/MISO | SPI_MISO | 扩展排针 |
| 44 | PA14/MOSI | SPI_MOSI | 扩展排针 |
| 45 | PA13/SCK | SPI_SCK | 扩展排针 |
| 46 | PA12/SCS | SPI_CS | 扩展排针 |
| 49 | EP | GND | 裸露焊盘接地 |

U1 的 4、7、9、17、19–24、30、37、38、47、48 在首版标 NC；若后续启用，必须同步修改原理图和固件复用配置。

## 外部接口

| Ref | 接口 | 关键网络 |
|---|---|---|
| J1 | USB-C | VBUS_RAW、CC1/CC2、USB_DEV_DP/DN、GND |
| J2 | USB-A Host | VBUS_HOST、USB_HOST_DP/DN、GND |
| J3 | PS/2 键盘 | KBD_DATA_5V、KBD_CLK_5V、5V_HOST、GND |
| J4 | PS/2 鼠标 | MOUSE_DATA_5V、MOUSE_CLK_5V、5V_HOST、GND |
| J5 | DB9 RS232 | pin 2 RS232_RX、pin 3 RS232_TX、pin 5 GND |
| J6 | 1S 受保护电池 | BAT、GND |
| J7 | OLED | GND、3V3、I2C_SCL、I2C_SDA |
| J8 | SPI 扩展 | 3V3、GND、SPI_MISO/MOSI/SCK/CS |
| J9 | WCH-Link | 3V3、GND、WCH_TCK/TIO、RESET |

## 电源与关键器件

| Ref | 器件 | 网络/作用 |
|---|---|---|
| U2 | BQ24074 | VBUS_RAW → BAT/SYS 充电与 power-path |
| U3 | TPS63031 | SYS → 3V3 |
| U4 | TPS61023 | SYS → 5V_HOST |
| U5 | SY6280AAC | 5V_HOST → VBUS_HOST，受 HOST_EN 控制 |
| U6 | MAX3232E | UART ↔ RS232 |
| U7/U8 | TPD4EUSB30DQAR | USB-C/USB-A D+/D− ESD |
| D1/D2 | PESD5V0U1UL | USB-C/USB-A VBUS TVS |

