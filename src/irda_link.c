#include "CH58x_common.h"
#include "HAL.h"

#include "board_pins.h"
#include "event_router.h"
#include "irda_link.h"
#include "static_spsc_ring.h"

#define IRDA_RX_CAPACITY             64u
#define IRDA_FRAME_MAX_LEN           128u
#define IRDA_PROCESS_BUDGET          32u
#define IRDA_CONFIG_RETRIES           3u
#define IRDA_WAKE_TICKS               2u /* 1.25 ms at the TMOS tick */
#define IRDA_CONFIG_TIMEOUT_TICKS     4u

#define IRDA_BOF                     0xC0u
#define IRDA_EOF                     0xC1u
#define IRDA_CE                      0x7Du
#define IRDA_XOR                     0x20u
#define IRDA_MCP_BAUD_9600          0x87u
#define IRDA_MCP_BAUD_APPLY         0x11u

typedef struct
{
    uint8_t byte;
    uint8_t status;
    uint16_t reserved;
} IrdaRxItem;

typedef enum
{
    IRDA_STATE_DISABLED = 0,
    IRDA_STATE_WAKE,
    IRDA_STATE_SEND_BAUD,
    IRDA_STATE_WAIT_BAUD_ECHO,
    IRDA_STATE_SEND_APPLY,
    IRDA_STATE_WAIT_APPLY_ECHO,
    IRDA_STATE_DATA
} IrdaState;

static IrdaRxItem g_irda_rx_storage[IRDA_RX_CAPACITY]
    __attribute__((aligned(4)));
static StaticSpscRing g_irda_rx_ring;
static uint8_t g_irda_frame[IRDA_FRAME_MAX_LEN]
    __attribute__((aligned(4)));
static uint8_t g_irda_frame_length;
static uint8_t g_irda_frame_in_progress;
static uint8_t g_irda_frame_escaped;
static uint8_t g_irda_state;
static uint8_t g_irda_retry;
static uint8_t g_irda_expected_echo;
static uint8_t g_irda_remote_paused;
static uint32_t g_irda_deadline;
static uint32_t g_irda_resume_deadline;
static IrdaLinkStats g_irda_stats __attribute__((aligned(4)));

static uint8_t IrdaLink_Due(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1u : 0u;
}

static uint16_t IrdaLink_FcsUpdate(uint16_t fcs, uint8_t value)
{
    uint8_t bit;

    fcs ^= value;
    for(bit = 0u; bit < 8u; ++bit)
    {
        if((fcs & 1u) != 0u)
        {
            fcs = (uint16_t)((fcs >> 1) ^ 0x8408u);
        }
        else
        {
            fcs >>= 1;
        }
    }
    return fcs;
}

static uint8_t IrdaLink_FcsValid(const uint8_t *frame, uint8_t length)
{
    uint16_t fcs = 0xFFFFu;
    uint8_t i;

    if((frame == 0) || (length < 4u))
    {
        return 0u;
    }
    for(i = 0u; i < length; ++i)
    {
        fcs = IrdaLink_FcsUpdate(fcs, frame[i]);
    }
    return (fcs == 0xF0B8u) ? 1u : 0u;
}

static void IrdaLink_ResetFrame(void)
{
    g_irda_frame_length = 0u;
    g_irda_frame_in_progress = 0u;
    g_irda_frame_escaped = 0u;
}

static void IrdaLink_ProcessFrameByte(uint8_t value)
{
    if(value == IRDA_BOF)
    {
        g_irda_frame_length = 0u;
        g_irda_frame_in_progress = 1u;
        g_irda_frame_escaped = 0u;
        return;
    }
    if(!g_irda_frame_in_progress)
    {
        return;
    }
    if(value == IRDA_EOF)
    {
        if(g_irda_frame_length < 4u)
        {
            ++g_irda_stats.frame_error;
        }
        else if(!IrdaLink_FcsValid(g_irda_frame, g_irda_frame_length))
        {
            ++g_irda_stats.fcs_error;
        }
        else if(!EventRouter_InjectStreamData(ROUTER_SRC_IRDA,
                                              g_irda_frame,
                                              g_irda_frame_length))
        {
            ++g_irda_stats.frame_backpressure;
        }
        else
        {
            ++g_irda_stats.frame_count;
        }
        IrdaLink_ResetFrame();
        return;
    }
    if(value == IRDA_CE)
    {
        if(g_irda_frame_escaped)
        {
            ++g_irda_stats.frame_error;
            IrdaLink_ResetFrame();
        }
        else
        {
            g_irda_frame_escaped = 1u;
        }
        return;
    }
    if(g_irda_frame_escaped)
    {
        value ^= IRDA_XOR;
        g_irda_frame_escaped = 0u;
    }
    if(g_irda_frame_length >= IRDA_FRAME_MAX_LEN)
    {
        ++g_irda_stats.frame_error;
        IrdaLink_ResetFrame();
        return;
    }
    g_irda_frame[g_irda_frame_length++] = value;
}

static void IrdaLink_StartConfig(void)
{
    GPIOB_ResetBits(BOARD_IRDA_SHUTDOWN_PIN);
    GPIOB_ResetBits(BOARD_IRDA_MODE_PIN);
    g_irda_state = IRDA_STATE_WAKE;
    g_irda_deadline = TMOS_GetSystemClock() + IRDA_WAKE_TICKS;
}

static uint8_t IrdaLink_SendConfigByte(uint8_t value)
{
    if(R8_UART0_TFC == UART_FIFO_SIZE)
    {
        return 0u;
    }
    UART0_SendByte(value);
    g_irda_expected_echo = value;
    g_irda_deadline = TMOS_GetSystemClock() + IRDA_CONFIG_TIMEOUT_TICKS;
    return 1u;
}

static void IrdaLink_ConfigFailure(void)
{
    ++g_irda_stats.config_error;
    if(g_irda_retry < IRDA_CONFIG_RETRIES)
    {
        ++g_irda_retry;
        ++g_irda_stats.config_retry;
        IrdaLink_StartConfig();
    }
    else
    {
        g_irda_state = IRDA_STATE_DISABLED;
        g_irda_stats.fault = 1u;
        GPIOB_SetBits(BOARD_IRDA_ENABLE_PIN | BOARD_IRDA_SHUTDOWN_PIN |
                      BOARD_IRDA_MODE_PIN);
        UART0_INTCfg(DISABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
        PFIC_DisableIRQ(UART0_IRQn);
    }
}

void IrdaLink_Init(void)
{
    StaticSpscRing_Init(&g_irda_rx_ring,
                        g_irda_rx_storage,
                        sizeof(g_irda_rx_storage[0]),
                        IRDA_RX_CAPACITY);
    g_irda_stats = (IrdaLinkStats){0};
    IrdaLink_ResetFrame();
    g_irda_retry = 0u;
    g_irda_expected_echo = 0u;
    g_irda_remote_paused = 0u;
    g_irda_resume_deadline = 0u;
    GPIOB_ModeCfg(BOARD_UART0_RX_PIN, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(BOARD_UART0_TX_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_SetBits(BOARD_IRDA_ENABLE_PIN | BOARD_IRDA_SHUTDOWN_PIN |
                  BOARD_IRDA_MODE_PIN);
    GPIOB_ModeCfg(BOARD_IRDA_ENABLE_PIN | BOARD_IRDA_SHUTDOWN_PIN |
                  BOARD_IRDA_MODE_PIN, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
    UART0_BaudRateCfg(BOARD_IRDA_INITIAL_BAUDRATE);
    UART0_ByteTrigCfg(UART_1BYTE_TRIG);
    UART0_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART0_IRQn);
    IrdaLink_StartConfig();
}

void IrdaLink_SetRemoteTxActive(uint8_t active)
{
    if(active != 0u)
    {
        if(g_irda_remote_paused == 0u)
        {
            ++g_irda_stats.remote_pause_count;
        }
        g_irda_remote_paused = 1u;
        /* TFBS4711 SD is active high. Shutting it down prevents the
         * remote-control carrier from entering the IrDA optical path. */
        GPIOB_SetBits(BOARD_IRDA_SHUTDOWN_PIN);
    }
    else if(g_irda_remote_paused != 0u)
    {
        g_irda_remote_paused = 0u;
        GPIOB_ResetBits(BOARD_IRDA_SHUTDOWN_PIN);
        /* Vishay specifies a finite startup time after SD goes low. Keep
         * the parser quiet for at least one 625 us TMOS tick. */
        g_irda_resume_deadline = TMOS_GetSystemClock() + 1u;
    }
}

void IrdaLink_Process(void)
{
    IrdaRxItem item;
    uint8_t processed = 0u;
    uint32_t now = TMOS_GetSystemClock();

    if(g_irda_state == IRDA_STATE_DISABLED)
    {
        return;
    }
    if(g_irda_state == IRDA_STATE_WAKE && IrdaLink_Due(now, g_irda_deadline))
    {
        if(IrdaLink_SendConfigByte(IRDA_MCP_BAUD_9600))
        {
            g_irda_state = IRDA_STATE_WAIT_BAUD_ECHO;
        }
    }
    else if((g_irda_state == IRDA_STATE_WAIT_BAUD_ECHO) ||
            (g_irda_state == IRDA_STATE_WAIT_APPLY_ECHO))
    {
        if(IrdaLink_Due(now, g_irda_deadline))
        {
            IrdaLink_ConfigFailure();
        }
    }

    while(processed < IRDA_PROCESS_BUDGET &&
          StaticSpscRing_Pop(&g_irda_rx_ring, &item))
    {
        ++processed;
        if((item.status & (STA_ERR_BREAK | STA_ERR_FRAME |
                           STA_ERR_PAR | STA_ERR_FIFOOV)) != 0u)
        {
            continue;
        }
        if((g_irda_state == IRDA_STATE_WAIT_BAUD_ECHO) ||
           (g_irda_state == IRDA_STATE_WAIT_APPLY_ECHO))
        {
            if(item.byte != g_irda_expected_echo)
            {
                IrdaLink_ConfigFailure();
                break;
            }
            if(g_irda_state == IRDA_STATE_WAIT_BAUD_ECHO)
            {
                g_irda_state = IRDA_STATE_SEND_APPLY;
            }
            else
            {
                g_irda_state = IRDA_STATE_DATA;
                g_irda_stats.ready = 1u;
                GPIOB_SetBits(BOARD_IRDA_MODE_PIN);
            }
            continue;
        }
        if(g_irda_state == IRDA_STATE_SEND_APPLY)
        {
            if(IrdaLink_SendConfigByte(IRDA_MCP_BAUD_APPLY))
            {
                g_irda_state = IRDA_STATE_WAIT_APPLY_ECHO;
            }
            continue;
        }
        if(g_irda_state == IRDA_STATE_DATA &&
           !g_irda_remote_paused &&
           IrdaLink_Due(TMOS_GetSystemClock(), g_irda_resume_deadline))
        {
            IrdaLink_ProcessFrameByte(item.byte);
        }
        else if(g_irda_state == IRDA_STATE_DATA)
        {
            ++g_irda_stats.remote_blocked_bytes;
        }
    }

    if(g_irda_state == IRDA_STATE_SEND_APPLY)
    {
        (void)IrdaLink_SendConfigByte(IRDA_MCP_BAUD_APPLY);
        g_irda_state = IRDA_STATE_WAIT_APPLY_ECHO;
    }
}

void IrdaLink_GetStats(IrdaLinkStats *stats)
{
    if(stats != 0)
    {
        *stats = g_irda_stats;
    }
}

__INTERRUPT
__HIGH_CODE
void UART0_IRQHandler(void)
{
    uint8_t interrupt = UART0_GetITFlag();
    uint8_t status;
    IrdaRxItem item;

    if((interrupt == UART_II_LINE_STAT) ||
       (interrupt == UART_II_RECV_RDY) ||
       (interrupt == UART_II_RECV_TOUT))
    {
        status = UART0_GetLinSTA();
        if((status & (STA_ERR_BREAK | STA_ERR_FRAME |
                      STA_ERR_PAR | STA_ERR_FIFOOV)) != 0u)
        {
            ++g_irda_stats.line_error;
        }
        while(R8_UART0_RFC != 0u)
        {
            item.byte = UART0_RecvByte();
            item.status = status;
            item.reserved = 0u;
            if(!StaticSpscRing_Push(&g_irda_rx_ring, &item))
            {
                ++g_irda_stats.rx_overrun;
            }
        }
    }
}
