#include "HAL.h"

#include "event_router.h"
#include "tmos_app.h"
#include "usb_device.h"

#define FIRMWARE_SERVICE_EVENT      0x0001u
#define FIRMWARE_TEST_PRESS_EVENT   0x0002u
#define FIRMWARE_TEST_RELEASE_EVENT 0x0004u

#define FIRMWARE_SERVICE_PERIOD_MS  2u

/* Set to 1 only for a bench test that deliberately emits an 'a' key. */
#ifndef CH582M_M1_TEST_PATTERN
#define CH582M_M1_TEST_PATTERN       0
#endif

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

/* Used only when BLE_MAC is enabled; keeping it static makes the contract
 * required by the WCH HAL explicit for the later BLE milestone. */
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};

static tmosTaskID g_firmware_task_id = INVALID_TASK_ID;

#if(CH582M_M1_TEST_PATTERN)
static void Firmware_TestKey(uint8_t pressed)
{
    HidKeyboardReport report;
    uint8_t i;

    for(i = 0; i < HID_KEYBOARD_REPORT_LEN; ++i)
    {
        report.bytes[i] = 0;
    }
    if(pressed)
    {
        report.bytes[2] = 0x04; /* Usage ID 0x04 = keyboard 'a'. */
    }
    (void)EventRouter_InjectKeyboardReport(ROUTER_SRC_TEST, &report);
}
#endif

static tmosEvents Firmware_ProcessEvent(tmosTaskID task_id, tmosEvents events)
{
    uint8_t cdc_data[USB_DEVICE_CDC_PACKET_MAX];
    uint8_t cdc_length;
    uint8_t i;

    (void)task_id;

    if(events & FIRMWARE_SERVICE_EVENT)
    {
        /* CDC RX is copied by the USB ISR; framing and routing happen here. */
        for(i = 0; i < 4u; ++i)
        {
            if(!UsbDevice_DequeueCdcRx(cdc_data, &cdc_length))
            {
                break;
            }
            (void)EventRouter_InjectStreamData(ROUTER_SRC_USB_CDC,
                                                cdc_data,
                                                cdc_length);
        }

        EventRouter_Process();
        UsbDevice_ProcessTask();
        if(g_firmware_task_id != INVALID_TASK_ID)
        {
            tmos_start_reload_task(g_firmware_task_id,
                                   FIRMWARE_SERVICE_EVENT,
                                   MS1_TO_SYSTEM_TIME(FIRMWARE_SERVICE_PERIOD_MS));
        }
        events ^= FIRMWARE_SERVICE_EVENT;
    }

#if(CH582M_M1_TEST_PATTERN)
    if(events & FIRMWARE_TEST_PRESS_EVENT)
    {
        Firmware_TestKey(1u);
        tmos_start_task(g_firmware_task_id,
                        FIRMWARE_TEST_RELEASE_EVENT,
                        MS1_TO_SYSTEM_TIME(100u));
        events ^= FIRMWARE_TEST_PRESS_EVENT;
    }
    if(events & FIRMWARE_TEST_RELEASE_EVENT)
    {
        Firmware_TestKey(0u);
        tmos_start_task(g_firmware_task_id,
                        FIRMWARE_TEST_PRESS_EVENT,
                        MS1_TO_SYSTEM_TIME(1000u));
        events ^= FIRMWARE_TEST_RELEASE_EVENT;
    }
#endif

    return events;
}

void Firmware_Init(void)
{
    /* This is the WCH BLE SDK/TMOS runtime initialization; no BLE role is
     * started in M1.  HOGP/NUS are intentionally deferred to M2. */
    CH58X_BLEInit();
    HAL_Init();
    EventRouter_Init();
    UsbDevice_Init();

    g_firmware_task_id = TMOS_ProcessEventRegister(Firmware_ProcessEvent);
    EventRouter_GetContext()->task_id = g_firmware_task_id;
    if(g_firmware_task_id != INVALID_TASK_ID)
    {
        tmos_start_reload_task(g_firmware_task_id,
                               FIRMWARE_SERVICE_EVENT,
                               MS1_TO_SYSTEM_TIME(FIRMWARE_SERVICE_PERIOD_MS));
#if(CH582M_M1_TEST_PATTERN)
        tmos_start_task(g_firmware_task_id,
                        FIRMWARE_TEST_PRESS_EVENT,
                        MS1_TO_SYSTEM_TIME(1000u));
#endif
    }
}

void Firmware_Run(void)
{
    TMOS_SystemProcess();
}
