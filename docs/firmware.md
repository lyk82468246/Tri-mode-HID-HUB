# CH582M 固件工程

## 工程入口

固件使用 MounRiver Studio 工程格式，入口文件为根目录的 `CH582M.wvproj`。工程目标为 WCH CH582M（CH58X / RISC-V / NoneOS），下载配置使用 WCH-Link。

```text
CH582M.wvproj       MounRiver 工程入口
CH582M.launch       调试启动配置
src/Main.c          当前应用入口
Startup/            CH583/CH582 系列启动文件
Ld/Link.ld         链接脚本
RVMSIS/             RISC-V CMSIS 兼容头文件
StdPeriphDriver/    WCH 外设驱动、头文件和 ISP 库
```

`.mrs/` 是 MounRiver 的本机工作区状态，可能包含绝对路径，因此不纳入版本控制；重新导入 `CH582M.wvproj` 即可恢复工程。

## 当前固件状态

这次提交加入的是可编译的 CH582M 空白固件工程及 WCH 外设支持文件。`src/Main.c` 仍是模板级 UART1 收发示例：上电后发送示例字符串，并把接收到的数据回显到 UART1。

它还不是三模 HID 的完成固件。USB Host HID、USB Device HID、BLE HOGP、PS/2 协议转换、RS232 bridge 和 OLED UI 将基于 `docs/pin-plan.md` 中的硬件规划逐项实现。

系统级数据流、静态内存布局、Ring Buffer 所有权和六阶段开发顺序见 [`docs/firmware-architecture.md`](firmware-architecture.md)。后续实现严格按 M1 至 M6 逐阶段推进。

## 在 MounRiver Studio 中使用

1. 打开 `CH582M.wvproj`，确认芯片选择为 CH582M。
2. 检查工程中的 WCH SDK/工具链路径已由本机 MounRiver Studio 正确解析。
3. 选择 `obj` 配置并执行 Build；生成物目录 `obj/` 已被 Git 忽略。
4. 连接 WCH-Link 后按工程的下载/调试配置烧录或启动调试。

不同版本的 MounRiver Studio 可能使用不同的 SDK 安装路径；工程文件保留了芯片、编译器、链接脚本和下载目标配置，但不把本机 SDK 安装目录写入仓库。
