# WCH BLE/TMOS 运行时

本目录只收纳本工程当前需要的沁恒 CH58x 官方 BLE SDK 运行时文件：

- `HAL/MCU.c`、`HAL/RTC.c` 及 `HAL/include/`：官方 TMOS/HAL 初始化和时基适配；
- `LIB/CH58xBLE_LIB.h`：与静态库匹配的官方接口声明；
- `LIB/LIBCH58xBLE.a`：官方 CH58x BLE 协议栈静态库。

Milestone 1 只启动 BLE 库和 TMOS 调度器；Milestone 2 已在 `src/ble_output.c` 中启动 BLE Peripheral，并在 `src/ble_hid_service.c`、`src/ble_nus_service.c` 中注册 HOGP 与 NUS-compatible GATT 服务。MounRiver 工程已把 `BLE/LIB` 加入头文件/库搜索路径，并链接 `CH58xBLE`。

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

这些文件来自 WCH 官方 [openwch/ch583](https://github.com/openwch/ch583) SDK；后续升级 SDK 时必须同步核对头文件、静态库、芯片型号和链接 map，不能只替换其中一个文件。

M2 的应用 FIFO、GATT 属性表、CCCD 状态、Report Map 和发送工作区全部是编译期静态对象。WCH `GATT_bm_alloc()` / `GATT_bm_free()` 仅用于 `GATT_Notification()` 所要求的协议栈报文池：发送成功后由协议栈接管，失败路径立即归还；应用代码不调用 libc `malloc/free`，也不把 Ring Buffer 地址交给异步 BLE 栈。
