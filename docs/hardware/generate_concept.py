"""Generate the Rev B placement study, not manufacturing/EDA data.

Coordinates: top view, origin at board upper left, x right, y down, mm.
Package geometry follows WCH CH583DS1 v1.9 p111 (10/14/10/14 pads).
Run with Python 3; standard library only. Rendering is a separate QA step.
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
              debug="#64748b", spare="#87929e")

# pin, silicon name, selected function/net, class, electrical/implementation note
PINS = [
 (1,"VDCID","CH_VDCI","power","内部电源节点；连接 VDCIA；按 DCDC/LDO 模式去耦"),
 (2,"VSW","CH_VSW","power","到本地电感再到 VDCID；是否启用内部 DCDC 与固件一致"),
 (3,"VIO33/VDD33","3V3","power","电源输入；贴近引脚去耦"),
 (4,"PA7/AIN11","USB_C_VBUS_ADC","power","新增：VBUS_RAW 分压/RC；禁止 5 V 直连"),
 (5,"PA8/RXD1","RS232_RX_TTL","uart","UART1 默认映射；MAX3232 ROUT1 到 MCU"),
 (6,"PA9/TXD1","RS232_TX_TTL","uart","UART1 默认映射；MCU 到 MAX3232 DIN1"),
 (7,"PB9","CHG_N","power","BQ24074 CHG#；开漏，外部上拉到 3V3"),
 (8,"PB8","CHG_EN1","power","BQ24074 EN1；外部下拉，上电默认 USB100"),
 (9,"PB17","CHG_EN2","power","BQ24074 EN2；外部下拉，上电默认 USB100"),
 (10,"PB16","INPUT_PGOOD_N","power","BQ24074 PGOOD#；数字输入，非 ADC"),
 (11,"PB15/TCK","WCH_TCK","debug","保留 WCH 两线调试，不复用 SPI"),
 (12,"PB14/TIO","WCH_TIO","debug","保留 WCH 两线调试，不复用 SPI"),
 (13,"PB13/U2D+","USB_HOST_DP_MCU","usb","USB2 Host，固定数据引脚"),
 (14,"PB12/U2D-","USB_HOST_DN_MCU","usb","USB2 Host；禁止 I2C 默认映射占用"),
 (15,"PB11/UD+","USB_DEV_DP_MCU","usb","USB Device，固定数据引脚"),
 (16,"PB10/UD-","USB_DEV_DN_MCU","usb","USB Device，固定数据引脚"),
 (17,"PB7/TXD0","IRDA_UART_TX","ir","到 MCP2120 pin12 TX；UART0 默认映射"),
 (18,"PB6","HOST_EN","power","TPS2553 EN，高有效；外部下拉，上电关断"),
 (19,"PB5","HOST_FAULT_N","power","TPS2553 FAULT#；外部上拉到 3V3；不是电压合格指示"),
 (20,"PB4/RXD0","IRDA_UART_RX","ir","来自 MCP2120 pin11 RX；UART0 默认映射"),
 (21,"PB3","IRDA_CODEC_EN","ir","MCP2120 pin13 EN，高有效；外部下拉"),
 (22,"PB2","IRDA_MODE","ir","MCP2120 pin7 MODE；高为数据，低为配置"),
 (23,"PB1","IR_REMOTE_RX","ir","TSOP38438 OUT；GPIOB 边沿中断，软件切换极性并记时间戳"),
 (24,"PB0/PWM6","IR_REMOTE_TX","ir","PWM6 约 38 kHz，经 MOSFET 驱动独立 940 nm LED"),
 (25,"PB23/RST#","RESET_N","debug","低有效复位；按键与调试口共用，不重映射 UART2"),
 (26,"PB22","BOOT_N","debug","低有效下载按键；保留 ISP 配置"),
 (27,"PB21/SCL_","I2C_SCL","i2c","I2C 必须重映射 RB_PIN_I2C=1"),
 (28,"PB20/SDA_","I2C_SDA","i2c","裸 I2C/OLED 共用 J7，外部上拉到 3V3"),
 (29,"PB19","IRDA_SD","ir","TFBS4711 pin4 SD，高关断；外部上拉"),
 (30,"PB18","USER_N","debug","用户键迁至此；外部上拉，按下接地"),
 (31,"X32MO","X32MO","rf","32 MHz 晶振；负载由晶体 CL 与寄生计算"),
 (32,"X32MI","X32MI","rf","32 MHz 晶振；最短回路，与 RF 馈线分开"),
 (33,"VINTA","CH_VINTA","power","内部模拟节点；只按手册去耦，不作为外部负载电源"),
 (34,"ANT","RF_ANT","rf","向北，50 ohm 馈线；可选调谐焊盘贴近馈点"),
 (35,"VDCIA","CH_VDCI","power","按 WCH 参考与 VDCID 相连并本地去耦"),
 (36,"PA4/RXD3","UART_TTL_RX","uart","新增 J10 裸串口；UART3 默认映射"),
 (37,"PA5/TXD3","UART_TTL_TX","uart","新增 J10 裸串口；MCU 视角命名，3.3 V"),
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

# These are conservative planning envelopes, not exact land patterns.
PLACEMENTS = [
 ("J3","PS/2 键盘","Mini-DIN-6",0,8,15,15,"ps2","west"),
 ("J4","PS/2 鼠标","Mini-DIN-6",0,27,15,15,"ps2","west"),
 ("J1","USB-C","Device / 5 V IN",76.6,12,9,10,"usb","east"),
 ("J2","USB-A","Host / 5 V OUT",73.6,25,12,16,"usb","east"),
 ("J5","RS232 · DB9 公座","DTE · TX / RX / GND",26,39.98,32,14,"uart","south"),
 ("J6","1S BAT","带保护电池",78,43.5,7.6,8,"power","east"),
 ("J7","I²C / OLED","1×4 · 3V3",50,11.5,11,4,"i2c","up"),
 ("J8","SPI0","2×4 · 3V3",5,46.5,11,5.5,"spi","up"),
 ("J9","WCH-Link","2×3 · VTref",53,34,8,5.5,"debug","up"),
 ("J10","UART3 TTL","1×4 · 3V3",20,2,11,4,"uart","up"),
 ("U9","IrDA SIR","TFBS4711",61,0,6.5,3.5,"ir","north"),
 ("U11","38 kHz RX","TSOP38438",70,0,5.5,5.5,"ir","north"),
 ("D_IR","940 nm TX","独立 LED",77,0,5,7,"ir","north"),
 ("U10+X2","MCP2120 + 7.3728 MHz","IrDA 编解码 / 本地去耦",62,7,13,11,"ir","zone"),
 ("Q_IR","LED 驱动","MOSFET / 限流",77,8,6,3,"ir","zone"),
 ("PS2-K","BSS138 ×2","上拉 / ESD / 限流",17,11,9,9,"ps2","zone"),
 ("PS2-M","BSS138 ×2","上拉 / ESD / 限流",17,29,9,9,"ps2","zone"),
 ("U6","MAX3232E","电荷泵电容",35,32,12,6.5,"uart","zone"),
 ("U4","TPS61023","5V_HOST + L/C",63,32.5,8.5,8.5,"power","zone"),
 ("U3","TPS63031","3V3 + L/C",60,43,8,9,"power","zone"),
 ("U2","BQ24074","充电 / SYS",69,43,8,9,"power","zone"),
 ("U5","TPS2553","限流 / FAULT#",62,25,6.5,3,"power","zone"),
 ("U7","ESD-C","短接地",72,19,3.5,3.5,"usb","zone"),
 ("U8","ESD-A","短接地",70,29,3,3,"usb","zone"),
 ("X1","32 MHz","晶振及负载",43,17,6,4,"rf","zone"),
 ("RF-M","RF 调谐","预留焊盘",39,16.5,3,3.5,"rf","zone"),
 ("LSE","32.768k","可选 DNP",34,28.5,6,3,"spare","zone"),
 ("DCDC","U1 去耦 / L","本地回路",35,26,5,2,"power","zone"),
 ("KEYS","USER / RST / BOOT","3 个顶部按键",52,19,11,3.5,"debug","zone"),
 ("U1","CH582M","5×5 mm",CX-2.5,CY-2.5,5,5,"debug","chip"),
]
ANT = dict(x=32,y=0,w=25,h=10)
GROUPS = [
 ("RF",[34],(41,10),"rf"),
 ("USB-A",[13,14],(72,31),"usb"),
 ("USB-C",[15,16],(75,19),"usb"),
 ("PS/2 K",[39,40],(26,16),"ps2"),
 ("PS/2 M",[41,42],(26,33),"ps2"),
 ("SPI0",[43,44,45,46],(16,47),"spi"),
 ("UART3",[36,37],(26,7),"uart"),
 ("RS232",[5,6],(41,32),"uart"),
 ("IrDA",[17,20],(64,18),"ir"),
 ("38 kHz",[23,24],(77,9),"ir"),
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
    svg=start(1600,1110,"信用卡尺寸 CH582M 多接口 PCB 概念布局 Rev B")
    svg += [text(55,52,"CH582M · 多接口 PCB 方位与引脚协同规划",30,weight=700),
            text(55,83,"Rev B / 2026-09-21   ·   顶视图   ·   PCB 85.60 × 53.98 mm   ·   建议 4 层",16,color="#627186"),
            text(55,110,"布局示意：矩形为器件/电路预留区，彩线为信号通道；不是可制造的封装、走线或天线图形。",15,color="#627186")]
    OX,OY,S=70,190,12
    P=lambda x,y:(OX+x*S,OY+y*S)
    R=lambda x,y,w,h,fill,stroke="none",r=0,extra="":rect(*P(x,y),w*S,h*S,fill,stroke,r,extra)
    T=lambda x,y,s,size=13,color="#243447",anchor="middle",weight=400: text(*P(x,y),s,size,color,anchor,weight)
    svg += [R(0,0,W,H,"#edf5f0","#547466",30,'stroke-width="2"'),
            path([P(0,-3),P(W,-3)],"#718295",1),T(W/2,-4,"85.60 mm",15),
            path([P(-3,0),P(-3,H)],"#718295",1),text(25,OY+H*S/2,"53.98 mm",15,extra=f'transform="rotate(-90 25 {OY+H*S/2})"')]
    # Low-contrast 5 mm placement grid.
    for x in range(5,85,5): svg.append(path([P(x,1),P(x,H-1)],"#cfdfd6",.6,dash="2 5"))
    for y in range(5,50,5): svg.append(path([P(1,y),P(W-1,y)],"#cfdfd6",.6,dash="2 5"))
    svg += [R(**ANT,fill="#fff4d9",stroke="#bd8e27",extra='stroke-dasharray="6 4"'),
            R(**ANT,fill="url(#hatch)"),T(44.5,2.6,"BLE 天线预留区",17,"#835d06",weight=700),
            T(44.5,5.0,"25 × 10 mm · 非天线尺寸",12,"#835d06"),
            T(44.5,7.2,"除天线本体外，全层净空",12,"#835d06"),
            T(44.5,9.1,"电池 / 金属外壳 / 导线避让",11,"#835d06")]
    # Signal corridors. Exact endpoint escape is shown in the pinout SVG.
    routes=[
      ([(41.425,22),(41,20),(41,10)],"rf"),
      ([(45.5,25.9),(49,28.8),(66,28.8),(70,30.5),(73.6,30.5)],"usb"),
      ([(45.5,25.0),(50,25),(52,23),(69,23),(72,20.5),(76.6,20.5)],"usb"),
      ([(40.5,23.8),(34,23.8),(30,16),(26,16),(15,16)],"ps2"),
      ([(40.5,24.5),(32,24.5),(28,33),(26,33),(15,33)],"ps2"),
      ([(40.5,25.7),(29,25.7),(28,40),(20,46),(16,48)],"spi"),
      ([(40.5,22.9),(32,17),(30,7),(26,6)],"uart"),
      ([(43,27),(43,32),(41,39.98)],"uart"),
      ([(45.5,24),(49.5,20),(49.5,17.5),(62,17.5)],"ir"),
      ([(45.5,22.35),(49.5,17),(49.5,10.8),(59,10.8),(59,6),(73,6),(77,8)],"ir"),
      ([(44,22),(50,16),(53,15.5)],"i2c"),
      ([(45.5,26.6),(49,32),(53,35)],"debug"),
    ]
    for pts,col in routes: svg.append(path([P(*p) for p in pts],COLORS[col],2.6,dash="6 3" if col=="debug" else ""))
    # Power distribution is indicative and does not prescribe copper widths.
    power=[[(77,21),(77,42),(73,43)],[(73,43),(69,41),(67,41)],[(69,47),(68,47)],
           [(63,43),(56,40),(49,30)],[(63,37),(29,39),(16,43),(16,10),(17,14)]]
    for pts in power: svg.append(path([P(*p) for p in pts],"#ae8245",3.2,"3 5",'opacity="0.6"'))
    for ref,label,sub,x,y,w,h,kind,edge in PLACEMENTS:
        if edge=="chip": continue
        c=COLORS[kind]
        fill={"usb":"#e4efff","ps2":"#dff2ec","uart":"#fcece2","ir":"#fce7f0","spi":"#eee8fc","power":"#f3eadd","i2c":"#e1f0f4","debug":"#e7ebf1","spare":"#f0f1f3","rf":"#fff0cd"}[kind]
        svg.append(R(x,y,w,h,fill,c,5,'stroke-width="1.4"'))
        # Compact blocks use two lines; larger envelopes use three.
        if ref in ("U9","U11","D_IR"):
            optical = {"U9":("IrDA","TFBS4711"),"U11":("38k RX","TSOP38438"),"D_IR":("IR TX","940 nm")}[ref]
            svg += [T(x+w/2,y+1.4,optical[0],10,c,weight=700),T(x+w/2,y+2.65,optical[1],8,c)]
        elif ref=="U10+X2":
            svg += [T(x+w/2,y+2.8,"IrDA 编解码",13,c,weight=700),T(x+w/2,y+5,"MCP2120",15,c,weight=700),T(x+w/2,y+7.1,"+ 7.3728 MHz",12,c),T(x+w/2,y+9.2,"电路 / 晶体 / 去耦区",10,c)]
        elif ref=="DCDC":
            svg += [T(x+w/2,y+1.3,"U1 L/C",10,c)]
        elif ref=="LSE":
            svg += [T(x+w/2,y+1.25,"32.768k",10,c),T(x+w/2,y+2.45,"可选 DNP",8,c)]
        elif h <= 4:
            svg += [T(x+w/2,y+1.6,label,11,c,weight=700),T(x+w/2,y+3.0,sub,9,c)]
        elif w <= 8.5:
            svg += [T(x+w/2,y+1.7,ref,11,c,weight=700),T(x+w/2,y+3.4,label,11,c),T(x+w/2,y+5.0,sub,9,c)]
        else:
            svg += [T(x+w/2,y+h/2-1.5,ref,12,c,weight=700),T(x+w/2,y+h/2+.5,label,16,c,weight=700),T(x+w/2,y+h/2+2.3,sub,11,c)]
        if edge in ("west","east","north","south"):
            if edge=="west": a,b=(x,y+h/2),(x-2,y+h/2)
            elif edge=="east": a,b=(x+w,y+h/2),(x+w+2,y+h/2)
            elif edge=="north": a,b=(x+w/2,y),(x+w/2,y-2)
            else: a,b=(x+w/2,y+h),(x+w/2,y+h+2)
            svg.append(path([P(*a),P(*b)],"#60748b",1.5,extra='marker-end="url(#arrow)"'))
    # Real 5 x 5 package outline and real unequal side pad counts.
    svg.append(R(CX-2.5,CY-2.5,5,5,"#263b45","#172d36",3))
    for pin,*_ in PINS[:48]:
        (px,py),n,side=pad(pin)
        svg.append(path([P(CX+px,CY+py),P(CX+px+n[0]*.55,CY+py+n[1]*.55)],"#b69c5a",2.1))
    svg += [T(CX,CY-.1,"CH582M",10,"#ffffff",weight=700),T(CX,CY+1.2,"U1 · 0°",9,"#c8e1da"),
            f'<circle cx="{P(CX-1.8,CY+1.8)[0]}" cy="{P(CX-1.8,CY+1.8)[1]}" r="2.5" fill="#fce59a"/>',
            T(48,31.0,"中央略偏北",10,"#516b61"),
            T(33.5,14.1,"UART3 / ADC",11,COLORS['uart']),
            T(30,43.0,"SPI 向西南扇出",11,COLORS['spi']),
            T(46.0,12.1,"50 Ω",11,COLORS['rf'])]
    # Right hand reading guide.
    sx=1140
    svg += [rect(sx,160,405,676,"#ffffff","#dce3ed",16),text(sx+24,200,"为什么选正放 0°",23,weight=700)]
    notes=[("北侧 · 固定 RF 优先","ANT34 与 32 MHz 晶振朝北；","天线区不放电池、金属件或排针。"),
           ("东侧 · 两组 USB + 红外","USB-C 在上、USB-A 在下，","对应 15/16 与 13/14 的引脚顺序。"),
           ("西侧 · PS/2 + SPI + TTL","保留 PA0–PA3 与 SPI0 默认脚；","UART3 从左上角出线到裸排针。"),
           ("南侧 · RS232 + 电源","UART1 向下经过 MAX3232 到 DB9；","充电与升降压集中在右下角。"),
           ("红外两套硬件独立","IrDA：UART0 + MCP2120 + TFBS4711；","遥控：PWM6 + LED，TSOP38438 接收。"),
           ("4 层可减少低速跨线约束","L2 保持连续地；少数电源检测线","可走底层，不必强求全板零交叉。")]
    yy=242
    for title,l1,l2 in notes:
        svg += [text(sx+24,yy,title,16,weight=700),text(sx+24,yy+24,l1,14,"#617086"),text(sx+24,yy+45,l2,14,"#617086")]
        yy+=91
    svg += [text(70,901,"角度比较 · 相同接口位置，相同 U1 中心",22,weight=700)]
    for i,a in enumerate((0,45,-45)):
        c=comparison(a); bx=70+i*353
        svg += [rect(bx,923,330,92,"#ffffff","#cfdae5",10),text(bx+18,951,f'{a:+d}°' if a else '0° · 推荐',21,weight=700),
                text(bx+18,977,f"平均出脚偏角 {c['mean_departure_angle_deg']:.1f}°",15),
                text(bx+18,998,f"背向目的区 {c['backward_groups']} / 11 组",13,"#627186")]
    svg += [text(70,1045,"比较指标只衡量首段扇出方向，不代表实际线长、过孔数量或 DRC。芯片旋转 ±45° 的包络从 5 mm 增到 7.07 mm。",14,"#627186"),
            text(70,1074,"板材外形按信用卡尺寸规划；连接器高度、外伸插头与电池体积另计。完整引脚表见 pin-allocation-revb.md / .csv。",14,"#627186")]
    svg += ["</g></svg>"]
    return "\n".join(svg)

def make_pinout():
    svg=start(1540,1270,"CH582M Rev B 完整引脚分配与封装方向")
    svg += [text(55,50,"CH582M · Rev B 引脚分配",30,weight=700),
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
            text(55,1210,"本表是布局提案，不表示云端原理图或现有固件已经更新。所有排针信号均为 3.3 V 逻辑。",16,"#627186"),"</g></svg>"]
    return "\n".join(svg)

def write_table():
    header=['pin','silicon','side_top_view','net','group','note']
    with (ROOT/'pin-allocation-revb.csv').open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.writer(f);w.writerow(header)
        for p,s,n,g,note in PINS:w.writerow([p,s,pad(p)[2],n,g,note])
    lines=['# CH582M Rev B 引脚分配提案','',
      '日期：2026-09-21。配套布局：`pcb-concept-revb.svg`；顶视放大图：`pinout-revb.svg`。',
      '这是新的硬件提案，不覆盖 Rev A 云端工程或当前 `src/board_pins.h`。','',
      '0° 定义：数据手册第 111 页 Top View，Pin 1 在左下。1–10 南侧从左到右；11–24 东侧从下到上；25–34 北侧从右到左；35–48 西侧从上到下。EP 在手册标 0，本提案 CSV 以库常用的 49 表示。','',
      '| Pin | 芯片功能 | 物理侧 | Rev B 网络 | 用途/约束 |',
      '|---:|---|---|---|---|']
    for p,s,n,g,note in PINS:lines.append(f'| {p} | {s} | {pad(p)[2]} | `{n}` | {note} |')
    lines += ['', '## 接插件定义（均按连接器自身脚号，不能按焊接面视觉猜编号）','',
      '| 接口 | 接线定义 |', '|---|---|',
      '| J1 USB-C | A4/A9/B4/B9 = VBUS_RAW；A6/B6 = D+；A7/B7 = D−；CC1/CC2 各自独立 5.1 kΩ 到地；SBU NC；GND 与壳体接法按 ESD 设计 |',
      '| J2 USB-A | 1 VBUS_HOST；2 D−；3 D+；4 GND；壳体接屏蔽/地方案 |',
      '| J3/J4 Mini-DIN-6 | 1 DATA；2 NC；3 GND；4 受保护的 5V_PS2_K/M；5 CLK；6 NC；外壳地；必须核对实际座子的 mating-face 与 PCB-side 图 |',
      '| J5 DB9 公座 DTE | 2 RX（到 MAX3232 RIN1）；3 TX（来自 DOUT1）；5 GND；1/4/6/7/8/9 NC；不提供 RTS/CTS 硬件握手 |',
      '| J6 电池 | 1 BAT+；2 GND（本板自定义，不能假定成品电池线颜色/极性一致）；外接带保护 1S 电池，NTC 另用焊盘连接 |',
      '| J7 I2C/OLED 1×4 | 1 GND；2 3V3_OUT；3 SCL；4 SDA。一个共享硬件 I2C 总线 |',
      '| J8 SPI0 2×4 | 1 3V3_OUT；2 GND；3 SCK；4 GND；5 MOSI；6 MISO；7 CS#；8 GND。顶视左列 1/3/5/7、右列 2/4/6/8 |',
      '| J9 WCH-Link 2×3 | 1 3V3_VTref；2 GND；3 TCK；4 TIO；5 RESET#；6 GND。目标板自行供电，VTref 不与调试器电源硬并联 |',
      '| J10 UART3 1×4 | 1 GND；2 3V3_OUT；3 TX（MCU 输出）；4 RX（MCU 输入）。不是 RS232 电平 |',
      '', 'J7/J10 的 pin1 均在图示横排左端；J8/J9 的 pin1 在左上。正式 footprint 应以方焊盘、丝印和 3D 模型再次确认。',
      '', '## 外设资源与复用','',
      '| 资源 | 选择 | 冲突处理 |','|---|---|---|',
      '| USB / USB2 | Device PB11/PB10；Host PB13/PB12 | 不占用作 UART1、SPI0 或 I2C 默认脚 |',
      '| UART0 | PB7 TX / PB4 RX，IrDA 编解码器 | RB_PIN_UART0=0；MODEM 功能关闭，PB0–PB6 按表分配 |',
      '| UART1 | PA9 TX / PA8 RX，RS232 | RB_PIN_UART1=0；现有 UART1 debug 输出需防止污染业务串口 |',
      '| UART2 | 不启用 | 默认 PA6/PA7 用作 ADC；重映射 PB22/PB23 保留给 BOOT/RESET |',
      '| UART3 | PA5 TX / PA4 RX，裸串口 | RB_PIN_UART3=0；PB20/PB21 留给 I2C |',
      '| SPI0 | PA12–PA15 默认组 | RB_PIN_SPI0=0；SPI1 禁用（其 PA0–PA2 已是 PS/2） |',
      '| I2C | PB21 SCL / PB20 SDA | RB_PIN_I2C=1，否则与 USB2 冲突 |',
      '| PWM6 | PB0，38 kHz 遥控载波 | 只使能 PWM6，不开启与 UART/SPI/ADC 重叠的其他 PWM 输出 |',
      '| GPIO 中断 / 时间基准 | PS/2 GPIOA；遥控接收 GPIOB PB1 | 遥控只在边沿记录时间；使用可用自由运行计时器，不在 ISR 解码 |',
      '| ADC | AIN10=PA6 电池；AIN11=PA7 上行 VBUS | 采样前关闭数字输入/上拉，按 ADC PGA 范围核算分压 |',
      '| LSE | PA10/PA11 可选晶振 | 与现有 LSI 模式二选一；没有分配给其他信号 |',
      '', 'PB16 不支持 ADC。Rev A 的 USB-A 电压模拟采样不再占用 PA4：PA4/PA5 用于独立 UART3；本提案使用 HOST_FAULT_N 报告开关故障，FAULT# 不能代替 5 V 电压测量。若还要求 USB-A 电压数值，需加模拟开关复用 ADC 或外置 ADC，不能把 PB16 标成 ADC。',
      '', '## 新增红外电路连接','',
      '- MCP2120：pin12 TX ← PB7；pin11 RX → PB4；pin7 MODE ← PB2；pin13 EN ← PB3；pin4 RESET 接 RESET_N；pin1 VDD=3V3、pin14 GND。',
      '- MCP2120 的 pin2/3 接独立 7.3728 MHz 晶体及计算后的负载；pin8/9/10（BAUD2/1/0）拉高选择软件速率配置，上电先按 9600 bps 初始化，再按数据手册切换。',
      '- MCP2120 pin6 TXIR → TFBS4711 pin2 TXD；TFBS4711 pin3 RXD → MCP2120 pin5 RXIR；TFBS4711 pin4 SD ← PB19。TFBS4711 pin5 用 3V3，pin1 LED 电源按其推荐电路及脉冲负载配置，pin6 GND。',
      '- TFBS4711 是 SIR 光学物理层，MCP2120 是脉冲编解码；完整 IrDA 协议如 IrLAP/IrLMP 不会由这两颗芯片自动提供，需要固件实现。',
      '- TSOP38438：pin1 OUT → PB1，pin2 GND，pin3=3V3 经本地滤波；其已解调输出用于 NEC/RC5 等包络接收。GPIOB 没有原生双边沿模式，ISR 要根据输入电平切换下一边沿极性，并检查竞态/丢沿与最坏中断延迟。',
      '- PB0/PWM6 → 栅极电阻 → 逻辑级 NMOS → 独立 940 nm LED，栅极下拉；LED 串联电阻和电源按允许的脉冲电流计算，禁止 GPIO 直接驱动大电流。',
      '- 两套光学器件彼此分隔并预留遮光隔墙。IrDA 和遥控都能实现，但同一时刻发射需仲裁，不能保证同方向同时光学通信互不干扰。',
      '', '## 与当前固件/Rev A 的差异','',
      '- 保留 USB 两组固定引脚、PS/2 PA0–PA3、SPI0 PA12–PA15、RS232 UART1、调试与 BOOT/RESET。',
      '- VBAT_SENSE：PB16 → PA6/AIN10；PA7/AIN11 新增上行 VBUS 检测；PA4/PA5 改为 UART3 裸排针。',
      '- USER：PB8 → PB18；CHG#：PB19 → PB9；PB8/PB17 新增充电模式控制；PB16 改为输入电源有效状态。',
      '- U5 计划由 SY6280 改为 TPS2553，新增 PB5 FAULT#，需要新封装/参数，绝不是直接替换料号。',
      '- 新增 UART0 + IrDA、PWM6 + 红外收发、UART3/I2C/SPI 初始化及相应协议。当前固件未实现这些新增功能；本次未修改运行固件。',
      '', '依据：[WCH CH583DS1 v1.9](https://github.com/openwch/ch583/blob/main/Datasheet/CH583DS1_zh.PDF) 第 3–7 页引脚说明、第 111 页封装图及相关外设章节，以及仓库 `StdPeriphDriver/inc/CH583SFR.h` 的复用定义。其余元件资料见 `pcb-design-study.md`。','']
    (ROOT/'pin-allocation-revb.md').write_text('\n'.join(lines),encoding='utf-8')

def validate():
    assert [p[0] for p in PINS] == list(range(1,50))
    gpio=[p[1].split('/')[0] for p in PINS if p[1].startswith(('PA','PB'))]
    assert len(gpio)==len(set(gpio))==40
    for ref,_,_,x,y,w,h,*rest in PLACEMENTS:
        assert x>=0 and y>=0 and x+w<=W+.001 and y+h<=H+.001,ref
        assert not (x < ANT['x']+ANT['w'] and x+w>ANT['x'] and y<ANT['h'] and y+h>0),f'{ref} violates antenna reservation'
    overlaps=[]
    for i,a in enumerate(PLACEMENTS):
        for b in PLACEMENTS[i+1:]:
            if a[3]<b[3]+b[5] and a[3]+a[5]>b[3] and a[4]<b[4]+b[6] and a[4]+a[6]>b[4]:
                overlaps.append((a[0],b[0]))
    assert not overlaps,overlaps
    for f in ('pcb-concept-revb.svg','pinout-revb.svg'):ET.parse(ROOT/f)
    report=dict(pin_rows=49,unique_gpio=40,planning_envelopes=len(PLACEMENTS),envelope_overlaps=overlaps,
      antenna_envelope_conflicts=0,board_mm=[W,H],mcu_mm=[CX,CY],
      verified_scope='Automated: pin uniqueness, envelope bounds/non-overlap, SVG XML. Peripheral mapping reviewed manually against WCH datasheet and SDK. Not ERC, DRC, SI, RF or assembly sign-off.',
      comparisons=[comparison(a) for a in (0,45,-45,90,180,270)])
    (ROOT/'concept-checks.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='comparisons'},ensure_ascii=False,indent=2))

if __name__=='__main__':
    ROOT.mkdir(parents=True,exist_ok=True)
    (ROOT/'pcb-concept-revb.svg').write_text(make_board(),encoding='utf-8')
    (ROOT/'pinout-revb.svg').write_text(make_pinout(),encoding='utf-8')
    write_table()
    placement=dict(board_mm=[W,H],origin='top-left, x right, y down, top view',rotation='0 deg = pin1 SW',
        antenna_reservation_mm=ANT,components=[dict(zip(['ref','label','detail','x_mm','y_mm','w_mm','h_mm','group','facing'],p)) for p in PLACEMENTS])
    (ROOT/'placement-revb.json').write_text(json.dumps(placement,ensure_ascii=False,indent=2),encoding='utf-8')
    validate()
