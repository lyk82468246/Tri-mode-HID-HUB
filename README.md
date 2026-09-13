# Tri-mode HID Hub

基于 CH582M 的三模 HID 转接器首版工程记录与固件工程。

## 当前状态

- 已在嘉立创 EDA 专业版中通过 Run API Gateway 完成单页原理图的结构重排版（Rev A），并完成一次 API 网表审计。
- USB 有线方向：USB-C 设备/供电/充电/烧录接口；USB-A HID 主机接口。
- 已纳入：1S 受保护锂电池接口、充电与 power-path、电源升降压、USB ESD、两路 PS/2、RS232、I²C OLED、按键、WCH-Link 调试口、SPI 扩展排针、BLE 天线与 32 MHz 晶振。
- J3/J4 已更换为 6 针圆形 DIN-6 PS/2 插座候选 C23689424；原先错误的 9 针长条封装已移除。
- 原理图按电源、USB、CH582M/RF、PS/2、RS232、调试/扩展分区；短引线不显示重复网络名，网络由引脚处端口维护。
- 暂不纳入 2.4 GHz 接收端软硬件；2.4 GHz 这里仅指 CH582M 的 BLE/RF 部分。
- 尚未开始 PCB；当前原理图是可继续审查的工程首版，不是可直接打板的 release 版本。
- USB-C、USB-A、DB9、电池座、OLED/排针、晶振、天线和按键中仍有若干 C990 Extended Part 机械候选，尚未达到生产 BOM 的可追溯要求。
- 已加入根目录的 CH582M MounRiver Studio 固件工程；Milestone 1 的 USB Device HID/CDC、Milestone 2 的 BLE HOGP/NUS-compatible 输出、Milestone 3 的 PS/2/UART 输入适配器和 Milestone 4 的 USB Host HID 枚举/解析代码已经落地。真实开发板验收仍需按 Roadmap 执行。

## 嘉立创 EDA 工程

工程保存在已连接的嘉立创 EDA 工作区：

- Project：`Tri-mode HID Hub`
- Project UUID：`6631163b415d4b76b85d6c91ef5d43f5`
- Schematic UUID：`160ff6e2b76d4104`
- Page UUID：`21fbca2f7877cf86`
- Board：`Board1`；原理图页：`P1`

当前仓库记录了设计意图和操作日志；嘉立创 EDA 的工程文件仍以云端工程为准，尚未伪造或手工生成一个不确定格式的二进制/文本导出文件。

## 首版功能框图

```text
 USB-C device/charge/program ─┐
 USB-A HID host ───────────────┤
 PS/2 keyboard ────────────────┤
 PS/2 mouse ───────────────────┤── CH582M ── USB device / BLE HID
 RS232 ────────────────────────┤          └─ UART bridge / future expansion
 OLED + buttons + debug ──────┘
```

## 固件工程

MounRiver Studio 入口为 [`CH582M.wvproj`](CH582M.wvproj)，固件源码从 [`src/Main.c`](src/Main.c) 开始；启动文件、链接脚本、RVMSIS 和 WCH 外设驱动也随工程一并纳入仓库。工程目标为 CH582M / CH58X / RISC-V / NoneOS，下载接口为 WCH-Link。

当前固件已经进入 M4：TMOS 运行官方 WCH BLE 库，USB Device 控制器提供一个多 Report ID HID 接口（Keyboard/Mouse/Gamepad）和一个 CDC ACM 接口；BLE 外设广播标准 HID Service（键盘、鼠标、手柄 Report Map/CCCD/Boot Report）及 Nordic UART Service UUID 兼容的 RX/TX 服务。两路 PS/2 使用 PA0/PA1、PA2/PA3 的 GPIOA 边沿采样与 Set 2/三键鼠标解码，UART1 使用 PA8/PA9 接收 RS232 转换器数据；USB PB 下行复用 USB2 Host 控制器，采用非阻塞 TMOS 枚举、Boot Keyboard/Mouse 轮询和固定上限 Report Descriptor 解析。所有输入先进入静态 Ring，再由 TMOS 路由，详见 [`docs/firmware.md`](docs/firmware.md)。

系统级路由器、静态内存模型和六阶段实现顺序见 [`docs/firmware-architecture.md`](docs/firmware-architecture.md)。

## 电源方案

```text
USB-C VBUS_RAW ── BQ24074 ── SYS ── TPS63031 ── 3V3
                         └── BAT ── TPS61023 ── 5V_HOST ── SY6280 ── VBUS_HOST
```

J6 按“自带保护板的 1S 锂电池”建模。充电电流、终止电流、TS/NTC、输入限流和 USB-A 最大负载仍需结合最终电池规格及热设计确认。

## 被动件封装约束

默认电阻、电容均使用 0603；输入/输出储能电容优先使用 0805。电感、ESD、IC、连接器和晶振按电气额定值与可采购封装选取，不强行缩小。

## 下一步

1. 在嘉立创 EDA 内重新打开当前 Rev A，做 ERC、封装/引脚号、库型号和数据手册逐项复核。
2. 确认 USB 主从控制器映射、CH582M 官方参考布局、电源开关与天线 keep-out。
3. 确认 PS/2 插座实际针脚定义、RS232 DB9 的 DTE/DCE 角色和 OLED 接插件方向。
4. 依据确认后的电流预算与电池型号修订充电/升压参数，再开始 PCB placement/routing。
5. 在开发板上完成 M1/M2/M3/M4 的 USB、BLE、PS/2、UART 和 USB Host 物理收发验收；下一阶段进入 M5，实现按连接状态选择活跃上行链路与完整状态合并。

## 参考资料

- WCH 官方开源仓库：[openwch/ch583](https://github.com/openwch/ch583)
- WCH 芯片资料入口：[WCH 官方 PDF](https://www.wch.cn/uploads/file/20240224/1708757629702367.pdf)
- TI BQ24074：[datasheet](https://www.ti.com/document-viewer/bq24074/datasheet)
- TI TPS63031：[datasheet PDF](https://www.ti.com/lit/ds/symlink/tps63031.pdf)
- TI TPS61023：[datasheet PDF](https://www.ti.com/lit/ds/symlink/tps61023.pdf)
