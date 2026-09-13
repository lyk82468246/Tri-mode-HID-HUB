# WCH BLE/TMOS 运行时

本目录只收纳本工程当前需要的沁恒 CH58x 官方 BLE SDK 运行时文件：

- `HAL/MCU.c`、`HAL/RTC.c` 及 `HAL/include/`：官方 TMOS/HAL 初始化和时基适配；
- `LIB/CH58xBLE_LIB.h`：与静态库匹配的官方接口声明；
- `LIB/LIBCH58xBLE.a`：官方 CH58x BLE 协议栈静态库。

Milestone 1 只启动 BLE 库和 TMOS 调度器，不启动 BLE 广播、连接、HOGP 或 NUS 业务。MounRiver 工程已把 `BLE/LIB` 加入头文件/库搜索路径，并链接 `CH58xBLE`。

为满足当前 32 KB SRAM 和非阻塞调试阶段的约束，M1 工程定义了：

```text
BLE_MEMHEAP_SIZE=6144
BLE_SNV=0
TEM_SAMPLE=0
BLE_CALIBRATION_ENABLE=0
HAL_SLEEP=0
HAL_KEY=0
HAL_LED=0
```

这些文件来自 WCH 官方 [openwch/ch583](https://github.com/openwch/ch583) SDK；后续升级 SDK 时必须同步核对头文件、静态库、芯片型号和链接 map，不能只替换其中一个文件。M2 另行审计 BLE 报文缓冲 API 的所有权，应用层不引入 `malloc/free`。
