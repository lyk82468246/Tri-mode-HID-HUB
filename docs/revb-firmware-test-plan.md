# Rev B 固件测试行动指南

本指南对应 `docs/hardware/pin-allocation-revb.csv` 和当前 Rev B 固件。当前没有连接开发板，因此本轮只能完成静态检查、交叉编译和代码审查；任何“通过”都必须注明是 `BUILD`，不能写成 `HW PASS`。

## 0. 测试前固定条件

1. 记录测试对象：Git commit、编译配置（`100 mA` 或 `500 mA`）、MounRiver Studio/SDK 版本、开发板丝印和接线照片。
2. 使用 Rev B 的信号命名。USB Device 是 PB11/PB10，USB Host 是 PB13/PB12；UART1 是 PA8/PA9，UART3 是 PA4/PA5，IrDA UART0 是 PB4/PB7。
3. 初次上电必须串入限流电源，并把 USB Host 外设、PS/2、红外 LED、OLED 都断开；先确认 3V3、复位和 WCH-Link 正常。
4. 不要把 PB5 `HOST_FAULT_N` 当作 5 V 电压或 power-good；PB16 是数字 `PGOOD_N`，不是 ADC。PA6/AIN10 和 PA7/AIN11 只在分压阻值和标定数据确定后才能换算成电压。

## 1. 无硬件时：静态检查和可重复构建

在仓库根目录执行：

```powershell
.\tools\validate-revb.ps1
.\tools\build-firmware.ps1 -UsbPowerMa 100
.\tools\build-firmware.ps1 -UsbPowerMa 500
git diff --check
```

验收记录至少包含：

| 项目 | 通过标准 |
|---|---|
| Rev B 引脚 | `src/board_pins.h` 与 CSV 逐项一致；没有把 Rev A 的 PB8/PB16/PB19/PA4 旧含义带入运行代码 |
| 工程源文件 | UART3、UART0、PWM、TMR0、SPI0、I2C 的 SDK 源文件没有被工程排除；UART2/SPI1 保持禁用 |
| 实时模型 | TMOS 事件循环；ISR 只采样、入 Ring 或推进微型时序状态；应用代码没有直接 `malloc/free` |
| 构建 100 mA | 链接成功，RAM 不超过 32 KB；当前基线为 Flash 174,664 B、RAM 28,744 B |
| 构建 500 mA | 链接成功；当前基线为 Flash 174,728 B、RAM 28,744 B |
| 诊断 | map 文件存在，确认 `service_overrun`、各 Ring 高水位、Host fault、总线错误字段可观察 |

若 RAM 余量低于 2 KB，停止增加缓存或协议状态机，先做容量削减和 map 分析。

## 2. 下载、启动和电源安全

### 2.1 第一次上电

1. 烧录 `100 mA` 版本，先不插 USB Host 设备。
2. 示波器/万用表观察 PB6 `HOST_EN`：复位、启动、USB Device 未配置时必须为低。
3. 观察 PB8/PB17：BQ24074 初始为 `EN2:EN1=00`，即 USB100；不能在启动瞬间进入 `10` 的 ILIM 状态。
4. 观察 PB5、PB9、PB16、PB18 的空闲电平：FAULT/CHG/PGOOD/USER 均为外部上拉、有效低。
5. 观察 PB3/PB19/PB2：MCP2120 未完成初始化前 EN 不应误开启，TFBS4711 的 SD 应保持高，MODE 应保持低或按初始化状态机变化。

### 2.2 电源状态矩阵

| 条件 | 预期 |
|---|---|
| USB 未配置 | Host 关闭；不向下游 PS/2 供电；充电模式保持安全低功耗模式 |
| 100 mA 版本、USB 已配置 | 仍不自动声明 500 mA；Host 许可必须受板级策略限制 |
| 500 mA 版本、PGOOD 有效、USB 已配置且未挂起 | 切换到 `EN2:EN1=01`，然后才允许 PB6 高 |
| USB suspend | 先拉低 PB6，再切换充电模式到 suspend；恢复后重新经过许可判断 |
| PB5 FAULT_N 拉低 | 立即拉低 PB6，锁存故障，Host 停止并释放输入状态；故障解除后不能自动清除锁存 |
| 清故障 | 只有 Host 已关闭且 PB5 已释放时调用 `BoardPower_ClearFault()`；随后重新走供电许可流程 |

用逻辑分析仪记录 PB6、PB8、PB17、USB suspend/resume 的先后关系，而不是只读最终电平。

## 3. USB Device 上行链路

1. 烧录后连接 Type-C Device 口，确认 PC 枚举 PB11/PB10 对应的复合设备。
2. 检查 HID 接口、CDC 控制接口、CDC Bulk EP2 和通知 EP3；VID/PID `0x1209:0x5820` 仍是开发占位值。
3. CDC OUT 发送 1、19、20、64、65 字节数据，确认设备只使用静态 Ring，按分片规则回送，不阻塞 TMOS。
4. 编译 `CH582M_M1_TEST_PATTERN=1` 的临时台架版本，确认 PC 收到 Usage ID `0x04` 的按下/释放；测试结束恢复为 0。
5. 观察 USB suspend/resume：HID 队列不应无限增长；恢复后应发送当前完整键盘、鼠标、手柄快照。
6. 长按、快速切换输出策略、拔插 PC，确认不会残留按键；`source down` 后应生成 release-all。

## 4. USB Host 与 PS/2 下行链路

### 4.1 USB Host

Host 默认受电源策略关闭。只有 `500 mA` 配置、输入 PGOOD 有效、Device 已 configured 且未 suspend 时才允许 PB6 高；因此在没有复现 Rev B 电源检测条件的开发板上，看到 Host 不枚举并不等于 USB Host 代码失败。

满足电源条件后：

1. 插入标准 USB 键盘，验证总线复位、设备描述符 8 字节/完整描述符两阶段读取、配置和 HID Report Descriptor 读取。
2. 先测 Boot Keyboard，再测 Boot Mouse；验证键盘修饰键、普通键、`E0` 等价状态、鼠标按钮和有符号 X/Y/Wheel。
3. 插入两个 HID 设备，确认每个设备独立分配接口状态；超过固定 Report Descriptor/原始报表容量的设备应被拒绝并增加诊断，而不是越界。
4. 人为触发 TPS2553 FAULT，确认 USB Host 停止、PS/2 也释放，并且恢复后需要重新枚举。

### 4.2 PS/2

1. Host 电源未许可时，PA0–PA3 应为输入/释放、GPIOA 中断关闭。
2. 许可后接入键盘，示波器确认时钟下降沿采样，验证 Set 2 普通键、扩展键、断码和错误校验。
3. 接入鼠标，验证 BAT、`F4`、ACK/RESEND 状态机和三字节数据包；不能在 ISR 中执行完整命令序列。
4. 断电、拔线或 Host fault 时，确认键盘和鼠标都生成 release-all，路由器不保留悬挂按键/按钮。

## 5. UART1、UART3、BLE 和统一数据流

### 5.1 两路有线 UART

1. UART1 通过 MAX3232 接 RS232 分析仪，PA8 RX/PA9 TX 使用 115200 8N1；发送已知数据，确认没有启动日志或调试字符串污染。
2. UART3 通过 J10 接 3.3 V TTL 分析仪，PA4 RX/PA5 TX 使用 115200 8N1；同时发送 UART1/UART3 数据，确认两个 Ring、分帧和路由源互不串线。
3. 分别测试 1、19、20 字节满帧、空闲超时、连续突发、线路错误和 Ring 满；错误只增加计数并丢弃当前坏项，不能阻塞 TMOS。
4. 从 CDC/NUS 发送控制帧 `[A5 5A 01 policy]` 或 `[A5 5A 02 mask]`，确认 USB_ONLY/BLE_ONLY/BOTH 等策略只改变输出资格，不破坏普通 stream 数据。

### 5.2 BLE

1. 手机或 PC BLE 工具确认 HOGP 广播、键盘/鼠标/手柄 Report Map、Report Reference 和 CCCD。
2. 只订阅键盘、只订阅 NUS、同时订阅全部三种组合，确认未订阅 HID Report 不会占住输出队首。
3. 验证 NUS 20 字节分片、回连、断连和 BLE_ONLY 策略；断连时所有源状态必须释放。
4. 观察 Router 的 `stream_tx_drop`、`hid_tx_drop`、`stream_unavailable_drop`，在接收端故意降低消费速度，确认系统丢弃有界且不会死锁。

## 6. UART0 / MCP2120 / TFBS4711 物理层

本模块当前实现的是 MCP2120 软件波特率配置、TFBS4711 SIR 物理层和字节去转义/FCS 校验，不是完整 IrDA 协议栈；没有 IrLAP/IrLMP 对端时，不能把“串口有波形”写成“标准 IrDA 通过”。

1. 用频率计确认 MCP2120 外部时钟为 7.3728 MHz；BAUD2/1/0 为软件配置模式。
2. 观察上电状态机：PB19 SD 先低、PB2 MODE 低、UART0 先 9600；配置字节 `0x87`、回显、`0x11` 应在 TMOS 中非阻塞推进。
3. 配置成功后 MODE 应进入高电平数据态，Irda 统计的 `ready=1`；失败必须有限重试并进入 fault，不得无限重试。
4. 用 IrDA 物理层对端或光电转换器发送 BOF `0xC0`、转义数据、FCS、EOF `0xC1`，验证好帧进入 `ROUTER_SRC_IRDA`，坏 FCS 只增加 `fcs_error`。
5. 启动遥控发射时确认 PB19 立即拉高，TFBS4711 关闭；遥控结束后 PB19 拉低，并留出至少一个 TMOS tick 的 TFBS 启动时间。检查 `remote_pause_count` 和 `remote_blocked_bytes`。

## 7. PB1/PB0 红外遥控

1. PB0 接示波器，确认 PWM6 约 38 kHz、占空比约 10/32；LED 必须经过 NMOS 和限流，不得直接由 GPIO 带脉冲大电流。
2. PB1 接 TSOP38438 输出，使用逻辑分析仪记录边沿时间戳；ISR 只切换下一边沿极性并入 128 项 Ring（Ring 容量保持为 2 的幂）。
3. 分别发送 NEC 普通 32 位帧、地址/命令反码错误帧、长间隔重复帧；只允许地址/命令反码正确的帧进入路由器。
4. 发送 RC5 不同 toggle、地址和命令，检查半位时间、Manchester 解码和非法脉冲统计。
5. 调用 `IrRemote_SendNec()`、`IrRemote_SendRc5()` 测量 leader、bit mark/space、RC5 半位和最终关断；发射期间不得并发第二帧。
6. 发射期间观察 TFBS SD 和 PB0，确认两套光学链路不会同时发射；TMR0 的资源占用需在 BLE 活跃、USB 活跃时一起验证。

## 8. I²C/SPI 外设总线控制帧

总线事务均为单笔、静态缓冲、IRQ 驱动、带超时；当前不绑定具体 OLED 型号或地址。

### 8.0 电源与红外控制帧

CDC/NUS 也接受以下 TMOS 控制命令；命令只提交请求，实际 GPIO 操作由对应任务在下一轮执行：

```text
A5 5A 03 <0|1>       请求关闭/开启 Host 供电
A5 5A 04 00          在 HOST_EN 已关闭且 FAULT# 已释放时清除故障锁存
A5 5A 20 <address> <command>
A5 5A 21 <address> <command> <toggle>
```

`0x20`/`0x21` 分别启动 NEC/RC5 发射；发射忙时请求被拒绝并计入红外/Router
统计。`0x04` 在故障仍存在时安全地保持锁存，不会强行重新打开 Host VBUS。

### 8.1 I²C

CDC/NUS 写入：

```text
A5 5A 10 <addr7> <write_len> <read_len> <write bytes...>
```

约束：`write_len<=14`、`read_len<=16`、两者不能同时为零。完成后输出：

```text
B5 10 <status> <read_len_or_0> <read bytes...>
```

`status=0` 为成功，`2` 为超时，`3` 为硬件错误。用无应答地址、SDA 持低、读 1/2/16 字节和写后 repeated-start 逐项测试。

### 8.2 SPI0

CDC/NUS 写入：

```text
A5 5A 11 <len> <tx bytes...>
```

约束：`1<=len<=16`。PA12 为手动 CS，PA13/PA14/PA15 为 SCK/MOSI/MISO，结果格式为：

```text
B5 11 <status> <len_or_0> <rx bytes...>
```

接逻辑分析仪验证 Mode 0、MSB first、CS 包络和每个字节的全双工回读；测试超时和 Ring/stream 背压，确认不会在 ISR 里发布 CDC/NUS 数据。

## 9. 压力、故障和长时间运行

1. 同时产生 USB Host HID、PS/2、UART1、UART3、BLE NUS、IrDA 和红外边沿，运行至少 30 分钟。
2. 每 2 ms TMOS 服务周期记录 `service_overrun`、最大服务耗时、各 Ring 高水位和所有 drop/error 计数。
3. 重复 USB 插拔、BLE 断连重连、Host FAULT、UART 线路错误、I²C NACK/超时、SPI 外设断开和红外连续噪声。
4. 验证所有错误均能回到可用状态：HID release-all、Host 重新枚举、IrDA 有限重试、总线回到 idle、遥控发射释放 PWM/TMR0。
5. M6 看门狗默认关闭；只有上述复位/供电/长稳测试通过后，才单独打开 `FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE=1` 做故障注入。

## 10. 记录模板与退出条件

每条记录使用以下字段：

```text
日期/操作者：
固件 commit：
配置：100mA / 500mA
硬件：开发板/Rev B PCB，接线与仪器：
测试编号：
输入条件：
观察点/原始数据：
诊断计数前后：
结果：BUILD PASS / HW PASS / FAIL / BLOCKED
失败现象与下一步：
```

软件迁移只有在 100/500 mA 两种构建、静态检查、协议边界和故障回归完成后才可标记“代码迁移完成”。标准 IrDA、ADC 电压值、USB Host 供电电流、OLED 驱动和最终 PCB 只有在对应硬件参数锁定并完成仪器实测后才能单独标记通过。
