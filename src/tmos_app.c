#include "HAL.h"

#include "ble_output.h"
#include "event_router.h"
#include "ps2_input.h"
#include "tmos_app.h"
#include "uart_input.h"
#include "usb_device.h"
#include "usb_host_hid.h"

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

static void Firmware_GetOutputAvailability(uint8_t *usb_hid_report_mask,
                                           uint8_t *ble_hid_report_mask,
                                           uint8_t *stream_available)
{
    uint8_t stream_mask = ROUTER_OUTPUT_NONE;

    *usb_hid_report_mask = ROUTER_HID_REPORT_MASK_NONE;
    *ble_hid_report_mask = ROUTER_HID_REPORT_MASK_NONE;
    if(UsbDevice_IsReady())
    {
        *usb_hid_report_mask = ROUTER_HID_REPORT_MASK_ALL;
        stream_mask |= ROUTER_OUTPUT_USB;
    }
    *ble_hid_report_mask = BleOutput_GetHidReportNotifyMask();
    if(BleOutput_IsStreamReady())
    {
        stream_mask |= ROUTER_OUTPUT_BLE;
    }
    *stream_available = stream_mask;
}

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
    uint8_t usb_hid_report_mask;
    uint8_t ble_hid_report_mask;
    uint8_t stream_available;
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

        Ps2Input_Process();
        UartInput_Process();
        UsbHostHid_Process();
        BleOutput_ProcessInput();
        Firmware_GetOutputAvailability(&usb_hid_report_mask,
                                       &ble_hid_report_mask,
                                       &stream_available);
        EventRouter_SetHidReportAvailability(usb_hid_report_mask,
                                             ble_hid_report_mask);
        EventRouter_SetStreamAvailability(stream_available);
        EventRouter_Process();
        UsbDevice_ProcessTask();
        BleOutput_Process();
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
    /* WCH BLE SDK/TMOS runtime initialization. */
    CH58X_BLEInit();
    HAL_Init();
    (void)GAPRole_PeripheralInit();
    EventRouter_Init();
    Ps2Input_Init();
    UartInput_Init();
    UsbHostHid_Init();
    UsbDevice_Init();
    BleOutput_Init();
    EventRouter_SetOutputPolicy(ROUTER_POLICY_USB_PREFERRED);

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
