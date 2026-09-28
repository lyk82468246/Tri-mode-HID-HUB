#ifndef TRI_MODE_HID_HUB_IR_REMOTE_H
#define TRI_MODE_HID_HUB_IR_REMOTE_H

#include <stdint.h>

typedef struct
{
    uint32_t edge_overrun;
    uint32_t edge_count;
    uint32_t invalid_pulse;
    uint32_t nec_frame_count;
    uint32_t nec_repeat_count;
    uint32_t rc5_frame_count;
    uint32_t tx_frame_count;
    uint32_t tx_busy;
    uint32_t tx_reject;
    uint8_t tx_active;
} IrRemoteStats;

void IrRemote_Init(void);
void IrRemote_Process(void);
uint8_t IrRemote_SendNec(uint8_t address, uint8_t command);
uint8_t IrRemote_SendRc5(uint8_t address, uint8_t command,
                         uint8_t toggle);
void IrRemote_GetStats(IrRemoteStats *stats);

#endif /* TRI_MODE_HID_HUB_IR_REMOTE_H */
