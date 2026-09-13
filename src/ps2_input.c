#include "CH58x_common.h"

#include "board_pins.h"
#include "event_router.h"
#include "ps2_input.h"
#include "static_spsc_ring.h"

#define PS2_EDGE_CAPACITY                 64u
#define PS2_EDGE_PROCESS_BUDGET           32u
#define PS2_IDLE_TIMEOUT_TICKS             5u /* 10 ms at the 2 ms TMOS tick */
#define PS2_TRACKED_USAGE_CAPACITY        16u
#define PS2_MOUSE_PACKET_LENGTH            3u

#define PS2_MOUSE_INIT_DELAY_TICKS        10u /* wait 20 ms after reset */
#define PS2_MOUSE_RETRY_DELAY_TICKS      500u /* retry once per second */
#define PS2_TX_IDLE_TIMEOUT_TICKS         10u /* no clock edge for 20 ms */

#define PS2_TX_IDLE                       0u
#define PS2_TX_INHIBIT                    1u
#define PS2_TX_CLOCKING                   2u
#define PS2_TX_WAIT_ACK_RISE              3u
#define PS2_TX_WAIT_ACK_FALL              4u

typedef struct
{
    uint8_t data_level;
    uint8_t flags;
    uint16_t reserved;
} Ps2EdgeSample;

typedef struct
{
    uint8_t bit_index;
    uint8_t data;
    uint8_t data_ones;
    uint8_t parity;
} Ps2BitDecoder;

typedef struct
{
    Ps2BitDecoder frame;
    uint8_t break_pending;
    uint8_t extended_pending;
    uint8_t pause_remaining;
    uint8_t modifiers;
    uint8_t usage_count;
    uint8_t usages[PS2_TRACKED_USAGE_CAPACITY];
    volatile uint8_t idle_ticks;
} Ps2KeyboardDecoder;

typedef struct
{
    Ps2BitDecoder frame;
    uint8_t packet_index;
    uint8_t packet[PS2_MOUSE_PACKET_LENGTH];
    uint8_t idle_ticks;
} Ps2MouseDecoder;

typedef struct
{
    volatile uint8_t state;
    volatile uint8_t edge_seen;
    volatile uint8_t ack_ok;
    volatile uint8_t result_pending;
    uint8_t command;
    uint8_t bit_index;
    uint8_t parity;
    volatile uint8_t idle_ticks;
} Ps2MouseTx;

static Ps2EdgeSample g_ps2_keyboard_edge_storage[PS2_EDGE_CAPACITY]
    EVENT_ROUTER_ALIGN4;
static Ps2EdgeSample g_ps2_mouse_edge_storage[PS2_EDGE_CAPACITY]
    EVENT_ROUTER_ALIGN4;
static StaticSpscRing g_ps2_keyboard_edge_ring;
static StaticSpscRing g_ps2_mouse_edge_ring;

static Ps2KeyboardDecoder g_ps2_keyboard_decoder EVENT_ROUTER_ALIGN4;
static Ps2MouseDecoder g_ps2_mouse_decoder EVENT_ROUTER_ALIGN4;
static Ps2MouseTx g_ps2_mouse_tx EVENT_ROUTER_ALIGN4;

static volatile uint32_t g_ps2_keyboard_edge_overrun;
static volatile uint32_t g_ps2_mouse_edge_overrun;
static volatile uint32_t g_ps2_frame_error;
static volatile uint32_t g_ps2_keyboard_resync;
static volatile uint32_t g_ps2_mouse_resync;
static volatile uint32_t g_ps2_unknown_keyboard_code;
static volatile uint32_t g_ps2_mouse_packet_error;

static uint32_t g_ps2_keyboard_overrun_reported;
static uint32_t g_ps2_mouse_overrun_reported;
static uint16_t g_ps2_mouse_init_delay_ticks;
static uint8_t g_ps2_mouse_init_done;

static void Ps2BitDecoder_Reset(Ps2BitDecoder *decoder)
{
    decoder->bit_index = 0u;
    decoder->data = 0u;
    decoder->data_ones = 0u;
    decoder->parity = 0u;
}

/* Return 0 when the frame is incomplete, 1 for a valid byte and 2 for a
 * parity/start/stop error.  PS/2 transmits the least-significant bit first. */
static uint8_t Ps2BitDecoder_Consume(Ps2BitDecoder *decoder,
                                     uint8_t level,
                                     uint8_t *value)
{
    uint8_t valid;

    if(decoder->bit_index == 0u)
    {
        if(level == 0u)
        {
            decoder->bit_index = 1u;
            decoder->data = 0u;
            decoder->data_ones = 0u;
            decoder->parity = 0u;
        }
        return 0u;
    }

    if(decoder->bit_index <= 8u)
    {
        if(level != 0u)
        {
            decoder->data |= (uint8_t)(1u << (decoder->bit_index - 1u));
            ++decoder->data_ones;
        }
        ++decoder->bit_index;
        return 0u;
    }

    if(decoder->bit_index == 9u)
    {
        decoder->parity = (level != 0u) ? 1u : 0u;
        decoder->bit_index = 10u;
        return 0u;
    }

    valid = (level != 0u) &&
            ((((uint8_t)(decoder->data_ones + decoder->parity)) & 1u) != 0u);
    *value = decoder->data;
    Ps2BitDecoder_Reset(decoder);
    return valid ? 1u : 2u;
}

static void Ps2Keyboard_ResetState(void)
{
    Ps2BitDecoder_Reset(&g_ps2_keyboard_decoder.frame);
    g_ps2_keyboard_decoder.break_pending = 0u;
    g_ps2_keyboard_decoder.extended_pending = 0u;
    g_ps2_keyboard_decoder.pause_remaining = 0u;
    g_ps2_keyboard_decoder.modifiers = 0u;
    g_ps2_keyboard_decoder.usage_count = 0u;
    g_ps2_keyboard_decoder.idle_ticks = 0u;
}

static void Ps2Mouse_ResetState(void)
{
    Ps2BitDecoder_Reset(&g_ps2_mouse_decoder.frame);
    g_ps2_mouse_decoder.packet_index = 0u;
    g_ps2_mouse_decoder.packet[0] = 0u;
    g_ps2_mouse_decoder.packet[1] = 0u;
    g_ps2_mouse_decoder.packet[2] = 0u;
    g_ps2_mouse_decoder.idle_ticks = 0u;
}

static void Ps2Keyboard_EmitReport(void)
{
    HidKeyboardReport report;
    uint8_t i;

    for(i = 0u; i < HID_KEYBOARD_REPORT_LEN; ++i)
    {
        report.bytes[i] = 0u;
    }
    report.bytes[0] = g_ps2_keyboard_decoder.modifiers;

    if(g_ps2_keyboard_decoder.usage_count > 6u)
    {
        for(i = 0u; i < 6u; ++i)
        {
            report.bytes[2u + i] = 0x01u; /* HID ErrorRollOver. */
        }
    }
    else
    {
        for(i = 0u; i < g_ps2_keyboard_decoder.usage_count; ++i)
        {
            report.bytes[2u + i] = g_ps2_keyboard_decoder.usages[i];
        }
    }
    (void)EventRouter_InjectKeyboardReport(ROUTER_SRC_PS2_KEYBOARD, &report);
}

static void Ps2Keyboard_ReleaseAll(void)
{
    Ps2Keyboard_ResetState();
    Ps2Keyboard_EmitReport();
    ++g_ps2_keyboard_resync;
}

static uint8_t Ps2Keyboard_ModifierMask(uint8_t extended, uint8_t code)
{
    if(!extended)
    {
        switch(code)
        {
            case 0x14u: return (1u << 0); /* Left Ctrl. */
            case 0x12u: return (1u << 1); /* Left Shift. */
            case 0x11u: return (1u << 2); /* Left Alt. */
            case 0x59u: return (1u << 5); /* Right Shift. */
            default: break;
        }
    }
    else
    {
        switch(code)
        {
            case 0x14u: return (1u << 4); /* Right Ctrl. */
            case 0x11u: return (1u << 6); /* Right Alt. */
            case 0x1Fu: return (1u << 3); /* Left GUI. */
            case 0x27u: return (1u << 7); /* Right GUI. */
            default: break;
        }
    }
    return 0u;
}

static uint8_t Ps2Keyboard_MapUsage(uint8_t extended,
                                    uint8_t code,
                                    uint8_t *usage)
{
    if(extended)
    {
        switch(code)
        {
            case 0x75u: *usage = 0x52u; return 1u; /* Up. */
            case 0x72u: *usage = 0x51u; return 1u; /* Down. */
            case 0x6Bu: *usage = 0x50u; return 1u; /* Left. */
            case 0x74u: *usage = 0x4Fu; return 1u; /* Right. */
            case 0x6Cu: *usage = 0x4Au; return 1u; /* Home. */
            case 0x69u: *usage = 0x4Du; return 1u; /* End. */
            case 0x7Du: *usage = 0x4Bu; return 1u; /* Page Up. */
            case 0x7Au: *usage = 0x4Eu; return 1u; /* Page Down. */
            case 0x70u: *usage = 0x49u; return 1u; /* Insert. */
            case 0x71u: *usage = 0x4Cu; return 1u; /* Delete. */
            case 0x5Au: *usage = 0x58u; return 1u; /* Keypad Enter. */
            case 0x4Au: *usage = 0x54u; return 1u; /* Keypad /. */
            default: return 0u;
        }
    }

    switch(code)
    {
        case 0x1Cu: *usage = 0x04u; return 1u; /* A */
        case 0x32u: *usage = 0x05u; return 1u; /* B */
        case 0x21u: *usage = 0x06u; return 1u; /* C */
        case 0x23u: *usage = 0x07u; return 1u; /* D */
        case 0x24u: *usage = 0x08u; return 1u; /* E */
        case 0x2Bu: *usage = 0x09u; return 1u; /* F */
        case 0x34u: *usage = 0x0Au; return 1u; /* G */
        case 0x33u: *usage = 0x0Bu; return 1u; /* H */
        case 0x43u: *usage = 0x0Cu; return 1u; /* I */
        case 0x3Bu: *usage = 0x0Du; return 1u; /* J */
        case 0x42u: *usage = 0x0Eu; return 1u; /* K */
        case 0x4Bu: *usage = 0x0Fu; return 1u; /* L */
        case 0x3Au: *usage = 0x10u; return 1u; /* M */
        case 0x31u: *usage = 0x11u; return 1u; /* N */
        case 0x44u: *usage = 0x12u; return 1u; /* O */
        case 0x4Du: *usage = 0x13u; return 1u; /* P */
        case 0x15u: *usage = 0x14u; return 1u; /* Q */
        case 0x2Du: *usage = 0x15u; return 1u; /* R */
        case 0x1Bu: *usage = 0x16u; return 1u; /* S */
        case 0x2Cu: *usage = 0x17u; return 1u; /* T */
        case 0x3Cu: *usage = 0x18u; return 1u; /* U */
        case 0x2Au: *usage = 0x19u; return 1u; /* V */
        case 0x1Du: *usage = 0x1Au; return 1u; /* W */
        case 0x22u: *usage = 0x1Bu; return 1u; /* X */
        case 0x35u: *usage = 0x1Cu; return 1u; /* Y */
        case 0x1Au: *usage = 0x1Du; return 1u; /* Z */

        case 0x16u: *usage = 0x1Eu; return 1u; /* 1 */
        case 0x1Eu: *usage = 0x1Fu; return 1u; /* 2 */
        case 0x26u: *usage = 0x20u; return 1u; /* 3 */
        case 0x25u: *usage = 0x21u; return 1u; /* 4 */
        case 0x2Eu: *usage = 0x22u; return 1u; /* 5 */
        case 0x36u: *usage = 0x23u; return 1u; /* 6 */
        case 0x3Du: *usage = 0x24u; return 1u; /* 7 */
        case 0x3Eu: *usage = 0x25u; return 1u; /* 8 */
        case 0x46u: *usage = 0x26u; return 1u; /* 9 */
        case 0x45u: *usage = 0x27u; return 1u; /* 0 */

        case 0x0Eu: *usage = 0x35u; return 1u; /* ` */
        case 0x4Eu: *usage = 0x2Du; return 1u; /* - */
        case 0x55u: *usage = 0x2Eu; return 1u; /* = */
        case 0x54u: *usage = 0x2Fu; return 1u; /* [ */
        case 0x5Bu: *usage = 0x30u; return 1u; /* ] */
        case 0x5Du: *usage = 0x31u; return 1u; /* \ */
        case 0x4Cu: *usage = 0x33u; return 1u; /* ; */
        case 0x52u: *usage = 0x34u; return 1u; /* ' */
        case 0x41u: *usage = 0x36u; return 1u; /* , */
        case 0x49u: *usage = 0x37u; return 1u; /* . */
        case 0x4Au: *usage = 0x38u; return 1u; /* / */

        case 0x29u: *usage = 0x2Cu; return 1u; /* Space */
        case 0x5Au: *usage = 0x28u; return 1u; /* Enter */
        case 0x66u: *usage = 0x2Au; return 1u; /* Backspace */
        case 0x0Du: *usage = 0x2Bu; return 1u; /* Tab */
        case 0x76u: *usage = 0x29u; return 1u; /* Escape */

        case 0x05u: *usage = 0x3Au; return 1u; /* F1 */
        case 0x06u: *usage = 0x3Bu; return 1u; /* F2 */
        case 0x04u: *usage = 0x3Cu; return 1u; /* F3 */
        case 0x0Cu: *usage = 0x3Du; return 1u; /* F4 */
        case 0x03u: *usage = 0x3Eu; return 1u; /* F5 */
        case 0x0Bu: *usage = 0x3Fu; return 1u; /* F6 */
        case 0x83u: *usage = 0x40u; return 1u; /* F7 */
        case 0x0Au: *usage = 0x41u; return 1u; /* F8 */
        case 0x01u: *usage = 0x42u; return 1u; /* F9 */
        case 0x09u: *usage = 0x43u; return 1u; /* F10 */
        case 0x78u: *usage = 0x44u; return 1u; /* F11 */
        case 0x07u: *usage = 0x45u; return 1u; /* F12 */

        case 0x58u: *usage = 0x39u; return 1u; /* Caps Lock */
        case 0x77u: *usage = 0x53u; return 1u; /* Num Lock */
        case 0x7Eu: *usage = 0x47u; return 1u; /* Scroll Lock */

        case 0x70u: *usage = 0x62u; return 1u; /* Keypad 0 */
        case 0x69u: *usage = 0x59u; return 1u; /* Keypad 1 */
        case 0x72u: *usage = 0x5Au; return 1u; /* Keypad 2 */
        case 0x7Au: *usage = 0x5Bu; return 1u; /* Keypad 3 */
        case 0x6Bu: *usage = 0x5Cu; return 1u; /* Keypad 4 */
        case 0x73u: *usage = 0x5Du; return 1u; /* Keypad 5 */
        case 0x74u: *usage = 0x5Eu; return 1u; /* Keypad 6 */
        case 0x6Cu: *usage = 0x5Fu; return 1u; /* Keypad 7 */
        case 0x75u: *usage = 0x60u; return 1u; /* Keypad 8 */
        case 0x7Du: *usage = 0x61u; return 1u; /* Keypad 9 */
        case 0x71u: *usage = 0x63u; return 1u; /* Keypad . */
        case 0x79u: *usage = 0x57u; return 1u; /* Keypad + */
        case 0x7Bu: *usage = 0x56u; return 1u; /* Keypad - */
        case 0x7Cu: *usage = 0x55u; return 1u; /* Keypad * */
        default: return 0u;
    }
}

static int8_t Ps2Keyboard_FindUsage(uint8_t usage)
{
    uint8_t i;

    for(i = 0u; i < g_ps2_keyboard_decoder.usage_count; ++i)
    {
        if(g_ps2_keyboard_decoder.usages[i] == usage)
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static void Ps2Keyboard_RemoveUsage(uint8_t usage)
{
    int8_t index = Ps2Keyboard_FindUsage(usage);
    uint8_t i;

    if(index < 0)
    {
        return;
    }
    for(i = (uint8_t)index; i + 1u < g_ps2_keyboard_decoder.usage_count; ++i)
    {
        g_ps2_keyboard_decoder.usages[i] = g_ps2_keyboard_decoder.usages[i + 1u];
    }
    --g_ps2_keyboard_decoder.usage_count;
}

static void Ps2Keyboard_Update(uint8_t extended,
                               uint8_t break_code,
                               uint8_t code)
{
    uint8_t modifier = Ps2Keyboard_ModifierMask(extended, code);
    uint8_t usage;

    if(modifier != 0u)
    {
        if(break_code)
        {
            g_ps2_keyboard_decoder.modifiers &= (uint8_t)~modifier;
        }
        else
        {
            g_ps2_keyboard_decoder.modifiers |= modifier;
        }
        Ps2Keyboard_EmitReport();
        return;
    }

    if(!Ps2Keyboard_MapUsage(extended, code, &usage))
    {
        ++g_ps2_unknown_keyboard_code;
        return;
    }

    if(break_code)
    {
        if(Ps2Keyboard_FindUsage(usage) >= 0)
        {
            Ps2Keyboard_RemoveUsage(usage);
            Ps2Keyboard_EmitReport();
        }
        return;
    }

    if(Ps2Keyboard_FindUsage(usage) >= 0)
    {
        return; /* Auto-repeat make codes do not duplicate a held key. */
    }
    if(g_ps2_keyboard_decoder.usage_count >= PS2_TRACKED_USAGE_CAPACITY)
    {
        ++g_ps2_frame_error;
        Ps2Keyboard_ReleaseAll();
        return;
    }
    g_ps2_keyboard_decoder.usages[g_ps2_keyboard_decoder.usage_count++] = usage;
    Ps2Keyboard_EmitReport();
}

static void Ps2Keyboard_HandleByte(uint8_t code)
{
    uint8_t extended;
    uint8_t break_code;

    /* BAT/ACK/RESEND and device error bytes are command-channel traffic, not
     * keyboard usages.  M3 receives them so later command phases stay aligned. */
    if((code == 0xFAu) || (code == 0xFEu) || (code == 0xAAu))
    {
        return;
    }
    if((code == 0xFCu) || (code == 0xFDu) || (code == 0xFFu))
    {
        ++g_ps2_frame_error;
        Ps2Keyboard_ReleaseAll();
        return;
    }
    if(code == 0xE1u)
    {
        g_ps2_keyboard_decoder.pause_remaining = 7u;
        g_ps2_keyboard_decoder.break_pending = 0u;
        g_ps2_keyboard_decoder.extended_pending = 0u;
        return;
    }
    if(g_ps2_keyboard_decoder.pause_remaining != 0u)
    {
        --g_ps2_keyboard_decoder.pause_remaining;
        return;
    }
    if(code == 0xE0u)
    {
        g_ps2_keyboard_decoder.extended_pending = 1u;
        return;
    }
    if(code == 0xF0u)
    {
        g_ps2_keyboard_decoder.break_pending = 1u;
        return;
    }

    extended = g_ps2_keyboard_decoder.extended_pending;
    break_code = g_ps2_keyboard_decoder.break_pending;
    g_ps2_keyboard_decoder.extended_pending = 0u;
    g_ps2_keyboard_decoder.break_pending = 0u;
    Ps2Keyboard_Update(extended, break_code, code);
}

static void Ps2Mouse_EmitReport(void)
{
    HidMouseReport report;
    uint8_t first = g_ps2_mouse_decoder.packet[0];
    uint8_t x = g_ps2_mouse_decoder.packet[1];
    uint8_t y = g_ps2_mouse_decoder.packet[2];

    report.bytes[0] = (uint8_t)(first & 0x07u);
    if(first & 0x40u)
    {
        report.bytes[1] = (first & 0x10u) ? 0x80u : 0x7Fu;
    }
    else
    {
        report.bytes[1] = x;
    }
    /* PS/2 positive Y is up; USB HID positive Y is down. */
    if(first & 0x80u)
    {
        report.bytes[2] = (first & 0x20u) ? 0x7Fu : 0x80u;
    }
    else
    {
        report.bytes[2] = (uint8_t)(0u - y);
    }
    report.bytes[3] = 0u;
    (void)EventRouter_InjectMouseReport(ROUTER_SRC_PS2_MOUSE, &report);
}

static void Ps2Mouse_HandleByte(uint8_t code)
{
    if(g_ps2_mouse_decoder.packet_index == 0u)
    {
        if((code == 0xFAu) || (code == 0xFEu) || (code == 0xAAu))
        {
            return;
        }
        if((code & 0x08u) == 0u)
        {
            ++g_ps2_mouse_packet_error;
            return;
        }
    }

    g_ps2_mouse_decoder.packet[g_ps2_mouse_decoder.packet_index++] = code;
    if(g_ps2_mouse_decoder.packet_index == PS2_MOUSE_PACKET_LENGTH)
    {
        Ps2Mouse_EmitReport();
        g_ps2_mouse_decoder.packet_index = 0u;
    }
}

static void Ps2Bus_DriveLow(uint32_t pin)
{
    GPIOA_ResetBits(pin);
    GPIOA_ModeCfg(pin, GPIO_ModeOut_PP_5mA);
}

static void Ps2Bus_Release(uint32_t pin)
{
    GPIOA_ModeCfg(pin, GPIO_ModeIN_PU);
}

static void Ps2MouseTx_SelectNextEdge(GPIOITModeTpDef mode)
{
    GPIOA_ITModeCfg(BOARD_PS2_MOUSE_CLK_PIN, mode);
}

static uint8_t Ps2MouseTx_OddParity(uint8_t value)
{
    uint8_t ones = 0u;
    uint8_t i;

    for(i = 0u; i < 8u; ++i)
    {
        if(value & (uint8_t)(1u << i))
        {
            ++ones;
        }
    }
    return ((ones & 1u) == 0u) ? 1u : 0u;
}

static void Ps2MouseTx_OutputBit(uint8_t bit_index)
{
    uint8_t bit;

    if(bit_index == 0u)
    {
        Ps2Bus_DriveLow(BOARD_PS2_MOUSE_DATA_PIN);
    }
    else if(bit_index <= 8u)
    {
        bit = (uint8_t)((g_ps2_mouse_tx.command >> (bit_index - 1u)) & 1u);
        if(bit != 0u)
        {
            Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
        }
        else
        {
            Ps2Bus_DriveLow(BOARD_PS2_MOUSE_DATA_PIN);
        }
    }
    else if(bit_index == 9u)
    {
        if(g_ps2_mouse_tx.parity != 0u)
        {
            Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
        }
        else
        {
            Ps2Bus_DriveLow(BOARD_PS2_MOUSE_DATA_PIN);
        }
    }
    else
    {
        /* Stop bit is a released (logic-high) open-drain line. */
        Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
    }
}

static void Ps2MouseTx_FinishFromISR(void)
{
    Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
    Ps2Bus_Release(BOARD_PS2_MOUSE_CLK_PIN);
    Ps2MouseTx_SelectNextEdge(GPIO_ITMode_FallEdge);
    g_ps2_mouse_tx.state = PS2_TX_IDLE;
    g_ps2_mouse_tx.result_pending = 1u;
    g_ps2_mouse_tx.edge_seen = 1u;
}

static void Ps2MouseTx_HandleEdgeFromISR(uint8_t clock_high)
{
    g_ps2_mouse_tx.edge_seen = 1u;
    g_ps2_mouse_tx.idle_ticks = 0u;

    if(clock_high != 0u)
    {
        if(g_ps2_mouse_tx.state == PS2_TX_WAIT_ACK_RISE)
        {
            g_ps2_mouse_tx.ack_ok =
                (GPIOA_ReadPortPin(BOARD_PS2_MOUSE_DATA_PIN) == 0u) ? 1u : 0u;
            g_ps2_mouse_tx.state = PS2_TX_WAIT_ACK_FALL;
        }
        Ps2MouseTx_SelectNextEdge(GPIO_ITMode_FallEdge);
        return;
    }

    if(g_ps2_mouse_tx.state == PS2_TX_CLOCKING)
    {
        if(g_ps2_mouse_tx.bit_index < 10u)
        {
            ++g_ps2_mouse_tx.bit_index;
            Ps2MouseTx_OutputBit(g_ps2_mouse_tx.bit_index);
            Ps2MouseTx_SelectNextEdge(GPIO_ITMode_RiseEdge);
        }
        else
        {
            /* The next rising edge is the device ACK bit. */
            Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
            g_ps2_mouse_tx.state = PS2_TX_WAIT_ACK_RISE;
            Ps2MouseTx_SelectNextEdge(GPIO_ITMode_RiseEdge);
        }
    }
    else if(g_ps2_mouse_tx.state == PS2_TX_WAIT_ACK_FALL)
    {
        Ps2MouseTx_FinishFromISR();
    }
}

static void Ps2MouseTx_Start(void)
{
    StaticSpscRing_Clear(&g_ps2_mouse_edge_ring);
    Ps2Mouse_ResetState();

    g_ps2_mouse_tx.command = 0xF4u; /* Enable data reporting. */
    g_ps2_mouse_tx.bit_index = 0u;
    g_ps2_mouse_tx.parity = Ps2MouseTx_OddParity(g_ps2_mouse_tx.command);
    g_ps2_mouse_tx.ack_ok = 0u;
    g_ps2_mouse_tx.edge_seen = 0u;
    g_ps2_mouse_tx.idle_ticks = 0u;
    g_ps2_mouse_tx.result_pending = 0u;

    /* Host inhibit must last at least 100 us.  Holding CLK low until the next
     * 2 ms TMOS tick is deliberately conservative and non-blocking. */
    Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
    Ps2Bus_DriveLow(BOARD_PS2_MOUSE_CLK_PIN);
    Ps2MouseTx_SelectNextEdge(GPIO_ITMode_FallEdge);
    g_ps2_mouse_tx.state = PS2_TX_INHIBIT;
}

static void Ps2MouseTx_ReleaseInhibit(void)
{
    Ps2Bus_DriveLow(BOARD_PS2_MOUSE_DATA_PIN); /* Start bit. */
    Ps2Bus_Release(BOARD_PS2_MOUSE_CLK_PIN);
    g_ps2_mouse_tx.bit_index = 0u;
    g_ps2_mouse_tx.edge_seen = 0u;
    g_ps2_mouse_tx.idle_ticks = 0u;
    g_ps2_mouse_tx.state = PS2_TX_CLOCKING;
    Ps2MouseTx_SelectNextEdge(GPIO_ITMode_RiseEdge);
}

static void Ps2MouseTx_Abort(void)
{
    Ps2Bus_Release(BOARD_PS2_MOUSE_DATA_PIN);
    Ps2Bus_Release(BOARD_PS2_MOUSE_CLK_PIN);
    Ps2MouseTx_SelectNextEdge(GPIO_ITMode_FallEdge);
    g_ps2_mouse_tx.state = PS2_TX_IDLE;
    g_ps2_mouse_tx.result_pending = 1u;
    g_ps2_mouse_tx.ack_ok = 0u;
    ++g_ps2_frame_error;
    g_ps2_mouse_init_delay_ticks = PS2_MOUSE_RETRY_DELAY_TICKS;
}

static void Ps2Input_ProcessKeyboardEdges(void)
{
    Ps2EdgeSample sample;
    uint8_t byte;
    uint8_t result;
    uint8_t processed = 0u;

    while((processed < PS2_EDGE_PROCESS_BUDGET) &&
          StaticSpscRing_Pop(&g_ps2_keyboard_edge_ring, &sample))
    {
        g_ps2_keyboard_decoder.idle_ticks = 0u;
        result = Ps2BitDecoder_Consume(&g_ps2_keyboard_decoder.frame,
                                       sample.data_level,
                                       &byte);
        if(result == 1u)
        {
            Ps2Keyboard_HandleByte(byte);
        }
        else if(result == 2u)
        {
            ++g_ps2_frame_error;
            Ps2Keyboard_ReleaseAll();
        }
        ++processed;
    }

    if((processed == 0u) && (g_ps2_keyboard_decoder.frame.bit_index != 0u))
    {
        if(++g_ps2_keyboard_decoder.idle_ticks >= PS2_IDLE_TIMEOUT_TICKS)
        {
            ++g_ps2_frame_error;
            Ps2Keyboard_ReleaseAll();
        }
    }
}

static void Ps2Input_ProcessMouseEdges(void)
{
    Ps2EdgeSample sample;
    uint8_t byte;
    uint8_t result;
    uint8_t processed = 0u;

    if(g_ps2_mouse_tx.state != PS2_TX_IDLE)
    {
        return;
    }
    while((processed < PS2_EDGE_PROCESS_BUDGET) &&
          StaticSpscRing_Pop(&g_ps2_mouse_edge_ring, &sample))
    {
        g_ps2_mouse_decoder.idle_ticks = 0u;
        result = Ps2BitDecoder_Consume(&g_ps2_mouse_decoder.frame,
                                       sample.data_level,
                                       &byte);
        if(result == 1u)
        {
            Ps2Mouse_HandleByte(byte);
        }
        else if(result == 2u)
        {
            ++g_ps2_frame_error;
            ++g_ps2_mouse_packet_error;
            Ps2Mouse_ResetState();
        }
        ++processed;
    }

    if((processed == 0u) && (g_ps2_mouse_decoder.frame.bit_index != 0u))
    {
        if(++g_ps2_mouse_decoder.idle_ticks >= PS2_IDLE_TIMEOUT_TICKS)
        {
            ++g_ps2_frame_error;
            Ps2Mouse_ResetState();
            ++g_ps2_mouse_resync;
            {
                HidMouseReport report = {{0u, 0u, 0u, 0u}};
                (void)EventRouter_InjectMouseReport(ROUTER_SRC_PS2_MOUSE, &report);
            }
        }
    }
}

static void Ps2Input_HandleOverrun(void)
{
    uint32_t keyboard_overrun = g_ps2_keyboard_edge_overrun;
    uint32_t mouse_overrun = g_ps2_mouse_edge_overrun;

    if(keyboard_overrun != g_ps2_keyboard_overrun_reported)
    {
        g_ps2_keyboard_overrun_reported = keyboard_overrun;
        StaticSpscRing_Clear(&g_ps2_keyboard_edge_ring);
        Ps2Keyboard_ReleaseAll();
    }
    if(mouse_overrun != g_ps2_mouse_overrun_reported)
    {
        g_ps2_mouse_overrun_reported = mouse_overrun;
        StaticSpscRing_Clear(&g_ps2_mouse_edge_ring);
        Ps2Mouse_ResetState();
        ++g_ps2_mouse_resync;
        {
            HidMouseReport report = {{0u, 0u, 0u, 0u}};
            (void)EventRouter_InjectMouseReport(ROUTER_SRC_PS2_MOUSE, &report);
        }
    }
}

static void Ps2Input_ProcessMouseTx(void)
{
    if(g_ps2_mouse_tx.result_pending != 0u)
    {
        g_ps2_mouse_tx.result_pending = 0u;
        Ps2Mouse_ResetState();
        if(g_ps2_mouse_tx.ack_ok != 0u)
        {
            g_ps2_mouse_init_done = 1u;
        }
        else
        {
            ++g_ps2_mouse_resync;
            g_ps2_mouse_init_delay_ticks = PS2_MOUSE_RETRY_DELAY_TICKS;
        }
    }

    if(g_ps2_mouse_tx.state != PS2_TX_IDLE)
    {
        if(g_ps2_mouse_tx.edge_seen != 0u)
        {
            g_ps2_mouse_tx.edge_seen = 0u;
            g_ps2_mouse_tx.idle_ticks = 0u;
        }
        else if(++g_ps2_mouse_tx.idle_ticks >= PS2_TX_IDLE_TIMEOUT_TICKS)
        {
            Ps2MouseTx_Abort();
        }
        if(g_ps2_mouse_tx.state == PS2_TX_INHIBIT)
        {
            Ps2MouseTx_ReleaseInhibit();
        }
        return;
    }

    if(!g_ps2_mouse_init_done)
    {
        if(g_ps2_mouse_init_delay_ticks != 0u)
        {
            --g_ps2_mouse_init_delay_ticks;
        }
        else
        {
            Ps2MouseTx_Start();
        }
    }
}

void Ps2Input_Init(void)
{
    uint32_t clock_pins = BOARD_PS2_KEYBOARD_CLK_PIN |
                          BOARD_PS2_MOUSE_CLK_PIN;
    uint32_t data_pins = BOARD_PS2_KEYBOARD_DATA_PIN |
                         BOARD_PS2_MOUSE_DATA_PIN;

    StaticSpscRing_Init(&g_ps2_keyboard_edge_ring,
                        g_ps2_keyboard_edge_storage,
                        sizeof(g_ps2_keyboard_edge_storage[0]),
                        PS2_EDGE_CAPACITY);
    StaticSpscRing_Init(&g_ps2_mouse_edge_ring,
                        g_ps2_mouse_edge_storage,
                        sizeof(g_ps2_mouse_edge_storage[0]),
                        PS2_EDGE_CAPACITY);
    Ps2Keyboard_ResetState();
    Ps2Mouse_ResetState();

    g_ps2_keyboard_edge_overrun = 0u;
    g_ps2_mouse_edge_overrun = 0u;
    g_ps2_frame_error = 0u;
    g_ps2_keyboard_resync = 0u;
    g_ps2_mouse_resync = 0u;
    g_ps2_unknown_keyboard_code = 0u;
    g_ps2_mouse_packet_error = 0u;
    g_ps2_keyboard_overrun_reported = 0u;
    g_ps2_mouse_overrun_reported = 0u;
    g_ps2_mouse_init_delay_ticks = PS2_MOUSE_INIT_DELAY_TICKS;
    g_ps2_mouse_init_done = 0u;
    g_ps2_mouse_tx.state = PS2_TX_IDLE;
    g_ps2_mouse_tx.result_pending = 0u;
    g_ps2_mouse_tx.edge_seen = 0u;
    g_ps2_mouse_tx.ack_ok = 0u;
    g_ps2_mouse_tx.command = 0u;
    g_ps2_mouse_tx.bit_index = 0u;
    g_ps2_mouse_tx.parity = 0u;
    g_ps2_mouse_tx.idle_ticks = 0u;

    GPIOA_ModeCfg(clock_pins | data_pins, GPIO_ModeIN_PU);
    GPIOA_ITModeCfg(clock_pins, GPIO_ITMode_FallEdge);
    PFIC_EnableIRQ(GPIO_A_IRQn);
}

void Ps2Input_Process(void)
{
    Ps2Input_HandleOverrun();
    Ps2Input_ProcessKeyboardEdges();
    Ps2Input_ProcessMouseTx();
    Ps2Input_ProcessMouseEdges();
}

void Ps2Input_GetStats(Ps2InputStats *stats)
{
    if(stats == NULL)
    {
        return;
    }
    stats->keyboard_edge_overrun = g_ps2_keyboard_edge_overrun;
    stats->mouse_edge_overrun = g_ps2_mouse_edge_overrun;
    stats->frame_error = g_ps2_frame_error;
    stats->keyboard_resync = g_ps2_keyboard_resync;
    stats->mouse_resync = g_ps2_mouse_resync;
    stats->unknown_keyboard_code = g_ps2_unknown_keyboard_code;
    stats->mouse_packet_error = g_ps2_mouse_packet_error;
}

/* GPIOA is shared by both PS/2 clocks.  The ISR only samples data into the
 * static edge rings, or advances the tightly timed mouse command bit phase. */
__INTERRUPT
__HIGH_CODE
void GPIOA_IRQHandler(void)
{
    uint16_t flags = GPIOA_ReadITFlagPort();
    uint16_t ps2_flags = (uint16_t)(flags &
                                    (BOARD_PS2_KEYBOARD_CLK_PIN |
                                     BOARD_PS2_MOUSE_CLK_PIN));
    Ps2EdgeSample sample;

    if(ps2_flags == 0u)
    {
        return;
    }
    GPIOA_ClearITFlagBit(ps2_flags);

    if((ps2_flags & BOARD_PS2_KEYBOARD_CLK_PIN) != 0u)
    {
        sample.data_level = (GPIOA_ReadPortPin(BOARD_PS2_KEYBOARD_DATA_PIN) != 0u) ? 1u : 0u;
        sample.flags = 0u;
        sample.reserved = 0u;
        if(!StaticSpscRing_Push(&g_ps2_keyboard_edge_ring, &sample))
        {
            ++g_ps2_keyboard_edge_overrun;
        }
    }

    if((ps2_flags & BOARD_PS2_MOUSE_CLK_PIN) != 0u)
    {
        if((g_ps2_mouse_tx.state == PS2_TX_CLOCKING) ||
           (g_ps2_mouse_tx.state == PS2_TX_WAIT_ACK_RISE) ||
           (g_ps2_mouse_tx.state == PS2_TX_WAIT_ACK_FALL))
        {
            Ps2MouseTx_HandleEdgeFromISR(
                (GPIOA_ReadPortPin(BOARD_PS2_MOUSE_CLK_PIN) != 0u) ? 1u : 0u);
        }
        else if(g_ps2_mouse_tx.state == PS2_TX_IDLE)
        {
            sample.data_level = (GPIOA_ReadPortPin(BOARD_PS2_MOUSE_DATA_PIN) != 0u) ? 1u : 0u;
            sample.flags = 0u;
            sample.reserved = 0u;
            if(!StaticSpscRing_Push(&g_ps2_mouse_edge_ring, &sample))
            {
                ++g_ps2_mouse_edge_overrun;
            }
        }
    }
}
