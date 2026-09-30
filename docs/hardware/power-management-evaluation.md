# IP5305T + AMS1117 电源方案评估

日期：2026-09-30
状态：**不进入当前 Rev B / Rev B-IR 制造基线；仅保留为受限原型方案。**

本文评估“用 IP5305T 取代 BQ24074、TPS63031、TPS61023，再用 AMS1117-3.3 产生 3V3”的想法。评估对象是当前这块带 CH582M、USB-A Host、两路 PS/2、RS232、OLED、触摸、BLE 和 1S 电池的板，而不是一个只需要 5 V 输出的普通充电宝。

## 结论

IP5305T 本身适合“1S 锂电池充电 + 单路 5 V 升压”的充电宝结构。它有集成 power-path，可以在输入存在时边充边向 VOUT 供电；数据表给出的典型能力是 1.2 A 充电、1.0 A 5 V 升压输出、单个 2.2 µH 电感和约 91% 的最高升压效率。[IP5305T 原厂数据表](https://www.injoinic.com/api/static/uploads/20250529/20250529092838_6837b846e7f6c.pdf)

它不适合直接替代当前电源树，原因有四个：

1. **VOUT 是唯一的 5 V 电源出口，且总额定输出只有 1 A。** USB-A、两路 PS/2、红外峰值电流以及由 5 V 输入的 3V3 负载会共享这一额度。IP5305T 不是 USB-A 的逐口限流开关，所以 USB-A 仍需要 TPS2553 或同类受控限流开关，不能只把 VOUT 直接接到 USB-A VBUS。
2. **低负载会自动关机。** 数据表给出的典型条件是 VOUT 负载持续低于 45 mA 约 32 s 后进入轻载关机；待机电流低于 100 µA。[IP5305T 电气特性](https://www.injoinic.com/api/static/uploads/20250529/20250529092838_6837b846e7f6c.pdf) 这会切断只有 BLE、OLED 或触摸待机时的 5 V 和 3V3，无法满足本板的常供电行为。用约 100 Ω 电阻长期耗掉 50 mA 可以绕过检测，但会持续浪费约 0.25 W，并破坏便携设备的待机意义。
3. **ESOP8 没有 BQ24074 那样的 CE、EN1/EN2、CHG#、PGOOD 和 TS 引脚。** 其引脚是 VIN、LED1/ VSET、LED2/ VTHS、LED3、KEY、BAT、SW、VOUT 和 PowerPAD。[IP5305T 引脚定义](https://datasheet4u.com/pdf-down/I/P/5/IP5305T-Injoinic.pdf) 现有 `CHG_N`、`INPUT_PGOOD_N`、`CHG_EN1`、`CHG_EN2` 电源状态机不能原样迁移；若保留这些功能，必须另加电压/充电状态监测器并重新定义固件默认状态。
4. **AMS1117 从 5 V 线性降到 3.3 V 会消耗热预算和电压裕量。** AMS1117-3.3 的数据表把 4.75 V 作为保证输出的输入下限，典型压差约 1.2 V，SOT-223 的热阻随铺铜和安装方式大约在 46–90 °C/W 范围。[AMS1117 数据表](https://datasheet.lcsc.com/lcsc/1810231814_Advanced-Monolithic-Systems-AMS1117-3-3_C6186.pdf) IP5305T 在 1 A 测试点的 VOUT 下限也是 4.75 V，因此满载时几乎没有额外裕量。

因此，本项目继续采用 `BQ24074 → SYS → TPS63031/ TPS61023 → TPS2553` 作为正式绘图基线。IP5305T + AMS1117 可以做一块独立的简化原型，但在低负载自动关机、总 5 V 电流和 3V3 热设计没有实测通过前，不能替换现有电源页。

## 与本板需求的逐项对照

| 需求 | IP5305T + AMS1117 的结果 | 设计决定 |
|---|---|---|
| 1S 电池充电 | 支持 4.20/4.30/4.35/4.40 V 目标电压，IP5305T 可用外部配置选择 | 若做原型，按最终电芯的 4.20 V 配置；不把默认值当成已确认的电池参数 |
| USB-C 5 V 输入 | 输入工作范围约 4.65–5.5 V；没有 USB PD 协商 | 保留 CC1/CC2 各 5.1 kΩ Rd；按 CC 广告的默认/1.5 A/3 A 档位和芯片输入限流取电，不宣称 PD 快充 |
| 边充边用 | 原厂说明集成 power-path | 仍须在输入插拔、满电和低电量时实测 VOUT 是否连续，不能只看方框图 |
| USB-A VBUS | 只有一条受保护的 5 V VOUT，无逐口 EN/FAULT/限流 | 继续放 TPS2553 或同类开关；`VBUS_HOST` 不得直接等同 `VOUT` |
| 两路 PS/2 | 可从 VOUT 分支，但和 USB-A 共用 1 A | 先给每路定义最大电流，再决定是否各自加负载开关；总预算不能只看芯片“1 A”字样 |
| CH582M 3V3 | AMS1117 可从 VOUT 产生 3.3 V | 只在测得 3V3 最大电流和温升合格时使用；优先改成低 IQ 开关稳压器 |
| BLE/触摸待机 | 低于 45 mA 的 VOUT 负载约 32 s 后会被关机 | 当前产品需求下不通过；不能依赖 MCU 在 5 V 被切断后再唤醒 IP5305T |
| 充电状态/输入状态 | ESOP8 没有独立 CHG#/PGOOD/EN1/EN2/TS | 需要外加监测器或删掉现有电源状态机；两者都意味着不是“只换两个 IC” |
| 电池安全 | 芯片有过充/过放/过流保护描述，但电池仍应使用受保护 1S 包 | 电池保护板、NTC、额定放电电流和运输要求仍按电芯资料确认 |

## 5 V 电流和电池电流必须先算

把 VOUT 的连续电流定义为：

```text
I5V_CONT = IUSB_A + IPS2_K + IPS2_M + I3V3 + IIR_PEAK_AVG + IOTHER
```

AMS1117 是线性稳压器，3V3 负载电流几乎原样从 5 V 侧流入，所以不能用“3.3 V 功率较小”来减少 VOUT 电流。设计阶段把 `I5V_CONT` 控制在 1 A 数据表额定值以下并留出瞬态余量；在没有完整外设电流数据前，不把 1 A 当作 USB-A 可以独占的额度。

电池侧的最坏近似为：

```text
IBAT ≈ (5 V × I5V_CONT) / (ηBOOST × VBAT_MIN)
```

例如 VOUT 需要 0.8 A、升压效率按 0.90、放电末端按 3.0 V 计算，电池电流约为 1.48 A，还没有计入启动峰值和电感纹波。电池保护板、连接器、保险丝和电芯必须按这个电流重新核对。

AMS1117 的耗散为：

```text
PAMS1117 = (VOUT − 3.3 V) × I3V3
Tj ≈ Ta + PAMS1117 × θJA
```

例如 5 V→3.3 V、0.30 A 时约耗散 0.51 W；在 90 °C/W 的 SOT-223 安装条件下，仅结温上升就约 46 °C。原理图必须标出 3V3 最大电流、输入/输出电容和散热铜区，不能只写“AMS1117-3.3”。

## 如果必须做 IP5305T 原型，原理图按下面的隔离方式绘制

这是一条**独立原型分支**，不能和当前 BQ/TPS 电源树混画，也不能把网络名 `SYS` 复用到未经验证的 VOUT。

1. USB-C 的 VBUS 经过入口保护后命名为 `IP5_VIN`，接 IP5305T VIN；CC1/CC2 各接 5.1 kΩ Rd。IP5305T VIN 旁放数据表要求的输入陶瓷电容。
2. IP5305T BAT 只接 1S 受保护电池 `BAT`；按最终电芯选择 VSET/LED1 配置，不能悬空或随意接 3V3。LED2/VTHS、LED3、KEY 依最终是否需要电量灯/按键功能按数据表处理。
3. SW、2.2 µH 电感、VOUT 电容必须照原厂典型应用的电流回路放置。VOUT 命名为 `5V_IP5`，先放测试点；只有低电量、满载、输入插拔和短路测试通过后，才能把它分配给外设。
4. `5V_IP5 → AMS1117-3.3 → 3V3`。AMS1117 的输入、输出电容靠近引脚，焊盘和散热铜按最终封装数据表布置；3V3 不得从 BAT 直接接入。
5. `5V_IP5 → TPS2553（或同类限流开关）→ VBUS_HOST`，USB-A 仍由 `HOST_EN` 控制并保留故障反馈。PS/2 5 V 可以从受控的 5 V 分支取得，但要把每路电流写进预算。
6. 不在该原型中连接旧的 `CHG_N`、`INPUT_PGOOD_N`、`CHG_EN1`、`CHG_EN2` 到 IP5305T 的 LED/KEY 脚。若固件仍需要这些状态，另加经过验证的监测电路，并在软件中定义上电默认值。
7. 原型必须有 `TP_IP5_VIN`、`TP_BAT`、`TP_5V_IP5`、`TP_3V3` 和 `TP_VBUS_HOST`；示波器要记录 VOUT 纹波、输入插拔、USB-A 热插拔和 32 s 轻载行为。

对应的单页示意图见 [IP5305T 原型电源示意](schematic-guide/power-candidate-ip5305t.svg)。图中红色标注是必须解决的否决点，不代表制造版网表。

## 允许晋级为正式方案的条件

只有以下结果全部通过，才可以建立新的 `Rev B-P` 电源版本并重新绘制 P00–P03：

- 电池电压 3.0–4.2 V、USB-C 输入存在/断开、USB-A 500 mA 负载、两路 PS/2 最大负载和红外峰值组合下，`5V_IP5` 不低于系统允许值，且 IP5305T 和电池不超过温升/电流限制。
- 最低功耗模式连续运行超过 10 min，VOUT 不因 45 mA/32 s 轻载检测而关闭；如果使用保持负载或定时唤醒，必须把电池续航损失写入产品指标。
- AMS1117-3.3 在最坏 `I3V3` 下有结温和输入裕量，3V3 纹波、复位和 BLE 发射不受 VOUT 负载阶跃影响。
- 充电终止电压、充电电流、输入限流和电池保护板与电芯规格一致；USB-C 默认电流能力不被过度宣称。
- 固件不再假定 BQ24074 的 EN1/EN2、CHG#、PGOOD#，USB Host/PS/2 的上电时序改为新的监测和使能策略。

在上述条件未通过前，原理图绘制指南中的正式电源树和 SVG 仍保持 BQ24074/TPS63031/TPS61023/TPS2553 版本。

## 资料

- [Injoinic IP5305T 原厂数据表 V1.0](https://www.injoinic.com/api/static/uploads/20250529/20250529092838_6837b846e7f6c.pdf)
- [IP5305T 引脚和典型应用镜像](https://datasheet4u.com/pdf-down/I/P/5/IP5305T-Injoinic.pdf)
- [Advanced Monolithic Systems AMS1117-3.3 数据表](https://datasheet.lcsc.com/lcsc/1810231814_Advanced-Monolithic-Systems-AMS1117-3-3_C6186.pdf)
- [TI BQ24074](https://www.ti.com/lit/ds/symlink/bq24074.pdf)、[TI TPS63031](https://www.ti.com/lit/ds/symlink/tps63031.pdf)、[TI TPS61023](https://www.ti.com/lit/ds/symlink/tps61023.pdf)、[TI TPS2553](https://www.ti.com/lit/ds/symlink/tps2553.pdf)
