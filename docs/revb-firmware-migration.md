# Rev B 固件迁移跟踪

目标：依据当前 `main` 中的 Rev B 硬件基线全面迁移运行固件。基准为
`docs/hardware/pin-allocation-revb.csv`、同名 Markdown 和 `pcb-design-study.md`。
当前开发板未连接；代码构建、主机测试和硬件实测分别记录，不互相替代。

## 要求与验收证据

| 要求 | 实现/验证 | 状态 |
|---|---|---|
| 所有 Rev B GPIO、固定 USB、UART、ADC、调试/时钟脚归属 | `board_pins.h` 对照 CSV；`Board_Init` 复用与启动电平 | 已接入，待硬件回归 |
| UART1 业务串口无 DEBUG 打印污染 | 删除 Main 调试串口初始化，两个工程配置移除 DEBUG 定义 | 已接入，待串口实测 |
| HOST_EN 上电关闭、供电许可、USB suspend、FAULT 锁定与恢复 | 电源策略模块 + Host 停止/释放/重枚举 | 已接入，待电源故障实测 |
| PB8/PB17 充电模式与描述符电流一致 | BQ24074 EN 表、挂起行为和 100/500 mA 构建配置 | 已接入，待电源实测 |
| PA6/AIN10、PA7/AIN11 非阻塞采样；PB16 数字 PGOOD | ADC 状态机、超时、校准/分压参数有效性 | 原始采样已接入；标定待硬件参数 |
| PB18 USER 消抖、PB9 CHG 状态 | TMOS 状态采样与诊断 | 已接入，待触摸/电平实测 |
| 独立 UART1 RS232、UART3 TTL、UART0 IrDA 物理链路 | UART1/UART3 为独立有界 RX ISR/Ring、超时/定长分帧和独立路由源；UART0 为双向 MCP2120/TFBS SIR 物理层与校验解析 | UART1/UART3 RX 已接入，UART0 物理层已接入，待实测 |
| MCP2120 初始化/速率、TFBS4711 休眠、光学仲裁 | UART0/EN/MODE/SD 非阻塞初始化、9600 软件配置回显、SIR 去转义/FCS、遥控期间关断 TFBS | 物理层已接入；速率/光学实测待完成 |
| 标准 IrDA 功能 | 明确 IrLAP/IrLMP 与原始光串口边界；实现/验证所需协议 | 待完成，不能用 UART 原始透传冒充标准 IrDA |
| PB1 遥控边沿接收、PB0 PWM6 发射 | GPIOB 时间戳 Ring、TMOS NEC/RC5 解码、PWM6 38 kHz 和 TMR0 包络已接入；光学仲裁和实测待完成 | 代码已接入，待实测 |
| PB21/20 I2C，PA12–15 SPI0 | 单笔中断驱动异步事务、超时/错误诊断、CDC/NUS 控制帧和结果流已接入；OLED型号未锁定，需实测 | 代码已接入，待外设参数/实测 |
| Router/控制协议覆盖新增功能 | 保留原 HID 行为；CDC/NUS `A5 5A 03/04` 电源、`20/21` 红外和 `10/11` 总线命令均已接入，I²C/SPI 结果以 `B5` 流返回 | 已接入，待协议回归 |
| 32KB RAM、静态对齐、TMOS、无 libc 堆和阻塞 | 100/500 mA 全量链接、map、可重复构建；RAM 28,752 B / 32 KB | 构建通过，待边界回归 |
| 同步软件说明、测试指南与工程源文件配置 | Rev B 文档、测试行动指南、工程源文件和排除项已同步 | 已同步，待硬件执行 |

## 尚未锁定的硬件参数

电池/VBUS 分压阻值、ADC 标定、电池阈值、全板功率预算、OLED 型号/地址
尚未在 Rev B 文档确定。实现应提供明确参数与有效位，不能伪造电压数值、
把 FAULT# 当作 power-good，或据未校准 ADC 自动开放高功率负载。

## 本轮进度

2026-09-21：完成硬件变更梳理；加入 Rev B 板级引脚定义与早期初始化，
移除业务 UART1 调试初始化与工程 DEBUG 宏；Host 和 PS/2 已改由电源策略接管。
2026-09-28：启用 SDK UART3 驱动，新增 UART3 PA4/PA5 独立 RX Ring、TMOS 分帧、
路由源和诊断计数；随后接入 UART0/MCP2120/TFBS4711 SIR 物理层、NEC/RC5 红外、
PB21/PB20 I2C、PA12–PA15 SPI0 和统一总线控制帧。100 mA/500 mA 配置均交叉编译
通过；修正 NEC 重复码边沿极性、增加红外边沿溢出复位和 SPI 超时后的完整外设重配；
当前仍是“代码已迁移、硬件待验证”，不宣称完整 IrLAP/IrLMP 或最终 PCB 验收。

### 串口方向边界

当前应用协议只定义 UART1（RS232）和 UART3（TTL）的输入路径：固件接收两路 RX，
按 20 B 满帧、CR/LF 或 6 ms 空闲分帧，再作为 `STREAM_DATA` 路由到 USB/BLE。
PA9/PA5 仍由底层驱动保持空闲高电平，但尚未定义业务 TX 帧格式、发送 API 或回显
策略，因此不能把 UART1/UART3 写成“固件双向收发已完成”。UART0 则是 MCP2120/TFBS
物理层的双向 IrDA SIR 链路；完整 IrLAP/IrLMP 仍是独立的后续工作项。
