#include "CH58x_common.h"

#include "ble_nus_service.h"
#include "event_router.h"
#include "firmware_diagnostics.h"

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
} FirmwareDiagnosticsRuntime;

static FirmwareDiagnosticsRuntime g_firmware_diagnostics
    __attribute__((aligned(4)));
static FirmwareDiagnosticsSnapshot g_firmware_diagnostics_snapshot
    __attribute__((aligned(4)));

static void FirmwareDiagnostics_UpdateWatermark(uint16_t value,
                                                uint16_t *watermark)
{
    if(value > *watermark)
    {
        *watermark = value;
    }
}

void FirmwareDiagnostics_Init(void)
{
    g_firmware_diagnostics = (FirmwareDiagnosticsRuntime){0};
    g_firmware_diagnostics_snapshot = (FirmwareDiagnosticsSnapshot){0};
    g_firmware_diagnostics.reset_status = SYS_GetLastResetSta();
    g_firmware_diagnostics.sys_clock_hz = GetSysClock();
    g_firmware_diagnostics.service_budget_cycles =
        g_firmware_diagnostics.sys_clock_hz / 500u;
    if(g_firmware_diagnostics.service_budget_cycles == 0u)
    {
        g_firmware_diagnostics.service_budget_cycles = 1u;
    }

#if(FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE)
    WWDG_ITCfg(DISABLE);
    WWDG_ResetCfg(ENABLE);
    WWDG_SetCounter(FIRMWARE_DIAGNOSTICS_WATCHDOG_RELOAD);
#endif
}

uint32_t FirmwareDiagnostics_BeginService(void)
{
    return SYS_GetSysTickCnt();
}

void FirmwareDiagnostics_SampleQueues(void)
{
    Event_Router *router = EventRouter_GetContext();

    FirmwareDiagnostics_UpdateWatermark(
        StaticSpscRing_Count(&router->input_ring),
        &g_firmware_diagnostics.max_input_events);
    FirmwareDiagnostics_UpdateWatermark(
        StaticSpscRing_Count(&router->usb_hid_tx_ring),
        &g_firmware_diagnostics.max_usb_hid_frames);
    FirmwareDiagnostics_UpdateWatermark(
        StaticSpscRing_Count(&router->ble_hid_tx_ring),
        &g_firmware_diagnostics.max_ble_hid_frames);
    FirmwareDiagnostics_UpdateWatermark(
        StaticSpscRing_Count(&router->usb_stream_tx_ring),
        &g_firmware_diagnostics.max_usb_stream_frames);
    FirmwareDiagnostics_UpdateWatermark(
        StaticSpscRing_Count(&router->ble_stream_tx_ring),
        &g_firmware_diagnostics.max_ble_stream_frames);
}

void FirmwareDiagnostics_EndService(uint32_t start_cycles)
{
    uint32_t elapsed = SYS_GetSysTickCnt() - start_cycles;

    g_firmware_diagnostics.last_service_cycles = elapsed;
    if(elapsed > g_firmware_diagnostics.max_service_cycles)
    {
        g_firmware_diagnostics.max_service_cycles = elapsed;
    }
    if(elapsed > g_firmware_diagnostics.service_budget_cycles)
    {
        ++g_firmware_diagnostics.service_overrun;
    }
    ++g_firmware_diagnostics.service_count;

    /* Keep a debugger-readable aggregate without copying the larger
     * snapshot on every 2 ms turn.  The public getter still assembles a
     * current snapshot on demand. */
    if((g_firmware_diagnostics.service_count & 0xFFu) == 0u)
    {
        FirmwareDiagnostics_GetSnapshot(&g_firmware_diagnostics_snapshot);
    }
}

void FirmwareDiagnostics_FeedWatchdog(void)
{
#if(FIRMWARE_DIAGNOSTICS_WATCHDOG_ENABLE)
    WWDG_SetCounter(FIRMWARE_DIAGNOSTICS_WATCHDOG_RELOAD);
    ++g_firmware_diagnostics.watchdog_kicks;
#endif
}

void FirmwareDiagnostics_GetSnapshot(FirmwareDiagnosticsSnapshot *snapshot)
{
    Event_Router *router;

    if(snapshot == NULL)
    {
        return;
    }

    router = EventRouter_GetContext();
    snapshot->reset_status = g_firmware_diagnostics.reset_status;
    snapshot->sys_clock_hz = g_firmware_diagnostics.sys_clock_hz;
    snapshot->service_budget_cycles =
        g_firmware_diagnostics.service_budget_cycles;
    snapshot->service_count = g_firmware_diagnostics.service_count;
    snapshot->service_overrun = g_firmware_diagnostics.service_overrun;
    snapshot->last_service_cycles =
        g_firmware_diagnostics.last_service_cycles;
    snapshot->max_service_cycles =
        g_firmware_diagnostics.max_service_cycles;
    snapshot->watchdog_kicks = g_firmware_diagnostics.watchdog_kicks;
    snapshot->max_input_events = g_firmware_diagnostics.max_input_events;
    snapshot->max_usb_hid_frames =
        g_firmware_diagnostics.max_usb_hid_frames;
    snapshot->max_ble_hid_frames =
        g_firmware_diagnostics.max_ble_hid_frames;
    snapshot->max_usb_stream_frames =
        g_firmware_diagnostics.max_usb_stream_frames;
    snapshot->max_ble_stream_frames =
        g_firmware_diagnostics.max_ble_stream_frames;
    snapshot->ble_nus_rx_drop = BleNusService_GetRxDropCount();
    snapshot->router = router->stats;
    Ps2Input_GetStats(&snapshot->ps2);
    UartInput_GetStats(&snapshot->uart);
    UsbHostHid_GetStats(&snapshot->usb_host);
}
