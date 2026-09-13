#include "HAL.h"

#include "ble_nus_service.h"
#include "static_spsc_ring.h"

#define BLE_NUS_ALIGN4                  __attribute__((aligned(4)))

#define BLE_NUS_IDX_SERVICE              0u
#define BLE_NUS_IDX_RX_DECL              1u
#define BLE_NUS_IDX_RX                   2u
#define BLE_NUS_IDX_TX_DECL              3u
#define BLE_NUS_IDX_TX                   4u
#define BLE_NUS_IDX_TX_CCCD              5u

#define BLE_NUS_RX_CAPACITY              4u

/* Nordic UART Service UUIDs, stored in the little-endian form expected by
 * the WCH GATT API: 6E400001/2/3-B5A3-F393-E0A9-E50E24DCCA9E. */
static const uint8_t g_ble_nus_service_uuid[ATT_UUID_SIZE] BLE_NUS_ALIGN4 =
    {0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
     0x93u, 0xF3u, 0xA3u, 0xB5u, 0x01u, 0x00u, 0x40u, 0x6Eu};
static const uint8_t g_ble_nus_rx_uuid[ATT_UUID_SIZE] BLE_NUS_ALIGN4 =
    {0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
     0x93u, 0xF3u, 0xA3u, 0xB5u, 0x02u, 0x00u, 0x40u, 0x6Eu};
static const uint8_t g_ble_nus_tx_uuid[ATT_UUID_SIZE] BLE_NUS_ALIGN4 =
    {0x9Eu, 0xCAu, 0xDCu, 0x24u, 0x0Eu, 0xE5u, 0xA9u, 0xE0u,
     0x93u, 0xF3u, 0xA3u, 0xB5u, 0x03u, 0x00u, 0x40u, 0x6Eu};

static const gattAttrType_t g_ble_nus_service =
    {ATT_UUID_SIZE, g_ble_nus_service_uuid};

static uint8_t g_ble_nus_rx_props = GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t g_ble_nus_rx_value[BLE_NUS_MAX_PAYLOAD] BLE_NUS_ALIGN4;
static uint8_t g_ble_nus_tx_props = GATT_PROP_NOTIFY;
static uint8_t g_ble_nus_tx_value[BLE_NUS_MAX_PAYLOAD] BLE_NUS_ALIGN4;
static gattCharCfg_t g_ble_nus_tx_cccd[GATT_MAX_NUM_CONN] BLE_NUS_ALIGN4;

static BleNusRxFrame g_ble_nus_rx_storage[BLE_NUS_RX_CAPACITY] BLE_NUS_ALIGN4;
static BleNusRxFrame g_ble_nus_rx_staging BLE_NUS_ALIGN4;
static StaticSpscRing g_ble_nus_rx_ring;
static uint32_t g_ble_nus_rx_drop_count;

static gattAttribute_t g_ble_nus_attr_table[] =
{
    {{ATT_BT_UUID_SIZE, primaryServiceUUID}, GATT_PERMIT_READ, 0,
     (uint8_t *)&g_ble_nus_service},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_nus_rx_props},
    {{ATT_UUID_SIZE, g_ble_nus_rx_uuid}, GATT_PERMIT_WRITE, 0,
     g_ble_nus_rx_value},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_nus_tx_props},
    {{ATT_UUID_SIZE, g_ble_nus_tx_uuid}, 0, 0,
     g_ble_nus_tx_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID}, GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0,
     (uint8_t *)g_ble_nus_tx_cccd}
};

static bStatus_t BleNusService_ReadAttrCB(uint16_t conn_handle,
                                          gattAttribute_t *attr,
                                          uint8_t *value,
                                          uint16_t *length,
                                          uint16_t offset,
                                          uint16_t max_length,
                                          uint8_t method)
{
    uint16_t cfg;

    (void)method;
    if(attr == &g_ble_nus_attr_table[BLE_NUS_IDX_TX_CCCD])
    {
        if((offset != 0u) || (max_length < 2u))
        {
            *length = 0;
            return (offset != 0u) ? ATT_ERR_ATTR_NOT_LONG : ATT_ERR_INVALID_VALUE_SIZE;
        }
        cfg = GATTServApp_ReadCharCfg(conn_handle, g_ble_nus_tx_cccd);
        value[0] = (uint8_t)(cfg & 0xFFu);
        value[1] = (uint8_t)(cfg >> 8);
        *length = 2u;
        return SUCCESS;
    }

    *length = 0;
    return ATT_ERR_READ_NOT_PERMITTED;
}

static bStatus_t BleNusService_WriteAttrCB(uint16_t conn_handle,
                                           gattAttribute_t *attr,
                                           uint8_t *value,
                                           uint16_t length,
                                           uint16_t offset,
                                           uint8_t method)
{
    uint16_t i;

    (void)method;
    if(gattPermitAuthorWrite(attr->permissions))
    {
        return ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    if(offset != 0u)
    {
        return ATT_ERR_ATTR_NOT_LONG;
    }

    if(attr == &g_ble_nus_attr_table[BLE_NUS_IDX_TX_CCCD])
    {
        return GATTServApp_ProcessCCCWriteReq(conn_handle, attr, value, length,
                                               offset, GATT_CLIENT_CFG_NOTIFY);
    }

    if(attr == &g_ble_nus_attr_table[BLE_NUS_IDX_RX])
    {
        if(length > BLE_NUS_MAX_PAYLOAD)
        {
            return ATT_ERR_INVALID_VALUE_SIZE;
        }
        g_ble_nus_rx_staging.length = (uint8_t)length;
        g_ble_nus_rx_staging.reserved[0] = 0u;
        g_ble_nus_rx_staging.reserved[1] = 0u;
        g_ble_nus_rx_staging.reserved[2] = 0u;
        for(i = 0; i < length; ++i)
        {
            g_ble_nus_rx_staging.bytes[i] = value[i];
        }
        if(!StaticSpscRing_Push(&g_ble_nus_rx_ring, &g_ble_nus_rx_staging))
        {
            ++g_ble_nus_rx_drop_count;
            return ATT_ERR_INSUFFICIENT_RESOURCES;
        }
        return SUCCESS;
    }

    return ATT_ERR_WRITE_NOT_PERMITTED;
}

static void BleNusService_HandleConnStatusCB(uint16_t conn_handle,
                                             uint8_t change_type)
{
    if(conn_handle == LOOPBACK_CONNHANDLE)
    {
        return;
    }
    if((change_type == LINKDB_STATUS_UPDATE_REMOVED) ||
       ((change_type == LINKDB_STATUS_UPDATE_STATEFLAGS) &&
        !linkDB_Up(conn_handle)))
    {
        GATTServApp_InitCharCfg(conn_handle, g_ble_nus_tx_cccd);
    }
}

static gattServiceCBs_t g_ble_nus_service_callbacks =
{
    BleNusService_ReadAttrCB,
    BleNusService_WriteAttrCB,
    NULL
};

bStatus_t BleNusService_AddService(void)
{
    StaticSpscRing_Init(&g_ble_nus_rx_ring,
                        g_ble_nus_rx_storage,
                        sizeof(g_ble_nus_rx_storage[0]),
                        BLE_NUS_RX_CAPACITY);
    g_ble_nus_rx_drop_count = 0u;
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, g_ble_nus_tx_cccd);
    linkDB_Register(BleNusService_HandleConnStatusCB);

    return GATTServApp_RegisterService(g_ble_nus_attr_table,
                                       GATT_NUM_ATTRS(g_ble_nus_attr_table),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &g_ble_nus_service_callbacks);
}

uint8_t BleNusService_DequeueRx(BleNusRxFrame *frame)
{
    return StaticSpscRing_Pop(&g_ble_nus_rx_ring, frame);
}

uint32_t BleNusService_GetRxDropCount(void)
{
    return g_ble_nus_rx_drop_count;
}

uint8_t BleNusService_IsTxNotifyEnabled(uint16_t connection_handle)
{
    return ((GATTServApp_ReadCharCfg(connection_handle, g_ble_nus_tx_cccd) &
             GATT_CLIENT_CFG_NOTIFY) != 0u);
}

bStatus_t BleNusService_Notify(uint16_t connection_handle,
                               const uint8_t *data,
                               uint8_t length)
{
    attHandleValueNoti_t notification;
    uint16_t mtu;
    uint8_t i;
    bStatus_t status;

    if(length > BLE_NUS_MAX_PAYLOAD)
    {
        return bleInvalidRange;
    }
    if(!BleNusService_IsTxNotifyEnabled(connection_handle))
    {
        return bleIncorrectMode;
    }
    mtu = ATT_GetMTU(connection_handle);
    if((mtu < 3u) || ((uint16_t)length > (mtu - 3u)))
    {
        return bleInvalidRange;
    }

    /* This is the WCH BLE stack's packet-pool allocation required by
     * GATT_Notification; it is not an application malloc. */
    notification.pValue = GATT_bm_alloc(connection_handle,
                                        ATT_HANDLE_VALUE_NOTI,
                                        length,
                                        NULL,
                                        0u);
    if(notification.pValue == NULL)
    {
        return bleMemAllocError;
    }
    notification.handle = g_ble_nus_attr_table[BLE_NUS_IDX_TX].handle;
    notification.len = length;
    for(i = 0; i < length; ++i)
    {
        notification.pValue[i] = data[i];
    }

    status = GATT_Notification(connection_handle, &notification, FALSE);
    if(status != SUCCESS)
    {
        GATT_bm_free((gattMsg_t *)&notification, ATT_HANDLE_VALUE_NOTI);
    }
    return status;
}
