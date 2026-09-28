#include "CH58x_common.h"
#include "CH58x_i2c.h"
#include "CH58x_spi.h"
#include "HAL.h"

#include "board_bus.h"
#include "board_pins.h"
#include "event_router.h"

#define BOARD_BUS_I2C_TIMEOUT_TICKS     320u /* 200 ms at 0.625 ms/tick */
#define BOARD_BUS_SPI_TIMEOUT_TICKS      80u /* 50 ms at 0.625 ms/tick */
#define BOARD_BUS_RESPONSE_MAX_LEN      20u
#define BOARD_BUS_STATUS_OK              0u
#define BOARD_BUS_STATUS_BUSY            1u
#define BOARD_BUS_STATUS_TIMEOUT         2u
#define BOARD_BUS_STATUS_ERROR           3u

typedef enum
{
    BUS_I2C_IDLE = 0,
    BUS_I2C_WRITE_ADDRESS,
    BUS_I2C_WRITE_DATA,
    BUS_I2C_RESTART,
    BUS_I2C_READ_ADDRESS,
    BUS_I2C_READ_DATA
} BoardBusI2cPhase;

static BoardBusStats g_bus_stats __attribute__((aligned(4)));

static volatile uint8_t g_i2c_busy;
static volatile uint8_t g_i2c_result_pending;
static volatile uint8_t g_i2c_result_status;
static uint8_t g_i2c_address;
static uint8_t g_i2c_write[BOARD_BUS_I2C_MAX_WRITE]
    __attribute__((aligned(4)));
static uint8_t g_i2c_read[BOARD_BUS_I2C_MAX_READ]
    __attribute__((aligned(4)));
static uint8_t g_i2c_write_length;
static uint8_t g_i2c_read_length;
static volatile uint8_t g_i2c_write_index;
static volatile uint8_t g_i2c_read_index;
static volatile uint8_t g_i2c_phase;
static volatile uint32_t g_i2c_deadline;

static volatile uint8_t g_spi_busy;
static volatile uint8_t g_spi_result_pending;
static volatile uint8_t g_spi_result_status;
static uint8_t g_spi_tx[BOARD_BUS_SPI_MAX_TRANSFER]
    __attribute__((aligned(4)));
static uint8_t g_spi_rx[BOARD_BUS_SPI_MAX_TRANSFER]
    __attribute__((aligned(4)));
static uint8_t g_spi_length;
static volatile uint8_t g_spi_index;
static volatile uint32_t g_spi_deadline;

static uint8_t g_response[BOARD_BUS_RESPONSE_MAX_LEN]
    __attribute__((aligned(4)));
static uint8_t g_response_length;
static volatile uint8_t g_response_pending;

static uint8_t BoardBus_Due(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1u : 0u;
}

static void BoardBus_DisableI2cIrq(void)
{
    I2C_ITConfig(I2C_IT_BUF, DISABLE);
    I2C_ITConfig(I2C_IT_EVT, DISABLE);
    I2C_ITConfig(I2C_IT_ERR, DISABLE);
}

static void BoardBus_ConfigureSpi(void)
{
    GPIOA_SetBits(BOARD_SPI_CS_PIN);
    GPIOA_ModeCfg(BOARD_SPI_CS_PIN | BOARD_SPI_SCK_PIN |
                  BOARD_SPI_MOSI_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(BOARD_SPI_MISO_PIN, GPIO_ModeIN_Floating);
    SPI0_MasterDefInit();
    SPI0_CLKCfg(4u);
    SPI0_DataMode(Mode0_HighBitINFront);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_BYTE_END |
                        SPI0_IT_FIFO_OV);
    PFIC_EnableIRQ(SPI0_IRQn);
}

static void BoardBus_I2cFinish(uint8_t status)
{
    I2C_GenerateSTOP(ENABLE);
    BoardBus_DisableI2cIrq();
    g_i2c_result_status = status;
    g_i2c_result_pending = 1u;
    g_i2c_busy = 0u;
    g_i2c_phase = BUS_I2C_IDLE;
    if(status == BOARD_BUS_STATUS_OK)
    {
        ++g_bus_stats.i2c_complete;
    }
    else if(status == BOARD_BUS_STATUS_TIMEOUT)
    {
        ++g_bus_stats.i2c_timeout;
    }
    else
    {
        ++g_bus_stats.i2c_error;
    }
}

static void BoardBus_SpiFinish(uint8_t status)
{
    GPIOA_SetBits(BOARD_SPI_CS_PIN);
    SPI0_ITCfg(DISABLE, SPI0_IT_CNT_END | SPI0_IT_BYTE_END |
                        SPI0_IT_FIFO_OV);
    g_spi_result_status = status;
    g_spi_result_pending = 1u;
    g_spi_busy = 0u;
    if(status == BOARD_BUS_STATUS_OK)
    {
        ++g_bus_stats.spi_complete;
    }
    else if(status == BOARD_BUS_STATUS_TIMEOUT)
    {
        ++g_bus_stats.spi_timeout;
    }
    else
    {
        ++g_bus_stats.spi_error;
    }
}

static void BoardBus_PublishResult(void)
{
    uint8_t i;

    if(g_i2c_result_pending && !g_response_pending)
    {
        g_response[0] = 0xB5u;
        g_response[1] = ROUTER_CONTROL_I2C_TRANSFER;
        g_response[2] = g_i2c_result_status;
        g_response[3] = (g_i2c_result_status == BOARD_BUS_STATUS_OK) ?
                            g_i2c_read_length : 0u;
        for(i = 0u; i < g_response[3]; ++i)
        {
            g_response[(uint8_t)(4u + i)] = g_i2c_read[i];
        }
        g_response_length = (uint8_t)(4u + g_response[3]);
        g_response_pending = 1u;
        g_i2c_result_pending = 0u;
    }
    if(g_spi_result_pending && !g_response_pending)
    {
        g_response[0] = 0xB5u;
        g_response[1] = ROUTER_CONTROL_SPI_TRANSFER;
        g_response[2] = g_spi_result_status;
        g_response[3] = (g_spi_result_status == BOARD_BUS_STATUS_OK) ?
                            g_spi_length : 0u;
        for(i = 0u; i < g_response[3]; ++i)
        {
            g_response[(uint8_t)(4u + i)] = g_spi_rx[i];
        }
        g_response_length = (uint8_t)(4u + g_response[3]);
        g_response_pending = 1u;
        g_spi_result_pending = 0u;
    }
    if(g_response_pending)
    {
        if(EventRouter_InjectStreamData(ROUTER_SRC_BUS,
                                         g_response, g_response_length))
        {
            g_response_pending = 0u;
            ++g_bus_stats.response_published;
        }
        else
        {
            ++g_bus_stats.response_backpressure;
        }
    }
    g_bus_stats.i2c_busy = g_i2c_busy;
    g_bus_stats.spi_busy = g_spi_busy;
    g_bus_stats.response_pending = g_response_pending;
}

void BoardBus_Init(void)
{
    g_bus_stats = (BoardBusStats){0};
    g_i2c_busy = 0u;
    g_i2c_result_pending = 0u;
    g_spi_busy = 0u;
    g_spi_result_pending = 0u;
    g_response_pending = 0u;

    GPIOB_ModeCfg(BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN,
                  GPIO_ModeIN_Floating);
    I2C_Init(I2C_Mode_I2C, 400000u, I2C_DutyCycle_2,
             I2C_Ack_Enable, I2C_AckAddr_7bit, 0u);
    BoardBus_DisableI2cIrq();
    PFIC_EnableIRQ(I2C_IRQn);

    BoardBus_ConfigureSpi();
}

uint8_t BoardBus_SubmitI2c(uint8_t address7,
                           const uint8_t *write_data,
                           uint8_t write_length,
                           uint8_t read_length)
{
    uint8_t i;

    if(address7 > 0x7Fu || write_length > BOARD_BUS_I2C_MAX_WRITE ||
       read_length > BOARD_BUS_I2C_MAX_READ ||
       ((write_length == 0u) && (read_length == 0u)) ||
       ((write_data == 0) && (write_length != 0u)) ||
       g_i2c_busy || g_i2c_result_pending || g_response_pending)
    {
        ++g_bus_stats.i2c_reject;
        return 0u;
    }
    for(i = 0u; i < write_length; ++i)
    {
        g_i2c_write[i] = write_data[i];
    }
    g_i2c_address = (uint8_t)(address7 << 1);
    g_i2c_write_length = write_length;
    g_i2c_read_length = read_length;
    g_i2c_write_index = 0u;
    g_i2c_read_index = 0u;
    g_i2c_phase = (write_length != 0u) ? BUS_I2C_WRITE_ADDRESS :
                                        BUS_I2C_READ_ADDRESS;
    g_i2c_busy = 1u;
    g_i2c_deadline = TMOS_GetSystemClock() + BOARD_BUS_I2C_TIMEOUT_TICKS;
    I2C_AcknowledgeConfig(ENABLE);
    I2C_GenerateSTART(ENABLE);
    I2C_ITConfig(I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, ENABLE);
    return 1u;
}

uint8_t BoardBus_SubmitSpi(const uint8_t *tx_data, uint8_t length)
{
    uint8_t i;

    if((tx_data == 0) || length == 0u ||
       length > BOARD_BUS_SPI_MAX_TRANSFER || g_spi_busy ||
       g_spi_result_pending || g_response_pending)
    {
        ++g_bus_stats.spi_reject;
        return 0u;
    }
    /* Reapply the complete mode after a timeout/error may have cleared CTRL_MOD. */
    BoardBus_ConfigureSpi();
    for(i = 0u; i < length; ++i)
    {
        g_spi_tx[i] = tx_data[i];
        g_spi_rx[i] = 0u;
    }
    g_spi_length = length;
    g_spi_index = 0u;
    g_spi_busy = 1u;
    g_spi_deadline = TMOS_GetSystemClock() + BOARD_BUS_SPI_TIMEOUT_TICKS;
    GPIOA_ResetBits(BOARD_SPI_CS_PIN);
    R8_SPI0_CTRL_MOD &= (uint8_t)~RB_SPI_FIFO_DIR;
    R16_SPI0_TOTAL_CNT = length;
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_BYTE_END |
                       RB_SPI_IF_FIFO_OV;
    SPI0_ITCfg(ENABLE, SPI0_IT_CNT_END | SPI0_IT_BYTE_END |
                       SPI0_IT_FIFO_OV);
    R8_SPI0_BUFFER = g_spi_tx[0];
    return 1u;
}

void BoardBus_Process(void)
{
    uint32_t now = TMOS_GetSystemClock();

    if(g_i2c_busy && BoardBus_Due(now, g_i2c_deadline))
    {
        I2C_SoftwareResetCmd(ENABLE);
        I2C_SoftwareResetCmd(DISABLE);
        BoardBus_I2cFinish(BOARD_BUS_STATUS_TIMEOUT);
    }
    if(g_spi_busy && BoardBus_Due(now, g_spi_deadline))
    {
        R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;
        BoardBus_SpiFinish(BOARD_BUS_STATUS_TIMEOUT);
    }
    BoardBus_PublishResult();
}

uint8_t BoardBus_HandleControlFrame(const RouterEvent *event)
{
    uint8_t write_length;
    uint8_t read_length;
    uint8_t length;

    if(event == 0 || ((event->source != ROUTER_SRC_USB_CDC) &&
                      (event->source != ROUTER_SRC_BLE_NUS)) ||
       event->length < 4u ||
       event->payload.raw[0] != ROUTER_CONTROL_MAGIC_0 ||
       event->payload.raw[1] != ROUTER_CONTROL_MAGIC_1)
    {
        return 0u;
    }
    if(event->payload.raw[2] == ROUTER_CONTROL_I2C_TRANSFER)
    {
        if(event->length < 6u)
        {
            return 0u;
        }
        write_length = event->payload.raw[4];
        read_length = event->payload.raw[5];
        if(write_length > BOARD_BUS_I2C_MAX_WRITE ||
           read_length > BOARD_BUS_I2C_MAX_READ ||
           event->length != (uint8_t)(6u + write_length))
        {
            return 0u;
        }
        return BoardBus_SubmitI2c(event->payload.raw[3],
                                  &event->payload.raw[6],
                                  write_length, read_length);
    }
    if(event->payload.raw[2] == ROUTER_CONTROL_SPI_TRANSFER)
    {
        length = event->payload.raw[3];
        if(length == 0u || length > BOARD_BUS_SPI_MAX_TRANSFER ||
           event->length != (uint8_t)(4u + length))
        {
            return 0u;
        }
        return BoardBus_SubmitSpi(&event->payload.raw[4], length);
    }
    return 0u;
}

void BoardBus_GetStats(BoardBusStats *stats)
{
    if(stats != 0)
    {
        *stats = g_bus_stats;
        stats->i2c_busy = g_i2c_busy;
        stats->spi_busy = g_spi_busy;
        stats->response_pending = g_response_pending;
    }
}

__INTERRUPT
__HIGH_CODE
void I2C_IRQHandler(void)
{
    uint32_t event;
    uint16_t status;

    if(!g_i2c_busy)
    {
        BoardBus_DisableI2cIrq();
        return;
    }
    status = R16_I2C_STAR1;
    if((status & (RB_I2C_BERR | RB_I2C_ARLO | RB_I2C_AF |
                  RB_I2C_TIMEOUT | RB_I2C_OVR)) != 0u)
    {
        R16_I2C_STAR1 = (uint16_t)~status;
        BoardBus_I2cFinish(BOARD_BUS_STATUS_ERROR);
        return;
    }
    event = I2C_GetLastEvent();
    if((event & I2C_FLAG_SB) != 0u)
    {
        if(g_i2c_phase == BUS_I2C_RESTART ||
           ((g_i2c_write_length == 0u) &&
            (g_i2c_phase == BUS_I2C_READ_ADDRESS)))
        {
            I2C_Send7bitAddress(g_i2c_address, I2C_Direction_Receiver);
            g_i2c_phase = BUS_I2C_READ_ADDRESS;
        }
        else
        {
            I2C_Send7bitAddress(g_i2c_address, I2C_Direction_Transmitter);
            g_i2c_phase = BUS_I2C_WRITE_ADDRESS;
        }
        return;
    }
    if((event & I2C_FLAG_ADDR) != 0u)
    {
        (void)R16_I2C_STAR1;
        (void)R16_I2C_STAR2;
        if(g_i2c_phase == BUS_I2C_READ_ADDRESS)
        {
            g_i2c_phase = BUS_I2C_READ_DATA;
            if(g_i2c_read_length == 1u)
            {
                I2C_AcknowledgeConfig(DISABLE);
                I2C_GenerateSTOP(ENABLE);
            }
        }
        else if(g_i2c_write_length != 0u)
        {
            I2C_SendData(g_i2c_write[g_i2c_write_index++]);
            g_i2c_phase = BUS_I2C_WRITE_DATA;
        }
        else
        {
            BoardBus_I2cFinish(BOARD_BUS_STATUS_OK);
        }
        return;
    }
    if(g_i2c_phase == BUS_I2C_WRITE_DATA &&
       ((event & I2C_FLAG_TXE) != 0u || (event & I2C_FLAG_BTF) != 0u))
    {
        if(g_i2c_write_index < g_i2c_write_length)
        {
            I2C_SendData(g_i2c_write[g_i2c_write_index++]);
        }
        else if(g_i2c_read_length != 0u)
        {
            I2C_GenerateSTART(ENABLE);
            g_i2c_phase = BUS_I2C_RESTART;
        }
        else
        {
            BoardBus_I2cFinish(BOARD_BUS_STATUS_OK);
        }
        return;
    }
    if(g_i2c_phase == BUS_I2C_READ_DATA &&
       (event & I2C_FLAG_RXNE) != 0u)
    {
        g_i2c_read[g_i2c_read_index++] = I2C_ReceiveData();
        if(g_i2c_read_index >= g_i2c_read_length)
        {
            BoardBus_I2cFinish(BOARD_BUS_STATUS_OK);
        }
        else if((uint8_t)(g_i2c_read_length - g_i2c_read_index) == 1u)
        {
            I2C_AcknowledgeConfig(DISABLE);
            I2C_GenerateSTOP(ENABLE);
        }
        return;
    }
    if(g_i2c_phase == BUS_I2C_READ_ADDRESS &&
       (event & I2C_FLAG_ADDR) == 0u)
    {
        g_i2c_phase = BUS_I2C_READ_DATA;
    }
}

__INTERRUPT
__HIGH_CODE
void SPI0_IRQHandler(void)
{
    uint8_t flags = R8_SPI0_INT_FLAG;

    if(!g_spi_busy)
    {
        R8_SPI0_INT_FLAG = flags;
        return;
    }
    if((flags & RB_SPI_IF_FIFO_OV) != 0u)
    {
        R8_SPI0_INT_FLAG = RB_SPI_IF_FIFO_OV;
        BoardBus_SpiFinish(BOARD_BUS_STATUS_ERROR);
        return;
    }
    if((flags & RB_SPI_IF_BYTE_END) != 0u)
    {
        R8_SPI0_INT_FLAG = RB_SPI_IF_BYTE_END;
        g_spi_rx[g_spi_index++] = R8_SPI0_BUFFER;
        if(g_spi_index >= g_spi_length)
        {
            BoardBus_SpiFinish(BOARD_BUS_STATUS_OK);
        }
        else
        {
            R8_SPI0_BUFFER = g_spi_tx[g_spi_index];
        }
    }
}
