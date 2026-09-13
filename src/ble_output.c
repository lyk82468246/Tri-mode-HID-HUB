#include "HAL.h"

#include "ble_hid_service.h"
#include "ble_nus_service.h"
#include "ble_output.h"
#include "event_router.h"

#define BLE_OUTPUT_START_DEVICE_EVENT       0x0001u
#define BLE_OUTPUT_MAX_RX_FRAMES_PER_TICK   2u
#define BLE_OUTPUT_ADVERTISING_INTERVAL     32u
#define BLE_OUTPUT_MIN_CONN_INTERVAL        8u
#define BLE_OUTPUT_MAX_CONN_INTERVAL        16u

#define BLE_OUTPUT_ALIGN4                   __attribute__((aligned(4)))

static tmosTaskID g_ble_output_task_id = INVALID_TASK_ID;
static volatile uint8_t g_ble_output_connected;
static volatile uint16_t g_ble_output_connection_handle = GAP_CONNHANDLE_INIT;
static uint8_t g_ble_output_profile_ready;

static uint8_t g_ble_output_device_name[] = "Tri-mode HID Hub";

static uint8_t g_ble_output_advert_data[] =
{
    0x02u, GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    0x03u, GAP_ADTYPE_APPEARANCE,
    LO_UINT16(GAP_APPEARE_GENERIC_HID), HI_UINT16(GAP_APPEARE_GENERIC_HID),

    0x03u, GAP_ADTYPE_16BIT_COMPLETE,
    0x12u, 0x18u,

    0x11u, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'T', 'r', 'i', '-', 'm', 'o', 'd', 'e', ' ',
    'H', 'I', 'D', ' ', 'H', 'u', 'b'
};

/* Nordic UART Service UUID in the WCH advertising byte order. */
static uint8_t g_ble_output_scan_response[] =
{
    0x11u, GAP_ADTYPE_128BIT_COMPLETE,
    0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
    0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu
};

static HidTxFrame g_ble_output_hid_work BLE_OUTPUT_ALIGN4;
static StreamTxFrame g_ble_output_stream_work BLE_OUTPUT_ALIGN4;

static void BleOutput_ProcessTMOSMsg(tmos_event_hdr_t *message);
static tmosEvents BleOutput_ProcessEvent(tmosTaskID task_id, tmosEvents events);
static void BleOutput_StateChangeCB(gapRole_States_t new_state,
                                    gapRoleEvent_t *event);
static void BleOutput_ParamUpdateCB(uint16_t connection_handle,
                                    uint16_t connection_interval,
                                    uint16_t connection_latency,
                                    uint16_t connection_timeout);
static void BleOutput_PasscodeCB(uint8_t *device_address,
                                 uint16_t connection_handle,
                                 uint8_t ui_inputs,
                                 uint8_t ui_outputs);
static void BleOutput_PairStateCB(uint16_t connection_handle,
                                  uint8_t state,
                                  uint8_t status);

static gapRolesCBs_t g_ble_output_role_callbacks =
{
    BleOutput_StateChangeCB,
    NULL,
    BleOutput_ParamUpdateCB
};

static gapBondCBs_t g_ble_output_bond_callbacks =
{
    BleOutput_PasscodeCB,
    BleOutput_PairStateCB,
    NULL
};

static void BleOutput_ClearConnection(uint16_t connection_handle)
{
    if(g_ble_output_connection_handle == connection_handle)
    {
        g_ble_output_connection_handle = GAP_CONNHANDLE_INIT;
        g_ble_output_connected = 0u;
    }
}

static void BleOutput_ProcessTMOSMsg(tmos_event_hdr_t *message)
{
    switch(message->event)
    {
        case GAP_MSG_EVENT:
            /* The GAP role callback consumes the state transition. */
            break;

        case GATT_MSG_EVENT:
            /* M2 only uses server-side GATT callbacks.  No client procedure
             * is started here, so there is no application-owned response
             * buffer to release. */
            break;

        default:
            break;
    }
}

static tmosEvents BleOutput_ProcessEvent(tmosTaskID task_id, tmosEvents events)
{
    uint8_t *message;

    (void)task_id;
    if(events & SYS_EVENT_MSG)
    {
        while((message = tmos_msg_receive(g_ble_output_task_id)) != NULL)
        {
            BleOutput_ProcessTMOSMsg((tmos_event_hdr_t *)message);
            tmos_msg_deallocate(message);
        }
        events ^= SYS_EVENT_MSG;
    }

    if(events & BLE_OUTPUT_START_DEVICE_EVENT)
    {
        if(g_ble_output_profile_ready)
        {
            (void)GAPRole_PeripheralStartDevice(g_ble_output_task_id,
                                                &g_ble_output_bond_callbacks,
                                                &g_ble_output_role_callbacks);
        }
        events ^= BLE_OUTPUT_START_DEVICE_EVENT;
    }

    return events;
}

static void BleOutput_StateChangeCB(gapRole_States_t new_state,
                                    gapRoleEvent_t *event)
{
    uint8_t advertising_enable;

    if((new_state & GAPROLE_STATE_ADV_MASK) == GAPROLE_STARTED)
    {
        uint8_t own_address[B_ADDR_LEN];

        if(GAPRole_GetParameter(GAPROLE_BD_ADDR, own_address) == SUCCESS)
        {
            GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, own_address);
        }
    }

    if((new_state & GAPROLE_STATE_ADV_MASK) == GAPROLE_CONNECTED)
    {
        if(event != NULL && event->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
        {
            gapEstLinkReqEvent_t *link = &event->linkCmpl;

            if(g_ble_output_connected)
            {
                (void)GAPRole_TerminateLink(link->connectionHandle);
            }
            else
            {
                g_ble_output_connection_handle = link->connectionHandle;
                g_ble_output_connected = 1u;
                advertising_enable = FALSE;
                (void)GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                                            sizeof(advertising_enable),
                                            &advertising_enable);
            }
        }
    }

    if(event != NULL && event->gap.opcode == GAP_LINK_TERMINATED_EVENT)
    {
        BleOutput_ClearConnection(event->linkTerminate.connectionHandle);
        advertising_enable = TRUE;
        (void)GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                                   sizeof(advertising_enable),
                                   &advertising_enable);
    }

}

static void BleOutput_ParamUpdateCB(uint16_t connection_handle,
                                    uint16_t connection_interval,
                                    uint16_t connection_latency,
                                    uint16_t connection_timeout)
{
    (void)connection_handle;
    (void)connection_interval;
    (void)connection_latency;
    (void)connection_timeout;
}

static void BleOutput_PasscodeCB(uint8_t *device_address,
                                 uint16_t connection_handle,
                                 uint8_t ui_inputs,
                                 uint8_t ui_outputs)
{
    uint32_t passcode;

    (void)device_address;
    (void)ui_inputs;
    (void)ui_outputs;
    passcode = 0u;
    (void)GAPBondMgr_PasscodeRsp(connection_handle, SUCCESS, passcode);
}

static void BleOutput_PairStateCB(uint16_t connection_handle,
                                  uint8_t state,
                                  uint8_t status)
{
    (void)connection_handle;
    (void)state;
    (void)status;
}

void BleOutput_Init(void)
{
    bStatus_t status;
    uint8_t initial_advertising_enable = TRUE;
    uint16_t desired_min_interval = BLE_OUTPUT_MIN_CONN_INTERVAL;
    uint16_t desired_max_interval = BLE_OUTPUT_MAX_CONN_INTERVAL;
    uint16_t appearance = GAP_APPEARE_GENERIC_HID;
    uint32_t passcode = 0u;
    uint8_t pairing_mode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8_t mitm = FALSE;
    uint8_t io_capabilities = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint8_t bonding = TRUE;

    g_ble_output_connected = 0u;
    g_ble_output_connection_handle = GAP_CONNHANDLE_INIT;
    g_ble_output_profile_ready = 0u;

    g_ble_output_task_id = TMOS_ProcessEventRegister(BleOutput_ProcessEvent);
    if(g_ble_output_task_id == INVALID_TASK_ID)
    {
        return;
    }

    status = GGS_AddService(GATT_ALL_SERVICES);
    if(status != SUCCESS)
    {
        return;
    }
    status = GATTServApp_AddService(GATT_ALL_SERVICES);
    if(status != SUCCESS)
    {
        return;
    }
    status = BleHidService_AddService();
    if(status != SUCCESS)
    {
        return;
    }
    status = BleNusService_AddService();
    if(status != SUCCESS)
    {
        return;
    }

    (void)GGS_SetParameter(GGS_DEVICE_NAME_ATT,
                           sizeof(g_ble_output_device_name),
                           g_ble_output_device_name);
    (void)GGS_SetParameter(GGS_APPEARANCE_ATT,
                           sizeof(appearance),
                           &appearance);

    (void)GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED,
                               sizeof(initial_advertising_enable),
                               &initial_advertising_enable);
    (void)GAPRole_SetParameter(GAPROLE_ADVERT_DATA,
                               sizeof(g_ble_output_advert_data),
                               g_ble_output_advert_data);
    (void)GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,
                               sizeof(g_ble_output_scan_response),
                               g_ble_output_scan_response);
    (void)GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL,
                               sizeof(desired_min_interval),
                               &desired_min_interval);
    (void)GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL,
                               sizeof(desired_max_interval),
                               &desired_max_interval);

    (void)GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN,
                            BLE_OUTPUT_ADVERTISING_INTERVAL);
    (void)GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX,
                            BLE_OUTPUT_ADVERTISING_INTERVAL);
    (void)GAP_SetParamValue(TGAP_LIM_ADV_TIMEOUT, 0u);

    (void)GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE,
                                   sizeof(passcode), &passcode);
    (void)GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,
                                   sizeof(pairing_mode), &pairing_mode);
    (void)GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION,
                                   sizeof(mitm), &mitm);
    (void)GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES,
                                   sizeof(io_capabilities), &io_capabilities);
    (void)GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED,
                                   sizeof(bonding), &bonding);

    g_ble_output_profile_ready = 1u;
    (void)tmos_set_event(g_ble_output_task_id, BLE_OUTPUT_START_DEVICE_EVENT);
}

static void BleOutput_ProcessNusRx(void)
{
    BleNusRxFrame frame;
    uint8_t processed;

    for(processed = 0u; processed < BLE_OUTPUT_MAX_RX_FRAMES_PER_TICK; ++processed)
    {
        if(!BleNusService_DequeueRx(&frame))
        {
            break;
        }
        (void)EventRouter_InjectStreamData(ROUTER_SRC_BLE_NUS,
                                            frame.bytes,
                                            frame.length);
    }
}

static void BleOutput_ProcessHidTx(void)
{
    bStatus_t status;

    if(!g_ble_output_connected ||
       !EventRouter_PeekBleHidFrame(&g_ble_output_hid_work))
    {
        return;
    }
    if(!BleHidService_IsReportNotifyEnabled(g_ble_output_connection_handle,
                                             g_ble_output_hid_work.report_id))
    {
        return;
    }

    status = BleHidService_Notify(g_ble_output_connection_handle,
                                  g_ble_output_hid_work.report_id,
                                  g_ble_output_hid_work.bytes,
                                  g_ble_output_hid_work.length);
    if(status == SUCCESS)
    {
        (void)EventRouter_DequeueBleHidFrame(&g_ble_output_hid_work);
    }
}

static void BleOutput_ProcessStreamTx(void)
{
    bStatus_t status;

    if(!g_ble_output_connected ||
       !EventRouter_PeekBleStreamFrame(&g_ble_output_stream_work))
    {
        return;
    }
    if(!BleNusService_IsTxNotifyEnabled(g_ble_output_connection_handle))
    {
        return;
    }

    status = BleNusService_Notify(g_ble_output_connection_handle,
                                  g_ble_output_stream_work.bytes,
                                  g_ble_output_stream_work.length);
    if(status == SUCCESS)
    {
        (void)EventRouter_DequeueBleStreamFrame(&g_ble_output_stream_work);
    }
}

void BleOutput_Process(void)
{
    BleOutput_ProcessHidTx();
    BleOutput_ProcessStreamTx();
}

void BleOutput_ProcessInput(void)
{
    BleOutput_ProcessNusRx();
}

uint8_t BleOutput_IsConnected(void)
{
    return g_ble_output_connected;
}

uint16_t BleOutput_GetConnectionHandle(void)
{
    return g_ble_output_connection_handle;
}
