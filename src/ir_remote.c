#include "CH58x_common.h"

#include "board_pins.h"
#include "event_router.h"
#include "irda_link.h"
#include "ir_remote.h"
#include "static_spsc_ring.h"

#define IR_REMOTE_EDGE_CAPACITY       128u /* StaticSpscRing requires 2^n. */
#define IR_REMOTE_PROCESS_BUDGET      64u
#define IR_REMOTE_NEC_PROTOCOL         1u
#define IR_REMOTE_RC5_PROTOCOL         2u
#define IR_REMOTE_TX_NEC               1u
#define IR_REMOTE_TX_RC5               2u

#define IR_REMOTE_CARRIER_DIV         51u
#define IR_REMOTE_CARRIER_CYCLE       PWMX_Cycle_31
#define IR_REMOTE_CARRIER_WIDTH       10u

typedef struct
{
    uint32_t timestamp;
    uint8_t level;
    uint8_t reserved[3];
} IrRemoteEdge;

typedef enum
{
    IR_NEC_IDLE = 0,
    IR_NEC_LEAD_SPACE,
    IR_NEC_BIT_MARK,
    IR_NEC_BIT_SPACE
} IrNecState;

typedef enum
{
    IR_TX_IDLE = 0,
    IR_TX_NEC_LEAD_MARK,
    IR_TX_NEC_LEAD_SPACE,
    IR_TX_NEC_BIT_MARK,
    IR_TX_NEC_BIT_SPACE,
    IR_TX_NEC_FINAL_MARK,
    IR_TX_RC5_HALF
} IrTxState;

static IrRemoteEdge g_ir_edge_storage[IR_REMOTE_EDGE_CAPACITY]
    __attribute__((aligned(4)));
static StaticSpscRing g_ir_edge_ring;
static IrRemoteStats g_ir_stats __attribute__((aligned(4)));
static uint32_t g_ir_cycles_per_us;
static uint32_t g_ir_last_timestamp;
static uint8_t g_ir_have_timestamp;
static uint8_t g_ir_last_level;
static uint8_t g_nec_state;
static uint8_t g_nec_bit_count;
static uint32_t g_nec_data;
static uint8_t g_rc5_half[32];
static uint8_t g_rc5_half_count;

static uint8_t g_tx_state;
static uint8_t g_tx_protocol;
static uint8_t g_tx_bit;
static uint16_t g_tx_rc5_frame;
static uint32_t g_tx_nec_frame;

static uint32_t IrRemote_Us(uint32_t us)
{
    return g_ir_cycles_per_us * us;
}

static uint8_t IrRemote_InRange(uint32_t duration,
                                uint32_t min_us,
                                uint32_t max_us)
{
    return (duration >= IrRemote_Us(min_us) &&
            duration <= IrRemote_Us(max_us)) ? 1u : 0u;
}

static void IrRemote_ResetNec(void)
{
    g_nec_state = IR_NEC_IDLE;
    g_nec_bit_count = 0u;
    g_nec_data = 0u;
}

static void IrRemote_ResetRc5(void)
{
    g_rc5_half_count = 0u;
}

static void IrRemote_PostCode(uint8_t protocol,
                              uint8_t address,
                              uint8_t command,
                              uint8_t flags)
{
    uint8_t payload[4];

    payload[0] = protocol;
    payload[1] = address;
    payload[2] = command;
    payload[3] = flags;
    (void)EventRouter_InjectStreamData(ROUTER_SRC_IR_REMOTE,
                                        payload, sizeof(payload));
}

static void IrRemote_ProcessNec(uint32_t duration, uint8_t level)
{
    uint8_t address;
    uint8_t command;

    switch(g_nec_state)
    {
        case IR_NEC_IDLE:
            if(level && IrRemote_InRange(duration, 8000u, 10000u))
            {
                g_nec_state = IR_NEC_LEAD_SPACE;
            }
            break;

        case IR_NEC_LEAD_SPACE:
            if(!level && IrRemote_InRange(duration, 3500u, 5500u))
            {
                g_nec_bit_count = 0u;
                g_nec_data = 0u;
                g_nec_state = IR_NEC_BIT_MARK;
            }
            else
            {
                IrRemote_ResetNec();
            }
            break;

        case IR_NEC_BIT_MARK:
            if(level && IrRemote_InRange(duration, 350u, 800u))
            {
                g_nec_state = IR_NEC_BIT_SPACE;
            }
            else
            {
                IrRemote_ResetNec();
            }
            break;

        case IR_NEC_BIT_SPACE:
            if(!level && IrRemote_InRange(duration, 350u, 800u))
            {
                /* Logical zero: no bit set. */
            }
            else if(!level && IrRemote_InRange(duration, 1300u, 2100u))
            {
                g_nec_data |= (uint32_t)1u << g_nec_bit_count;
            }
            else
            {
                IrRemote_ResetNec();
                break;
            }
            ++g_nec_bit_count;
            if(g_nec_bit_count == 32u)
            {
                address = (uint8_t)g_nec_data;
                command = (uint8_t)(g_nec_data >> 16);
                if((((uint8_t)(g_nec_data >> 8)) == (uint8_t)~address) &&
                   (((uint8_t)(g_nec_data >> 24)) == (uint8_t)~command))
                {
                    ++g_ir_stats.nec_frame_count;
                    IrRemote_PostCode(IR_REMOTE_NEC_PROTOCOL, address,
                                      command, 0u);
                }
                else
                {
                    ++g_ir_stats.invalid_pulse;
                }
                IrRemote_ResetNec();
            }
            else
            {
                g_nec_state = IR_NEC_BIT_MARK;
            }
            break;

        default:
            IrRemote_ResetNec();
            break;
    }
}

static void IrRemote_ProcessRc5Frame(void)
{
    uint8_t polarity;
    uint8_t offset;
    uint8_t i;
    uint8_t bit;
    uint16_t frame;
    uint8_t address;
    uint8_t command;
    uint8_t toggle;

    /* offset=1 consumes half[1]..half[28], so 29 samples are required. */
    if(g_rc5_half_count < 29u)
    {
        return;
    }
    for(polarity = 0u; polarity < 2u; ++polarity)
    {
        for(offset = 0u; offset < 2u; ++offset)
        {
            frame = 0u;
            for(i = 0u; i < 14u; ++i)
            {
                uint8_t first = g_rc5_half[(uint8_t)(offset + i * 2u)];
                uint8_t second = g_rc5_half[(uint8_t)(offset + i * 2u + 1u)];
                if((first == second) || (first > 1u) || (second > 1u))
                {
                    break;
                }
                bit = (first == 1u && second == 0u) ? 1u : 0u;
                if(polarity != 0u)
                {
                    bit ^= 1u;
                }
                frame = (uint16_t)((frame << 1) | bit);
            }
            if(i != 14u || (frame >> 12) != 3u)
            {
                continue;
            }
            toggle = (uint8_t)((frame >> 11) & 1u);
            address = (uint8_t)((frame >> 6) & 0x1Fu);
            command = (uint8_t)(frame & 0x3Fu);
            ++g_ir_stats.rc5_frame_count;
            IrRemote_PostCode(IR_REMOTE_RC5_PROTOCOL, address, command,
                              toggle);
            IrRemote_ResetRc5();
            return;
        }
    }
    ++g_ir_stats.invalid_pulse;
    IrRemote_ResetRc5();
}

static void IrRemote_ProcessRc5(uint32_t duration, uint8_t previous_level,
                                uint8_t current_level)
{
    if(!IrRemote_InRange(duration, 400u, 2300u))
    {
        IrRemote_ResetRc5();
        return;
    }
    if(IrRemote_InRange(duration, 400u, 1300u))
    {
        if(g_rc5_half_count < sizeof(g_rc5_half))
        {
            g_rc5_half[g_rc5_half_count++] = previous_level;
        }
    }
    else
    {
        if((uint8_t)(g_rc5_half_count + 1u) < sizeof(g_rc5_half))
        {
            g_rc5_half[g_rc5_half_count++] = previous_level;
            g_rc5_half[g_rc5_half_count++] = current_level;
        }
    }
    if(g_rc5_half_count >= 28u)
    {
        IrRemote_ProcessRc5Frame();
    }
}

static uint8_t IrRemote_TxBusy(void)
{
    return (g_tx_state != IR_TX_IDLE) ? 1u : 0u;
}

static void IrRemote_TxCarrier(uint8_t enabled)
{
    if(enabled)
    {
        R8_PWM_OUT_EN |= RB_PWM6_OUT_EN;
    }
    else
    {
        R8_PWM_OUT_EN &= (uint8_t)~RB_PWM6_OUT_EN;
    }
}

static void IrRemote_TxSchedule(uint32_t microseconds)
{
    TMR0_TimerInit(IrRemote_Us(microseconds));
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR0_IRQn);
}

static uint8_t IrRemote_Rc5HalfCarrier(void)
{
    uint8_t bit_index = (uint8_t)(13u - (g_tx_bit / 2u));
    uint8_t bit = (uint8_t)((g_tx_rc5_frame >> bit_index) & 1u);
    uint8_t first_half = (g_tx_bit & 1u) == 0u;

    return (bit != 0u) ? first_half : (uint8_t)!first_half;
}

void IrRemote_Init(void)
{
    StaticSpscRing_Init(&g_ir_edge_ring,
                        g_ir_edge_storage,
                        sizeof(g_ir_edge_storage[0]),
                        IR_REMOTE_EDGE_CAPACITY);
    g_ir_stats = (IrRemoteStats){0};
    g_ir_cycles_per_us = GetSysClock() / 1000000u;
    if(g_ir_cycles_per_us == 0u)
    {
        g_ir_cycles_per_us = 1u;
    }
    g_ir_have_timestamp = 0u;
    g_ir_last_level = 1u;
    IrRemote_ResetNec();
    IrRemote_ResetRc5();
    g_tx_state = IR_TX_IDLE;
    GPIOB_ModeCfg(BOARD_IR_TX_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(BOARD_IR_TX_PIN);
    PWMX_CycleCfg(IR_REMOTE_CARRIER_CYCLE);
    PWMX_CLKCfg(IR_REMOTE_CARRIER_DIV);
    PWM6_ActDataWidth(IR_REMOTE_CARRIER_WIDTH);
    PWMX_ACTOUT(CH_PWM6, IR_REMOTE_CARRIER_WIDTH, High_Level, DISABLE);
    GPIOB_ModeCfg(BOARD_IR_RX_PIN, GPIO_ModeIN_PU);
    GPIOB_ClearITFlagBit(BOARD_IR_RX_PIN);
    GPIOB_ITModeCfg(BOARD_IR_RX_PIN, GPIO_ITMode_FallEdge);
    PFIC_EnableIRQ(GPIO_B_IRQn);
    R8_TMR0_INTER_EN = 0u;
    R8_TMR0_CTRL_MOD = RB_TMR_ALL_CLEAR;
}

void IrRemote_Process(void)
{
    IrRemoteEdge edge;
    uint8_t processed = 0u;
    uint32_t duration;

    while(processed < IR_REMOTE_PROCESS_BUDGET &&
          StaticSpscRing_Pop(&g_ir_edge_ring, &edge))
    {
        ++processed;
        ++g_ir_stats.edge_count;
        if(!g_ir_have_timestamp)
        {
            g_ir_last_timestamp = edge.timestamp;
            g_ir_last_level = edge.level;
            g_ir_have_timestamp = 1u;
            continue;
        }
        duration = edge.timestamp - g_ir_last_timestamp;
        g_ir_last_timestamp = edge.timestamp;
        if(duration > IrRemote_Us(3000u))
        {
            IrRemote_ProcessRc5Frame();
            IrRemote_ResetNec();
            IrRemote_ResetRc5();
        }
        else
        {
            IrRemote_ProcessNec(duration, edge.level);
            IrRemote_ProcessRc5(duration, g_ir_last_level, edge.level);
        }
        g_ir_last_level = edge.level;
    }
}

uint8_t IrRemote_SendNec(uint8_t address, uint8_t command)
{
    if(IrRemote_TxBusy())
    {
        ++g_ir_stats.tx_busy;
        return 0u;
    }
    g_tx_protocol = IR_REMOTE_TX_NEC;
    g_tx_nec_frame = (uint32_t)address |
                     ((uint32_t)(uint8_t)~address << 8) |
                     ((uint32_t)command << 16) |
                     ((uint32_t)(uint8_t)~command << 24);
    g_tx_bit = 0u;
    g_tx_state = IR_TX_NEC_LEAD_MARK;
    IrdaLink_SetRemoteTxActive(1u);
    IrRemote_TxCarrier(1u);
    IrRemote_TxSchedule(9000u);
    ++g_ir_stats.tx_frame_count;
    g_ir_stats.tx_active = 1u;
    return 1u;
}

uint8_t IrRemote_SendRc5(uint8_t address, uint8_t command, uint8_t toggle)
{
    if(IrRemote_TxBusy() || address > 31u || command > 63u || toggle > 1u)
    {
        ++g_ir_stats.tx_reject;
        return 0u;
    }
    g_tx_protocol = IR_REMOTE_TX_RC5;
    g_tx_rc5_frame = (uint16_t)(0x3000u | ((uint16_t)toggle << 11) |
                                ((uint16_t)address << 6) | command);
    g_tx_bit = 0u;
    g_tx_state = IR_TX_RC5_HALF;
    IrdaLink_SetRemoteTxActive(1u);
    IrRemote_TxCarrier(IrRemote_Rc5HalfCarrier());
    IrRemote_TxSchedule(889u);
    ++g_ir_stats.tx_frame_count;
    g_ir_stats.tx_active = 1u;
    return 1u;
}

void IrRemote_GetStats(IrRemoteStats *stats)
{
    if(stats != 0)
    {
        *stats = g_ir_stats;
    }
}

__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    if(GPIOB_ReadITFlagBit(BOARD_IR_RX_PIN) != 0u)
    {
        IrRemoteEdge edge;
        GPIOB_ClearITFlagBit(BOARD_IR_RX_PIN);
        edge.timestamp = SYS_GetSysTickCnt();
        edge.level = (GPIOB_ReadPortPin(BOARD_IR_RX_PIN) != 0u) ? 1u : 0u;
        edge.reserved[0] = 0u;
        edge.reserved[1] = 0u;
        edge.reserved[2] = 0u;
        if(!StaticSpscRing_Push(&g_ir_edge_ring, &edge))
        {
            ++g_ir_stats.edge_overrun;
        }
        GPIOB_ITModeCfg(BOARD_IR_RX_PIN,
                        edge.level ? GPIO_ITMode_FallEdge :
                                     GPIO_ITMode_RiseEdge);
    }
}

__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void)
{
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    if(g_tx_state == IR_TX_IDLE)
    {
        TMR0_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
        TMR0_Disable();
        return;
    }
    if(g_tx_protocol == IR_REMOTE_TX_NEC)
    {
        switch(g_tx_state)
        {
            case IR_TX_NEC_LEAD_MARK:
                IrRemote_TxCarrier(0u);
                g_tx_state = IR_TX_NEC_LEAD_SPACE;
                IrRemote_TxSchedule(4500u);
                break;
            case IR_TX_NEC_LEAD_SPACE:
                IrRemote_TxCarrier(1u);
                g_tx_state = IR_TX_NEC_BIT_MARK;
                IrRemote_TxSchedule(560u);
                break;
            case IR_TX_NEC_BIT_MARK:
                IrRemote_TxCarrier(0u);
                g_tx_state = IR_TX_NEC_BIT_SPACE;
                IrRemote_TxSchedule(
                    ((g_tx_nec_frame >> g_tx_bit) & 1u) ? 1690u : 560u);
                break;
            case IR_TX_NEC_BIT_SPACE:
                ++g_tx_bit;
                if(g_tx_bit < 32u)
                {
                    IrRemote_TxCarrier(1u);
                    g_tx_state = IR_TX_NEC_BIT_MARK;
                    IrRemote_TxSchedule(560u);
                }
                else
                {
                    IrRemote_TxCarrier(1u);
                    g_tx_state = IR_TX_NEC_FINAL_MARK;
                    IrRemote_TxSchedule(560u);
                }
                break;
            case IR_TX_NEC_FINAL_MARK:
            default:
                IrRemote_TxCarrier(0u);
                IrdaLink_SetRemoteTxActive(0u);
                g_tx_state = IR_TX_IDLE;
                g_ir_stats.tx_active = 0u;
                TMR0_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
                TMR0_Disable();
                break;
        }
    }
    else
    {
        ++g_tx_bit;
        if(g_tx_bit >= 28u)
        {
            IrRemote_TxCarrier(0u);
            IrdaLink_SetRemoteTxActive(0u);
            g_tx_state = IR_TX_IDLE;
            g_ir_stats.tx_active = 0u;
            TMR0_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
            TMR0_Disable();
        }
        else
        {
            IrRemote_TxCarrier(IrRemote_Rc5HalfCarrier());
            IrRemote_TxSchedule(889u);
        }
    }
}
