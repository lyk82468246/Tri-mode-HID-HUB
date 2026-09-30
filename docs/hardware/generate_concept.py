"""Generate the Rev B placement study, not manufacturing/EDA data.

Coordinates: top view, origin at board upper left, x right, y down, mm.
Package geometry follows WCH CH583DS1 v1.9 p111 (10/14/10/14 pads).
Run with Python 3; standard library only. Rendering is a separate QA step.

The Rev B-M mechanical overlay records the user's inverted stack-up: the
top-copper components face the 3D-printed basin, while the bottom side carries
the display and capacitive touch electrodes toward the user.  It is a concept
overlay, not an EDA footprint or a clearance-approved mechanical drawing.
"""
from pathlib import Path
import csv
import html
import json
import math
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
W, H = 85.60, 53.98
CX, CY = 43.0, 24.5
COLORS = dict(rf="#a66a04", usb="#2675d8", ps2="#168573", spi="#7c51c0",
              uart="#d06338", ir="#c74679", i2c="#39829d", power="#926432",
              debug="#64748b", spare="#87929e", mech="#9a6d20",
              display="#2d7c9f", touch="#b04b82")

# pin, silicon name, selected function/net, class, electrical/implementation note
PINS = [
 (1,"VDCID","CH_VDCI","power","内部电源节点；连接 VDCIA；按 DCDC/LDO 模式去耦"),
 (2,"VSW","CH_VSW","power","到本地电感再到 VDCID；是否启用内部 DCDC 与固件一致"),
 (3,"VIO33/VDD33","3V3","power","电源输入；贴近引脚去耦"),
 (4,"PA7/AIN11","USB_C_VBUS_ADC","power","新增：VBUS_RAW 分压/RC；禁止 5 V 直连"),
 (5,"PA8/RXD1","RS232_RX_TTL","uart","UART1 默认映射；MAX3232 ROUT1 到 MCU"),
 (6,"PA9/TXD1","RS232_TX_TTL","uart","UART1 默认映射；MCU 到 MAX3232 DIN1"),
 (7,"PB9","STAT_CHG_N","power","ETA9697 STAT；开漏，外部约 10 kΩ 上拉到 3V3，低电平表示充电中"),
 (8,"PB8","POWER_AUX_EN","power","高电流分支运行时控制/监测预留；硬件总开关必须取 SW_SYS 后节点，ETA9697 不使用该脚，外部下拉保持安全态"),
 (9,"PB17","POWER_AUX_FAULT_N","power","预留给高电流升压/电源监测故障输入；ETA9697 不使用该脚，外部上拉或 NC 按最终分支处理"),
 (10,"PB16","POWER_RESERVED","power","ETA9697 没有 PGOOD 输出；保留数字测试点，禁止当 ADC 使用"),
 (11,"PB15/TCK","WCH_TCK","debug","保留 WCH 两线调试，不复用 SPI"),
 (12,"PB14/TIO","WCH_TIO","debug","保留 WCH 两线调试，不复用 SPI"),
 (13,"PB13/U2D+","USB_HOST_DP_MCU","usb","USB2 Host，固定数据引脚"),
 (14,"PB12/U2D-","USB_HOST_DN_MCU","usb","USB2 Host；禁止 I2C 默认映射占用"),
 (15,"PB11/UD+","USB_DEV_DP_MCU","usb","USB Device，固定数据引脚"),
 (16,"PB10/UD-","USB_DEV_DN_MCU","usb","USB Device，固定数据引脚"),
(17,"PB7/TXD0/PWM9","IR_TXD_U9","ir","直接接 U9 TXD；IrDA 用 UART0 TX，遥控用同一脚的 PWM9/定时器载波与包络；不放外部 TX 多路器"),
 (18,"PB6","HOST_EN","power","TPS2553 EN，高有效；外部下拉，上电关断"),
 (19,"PB5","HOST_FAULT_N","power","TPS2553 FAULT#；外部上拉到 3V3；不是电压合格指示"),
(20,"PB4/RXD0","IR_RX_RAW","ir","直接接 U9 RXD；UART0 IrDA 接收；同一高阻节点可选 0 Ω/串阻分支到 PB1 学习捕获"),
(21,"PB3","RESERVED_GPIO","spare","不再承担外部红外模式选择；保留为普通 GPIO/测试点，原理图不连接 U12"),
(22,"PB2","RESERVED_GPIO","spare","不再承担 MCP2120 MODE；可作为未来扩展 GPIO，默认 NC/测试点"),
(23,"PB1","IR_RX_CAPTURE","ir","可选高阻输入捕获 U9 RXD；遥控学习记录原始载波脉冲，不放 TSOP"),
(24,"PB0/PWM6","IR_EXT_SINK_GATE","ir","仅在可选 DNP IREDC/Q_IR 支路装配时作栅极控制；直接 TXD 基线不使用"),
 (25,"PB23/RST#","RESET_N","debug","低有效复位；底层 RST 触摸电极与调试口共用，不重映射 UART2"),
 (26,"PB22","BOOT_N","debug","底层 BOOT 触摸电极；保留 ISP 配置"),
 (27,"PB21/SCL_","I2C_SCL","i2c","I2C 必须重映射 RB_PIN_I2C=1"),
 (28,"PB20/SDA_","I2C_SDA","i2c","板边直角 J7 I2C/OLED 排母，外部上拉到 3V3"),
(29,"PB19","IRDA_SD","ir","U9 pin5 SD，高关断；10 kΩ 左右下拉到 GND，PB19 拉高才关断"),
(30,"PB18","USER_TOUCH","debug","底层 USER 电容触摸通道；按 WCH touch 配置使用，不默认放 GPIO 上拉"),
 (31,"X32MO","X32MO","rf","32 MHz 晶振；负载由晶体 CL 与寄生计算"),
 (32,"X32MI","X32MI","rf","32 MHz 晶振；最短回路，与 RF 馈线分开"),
 (33,"VINTA","CH_VINTA","power","内部模拟节点；只按手册去耦，不作为外部负载电源"),
 (34,"ANT","RF_ANT","rf","向北，50 ohm 馈线；可选调谐焊盘贴近馈点"),
 (35,"VDCIA","CH_VDCI","power","按 WCH 参考与 VDCID 相连并本地去耦"),
 (36,"PA4/RXD3","UART_TTL_RX","uart","新增 J10 直角排母；UART3 默认映射"),
 (37,"PA5/TXD3","UART_TTL_TX","uart","新增 J10 直角排母；MCU 视角命名，3.3 V"),
 (38,"PA6/AIN10","VBAT_SENSE","power","BAT 分压/RC；从不支持 ADC 的 PB16 迁入"),
 (39,"PA0","KBD_CLK_MCU","ps2","保留；GPIOA 中断，经 BSS138 开漏电平转换"),
 (40,"PA1","KBD_DATA_MCU","ps2","保留；仅拉低/释放，经 BSS138"),
 (41,"PA2","MOUSE_CLK_MCU","ps2","保留；GPIOA 中断，经 BSS138"),
 (42,"PA3","MOUSE_DATA_MCU","ps2","保留；仅拉低/释放，经 BSS138"),
 (43,"PA15/MISO","SPI_MISO","spi","SPI0 默认映射；禁止 UART0 重映射占用"),
 (44,"PA14/MOSI","SPI_MOSI","spi","SPI0 默认映射"),
 (45,"PA13/SCK0","SPI_SCK","spi","SPI0 默认映射；不启用 PWM5 输出"),
 (46,"PA12/SCS","SPI_CS_N","spi","SPI0 默认映射；不启用 PWM4 输出"),
 (47,"PA11/X32KO","LSE_OUT_RESERVED","spare","可选 32.768 kHz 晶振；未装时 NC，不接排针"),
 (48,"PA10/X32KI","LSE_IN_RESERVED","spare","可选 32.768 kHz 晶振；当前 LSI 方案可不装"),
 (49,"EP / GND (datasheet pin 0)","GND","power","EP 接完整地平面与接地过孔；核对库的 0/49 编号映射"),
]

# These are conservative planning envelopes, not exact land patterns.  Edge
# headers, bottom-side user interfaces, mounting holes and the battery basin
# are kept in separate mechanical tables so that their conceptual overlays do
# not get mistaken for electrical courtyard-overlap checks.
PLACEMENTS = [
 ("J3","PS/2 键盘","Mini-DIN-6",0,8,15,15,"ps2","west"),
 ("J4","PS/2 鼠标","Mini-DIN-6",0,27,15,15,"ps2","west"),
 ("J1","USB-C","Device / 5 V IN",76.6,12,9,10,"usb","east"),
 ("J2","USB-A","Host / 5 V OUT",73.6,25,12,16,"usb","east"),
 ("J5","RS232 · DB9 公座","DTE · TX / RX / GND",26,39.98,32,14,"uart","south"),
 ("U9","IrDA / 遥控共用收发","TFBS4650 · IREDA/IREDC/TXD/RXD",60.5,0,6.8,3.5,"ir","north"),
  ("Q_IR","可选 IREDC 电流汇","小信号 NMOS · DNP 备用",68.5,4.5,3,2.5,"ir","zone"),
  ("R_IR","可选 IRED 限流","IREDA 支路 · DNP/按数据手册",72.5,4.5,3.5,2.5,"ir","zone"),
 ("U10+X2","MCP2120 + 7.3728 MHz","可选 DNP；MCU 编解码旁路",62,7,13,11,"ir","zone"),
 ("PS2-K","BSS138 ×2","上拉 / ESD / 限流",17,11,9,9,"ps2","zone"),
 ("PS2-M","BSS138 ×2","上拉 / ESD / 限流",17,29,9,9,"ps2","zone"),
 ("U6","MAX3232E","电荷泵电容",35,32,12,6.5,"uart","zone"),
 ("U4","高电流升压","5V_HOST + L/C · 待选",63,32.5,8.5,8.5,"power","zone"),
 ("U3","AMS1117-3.3","3V3 + L/C · 低功耗分支",60,43,8,9,"power","zone"),
 ("U2","ETA9697","充电 / 5V_ETA",69,43,8,9,"power","zone"),
 ("U5","TPS2553","限流 / FAULT#",62,25,6.5,3,"power","zone"),
 ("U7","ESD-C","短接地",72,19,3.5,3.5,"usb","zone"),
 ("U8","ESD-A","短接地",70,29,3,3,"usb","zone"),
 ("X1","32 MHz","晶振及负载",43,17,6,4,"rf","zone"),
 ("RF-M","RF 调谐","预留焊盘",39,16.5,3,3.5,"rf","zone"),
 ("LSE","32.768k","可选 DNP",34,28.5,6,3,"spare","zone"),
 ("DCDC","U1 去耦 / L","本地回路",35,26,5,2,"power","zone"),
 ("U1","CH582M","5×5 mm",CX-2.5,CY-2.5,5,5,"debug","chip"),
]
ANT = dict(x=32,y=0,w=25,h=10)

# Right-angle female headers.  The rectangle is the board-side body envelope;
# the arrow points toward the mating cable and the enclosure wall opening.
SIDE_HEADERS = [
 ("J10","UART3 TTL","1×4 · 3V3",20,0,11,3,"uart","north"),
 ("J7","I²C / OLED","1×4 · 3V3",17,47.0,9,6.98,"i2c","south"),
 ("J8","SPI0","2×4 · 3V3",8,47.0,8,6.98,"spi","south"),
 ("J9","WCH-Link","2×3 · VTref",61,47.0,9,6.98,"debug","south"),
]

# Bottom-side user-facing features.  The board view is still a top view, so
# these are intentionally dashed/mirrored overlays rather than top-side
# component placement envelopes.
BOTTOM_ZONES = [
 ("OLED","OLED / 显示屏","底层 · 可视面朝上",23,14,39,24,"display"),
 ("TOUCH-L","USER / RST / BOOT","底层触摸电极",20,40.5,10,6.0,"touch"),
 ("TOUCH-M","触摸电极 2","底层触摸电极",37,40.5,10,6.0,"touch"),
 ("TOUCH-R","触摸电极 3","底层触摸电极",54,40.5,10,6.0,"touch"),
]

# Mechanical references, not electrical placements.  Hole diameter is a
# starting M3 candidate; the final screw, washer, insert and fab annulus must
# be selected together with the 3D printed floor.
MOUNTING_HOLES = [
 ("H1",4.5,4.5,1.6), ("H2",81.1,4.5,1.6),
 ("H3",4.5,49.48,1.6), ("H4",81.1,49.48,1.6),
]
BASIN = dict(x=18,y=11.5,w=52,h=36.5)
BATTERY_POCKET = dict(x=25,y=15.5,w=38,h=24)
GROUPS = [
 ("RF",[34],(41,10),"rf"),
 ("USB-A",[13,14],(72,31),"usb"),
 ("USB-C",[15,16],(75,19),"usb"),
 ("PS/2 K",[39,40],(26,16),"ps2"),
 ("PS/2 M",[41,42],(26,33),"ps2"),
 ("SPI0",[43,44,45,46],(16,47),"spi"),
 ("UART3",[36,37],(26,7),"uart"),
 ("RS232",[5,6],(41,32),"uart"),
  ("IR TX/RX",[17,20,23,24,29],(64,18),"ir"),
 ("I2C",[27,28],(53,15.5),"i2c"),
]

def pad(pin):
    if pin <= 10: return (-1.575+.35*(pin-1),2.5),(0,1),"南"
    if pin <= 24: return (2.5,2.275-.35*(pin-11)),(1,0),"东"
    if pin <= 34: return (1.575-.35*(pin-25),-2.5),(0,-1),"北"
    if pin <= 48: return (-2.5,-2.275+.35*(pin-35)),(-1,0),"西"
    return (0,0),(0,0),"底部 EP"

def rotate(v,angle):
    a=math.radians(angle)
    return v[0]*math.cos(a)-v[1]*math.sin(a), v[0]*math.sin(a)+v[1]*math.cos(a)

def comparison(angle):
    rows=[]
    for name,pins,end,color in GROUPS:
        pts=[rotate(pad(p)[0],angle) for p in pins]
        q=(sum(p[0] for p in pts)/len(pts),sum(p[1] for p in pts)/len(pts))
        n=rotate(pad(pins[0])[1],angle)
        d=(end[0]-CX-q[0],end[1]-CY-q[1])
        a=math.degrees(math.acos(max(-1,min(1,(d[0]*n[0]+d[1]*n[1])/math.hypot(*d)))))
        rows.append(dict(group=name,angle_deg=round(a,2),straight_distance_mm=round(math.hypot(*d),2)))
    return dict(rotation_cw_deg=angle,mean_departure_angle_deg=round(sum(r['angle_deg'] for r in rows)/len(rows),2),
                backward_groups=sum(r['angle_deg']>90 for r in rows),groups=rows)

def esc(s): return html.escape(str(s))
def text(x,y,s,size=16,color="#243447",anchor="start",weight=400,extra=""):
    return f'<text x="{x:.2f}" y="{y:.2f}" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}" {extra}>{esc(s)}</text>'
def rect(x,y,w,h,fill,stroke="none",radius=0,extra=""):
    return f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="{radius}" fill="{fill}" stroke="{stroke}" {extra}/>'
def path(points,color,width=2,dash="",extra=""):
    pts=" ".join(f'{x:.2f},{y:.2f}' for x,y in points)
    return f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round" stroke-dasharray="{dash}" {extra}/>'
def start(w,h,title):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" role="img"><title>{esc(title)}</title>',
      '<defs><pattern id="hatch" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)"><line x1="0" y1="0" x2="0" y2="10" stroke="#e6c778" stroke-width="2"/></pattern><marker id="arrow" markerWidth="8" markerHeight="8" refX="6" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8" fill="none" stroke="#60748b"/></marker></defs>',
      '<g font-family="Microsoft YaHei, Noto Sans CJK SC, Segoe UI, Arial, sans-serif">',rect(0,0,w,h,"#f7f9fc")]

def make_board():
    svg=start(1500,1080,"信用卡尺寸 CH582M 顶层器件布局 Rev B-IR")
    svg += [text(55,52,"CH582M · 顶层器件与板边接口布局",30,weight=700),
            text(55,82,"Rev B-IR / 2026-09-30   ·   顶视图   ·   PCB 85.60 × 53.98 mm   ·   0° = Pin 1 左下",16,color="#627186"),
            text(55,108,"红外采用一个 TXD/RXD 光电收发模块；底层 OLED/触摸与中央电池盆地移到独立图，避免互相遮挡。",15,color="#627186")]
    OX,OY,S=70,180,12.5
    P=lambda x,y:(OX+x*S,OY+y*S)
    R=lambda x,y,w,h,fill,stroke="none",r=0,extra="":rect(*P(x,y),w*S,h*S,fill,stroke,r,extra)
    T=lambda x,y,s,size=13,color="#243447",anchor="middle",weight=400: text(*P(x,y),s,size,color,anchor,weight)
    svg += [R(0,0,W,H,"#edf5f0","#547466",30,'stroke-width="2"'),
            path([P(0,-3),P(W,-3)],"#718295",1),T(W/2,-4,"85.60 mm",15),
            path([P(-3,0),P(-3,H)],"#718295",1),text(25,OY+H*S/2,"53.98 mm",15,extra=f'transform="rotate(-90 25 {OY+H*S/2})"')]
    for x in range(5,85,5): svg.append(path([P(x,1),P(x,H-1)],"#cfdfd6",.5,dash="2 6"))
    for y in range(5,50,5): svg.append(path([P(1,y),P(W-1,y)],"#cfdfd6",.5,dash="2 6"))
    # Signal corridors are deliberately faint and drawn first; component
    # envelopes and labels remain on top and readable.
    routes=[
      ([(41.425,22),(41,20),(41,10)],"rf"),
      ([(45.5,25.9),(49,28.8),(66,28.8),(70,30.5),(73.6,30.5)],"usb"),
      ([(45.5,25.0),(50,25),(52,23),(69,23),(72,20.5),(76.6,20.5)],"usb"),
      ([(40.5,23.8),(34,23.8),(30,16),(26,16),(15,16)],"ps2"),
      ([(40.5,24.5),(32,24.5),(28,33),(26,33),(15,33)],"ps2"),
      ([(40.5,25.7),(29,25.7),(28,40),(20,46),(16,48)],"spi"),
      ([(40.5,22.9),(32,17),(30,7),(26,6)],"uart"),
      ([(43,27),(43,32),(41,39.98)],"uart"),
      ([(45.5,24),(53,16),(63.9,3.5)],"ir"),
      ([(45.5,22.35),(52,10.8),(59,6),(69,5.7)],"ir"),
      ([(63.9,3.5),(63.5,7),(62,7)],"ir"),
      ([(44,22),(50,16),(53,15.5)],"i2c"),
      ([(45.5,26.6),(49,32),(53,35)],"debug"),
    ]
    for pts,col in routes: svg.append(path([P(*p) for p in pts],COLORS[col],1.8,dash="5 4" if col=="debug" else "",extra='opacity="0.45"'))
    # BLE antenna reservation is the only large keep-out shown on this view.
    svg += [R(**ANT,fill="#fff4d9",stroke="#bd8e27",extra='stroke-dasharray="6 4"'),
            R(**ANT,fill="url(#hatch)"),T(44.5,3.0,"BLE 天线净空",15,"#835d06",weight=700),
            T(44.5,5.1,"25 × 10 mm · 电池/金属/墙体避让",10,"#835d06"),
            T(44.5,7.2,"天线本体和馈线按最终层叠重画",9,"#835d06")]
    fill_map={"usb":"#e4efff","ps2":"#dff2ec","uart":"#fcece2","ir":"#fce7f0","spi":"#eee8fc","power":"#f3eadd","i2c":"#e1f0f4","debug":"#e7ebf1","spare":"#f0f1f3","rf":"#fff0cd"}
    for ref,label,sub,x,y,w,h,kind,edge in PLACEMENTS:
        if edge=="chip": continue
        c=COLORS[kind]; svg.append(R(x,y,w,h,fill_map[kind],c,4,'stroke-width="1.25"'))
        if ref=="U9":
            optical=("IR TX/RX","TFBS4650 · cathode")
            svg += [T(x+w/2,y+1.35,optical[0],9,c,weight=700),T(x+w/2,y+2.55,optical[1],7,c)]
        elif ref=="Q_IR":
            svg += [T(x+w/2,y+1.15,"Q_IR",8,c,weight=700),T(x+w/2,y+2.2,"DNP",7,c)]
        elif ref=="R_IR":
            svg += [T(x+w/2,y+1.15,"R_IR",8,c,weight=700),T(x+w/2,y+2.2,"DNP",7,c)]
        elif ref=="U10+X2":
            svg += [T(x+w/2,y+3.0,"MCP2120",12,c,weight=700),T(x+w/2,y+5.1,"可选 DNP",9,c),T(x+w/2,y+7.4,"旁路/去耦区",8,c)]
        elif ref in ("DCDC","LSE"):
            svg += [T(x+w/2,y+1.4,ref,9,c,weight=700),T(x+w/2,y+2.55,"DNP/本地回路",7,c)]
        elif w>=10 and h>=8:
            svg += [T(x+w/2,y+h/2-1.8,ref,10,c,weight=700),T(x+w/2,y+h/2+0.2,label,12,c,weight=700),T(x+w/2,y+h/2+2.0,sub,8,c)]
        elif w>=6 and h>=5:
            svg += [T(x+w/2,y+1.7,ref,9,c,weight=700),T(x+w/2,y+3.35,label,9,c),T(x+w/2,y+4.8,sub,7,c)]
        else:
            svg += [T(x+w/2,y+1.45,ref,8,c,weight=700),T(x+w/2,y+2.65,label,7,c)]
        if edge in ("west","east","north","south"):
            if edge=="west": a,b=(x,y+h/2),(x-1.7,y+h/2)
            elif edge=="east": a,b=(x+w,y+h/2),(x+w+1.7,y+h/2)
            elif edge=="north": a,b=(x+w/2,y),(x+w/2,y-1.7)
            else: a,b=(x+w/2,y+h),(x+w/2,y+h+1.7)
            svg.append(path([P(*a),P(*b)],"#60748b",1.2,extra='marker-end="url(#arrow)"'))
    # Edge headers are separate dashed envelopes; labels are outside the board.
    header_fill={"uart":"#fff0e8","i2c":"#e8f6f8","spi":"#f0eaff","debug":"#edf0f5"}
    for ref,label,sub,x,y,w,h,kind,edge in SIDE_HEADERS:
        c=COLORS[kind]; svg.append(R(x,y,w,h,header_fill[kind],c,3,'stroke-width="1.3" stroke-dasharray="3 2"'))
        svg.append(T(x+w/2,y+h/2+0.4,ref,9,c,weight=700))
        if edge=="north": a,b=(x+w/2,y),(x+w/2,y-1.8); lx,ly=x+w/2,y-2.7
        else: a,b=(x+w/2,y+h),(x+w/2,y+h+1.8); lx,ly=x+w/2,y+h+3.0
        svg += [path([P(*a),P(*b)],"#344955",1.5,extra='marker-end="url(#arrow)"'),T(lx,ly,label,8,c)]
    # Four mounting holes: the dashed ring is a preliminary Ø5.6 mm keep-out.
    for ref,x,y,r in MOUNTING_HOLES:
        px,py=P(x,y)
        svg += [f'<circle cx="{px:.2f}" cy="{py:.2f}" r="{2.8*S:.2f}" fill="#ffffff" fill-opacity="0.86" stroke="#586b78" stroke-width="1.1" stroke-dasharray="4 3"/>',
                f'<circle cx="{px:.2f}" cy="{py:.2f}" r="{r*S:.2f}" fill="#d9e1e6" stroke="#344955" stroke-width="1.2"/>',
                path([P(x-1.2,y),P(x+1.2,y)],"#70818b",.7),path([P(x,y-1.2),P(x,y+1.2)],"#70818b",.7)]
    # Real QFN outline and pad fan-out; no extra annotations are placed over it.
    svg.append(R(CX-2.5,CY-2.5,5,5,"#263b45","#172d36",3))
    for pin,*_ in PINS[:48]:
        (px,py),n,side=pad(pin); svg.append(path([P(CX+px,CY+py),P(CX+px+n[0]*.55,CY+py+n[1]*.55)],"#b69c5a",1.8))
    svg += [T(CX,CY-0.2,"CH582M",9,"#ffffff",weight=700),T(CX,CY+1.15,"U1 · 0° · 面朝盆地",7,"#c8e1da"),
            f'<circle cx="{P(CX-1.8,CY+1.8)[0]}" cy="{P(CX-1.8,CY+1.8)[1]}" r="2.2" fill="#fce59a"/>']
    # Compact reading guide kept outside the board.
    sx=1215
    svg += [rect(sx,150,250,705,"#ffffff","#dce3ed",14),text(sx+20,188,"顶层图例",22,weight=700)]
    legend=[("彩色实线框","顶层器件 / 电路预留区"),("虚线框","侧插排母的板内包络"),("圆形虚线","安装孔初始 keep-out Ø5.6"),("浅色虚线","信号方向示意，非铜线")]
    yy=225
    for title,desc in legend:
        svg += [text(sx+20,yy,title,14,weight=700),text(sx+20,yy+21,desc,12,"#617086")]; yy+=60
    notes=["U1 保持 0°，Pin 1 在左下；ANT34 向北。","PB7 直连 TXD；UART0/PWM9 内部切换。","Q_IR/R_IR 仅可选 DNP；触摸/电池分开。","正式 footprint / 3D / DRC 待建立。"]
    svg += [text(sx+20,yy+4,"阅读约束",15,weight=700)]
    for i,note in enumerate(notes): svg.append(text(sx+20,yy+30+i*36,f"{i+1}. {note}",11,"#536575"))
    svg += [text(70,915,"配套图",20,weight=700),
            rect(70,935,440,68,"#ffffff","#cfdae5",9),text(90,962,"底层用户界面",16,weight=700),text(90,986,"OLED + 三块触摸电极 + FPC + 孔环",12,"#617086"),
            rect(530,935,440,68,"#ffffff","#cfdae5",9),text(550,962,"物理剖面",16,weight=700),text(550,986,"电池盆地 + CH582M 面向盆地 + 四墙/底板",12,"#617086"),
            rect(990,935,410,68,"#ffffff","#cfdae5",9),text(1010,962,"角度比较",16,weight=700),text(1010,986,"0° 26.4° · +45° 48.4° · −45° 53.6°",12,"#617086"),
            text(70,1040,"本图刻意不把底层和机械覆盖层叠到顶层器件上；请分别查看 pcb-bottom-revb.svg 与 pcb-stack-revb.svg。",13,"#627186"),"</g></svg>"]
    return "\n".join(svg)

def make_bottom():
    """Draw the bottom-side user interface without top-side clutter."""
    svg=start(1500,980,"CH582M Rev B-M 底层 OLED 与电容触摸布局")
    svg += [text(55,52,"CH582M · 底层用户界面布局",30,weight=700),
            text(55,82,"Rev B-M / 2026-09-30   ·   从底层铜面观察   ·   OLED 与触摸有效面朝用户",16,"#627186"),
            text(55,108,"本图只画显示屏、触摸电极、FPC 和安装孔；顶层器件与接口请看 pcb-concept-revb.svg。",15,"#627186")]
    OX,OY,S=70,175,12.5
    P=lambda x,y:(OX+x*S,OY+y*S)
    R=lambda x,y,w,h,fill,stroke="none",r=0,extra="":rect(*P(x,y),w*S,h*S,fill,stroke,r,extra)
    T=lambda x,y,s,size=13,color="#243447",anchor="middle",weight=400: text(*P(x,y),s,size,color,anchor,weight)
    svg += [R(0,0,W,H,"#f1f7fa","#5b7c8c",30,'stroke-width="2"'),
            path([P(0,-3),P(W,-3)],"#718295",1),T(W/2,-4,"85.60 mm",15),
            path([P(-3,0),P(-3,H)],"#718295",1),text(25,OY+H*S/2,"53.98 mm",15,extra=f'transform="rotate(-90 25 {OY+H*S/2})"')]
    for x in range(5,85,5): svg.append(path([P(x,1),P(x,H-1)],"#d5e5ea",.5,dash="2 6"))
    for y in range(5,50,5): svg.append(path([P(1,y),P(W-1,y)],"#d5e5ea",.5,dash="2 6"))
    # The battery is on the opposite side.  Show its projection faintly so
    # touch/display coupling is explicit, but do not put its label over the UI.
    svg.append(R(BATTERY_POCKET["x"],BATTERY_POCKET["y"],BATTERY_POCKET["w"],BATTERY_POCKET["h"],"#fff8e8","#c99842",8,'stroke-width="1.2" stroke-dasharray="6 5" opacity="0.32"'))
    # OLED glass, active area, bezel and a conservative FPC bend envelope.
    ox,oy,ow,oh=23,14,39,24
    svg += [R(ox,oy,ow,oh,"#d9eef7",COLORS["display"],5,'stroke-width="1.8"'),
            R(ox+2,oy+2,ow-4,oh-4,"#f7fcfe",COLORS["display"],3,'stroke-width="1.2"'),
            T(ox+ow/2,oy+5.2,"OLED 显示屏",16,COLORS["display"],weight=700),
            T(ox+ow/2,oy+7.6,"底层 · 可视面朝上",11,COLORS["display"]),
            T(ox+ow/2,oy+oh-2.8,"有效区 / 边框 / 压合面待按实物收口",9,COLORS["display"]),
            R(62.5,20,4.5,8,"#d9eef7",COLORS["display"],2,'stroke-width="1.2" stroke-dasharray="3 2"'),
            T(64.75,24.7,"FPC",8,COLORS["display"],weight=700),
            path([P(67,24),P(71,24),P(73,26)],COLORS["display"],1.5,"4 3")]
    # Three isolated capacitive pads with a visible guard outline.
    touch_specs=[("TOUCH-L","USER",20), ("TOUCH-M","RST",37), ("TOUCH-R","BOOT",54)]
    for ref,label,x in touch_specs:
        svg += [R(x,40.5,10,6,"#f9e7f1",COLORS["touch"],4,'stroke-width="1.4" stroke-dasharray="5 3"'),
                R(x+1.0,41.5,8,4,"#fff8fb",COLORS["touch"],3,'stroke-width="1.5"'),
                T(x+5,44.0,label,11,COLORS["touch"],weight=700),
                T(x+5,46.0,"铜箔电极",8,COLORS["touch"])]
    # Keep touch traces grouped along the lower edge and away from the OLED
    # active area; the actual controller/RC network remains to be routed.
    for x in (25,42,59): svg.append(path([P(x,46.8),P(x,48.5),P(68,48.5)],COLORS["touch"],1.4,"3 3"))
    # Four holes, with labels just outside the board so they do not obscure UI.
    for ref,x,y,r in MOUNTING_HOLES:
        px,py=P(x,y)
        svg += [f'<circle cx="{px:.2f}" cy="{py:.2f}" r="{2.8*S:.2f}" fill="#ffffff" fill-opacity="0.8" stroke="#586b78" stroke-width="1.1" stroke-dasharray="4 3"/>',
                f'<circle cx="{px:.2f}" cy="{py:.2f}" r="{r*S:.2f}" fill="#d9e1e6" stroke="#344955" stroke-width="1.2"/>',
                path([P(x-1.2,y),P(x+1.2,y)],"#70818b",.7),path([P(x,y-1.2),P(x,y+1.2)],"#70818b",.7),
                T(x,y+4.2,ref,8,"#465b67")]
    # Reading guide outside the board.
    sx=1215
    svg += [rect(sx,150,250,660,"#ffffff","#dce3ed",14),text(sx+20,188,"底层图例",22,weight=700),
            text(sx+20,228,"蓝色大框",14,weight=700),text(sx+20,249,"OLED 玻璃 / 有效显示区 / FPC",12,"#617086"),
            text(sx+20,286,"粉色三框",14,weight=700),text(sx+20,307,"USER / RST / BOOT 触摸电极",12,"#617086"),
            text(sx+20,344,"浅黄色虚线",14,weight=700),text(sx+20,365,"顶层电池投影；需验证触摸耦合",12,"#617086"),
            text(sx+20,402,"孔环",14,weight=700),text(sx+20,423,"初始 Ø3.2 通孔 / Ø5.6 keep-out",12,"#617086"),
            text(sx+20,466,"必须确认",15,weight=700),
            text(sx+20,495,"1. 底层镜像和 FPC 出线方向",12,"#536575"),
            text(sx+20,523,"2. 电池、屏蔽层对触摸灵敏度的影响",12,"#536575"),
            text(sx+20,551,"3. 触摸 guard、RC 和 ESD 方案",12,"#536575"),
            text(sx+20,579,"4. 屏幕窗口、压合和维修空间",12,"#536575")]
    svg += [rect(70,880,1100,55,"#ffffff","#cfdae5",9),
            text(90,904,"安装姿态",14,weight=700),
            text(190,904,"用户从上方看到 OLED/触摸；PCB 顶层在另一侧朝向电池盆地。此图不是顶层元件布局，也不是显示屏采购尺寸。",12,"#617086"),
            "</g></svg>"]
    return "\n".join(svg)

def make_stack():
    """Draw the physical orientation and the 3D-printed floor/wall concept."""
    svg=start(1500,900,"CH582M Rev B-M PCB 与 3D 打印底板剖面概念")
    svg += [text(55,52,"CH582M · PCB / 电池盆地 / 3D 打印结构剖面",30,weight=700),
            text(55,83,"Rev B-M / 2026-09-30   ·   非比例机械示意   ·   需用最终封装、螺丝和电池实物收口",16,"#627186"),
            text(55,112,"上方是用户面；底层 OLED/触摸朝上，顶层器件和 CH582M 朝下进入盆地。",15,"#627186")]

    # Separate horizontal bands keep the orientation labels away from the
    # component bodies and make the load path obvious.
    svg += [rect(300,145,900,62,"#e5f4fb","#2d7c9f",10,'stroke-width="2"'),
            text(750,171,"用户面 / PCB 底层：OLED + 触摸（脸朝上）",19,"#24627f","middle",700),
            text(750,192,"窗口、FPC 和触摸走线由底层图单独收口",12,"#24627f","middle"),
            rect(280,255,940,38,"#d6ad63","#8b6524",4,'stroke-width="2"'),
            text(750,279,"FR-4 PCB 基材 / 4 层板",17,"#6b4b18","middle",700),
            path([(280,250),(1220,250)],"#b06e28",3),
            text(750,244,"底层铜 ↑     顶层铜 ↓",12,"#89531e","middle")]

    # Perimeter height envelopes and central MCU.  No leader line crosses a
    # label; the callouts are placed in the clear spaces between blocks.
    for x,w,h,label,color in ((330,110,120,"PS/2 / 侧插座","#dff2ec"),
                              (490,90,92,"弯插排母","#edf0f5"),
                              (1010,112,132,"USB / DB9 / 光学头","#e4efff"),
                              (1165,70,112,"电源 / 高件","#f3eadd")):
        svg += [rect(x,303,w,h,color,"#637783",6,'stroke-width="1.8"'),
                text(x+w/2,331,label,13,"#536772","middle",700),
                text(x+w/2,351,"向墙体开口",10,"#687984","middle")]
    svg += [rect(690,303,120,70,"#263b45","#172d36",8,'stroke-width="2"'),
            text(750,331,"U1 CH582M",17,"#ffffff","middle",700),
            text(750,351,"标记面朝盆地",11,"#c8e1da","middle"),
            rect(690,392,120,30,"#fff2cf","#bd8e27",7,'stroke-width="1.5" stroke-dasharray="6 4"'),
            text(750,411,"禁压 / 绝缘缓冲",11,"#8a620f","middle",700)]

    # Battery pocket is centered below the components and above the printed
    # floor.  The clear gap is intentionally visible in the drawing.
    svg += [rect(545,460,410,82,"#fffdf5","#c99842",12,'stroke-width="2"'),
            text(750,490,"1S Li-ion 电池 / 中央盆地",21,"#946b1e","middle",700),
            text(750,515,"保护板 + 泡棉托盘；不接触焊点、U1、电感或金属螺丝",12,"#946b1e","middle"),
            text(750,534,"电芯尺寸与 z 向余量尚未冻结",10,"#946b1e","middle")]

    # Four walls and the floor.  Front/back walls are dashed in this side cut;
    # they still exist in the enclosure and are called out below.
    svg += [rect(180,640,1140,42,"#e0d2c4","#806c5a",5,'stroke-width="2"'),
            text(750,666,"3D 打印底板：电池托盘 / 屏幕窗口 / 排线避让",17,"#665545","middle",700),
            rect(180,420,44,220,"#e0d2c4","#806c5a",4,'stroke-width="2"'),
            rect(1276,420,44,220,"#e0d2c4","#806c5a",4,'stroke-width="2"'),
            path([(250,420),(250,615),(1250,615),(1250,420)],"#9a8a79",2,"7 5"),
            text(116,440,"四面墙",12,"#665545","start",700),
            text(116,459,"非导电材料",11,"#665545")]

    # Two visible screw paths represent the four corner holes (front/back
    # pairs share each path in this section).
    for x,label in ((300,"H1 / H3"),(1200,"H2 / H4")):
        svg += [path([(x,255),(x,640)],"#566873",2,"4 3"),
                f'<circle cx="{x}" cy="268" r="13" fill="#d9e1e6" stroke="#344955" stroke-width="2"/>',
                f'<circle cx="{x}" cy="640" r="13" fill="#d9e1e6" stroke="#344955" stroke-width="2"/>',
                text(x,235,label,11,"#465b67","middle",700),
                text(x,706,"螺丝 / 支柱 / 螺母座",10,"#465b67","middle")]

    svg += [rect(55,735,1390,112,"#ffffff","#dce3ed",14),
            text(80,767,"机械收口条件",18,weight=700),
            text(80,793,"1. H1–H4 贯穿 PCB 与底板；先选 M2.5/M3、垫片、支柱和热熔螺母，再反推孔径、焊盘和 keep-out。",13,"#536575"),
            text(80,816,"2. 墙体用非导电、非碳纤材料；USB/DB9/PS2/光学头/弯插排母要有独立开口，天线保留净空。",13,"#536575"),
            text(80,839,"3. 电池可放中央，但必须以实物电芯、泡棉、压力、触摸灵敏度和温升测试验证 z 向余量。",13,"#536575"),
            "</g></svg>"]
    return "\n".join(svg)

def make_pinout():
    svg=start(1540,1270,"CH582M Rev B-IR 完整引脚分配与封装方向")
    svg += [text(55,50,"CH582M · Rev B-IR 引脚分配",30,weight=700),
            text(55,82,"顶视图 · 0° = Pin 1 在左下 · 上/下各 10 脚，左/右各 14 脚 · QFN48 5×5 mm，pitch 0.35 mm",16,"#627186")]
    cx,cy,sc=770,590,95
    svg.append(rect(cx-2.5*sc,cy-2.5*sc,5*sc,5*sc,"#203f48","#162b32",14))
    svg += [text(cx,cy-38,"CH582M",36,"#ffffff","middle",700),text(cx,cy+3,"48 pins + EP",21,"#cce3dc","middle"),
            text(cx,cy+38,"EP → GND",24,"#f1d68b","middle",700),text(cx,cy+74,"datasheet 0 / library 49",16,"#cce3dc","middle")]
    for pin,silicon,net,kind,note in PINS[:48]:
        p,n,side=pad(pin); x,y=cx+p[0]*sc,cy+p[1]*sc;c=COLORS[kind]
        svg.append(path([(x,y),(x+n[0]*23,y+n[1]*23)],c,9))
        short=silicon.split('/')[0]
        label=f"{pin:02d}  {short}  {net}"
        if side=="西": svg.append(text(x-32,y+5,label,13,c,"end",600))
        elif side=="东": svg.append(text(x+32,y+5,label,13,c,"start",600))
        elif side=="北":
            svg.append(text(x+5,y-35,label,13,c,"start",600,extra=f'transform="rotate(-90 {x+5} {y-35})"'))
        else:
            svg.append(text(x-4,y+38,label,13,c,"start",600,extra=f'transform="rotate(90 {x-4} {y+38})"'))
    svg.append(f'<circle cx="{cx-2.05*sc}" cy="{cy+2.05*sc}" r="10" fill="#f1d68b"/>')
    svg += [text(55,1115,"必须固定的复用约束",22,weight=700),
            text(55,1150,"I²C 重映射到 PB21/PB20；UART0/1/3 与 SPI0 保持默认映射。UART2 不启用。",17),
            text(55,1180,"USB2 占 PB13/PB12；调试占 PB15/PB14；复位/下载占 PB23/PB22；这些引脚不能再给其他接口。",17),
            text(55,1210,"本表是 Rev B-IR 硬件基线；当前固件仍按旧的 MCP2120/TSOP 分立原型，单模块方案需迁移后实测。所有排针信号均为 3.3 V 逻辑。",16,"#627186"),"</g></svg>"]
    return "\n".join(svg)

def write_table():
    header=['pin','silicon','side_top_view','net','group','note']
    with (ROOT/'pin-allocation-revb.csv').open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.writer(f);w.writerow(header)
        for p,s,n,g,note in PINS:w.writerow([p,s,pad(p)[2],n,g,note])
    lines=['# CH582M Rev B-IR 引脚分配提案','',
      '日期：2026-09-30。配套布局：`pcb-concept-revb.svg`；底层用户界面：`pcb-bottom-revb.svg`；物理剖面：`pcb-stack-revb.svg`；顶视放大图：`pinout-revb.svg`。',
      '这是单光电收发模块的 Rev B-IR 硬件基线；当前固件仍按旧的 MCP2120/TSOP 分立原型，最终电气行为需在开发板/PCB 上实测确认。','',
      '0° 定义：数据手册第 111 页 Top View，Pin 1 在左下。1–10 南侧从左到右；11–24 东侧从下到上；25–34 北侧从右到左；35–48 西侧从上到下。EP 在手册标 0，本提案 CSV 以库常用的 49 表示。','',
      '| Pin | 芯片功能 | 物理侧 | Rev B-IR 网络 | 用途/约束 |',
      '|---:|---|---|---|---|']
    for p,s,n,g,note in PINS:lines.append(f'| {p} | {s} | {pad(p)[2]} | `{n}` | {note} |')
    lines += ['', '## 接插件定义（均按连接器自身脚号，不能按焊接面视觉猜编号）','',
      '| 接口 | 接线定义 |', '|---|---|',
      '| J1 USB-C | A4/A9/B4/B9 = VBUS_RAW → 入口保护 → ETA_VIN；A6/B6 = D+；A7/B7 = D−；CC1/CC2 各自独立 5.1 kΩ 到地；SBU NC；GND 与壳体接法按 ESD 设计 |',
      '| J2 USB-A | 1 VBUS_HOST；2 D−；3 D+；4 GND；壳体接屏蔽/地方案 |',
      '| J3/J4 Mini-DIN-6 | 1 DATA；2 NC；3 GND；4 受保护的 5V_PS2_K/M；5 CLK；6 NC；外壳地；必须核对实际座子的 mating-face 与 PCB-side 图 |',
      '| J5 DB9 公座 DTE | 2 RX（到 MAX3232 RIN1）；3 TX（来自 DOUT1）；5 GND；1/4/6/7/8/9 NC；不提供 RTS/CTS 硬件握手 |',
      '| J6 电池 | 1 BAT+；2 GND（本板自定义，不能假定成品电池线颜色/极性一致）；改为中央盆地内的带保护 1S 电池连接，NTC 另用焊盘连接；电芯不得压到 U1/电感/焊点 |',
      '| J7 I2C/OLED 1×4 | 1 GND；2 3V3_OUT；3 SCL；4 SDA。右角弯插排母，向板外插拔；OLED 本体在 PCB 底层 |',
      '| J8 SPI0 2×4 | 1 3V3_OUT；2 GND；3 SCK；4 GND；5 MOSI；6 MISO；7 CS#；8 GND。左列 1/3/5/7、右列 2/4/6/8；底边弯插排母 |',
      '| J9 WCH-Link 2×3 | 1 3V3_VTref；2 GND；3 TCK；4 TIO；5 RESET#；6 GND。底边弯插排母；目标板自行供电，VTref 不与调试器电源硬并联 |',
      '| J10 UART3 1×4 | 1 GND；2 3V3_OUT；3 TX（MCU 输出）；4 RX（MCU 输入）。顶边弯插排母，不是 RS232 电平 |',
      '', 'J7/J8/J9/J10 均是向板外插拔的直角排母；正式 footprint 应以方焊盘、丝印、mating-face 和 3D 模型再次确认。底层 OLED/触摸电极的镜像、FPC 和螺丝孔避让必须在 EDA 中单独检查。',
      '', '## Rev B-M 物理层叠约束','',
      '- PCB 顶层器件在装配姿态中朝向盆地；CH582M 的封装标记面朝下。底层 OLED 与三块 USER/RST/BOOT 触摸电极朝用户面。这个“顶/底”是 PCB 层名，不等于观察图的朝向。',
      '- 中央盆地只定义电池空间，不豁免 U1、晶振、DC/DC 电感、焊点与电池之间的绝缘/压力/温升验证；先锁定电芯厚度、泡棉和支撑高度，再确定 pocket。',
      '- H1–H4 为四角贯穿安装孔候选，当前按 3.2 mm 通孔的 M3 起点绘制；螺丝、垫片、支柱、热熔螺母和孔环形焊盘需作为一组重新收口。',
      '- 3D 打印底板与四面墙建议采用非导电、非碳纤材料；墙体为 USB/DB9/PS2/共用光学头/直角排母开窗，BLE 天线保留净空。',
      '', '## 外设资源与复用','',
      '| 资源 | 选择 | 冲突处理 |','|---|---|---|',
      '| USB / USB2 | Device PB11/PB10；Host PB13/PB12 | 不占用作 UART1、SPI0 或 I2C 默认脚 |',
       '| UART0 | PB7 TX / PB4 RX，标准 IrDA 数据通道 | RB_PIN_UART0=0；同一 PB7 也可切换 PWM9 生成遥控载波 |',
      '| UART1 | PA9 TX / PA8 RX，RS232 | RB_PIN_UART1=0；现有 UART1 debug 输出需防止污染业务串口 |',
      '| UART2 | 不启用 | 默认 PA6/PA7 用作 ADC；重映射 PB22/PB23 保留给 BOOT/RESET |',
      '| UART3 | PA5 TX / PA4 RX，板边直角排母 | RB_PIN_UART3=0；PB20/PB21 留给 I2C |',
      '| SPI0 | PA12–PA15 默认组 | RB_PIN_SPI0=0；SPI1 禁用（其 PA0–PA2 已是 PS/2） |',
      '| I2C | PB21 SCL / PB20 SDA | RB_PIN_I2C=1，否则与 USB2 冲突 |',
       '| PB7/PWM9 | 同一 PB7/TXD0 产生 38 kHz 载波和包络 | 由 MCU 内部复用选择；不放外部 TX 多路器 |',
       '| PB0/PWM6 | 可选 IREDC/Q_IR 栅极 | 仅装配 DNP 电流汇增强支路时使用；基线 NC/测试点 |',
      '| GPIO 中断 / 时间基准 | PS/2 GPIOA；共用光模块 RXD 可送 PB1 | 遥控学习记录 RXD 原始脉冲；不在 ISR 解码整帧 |',
      '| ADC | AIN10=PA6 电池；AIN11=PA7 上行 VBUS | 采样前关闭数字输入/上拉，按 ADC PGA 范围核算分压 |',
      '| LSE | PA10/PA11 可选晶振 | 与现有 LSI 模式二选一；没有分配给其他信号 |',
      '', 'PB16 不支持 ADC。Rev A 的 USB-A 电压模拟采样不再占用 PA4：PA4/PA5 用于独立 UART3；本提案使用 HOST_FAULT_N 报告开关故障，FAULT# 不能代替 5 V 电压测量。ETA9697 的 STAT 接 PB9；PB8/PB17 预留给高电流分支 EN/故障，不能假定为 BQ24074 的 EN1/EN2；若还要求 USB-A 电压数值，需加模拟开关复用 ADC 或外置 ADC，不能把 PB16 标成 ADC。',
      '', '## Rev B-IR 红外电路连接','',
       '- U9 基线改为带 `IREDC` 阴极引出的 TFBS4650（侧视封装）：一个器件内仍同时包含 IRED、PIN 光电二极管和接收 ASIC，只保留一个朝外光窗。基线把 U9 `TXD` 直接接到 PB7；`IREDA`、`IREDC`、VCC、去耦、SD、RXD 按制造商资料画出。',
       '- **不放 U12 外部多路器**：PB7/TXD0 是同一根物理线，标准 IrDA 时配置为 UART0 TX，家电遥控时配置为 PWM9/定时器输出载波与包络；这是 CH582M 内部复用和固件状态机完成的互斥，不需要 PB3 选择脚。',
       '- `IREDC`/Q_IR/R_IR 只保留为可选 DNP 增强支路。若实测直接 TXD 的光强、脉宽或外部 IRED 电流不足，再用 PB0/PWM6 通过栅极电阻驱动 Q_IR；装配该支路时，固件必须先让 PB7/TXD0 进入安全低电平，避免两种发射路径叠加。',
       '- U9 RXD 为共用接收节点 `IR_RX_RAW`：直接送 PB4/RXD0，另以高阻/0 Ω 可选分支送 PB1 捕获。遥控学习记录 U9 的原始载波脉冲串，不需要 TSOP；不能让两个推挽输出同时驱动该节点。',
       '- 标准 IrDA 和遥控发射都由 MCU 软件/外设完成；MCP2120 只作为可选 DNP 物理层编码器，若装配则用 0 Ω 选择 TX 源，不能与 PB7 推挽输出硬并联。',
       '- TFBS4650 的 TXD 脉冲宽度窗口覆盖常见 38 kHz、1/3–1/2 占空比载波高电平，但最终光强、距离、学习灵敏度仍需实物验证；同一光窗在物理层仍是半双工。',
      '- U9 的 TX/RX 不能同时当作无回声全双工链路；发射期间 RXD 会回显/饱和，固件须屏蔽回显并在发射结束后按数据手册留出接收恢复时间。',
      '- TFBS4650 内置 IRED 峰值约 870–910 nm；若产品必须使用 940 nm 外部发射器，应在 IREDC 支路改接经验证的外部 IRED，并重新做光学窗口、限流、热和眼安全评估。TFBS4711 可作为紧凑的 IrDA-only 备选，但因无 IREDC 不能作为本 Rev B-IR 遥控发射基线。',
      '', '## 与当前固件/Rev A 的差异','',
      '- 保留 USB 两组固定引脚、PS/2 PA0–PA3、SPI0 PA12–PA15、RS232 UART1、调试与 BOOT/RESET。',
      '- VBAT_SENSE：PA6/AIN10；PA7/AIN11 新增上行 VBUS 检测；PA4/PA5 改为 UART3 板边直角排母。',
      '- USER：PB18；ETA9697 STAT：PB9；PB8/PB17 预留给高电流分支 EN/故障；PB16 保留数字测试点，不作为 ADC/PGOOD。',
      '- U5 计划由 SY6280 改为 TPS2553，新增 PB5 FAULT#，需要新封装/参数，绝不是直接替换料号。',
       '- 当前运行固件仍是旧的 UART0/MCP2120/TFBS4711 + PB1 TSOP/PB0 独立 LED 原型；单模块方案需要迁移到 PB7 直连 TXD、PB4 直连 RXD、可选 PB1 捕获和 MCU 内部 UART/PWM 模式状态机后，才能宣称软件与硬件一致。',
       '', '依据：[WCH CH583/CH582 官方资料仓库](https://github.com/openwch/ch583)及 CH582M 数据手册引脚复用；[Vishay TFBS4650 数据手册](https://www.vishay.com/docs/84672/tfbs4650.pdf)；[Vishay IrDA 收发器应用笔记](https://www.vishay.com/doc/?82610=)；[Microchip MCP2120 数据手册](https://ww1.microchip.com/downloads/en/devicedoc/21618b.pdf)。其余元件资料见 `pcb-design-study.md`。','']
    (ROOT/'pin-allocation-revb.md').write_text('\n'.join(lines),encoding='utf-8')

def validate():
    assert [p[0] for p in PINS] == list(range(1,50))
    gpio=[p[1].split('/')[0] for p in PINS if p[1].startswith(('PA','PB'))]
    assert len(gpio)==len(set(gpio))==40
    for ref,_,_,x,y,w,h,*rest in PLACEMENTS:
        assert x>=0 and y>=0 and x+w<=W+.001 and y+h<=H+.001,ref
        assert not (x < ANT['x']+ANT['w'] and x+w>ANT['x'] and y<ANT['h'] and y+h>0),f'{ref} violates antenna reservation'
    for ref,_,_,x,y,w,h,*rest in SIDE_HEADERS:
        assert x>=0 and y>=0 and x+w<=W+.001 and y+h<=H+.001,ref
        assert not (x < ANT['x']+ANT['w'] and x+w>ANT['x'] and y<ANT['h'] and y+h>0),f'{ref} violates antenna reservation'
    for ref,_,_,x,y,w,h,*rest in BOTTOM_ZONES:
        assert x>=0 and y>=0 and x+w<=W+.001 and y+h<=H+.001,ref
    for ref,x,y,r in MOUNTING_HOLES:
        assert r>0 and x-2.8>=0 and y-2.8>=0 and x+2.8<=W and y+2.8<=H,ref
    def hole_hits_box(hx,hy,hr,box):
        _,_,_,x,y,w,h,*_=box
        dx=max(x-hx,0,hx-(x+w)); dy=max(y-hy,0,hy-(y+h))
        return dx*dx+dy*dy < hr*hr
    hole_conflicts=[]
    for hole_ref,hx,hy,_ in MOUNTING_HOLES:
        for box in PLACEMENTS+SIDE_HEADERS:
            if hole_hits_box(hx,hy,2.8,box): hole_conflicts.append((hole_ref,box[0]))
    assert not hole_conflicts,hole_conflicts
    overlaps=[]
    for i,a in enumerate(PLACEMENTS):
        for b in PLACEMENTS[i+1:]:
            if a[3]<b[3]+b[5] and a[3]+a[5]>b[3] and a[4]<b[4]+b[6] and a[4]+a[6]>b[4]:
                overlaps.append((a[0],b[0]))
    assert not overlaps,overlaps
    for f in ('pcb-concept-revb.svg','pcb-bottom-revb.svg','pcb-stack-revb.svg','pinout-revb.svg'):ET.parse(ROOT/f)
    report=dict(pin_rows=49,unique_gpio=40,planning_envelopes=len(PLACEMENTS),envelope_overlaps=overlaps,
      side_headers=len(SIDE_HEADERS),bottom_user_zones=len(BOTTOM_ZONES),mounting_holes=len(MOUNTING_HOLES),
      mounting_hole_keepout_conflicts=hole_conflicts,
      antenna_envelope_conflicts=0,board_mm=[W,H],mcu_mm=[CX,CY],
      mechanical_overlay=dict(basin_mm=[BASIN['x'],BASIN['y'],BASIN['w'],BASIN['h']],battery_pocket_mm=[BATTERY_POCKET['x'],BATTERY_POCKET['y'],BATTERY_POCKET['w'],BATTERY_POCKET['h']]),
      views=['pcb-concept-revb.svg','pcb-bottom-revb.svg','pcb-stack-revb.svg','pinout-revb.svg'],
      verified_scope='Automated: pin uniqueness, envelope bounds/non-overlap, SVG XML. Peripheral mapping reviewed manually against WCH datasheet and SDK. Not ERC, DRC, SI, RF or assembly sign-off.',
      comparisons=[comparison(a) for a in (0,45,-45,90,180,270)])
    (ROOT/'concept-checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='comparisons'},ensure_ascii=False,indent=2))

if __name__=='__main__':
    ROOT.mkdir(parents=True,exist_ok=True)
    (ROOT/'pcb-concept-revb.svg').write_text(make_board(),encoding='utf-8')
    (ROOT/'pcb-bottom-revb.svg').write_text(make_bottom(),encoding='utf-8')
    (ROOT/'pcb-stack-revb.svg').write_text(make_stack(),encoding='utf-8')
    (ROOT/'pinout-revb.svg').write_text(make_pinout(),encoding='utf-8')
    write_table()
    placement=dict(board_mm=[W,H],origin='top-left, x right, y down, top view',rotation='0 deg = pin1 SW',
        antenna_reservation_mm=ANT,
        components=[dict(zip(['ref','label','detail','x_mm','y_mm','w_mm','h_mm','group','facing'],p)) for p in PLACEMENTS],
        side_headers=[dict(zip(['ref','label','detail','x_mm','y_mm','w_mm','h_mm','group','facing'],p)) for p in SIDE_HEADERS],
        bottom_zones=[dict(zip(['ref','label','detail','x_mm','y_mm','w_mm','h_mm','group'],p)) for p in BOTTOM_ZONES],
        mounting_holes=[dict(ref=ref,x_mm=x,y_mm=y,initial_radius_mm=r) for ref,x,y,r in MOUNTING_HOLES],
        mechanical_overlay=dict(basin_mm=BASIN,battery_pocket_mm=BATTERY_POCKET,bottom_svg='pcb-bottom-revb.svg',stack_svg='pcb-stack-revb.svg'))
    (ROOT/'placement-revb.json').write_text(json.dumps(placement,ensure_ascii=False,indent=2),encoding='utf-8')
    validate()
