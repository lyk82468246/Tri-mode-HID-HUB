#ifndef TRI_MODE_HID_HUB_FIRMWARE_DIAGNOSTICS_H
#define TRI_MODE_HID_HUB_FIRMWARE_DIAGNOSTICS_H

#include <stdint.h>

#include "event_router_types.h"
#include "ps2_input.h"
#include "uart_input.h"
#include "usb_host_hid.h"

/* The watchdog remains opt-in until the development-board power and reset
 * paths have passed the M6 soak test. */
#ifndef FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE
#define FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE 0
#endif

#define FIRMWARE_DIAGNOSTICS_WATCHDOG_RELOAD 0xFFu

typedef struct
{
    uint32_t reset_status;
    uint32_t sys_clock_hz;
    uint32_t service_budget_cycles;
    uint32_t service_count;
    uint32_t service_overrun;
    uint32_t last_service_cycles;
    uint32_t max_service_cycles;
    uint32_t watchdog_kicks;
    uint16_t max_input_events;
    uint16_t max_usb_hid_frames;
    uint16_t max_ble_hid_frames;
    uint16_t max_usb_stream_frames;
    uint16_t max_ble_stream_frames;
    uint32_t ble_nus_rx_drop;
    RouterStats router;
    Ps2InputStats ps2;
    UartInputStats uart;
    UsbHostHidStats usb_host;
} FirmwareDiagnosticsSnapshot;

void FirmwareDiagnostics_Init(void);
uint32_t FirmwareDiagnostics_BeginService(void);
void FirmwareDiagnostics_SampleQueues(void);
void FirmwareDiagnostics_EndService(uint32_t start_cycles);
void FirmwareDiagnostics_FeedWatchdog(void);
void FirmwareDiagnostics_GetSnapshot(FirmwareDiagnosticsSnapshot *snapshot);

#endif /* TRI_MODE_HID_HUB_FIRMWARE_DIAGNOSTICS_H */
