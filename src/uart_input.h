#ifndef TRI_MODE_HID_HUB_UART_INPUT_H
#define TRI_MODE_HID_HUB_UART_INPUT_H

#include <stdint.h>

typedef struct
{
    uint32_t rx_overrun;
    uint32_t line_error;
    uint32_t frame_flush;
    uint32_t frame_backpressure;
} UartInputStats;

void UartInput_Init(void);
void UartInput_Process(void);
void UartInput_GetStats(UartInputStats *stats);

#endif /* TRI_MODE_HID_HUB_UART_INPUT_H */
