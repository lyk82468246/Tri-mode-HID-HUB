#ifndef TRI_MODE_HID_HUB_BOARD_BUS_H
#define TRI_MODE_HID_HUB_BOARD_BUS_H

#include <stdint.h>

#include "event_router_types.h"

#define BOARD_BUS_I2C_MAX_WRITE        14u
#define BOARD_BUS_I2C_MAX_READ         16u
#define BOARD_BUS_SPI_MAX_TRANSFER     16u

typedef struct
{
    uint32_t i2c_complete;
    uint32_t i2c_error;
    uint32_t i2c_timeout;
    uint32_t i2c_reject;
    uint32_t spi_complete;
    uint32_t spi_error;
    uint32_t spi_timeout;
    uint32_t spi_reject;
    uint32_t response_backpressure;
    uint32_t response_published;
    uint8_t i2c_busy;
    uint8_t spi_busy;
    uint8_t response_pending;
} BoardBusStats;

void BoardBus_Init(void);
void BoardBus_Process(void);
uint8_t BoardBus_SubmitI2c(uint8_t address7,
                           const uint8_t *write_data,
                           uint8_t write_length,
                           uint8_t read_length);
uint8_t BoardBus_SubmitSpi(const uint8_t *tx_data, uint8_t length);
uint8_t BoardBus_HandleControlFrame(const RouterEvent *event);
void BoardBus_GetStats(BoardBusStats *stats);

#endif /* TRI_MODE_HID_HUB_BOARD_BUS_H */
