#ifndef TRI_MODE_HID_HUB_PS2_INPUT_H
#define TRI_MODE_HID_HUB_PS2_INPUT_H

#include <stdint.h>

typedef struct
{
    uint32_t keyboard_edge_overrun;
    uint32_t mouse_edge_overrun;
    uint32_t frame_error;
    uint32_t keyboard_resync;
    uint32_t mouse_resync;
    uint32_t unknown_keyboard_code;
    uint32_t mouse_packet_error;
} Ps2InputStats;

void Ps2Input_Init(void);
void Ps2Input_Process(void);
void Ps2Input_GetStats(Ps2InputStats *stats);

#endif /* TRI_MODE_HID_HUB_PS2_INPUT_H */
