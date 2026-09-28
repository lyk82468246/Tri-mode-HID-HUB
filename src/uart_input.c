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
static UartRxItem g_uart3_rx_storage[UART_INPUT_RX_CAPACITY]
    EVENT_ROUTER_ALIGN4;
static StaticSpscRing g_uart3_rx_ring;

static uint8_t g_uart_frame[UART_INPUT_FRAME_MAX_LEN] EVENT_ROUTER_ALIGN4;
static uint8_t g_uart_frame_length;
static uint8_t g_uart_frame_idle_ticks;
static uint8_t g_uart3_frame[UART_INPUT_FRAME_MAX_LEN] EVENT_ROUTER_ALIGN4;
static uint8_t g_uart3_frame_length;
static uint8_t g_uart3_frame_idle_ticks;

static volatile uint32_t g_uart_rx_overrun;
static volatile uint32_t g_uart_line_error;
static uint32_t g_uart_frame_flush;
static uint32_t g_uart_frame_backpressure;
static volatile uint32_t g_uart3_rx_overrun;
static volatile uint32_t g_uart3_line_error;
static uint32_t g_uart3_frame_flush;
static uint32_t g_uart3_frame_backpressure;

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

static void UartInput_DrainUart3Fifo(uint8_t line_status)
{
    UartRxItem item;

    if((line_status & UART_INPUT_LINE_ERROR_MASK) != 0u)
    {
        ++g_uart3_line_error;
    }
    while(R8_UART3_RFC != 0u)
    {
        item.byte = UART3_RecvByte();
        item.status = line_status;
        item.reserved = 0u;
        if(!StaticSpscRing_Push(&g_uart3_rx_ring, &item))
        {
            ++g_uart3_rx_overrun;
        }
    }
}

static uint8_t UartInput_TryFlushFrame(uint8_t *frame,
                                       uint8_t *length,
                                       uint8_t *idle_ticks,
                                       uint8_t source,
                                       uint32_t *backpressure,
                                       uint32_t *flush_count)
{
    if(*length == 0u)
    {
        return 1u;
    }
    if(!EventRouter_InjectStreamData(source, frame, *length))
    {
        ++*backpressure;
        return 0u;
    }
    *length = 0u;
    *idle_ticks = 0u;
    ++*flush_count;
    return 1u;
}

static void UartInput_ProcessPort(StaticSpscRing *ring,
                                  uint8_t *frame,
                                  uint8_t *frame_length,
                                  uint8_t *frame_idle_ticks,
                                  uint8_t source,
                                  uint32_t *frame_backpressure,
                                  uint32_t *frame_flush)
{
    UartRxItem item;
    uint8_t processed = 0u;

    /* A full frame is held until the router accepts it. */
    if((*frame_length == UART_INPUT_FRAME_MAX_LEN) &&
       !UartInput_TryFlushFrame(frame, frame_length, frame_idle_ticks,
                                source, frame_backpressure, frame_flush))
    {
        return;
    }

    while(processed < UART_INPUT_PROCESS_BUDGET)
    {
        if((*frame_length == UART_INPUT_FRAME_MAX_LEN) &&
           !UartInput_TryFlushFrame(frame, frame_length, frame_idle_ticks,
                                    source, frame_backpressure, frame_flush))
        {
            return;
        }
        if(!StaticSpscRing_Pop(ring, &item))
        {
            break;
        }
        ++processed;
        if((item.status & UART_INPUT_LINE_ERROR_MASK) != 0u)
        {
            continue;
        }
        frame[(*frame_length)++] = item.byte;
        *frame_idle_ticks = 0u;
        if((item.byte == '\r') || (item.byte == '\n') ||
           (*frame_length == UART_INPUT_FRAME_MAX_LEN))
        {
            if(!UartInput_TryFlushFrame(frame, frame_length,
                                        frame_idle_ticks, source,
                                        frame_backpressure, frame_flush))
            {
                return;
            }
        }
    }

    if((processed == 0u) && (*frame_length != 0u) &&
       (++*frame_idle_ticks >= UART_INPUT_FRAME_TIMEOUT_TICKS))
    {
        (void)UartInput_TryFlushFrame(frame, frame_length, frame_idle_ticks,
                                      source, frame_backpressure, frame_flush);
    }
}

void UartInput_Init(void)
{
    StaticSpscRing_Init(&g_uart_rx_ring,
                        g_uart_rx_storage,
                        sizeof(g_uart_rx_storage[0]),
                        UART_INPUT_RX_CAPACITY);
    StaticSpscRing_Init(&g_uart3_rx_ring,
                        g_uart3_rx_storage,
                        sizeof(g_uart3_rx_storage[0]),
                        UART_INPUT_RX_CAPACITY);
    g_uart_frame_length = 0u;
    g_uart_frame_idle_ticks = 0u;
    g_uart_rx_overrun = 0u;
    g_uart_line_error = 0u;
    g_uart_frame_flush = 0u;
    g_uart_frame_backpressure = 0u;
    g_uart3_frame_length = 0u;
    g_uart3_frame_idle_ticks = 0u;
    g_uart3_rx_overrun = 0u;
    g_uart3_line_error = 0u;
    g_uart3_frame_flush = 0u;
    g_uart3_frame_backpressure = 0u;

    /* Rev B keeps UART1 on PA8/PA9 for the MAX3232 RS232 path. */
    GPIOA_SetBits(BOARD_UART1_TX_PIN);
    GPIOA_ModeCfg(BOARD_UART1_RX_PIN, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(BOARD_UART1_TX_PIN, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
    UART1_BaudRateCfg(BOARD_UART1_BAUDRATE);
    UART1_ByteTrigCfg(UART_4BYTE_TRIG);
    UART1_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART1_IRQn);

    /* UART3 is an independent 3.3 V TTL connector on PA4/PA5. */
    GPIOA_SetBits(BOARD_UART3_TX_PIN);
    GPIOA_ModeCfg(BOARD_UART3_RX_PIN, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(BOARD_UART3_TX_PIN, GPIO_ModeOut_PP_5mA);
    UART3_DefInit();
    UART3_BaudRateCfg(BOARD_UART3_BAUDRATE);
    UART3_ByteTrigCfg(UART_4BYTE_TRIG);
    UART3_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART3_IRQn);
}

void UartInput_Process(void)
{
    UartInput_ProcessPort(&g_uart_rx_ring, g_uart_frame,
                          &g_uart_frame_length, &g_uart_frame_idle_ticks,
                          ROUTER_SRC_UART, &g_uart_frame_backpressure,
                          &g_uart_frame_flush);
    UartInput_ProcessPort(&g_uart3_rx_ring, g_uart3_frame,
                          &g_uart3_frame_length, &g_uart3_frame_idle_ticks,
                          ROUTER_SRC_UART3, &g_uart3_frame_backpressure,
                          &g_uart3_frame_flush);
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
    stats->uart3_rx_overrun = g_uart3_rx_overrun;
    stats->uart3_line_error = g_uart3_line_error;
    stats->uart3_frame_flush = g_uart3_frame_flush;
    stats->uart3_frame_backpressure = g_uart3_frame_backpressure;
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

__INTERRUPT
__HIGH_CODE
void UART3_IRQHandler(void)
{
    uint8_t interrupt = UART3_GetITFlag();

    switch(interrupt)
    {
        case UART_II_LINE_STAT:
            UartInput_DrainUart3Fifo(UART3_GetLinSTA());
            break;

        case UART_II_RECV_RDY:
        case UART_II_RECV_TOUT:
            UartInput_DrainUart3Fifo(UART3_GetLinSTA());
            break;

        case UART_II_THR_EMPTY:
        case UART_II_MODEM_CHG:
        default:
            break;
    }
}
