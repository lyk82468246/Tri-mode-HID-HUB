#ifndef TRI_MODE_HID_HUB_IRDA_LINK_H
#define TRI_MODE_HID_HUB_IRDA_LINK_H

#include <stdint.h>

typedef struct
{
    uint32_t rx_overrun;
    uint32_t line_error;
    uint32_t config_retry;
    uint32_t config_error;
    uint32_t frame_error;
    uint32_t fcs_error;
    uint32_t frame_count;
    uint32_t frame_backpressure;
    uint32_t remote_pause_count;
    uint32_t remote_blocked_bytes;
    uint8_t ready;
    uint8_t fault;
} IrdaLinkStats;

void IrdaLink_Init(void);
void IrdaLink_Process(void);
/* The TSOP/IR LED path shares optical space with TFBS4711. */
void IrdaLink_SetRemoteTxActive(uint8_t active);
void IrdaLink_GetStats(IrdaLinkStats *stats);

#endif /* TRI_MODE_HID_HUB_IRDA_LINK_H */
