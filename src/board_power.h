#ifndef TRI_MODE_BOARD_POWER_H
#define TRI_MODE_BOARD_POWER_H
#include <stdint.h>

typedef struct
{
    uint32_t fault_count;
    uint32_t adc_timeout;
    uint32_t user_press_count;
    uint16_t battery_raw;
    uint16_t vbus_raw;
    uint8_t adc_valid_mask; /* bit0=PA6; bit1=PA7, raw only */
    uint8_t charging;
    uint8_t input_good;
    uint8_t host_fault_latched;
    uint8_t host_requested;
    uint8_t host_enabled;
    uint8_t charger_mode; /* EN2:EN1: 0=100mA,1=500mA,3=suspend */
    uint8_t user_pressed;
} BoardPowerStatus;

void BoardPower_Init(void);
void BoardPower_Process(void);
void BoardPower_RequestHost(uint8_t enabled);
/* Clear only with the rail disabled and FAULT# deasserted. */
uint8_t BoardPower_ClearFault(void);
uint8_t BoardPower_HostEnabled(void);
void BoardPower_GetStatus(BoardPowerStatus *status);
#endif
