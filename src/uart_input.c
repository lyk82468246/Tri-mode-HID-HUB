#include "CH58x_common.h"

#include "board_pins.h"
#include "event_router.h"
#include "static_spsc_ring.h"
#include "uart_input.h"

#define UART_INPUT_RX_CAPACITY             128u
#define UART_INPUT_PROCESS_BUDGET           32u
#define UART_INPUT_FRAME_MAX_LEN            20u
#define UART_INPUT_FRAME_TIMEOUT_TICKS       3u /* 6 ms at the 2 ms tick */

#define UART_INPUT_LINE_ERROR_MASK \
    (STA_ERR_BREAK | STA_ERR_FRAME | STA_ERR_PAR | STA_ERR_FIFOOV)

typedef struct
{
    uint8_t byte;
    uint8_t status;
    uint16_t reserved;
} UartRxItem;

static UartRxItem g_uart_rx_storage[UART_INPUT_RX_CAPACITY]
    EVENT_ROUTER_ALIGN4;
static StaticSpscRing g_uart_rx_ring;

static uint8_t g_uart_frame[UART_INPUT_FRAME_MAX_LEN] EVENT_ROUTER_ALIGN4;
static uint8_t g_uart_frame_length;
static uint8_t g_uart_frame_idle_ticks;

static volatile uint32_t g_uart_rx_overrun;
static volatile uint32_t g_uart_line_error;
static uint32_t g_uart_frame_flush;
static uint32_t g_uart_frame_backpressure;

static void UartInput_DrainRxFifo(uint8_t line_status)
{
    UartRxItem item;

    if((line_status & UART_INPUT_LINE_ERROR_MASK) != 0u)
    {
        ++g_uart_line_error;
    }
    while(R8_UART1_RFC != 0u)
    {
        item.byte = UART1_RecvByte();
        item.status = line_status;
        item.reserved = 0u;
        if(!StaticSpscRing_Push(&g_uart_rx_ring, &item))
        {
            ++g_uart_rx_overrun;
        }
    }
}

static uint8_t UartInput_TryFlushFrame(void)
{
    if(g_uart_frame_length == 0u)
    {
        return 1u;
    }
    if(!EventRouter_InjectStreamData(ROUTER_SRC_UART,
                                     g_uart_frame,
                                     g_uart_frame_length))
    {
        ++g_uart_frame_backpressure;
        return 0u;
    }
    g_uart_frame_length = 0u;
    g_uart_frame_idle_ticks = 0u;
    ++g_uart_frame_flush;
    return 1u;
}

void UartInput_Init(void)
{
    StaticSpscRing_Init(&g_uart_rx_ring,
                        g_uart_rx_storage,
                        sizeof(g_uart_rx_storage[0]),
                        UART_INPUT_RX_CAPACITY);
    g_uart_frame_length = 0u;
    g_uart_frame_idle_ticks = 0u;
    g_uart_rx_overrun = 0u;
    g_uart_line_error = 0u;
    g_uart_frame_flush = 0u;
    g_uart_frame_backpressure = 0u;

    /* UART1 is PA8/PA9 in the first PCB plan and on the bench harness. */
    GPIOA_SetBits(BOARD_UART1_TX_PIN);
    GPIOA_ModeCfg(BOARD_UART1_RX_PIN, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(BOARD_UART1_TX_PIN, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    UART1_BaudRateCfg(BOARD_UART1_BAUDRATE);
    UART1_ByteTrigCfg(UART_4BYTE_TRIG);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART1_IRQn);
}

void UartInput_Process(void)
{
    UartRxItem item;
    uint8_t processed = 0u;

    /* A full frame is held until the router accepts it.  This keeps router
     * backpressure from silently discarding UART bytes. */
    if((g_uart_frame_length == UART_INPUT_FRAME_MAX_LEN) &&
       !UartInput_TryFlushFrame())
    {
        return;
    }

    while(processed < UART_INPUT_PROCESS_BUDGET)
    {
        if((g_uart_frame_length == UART_INPUT_FRAME_MAX_LEN) &&
           !UartInput_TryFlushFrame())
        {
            return;
        }
        if(!StaticSpscRing_Pop(&g_uart_rx_ring, &item))
        {
            break;
        }
        ++processed;

        if((item.status & UART_INPUT_LINE_ERROR_MASK) != 0u)
        {
            /* The byte was captured to drain hardware FIFO state, but a
             * framing/parity/overrun byte must not enter the data stream. */
            continue;
        }
        g_uart_frame[g_uart_frame_length++] = item.byte;
        g_uart_frame_idle_ticks = 0u;

        if((item.byte == '\r') || (item.byte == '\n') ||
           (g_uart_frame_length == UART_INPUT_FRAME_MAX_LEN))
        {
            if(!UartInput_TryFlushFrame())
            {
                return;
            }
        }
    }

    if((processed == 0u) && (g_uart_frame_length != 0u))
    {
        if(++g_uart_frame_idle_ticks >= UART_INPUT_FRAME_TIMEOUT_TICKS)
        {
            (void)UartInput_TryFlushFrame();
        }
    }
}

void UartInput_GetStats(UartInputStats *stats)
{
    if(stats == NULL)
    {
        return;
    }
    stats->rx_overrun = g_uart_rx_overrun;
    stats->line_error = g_uart_line_error;
    stats->frame_flush = g_uart_frame_flush;
    stats->frame_backpressure = g_uart_frame_backpressure;
}

__INTERRUPT
__HIGH_CODE
void UART1_IRQHandler(void)
{
    uint8_t interrupt = UART1_GetITFlag();

    switch(interrupt)
    {
        case UART_II_LINE_STAT:
            UartInput_DrainRxFifo(UART1_GetLinSTA());
            break;

        case UART_II_RECV_RDY:
        case UART_II_RECV_TOUT:
            UartInput_DrainRxFifo(UART1_GetLinSTA());
            break;

        case UART_II_THR_EMPTY:
        case UART_II_MODEM_CHG:
        default:
            break;
    }
}
