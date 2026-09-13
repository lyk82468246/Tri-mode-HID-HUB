# Milestone 6：资源与可靠性验收记录

状态：诊断代码已加入；开发板长时间测试、USB/BLE 实机测试和 PCB 收口仍待执行。

本阶段不新增 HID 协议。目标是证明 M1～M5 在 CH582M 的 32 KB SRAM、448 KB 可执行 Flash 和 TMOS 非阻塞约束下可持续运行，并把失败恢复与观测方法固定下来。

## 1. 代码侧验收

`src/firmware_diagnostics.c/.h` 提供固定内存的 `FirmwareDiagnosticsSnapshot`，不调用 libc 动态分配。每次 2 ms 固件服务周期记录：

- 服务执行次数、最近一次/最大执行周期数、超过 `sys_clock / 500` 的超时次数；
- 输入 Ring、USB/BLE HID Ring、USB/BLE stream Ring 的高水位；
- 上次复位原因、系统时钟、BLE NUS RX 丢弃数；
- Router、PS/2、UART 和 USB Host 的现有错误/丢包统计。

在调试器中调用 `FirmwareDiagnostics_GetSnapshot()`，由调用者提供静态的 `FirmwareDiagnosticsSnapshot` 对象。`max_service_cycles` 使用 `sys_clock_hz` 换算为时间；60 MHz 时，1 个周期约为 16.7 ns。

看门狗默认关闭。确认开发板供电、复位和 WCH-Link 观察链路稳定后，可在 MounRiver 的 C 预处理宏中加入：

```text
FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE=1
```

启用后仅在完整 TMOS 服务周期结束时喂狗；服务卡死或异常跳出时由硬件复位。`FIRMWARE_DIAGNOSTICS_WATCHDOG_RELOAD` 当前为 `0xFF`，实际超时时间必须结合芯片时钟和目标 SDK 在板上测量，不能用编译结果替代。

## 2. 开发板测试矩阵

| 编号 | 场景 | 操作 | 通过条件 |
|---|---|---|---|
| PWR-01 | 上电/复位 | 反复冷启动、WCH-Link 复位、观察 `reset_status` | 能区分上电、手动、软件和看门狗复位；无异常持续重启 |
| USB-01 | Device reset/suspend | PC 反复插拔 USB-C，挂起后恢复，持续 CDC 收发 | HID 无 stuck key；CDC 无阻塞；服务超时为 0 |
| USB-02 | Host 热插拔 | USB-A 反复插拔键盘/鼠标，制造无响应和快速拔出 | 事务超时/错误计数增加但能重枚举；旧按键释放 |
| BLE-01 | HOGP/NUS CCCD | 分别只订阅键盘、鼠标、手柄和 NUS，交替断连/重连 | 未订阅 Report 不阻塞已订阅 Report；NUS 与 HID 不串线 |
| BLE-02 | MTU/发送资源 | 改变 ATT MTU，持续 NUS 双向数据和 HID 输入 | stream 仍按有效 MTU 分片；发送资源暂不可用时不忙等 |
| IN-01 | 多源合并 | 两路 PS/2、USB Host 键盘同时按键，交错按下/释放 | 键盘按 Usage 去重；任一源释放不会释放另一源仍持有的键 |
| IN-02 | 鼠标/串口压力 | 同时移动 PS/2/USB 鼠标并连续输入 UART | 鼠标增量不跨输出串线；Ring 满时统计并继续恢复 |
| ERR-01 | 溢出恢复 | 人为制造 edge、Host report、Router 和 stream 队列压力 | 高水位可读；计数增加；无死循环、无非法指针 |
| SOAK-01 | 长稳 | 连接 USB、BLE、PS/2、UART，持续运行至少 8 h | 无异常复位；`service_overrun=0`；无 stuck key/button |

建议每个场景记录开始/结束时间、固件提交号、供电条件、输入设备型号、`FirmwareDiagnosticsSnapshot` 和主机日志。未执行的项目保持“待执行”，不能用交叉编译通过替代物理验收。

## 3. 开发板到首版 PCB 收口

实测通过后才迁移到首版 PCB，并逐项复核：

1. USB Device 使用 PB11/PB10，USB Host 使用 PB13/PB12，`HOST_EN` 使用 PB6；确认 VBUS 开关、限流和 ESD 位置。
2. PS/2 使用 PA0/PA1、PA2/PA3，先确认 5 V 外设侧和 3.3 V MCU 侧上拉、电平转换方向及空闲电平。
3. RS232 经 MAX3232 后使用 PA8/PA9；确认 DB9 的 DTE/DCE 角色和收发交叉关系。
4. 复核 WCH-Link、32 MHz 晶振、RF 天线 keep-out、复位/BOOT 按键及未使用引脚。
5. PCB 迁移只允许改变 `src/board_pins.h` 或板级初始化，不改变 `RouterEvent`、Report 格式、Ring 所有权和 TMOS 任务边界。

## 4. M6 退出条件

- MRS 全工程语法检查、链接和 map 容量检查通过；
- 应用层无 `malloc/free`，所有可变缓冲区仍为静态对象并保持 4 字节对齐；
- USB Host 控制传输无阻塞 helper；
- 开发板测试矩阵中 PWR/USB/BLE/输入/异常/长稳项目均有记录；
- 任何未修复的异常都有明确计数、复现步骤和阻断说明；
- PCB 电平、电源、USB、RF 和调试接口复核完成后，才创建硬件 release 标签。
