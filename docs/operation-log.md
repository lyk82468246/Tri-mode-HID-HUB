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

