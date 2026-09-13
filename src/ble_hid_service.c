#include "HAL.h"

#include "ble_hid_service.h"

#define BLE_HID_REPORT_KEYBOARD_LEN      8u
#define BLE_HID_REPORT_MOUSE_LEN         4u
#define BLE_HID_REPORT_GAMEPAD_LEN       8u
#define BLE_HID_REPORT_REFERENCE_LEN     2u

#define BLE_HID_ALIGN4                   __attribute__((aligned(4)))

#define BLE_HID_IDX_SERVICE               0u
#define BLE_HID_IDX_INFO_DECL             1u
#define BLE_HID_IDX_INFO                  2u
#define BLE_HID_IDX_CONTROL_DECL          3u
#define BLE_HID_IDX_CONTROL               4u
#define BLE_HID_IDX_PROTOCOL_DECL         5u
#define BLE_HID_IDX_PROTOCOL              6u
#define BLE_HID_IDX_REPORT_MAP_DECL       7u
#define BLE_HID_IDX_REPORT_MAP            8u
#define BLE_HID_IDX_KEY_DECL              9u
#define BLE_HID_IDX_KEY                  10u
#define BLE_HID_IDX_KEY_CCCD             11u
#define BLE_HID_IDX_KEY_REF              12u
#define BLE_HID_IDX_LED_DECL             13u
#define BLE_HID_IDX_LED                  14u
#define BLE_HID_IDX_LED_REF              15u
#define BLE_HID_IDX_MOUSE_DECL           16u
#define BLE_HID_IDX_MOUSE               17u
#define BLE_HID_IDX_MOUSE_CCCD          18u
#define BLE_HID_IDX_MOUSE_REF           19u
#define BLE_HID_IDX_GAMEPAD_DECL         20u
#define BLE_HID_IDX_GAMEPAD              21u
#define BLE_HID_IDX_GAMEPAD_CCCD         22u
#define BLE_HID_IDX_GAMEPAD_REF          23u
#define BLE_HID_IDX_BOOT_KEY_DECL        24u
#define BLE_HID_IDX_BOOT_KEY             25u
#define BLE_HID_IDX_BOOT_KEY_CCCD        26u
#define BLE_HID_IDX_BOOT_KEY_OUT_DECL    27u
#define BLE_HID_IDX_BOOT_KEY_OUT         28u
#define BLE_HID_IDX_BOOT_MOUSE_DECL      29u
#define BLE_HID_IDX_BOOT_MOUSE           30u
#define BLE_HID_IDX_BOOT_MOUSE_CCCD      31u

static const uint8_t g_ble_hid_service_uuid[ATT_BT_UUID_SIZE] = {0x12u, 0x18u};
static const uint8_t g_ble_hid_info_uuid[ATT_BT_UUID_SIZE] = {0x4Au, 0x2Au};
static const uint8_t g_ble_hid_control_uuid[ATT_BT_UUID_SIZE] = {0x4Cu, 0x2Au};
static const uint8_t g_ble_hid_protocol_uuid[ATT_BT_UUID_SIZE] = {0x4Eu, 0x2Au};
static const uint8_t g_ble_hid_report_map_uuid[ATT_BT_UUID_SIZE] = {0x4Bu, 0x2Au};
static const uint8_t g_ble_hid_report_uuid[ATT_BT_UUID_SIZE] = {0x4Du, 0x2Au};
static const uint8_t g_ble_hid_boot_key_input_uuid[ATT_BT_UUID_SIZE] = {0x22u, 0x2Au};
static const uint8_t g_ble_hid_boot_key_output_uuid[ATT_BT_UUID_SIZE] = {0x32u, 0x2Au};
static const uint8_t g_ble_hid_boot_mouse_input_uuid[ATT_BT_UUID_SIZE] = {0x33u, 0x2Au};

static const uint8_t g_ble_hid_information[4] BLE_HID_ALIGN4 =
{
    0x11u, 0x01u, 0x00u, 0x03u
};

/* The BLE report map intentionally matches the USB Device report map. */
static const uint8_t g_ble_hid_report_map[] BLE_HID_ALIGN4 =
{
    /* Report ID 1: Boot keyboard, 8-byte payload. */
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01,
    0x29, 0x05, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03,
    0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00,
    0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
    0x81, 0x00, 0xC0,

    /* Report ID 2: three-button mouse, 4-byte payload. */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,
    0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x03, 0x81, 0x02, 0x75, 0x05, 0x95, 0x01,
    0x81, 0x01, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,

    /* Report ID 3: 16 buttons, hat switch and five 8-bit axes. */
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x03,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07,
    0x35, 0x00, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x75,
    0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00, 0x75,
    0x04, 0x95, 0x01, 0x81, 0x01, 0x09, 0x30, 0x09,
    0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x36, 0x15,
    0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x05, 0x81,
    0x02, 0xC0
};

static const uint16_t g_ble_hid_report_map_len = sizeof(g_ble_hid_report_map);

static uint8_t g_ble_hid_info_props = GATT_PROP_READ;
static uint8_t g_ble_hid_control_props = GATT_PROP_WRITE_NO_RSP;
static uint8_t g_ble_hid_control_value;
static uint8_t g_ble_hid_protocol_props = GATT_PROP_READ | GATT_PROP_WRITE_NO_RSP;
static uint8_t g_ble_hid_protocol_value = BLE_HID_PROTOCOL_REPORT;
static uint8_t g_ble_hid_report_map_props = GATT_PROP_READ;

static uint8_t g_ble_hid_key_props = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t g_ble_hid_key_value[BLE_HID_REPORT_KEYBOARD_LEN] BLE_HID_ALIGN4;
static gattCharCfg_t g_ble_hid_key_cccd[GATT_MAX_NUM_CONN] BLE_HID_ALIGN4;
static uint8_t g_ble_hid_key_ref[BLE_HID_REPORT_REFERENCE_LEN] =
    {BLE_HID_REPORT_ID_KEYBOARD, BLE_HID_REPORT_TYPE_INPUT};

static uint8_t g_ble_hid_led_props = GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t g_ble_hid_led_value;
static uint8_t g_ble_hid_led_ref[BLE_HID_REPORT_REFERENCE_LEN] =
    {BLE_HID_REPORT_ID_KEYBOARD, BLE_HID_REPORT_TYPE_OUTPUT};

static uint8_t g_ble_hid_mouse_props = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t g_ble_hid_mouse_value[BLE_HID_REPORT_MOUSE_LEN] BLE_HID_ALIGN4;
static gattCharCfg_t g_ble_hid_mouse_cccd[GATT_MAX_NUM_CONN] BLE_HID_ALIGN4;
static uint8_t g_ble_hid_mouse_ref[BLE_HID_REPORT_REFERENCE_LEN] =
    {BLE_HID_REPORT_ID_MOUSE, BLE_HID_REPORT_TYPE_INPUT};

static uint8_t g_ble_hid_gamepad_props = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t g_ble_hid_gamepad_value[BLE_HID_REPORT_GAMEPAD_LEN] BLE_HID_ALIGN4;
static gattCharCfg_t g_ble_hid_gamepad_cccd[GATT_MAX_NUM_CONN] BLE_HID_ALIGN4;
static uint8_t g_ble_hid_gamepad_ref[BLE_HID_REPORT_REFERENCE_LEN] =
    {BLE_HID_REPORT_ID_GAMEPAD, BLE_HID_REPORT_TYPE_INPUT};

static uint8_t g_ble_hid_boot_key_props = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t g_ble_hid_boot_key_value[BLE_HID_REPORT_KEYBOARD_LEN] BLE_HID_ALIGN4;
static gattCharCfg_t g_ble_hid_boot_key_cccd[GATT_MAX_NUM_CONN] BLE_HID_ALIGN4;
static uint8_t g_ble_hid_boot_key_out_props = GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP;
static uint8_t g_ble_hid_boot_key_out_value;

static uint8_t g_ble_hid_boot_mouse_props = GATT_PROP_READ | GATT_PROP_NOTIFY;
static uint8_t g_ble_hid_boot_mouse_value[BLE_HID_REPORT_MOUSE_LEN] BLE_HID_ALIGN4;
static gattCharCfg_t g_ble_hid_boot_mouse_cccd[GATT_MAX_NUM_CONN] BLE_HID_ALIGN4;

static const gattAttrType_t g_ble_hid_service =
    {ATT_BT_UUID_SIZE, g_ble_hid_service_uuid};

static gattAttribute_t g_ble_hid_attr_table[] =
{
    {{ATT_BT_UUID_SIZE, primaryServiceUUID}, GATT_PERMIT_READ, 0,
     (uint8_t *)&g_ble_hid_service},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_info_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_info_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     (uint8_t *)g_ble_hid_information},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_control_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_control_uuid}, GATT_PERMIT_ENCRYPT_WRITE, 0,
     &g_ble_hid_control_value},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_protocol_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_protocol_uuid}, GATT_PERMIT_ENCRYPT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     &g_ble_hid_protocol_value},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_report_map_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_report_map_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     (uint8_t *)g_ble_hid_report_map},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_key_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_report_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     g_ble_hid_key_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID}, GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     (uint8_t *)g_ble_hid_key_cccd},
    {{ATT_BT_UUID_SIZE, reportRefUUID}, GATT_PERMIT_READ, 0,
     g_ble_hid_key_ref},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_led_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_report_uuid}, GATT_PERMIT_ENCRYPT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     &g_ble_hid_led_value},
    {{ATT_BT_UUID_SIZE, reportRefUUID}, GATT_PERMIT_READ, 0,
     g_ble_hid_led_ref},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_mouse_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_report_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     g_ble_hid_mouse_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID}, GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     (uint8_t *)g_ble_hid_mouse_cccd},
    {{ATT_BT_UUID_SIZE, reportRefUUID}, GATT_PERMIT_READ, 0,
     g_ble_hid_mouse_ref},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_gamepad_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_report_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     g_ble_hid_gamepad_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID}, GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     (uint8_t *)g_ble_hid_gamepad_cccd},
    {{ATT_BT_UUID_SIZE, reportRefUUID}, GATT_PERMIT_READ, 0,
     g_ble_hid_gamepad_ref},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_boot_key_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_boot_key_input_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     g_ble_hid_boot_key_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID}, GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     (uint8_t *)g_ble_hid_boot_key_cccd},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_boot_key_out_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_boot_key_output_uuid}, GATT_PERMIT_ENCRYPT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     &g_ble_hid_boot_key_out_value},

    {{ATT_BT_UUID_SIZE, characterUUID}, GATT_PERMIT_READ, 0,
     &g_ble_hid_boot_mouse_props},
    {{ATT_BT_UUID_SIZE, g_ble_hid_boot_mouse_input_uuid}, GATT_PERMIT_ENCRYPT_READ, 0,
     g_ble_hid_boot_mouse_value},
    {{ATT_BT_UUID_SIZE, clientCharCfgUUID}, GATT_PERMIT_READ | GATT_PERMIT_ENCRYPT_WRITE, 0,
     (uint8_t *)g_ble_hid_boot_mouse_cccd}
};

static uint8_t BleHidService_CopyRead(uint8_t *dst,
                                      uint16_t *dst_len,
                                      const uint8_t *src,
                                      uint16_t src_len,
                                      uint16_t offset,
                                      uint16_t max_len)
{
    uint16_t copy_len;
    uint16_t i;

    if(offset > src_len)
    {
        *dst_len = 0;
        return ATT_ERR_INVALID_OFFSET;
    }
    copy_len = (uint16_t)(src_len - offset);
    if(copy_len > max_len)
    {
        copy_len = max_len;
    }
    for(i = 0; i < copy_len; ++i)
    {
        dst[i] = src[offset + i];
    }
    *dst_len = copy_len;
    return SUCCESS;
}

static uint8_t BleHidService_ReadCccd(uint16_t conn_handle,
                                      gattAttribute_t *attr,
                                      uint8_t *value,
                                      uint16_t *length,
                                      uint16_t offset,
                                      uint16_t max_length)
{
    uint16_t cfg;

    if(offset > 0u)
    {
        return ATT_ERR_ATTR_NOT_LONG;
    }
    if(max_length < 2u)
    {
        *length = 0;
        return ATT_ERR_INVALID_VALUE_SIZE;
    }
    cfg = GATTServApp_ReadCharCfg(conn_handle, (gattCharCfg_t *)attr->pValue);
    value[0] = (uint8_t)(cfg & 0xFFu);
    value[1] = (uint8_t)(cfg >> 8);
    *length = 2u;
    return SUCCESS;
}

static bStatus_t BleHidService_ReadAttrCB(uint16_t conn_handle,
                                          gattAttribute_t *attr,
                                          uint8_t *value,
                                          uint16_t *length,
                                          uint16_t offset,
                                          uint16_t max_length,
                                          uint8_t method)
{
    (void)method;

    if((attr == &g_ble_hid_attr_table[BLE_HID_IDX_KEY_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_MOUSE_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_GAMEPAD_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_KEY_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_MOUSE_CCCD]))
    {
        return BleHidService_ReadCccd(conn_handle, attr, value, length,
                                      offset, max_length);
    }

    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_INFO])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_information,
                                      sizeof(g_ble_hid_information), offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_REPORT_MAP])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_report_map,
                                      g_ble_hid_report_map_len, offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_PROTOCOL])
    {
        return BleHidService_CopyRead(value, length, &g_ble_hid_protocol_value,
                                      1u, offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_KEY])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_key_value,
                                      sizeof(g_ble_hid_key_value), offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_LED])
    {
        return BleHidService_CopyRead(value, length, &g_ble_hid_led_value,
                                      1u, offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_MOUSE])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_mouse_value,
                                      sizeof(g_ble_hid_mouse_value), offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_GAMEPAD])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_gamepad_value,
                                      sizeof(g_ble_hid_gamepad_value), offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_KEY])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_boot_key_value,
                                      sizeof(g_ble_hid_boot_key_value), offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_KEY_OUT])
    {
        return BleHidService_CopyRead(value, length, &g_ble_hid_boot_key_out_value,
                                      1u, offset, max_length);
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_MOUSE])
    {
        return BleHidService_CopyRead(value, length, g_ble_hid_boot_mouse_value,
                                      sizeof(g_ble_hid_boot_mouse_value), offset, max_length);
    }
    if((attr == &g_ble_hid_attr_table[BLE_HID_IDX_KEY_REF]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_LED_REF]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_MOUSE_REF]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_GAMEPAD_REF]))
    {
        return BleHidService_CopyRead(value, length, attr->pValue,
                                      BLE_HID_REPORT_REFERENCE_LEN, offset, max_length);
    }

    *length = 0;
    return ATT_ERR_ATTR_NOT_FOUND;
}

static bStatus_t BleHidService_WriteAttrCB(uint16_t conn_handle,
                                           gattAttribute_t *attr,
                                           uint8_t *value,
                                           uint16_t length,
                                           uint16_t offset,
                                           uint8_t method)
{
    (void)method;

    if(gattPermitAuthorWrite(attr->permissions))
    {
        return ATT_ERR_INSUFFICIENT_AUTHOR;
    }
    if(offset > 0u)
    {
        return ATT_ERR_ATTR_NOT_LONG;
    }

    if((attr == &g_ble_hid_attr_table[BLE_HID_IDX_KEY_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_MOUSE_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_GAMEPAD_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_KEY_CCCD]) ||
       (attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_MOUSE_CCCD]))
    {
        return GATTServApp_ProcessCCCWriteReq(conn_handle, attr, value, length,
                                               offset, GATT_CLIENT_CFG_NOTIFY);
    }

    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_PROTOCOL])
    {
        if(length != 1u)
        {
            return ATT_ERR_INVALID_VALUE_SIZE;
        }
        if((value[0] != BLE_HID_PROTOCOL_BOOT) &&
           (value[0] != BLE_HID_PROTOCOL_REPORT))
        {
            return ATT_ERR_INVALID_VALUE;
        }
        g_ble_hid_protocol_value = value[0];
        return SUCCESS;
    }

    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_CONTROL])
    {
        if((length != 1u) || (value[0] > 1u))
        {
            return ATT_ERR_INVALID_VALUE;
        }
        g_ble_hid_control_value = value[0];
        return SUCCESS;
    }

    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_LED])
    {
        if(length != 1u)
        {
            return ATT_ERR_INVALID_VALUE_SIZE;
        }
        g_ble_hid_led_value = value[0];
        return SUCCESS;
    }
    if(attr == &g_ble_hid_attr_table[BLE_HID_IDX_BOOT_KEY_OUT])
    {
        if(length != 1u)
        {
            return ATT_ERR_INVALID_VALUE_SIZE;
        }
        g_ble_hid_boot_key_out_value = value[0];
        return SUCCESS;
    }

    return ATT_ERR_WRITE_NOT_PERMITTED;
}

static void BleHidService_HandleConnStatusCB(uint16_t conn_handle,
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
        GATTServApp_InitCharCfg(conn_handle, g_ble_hid_key_cccd);
        GATTServApp_InitCharCfg(conn_handle, g_ble_hid_mouse_cccd);
        GATTServApp_InitCharCfg(conn_handle, g_ble_hid_gamepad_cccd);
        GATTServApp_InitCharCfg(conn_handle, g_ble_hid_boot_key_cccd);
        GATTServApp_InitCharCfg(conn_handle, g_ble_hid_boot_mouse_cccd);
        g_ble_hid_protocol_value = BLE_HID_PROTOCOL_REPORT;
    }
}

static gattServiceCBs_t g_ble_hid_service_callbacks =
{
    BleHidService_ReadAttrCB,
    BleHidService_WriteAttrCB,
    NULL
};

static gattCharCfg_t *BleHidService_CccdForReport(uint8_t report_id,
                                                  uint8_t *boot_report,
                                                  uint16_t *handle,
                                                  uint8_t **value,
                                                  uint8_t *length)
{
    *boot_report = 0u;
    switch(report_id)
    {
        case BLE_HID_REPORT_ID_KEYBOARD:
            *length = BLE_HID_REPORT_KEYBOARD_LEN;
            if(g_ble_hid_protocol_value == BLE_HID_PROTOCOL_BOOT)
            {
                *boot_report = 1u;
                *handle = g_ble_hid_attr_table[BLE_HID_IDX_BOOT_KEY].handle;
                *value = g_ble_hid_boot_key_value;
                return g_ble_hid_boot_key_cccd;
            }
            *handle = g_ble_hid_attr_table[BLE_HID_IDX_KEY].handle;
            *value = g_ble_hid_key_value;
            return g_ble_hid_key_cccd;

        case BLE_HID_REPORT_ID_MOUSE:
            *length = BLE_HID_REPORT_MOUSE_LEN;
            if(g_ble_hid_protocol_value == BLE_HID_PROTOCOL_BOOT)
            {
                *boot_report = 1u;
                *handle = g_ble_hid_attr_table[BLE_HID_IDX_BOOT_MOUSE].handle;
                *value = g_ble_hid_boot_mouse_value;
                return g_ble_hid_boot_mouse_cccd;
            }
            *handle = g_ble_hid_attr_table[BLE_HID_IDX_MOUSE].handle;
            *value = g_ble_hid_mouse_value;
            return g_ble_hid_mouse_cccd;

        case BLE_HID_REPORT_ID_GAMEPAD:
            *length = BLE_HID_REPORT_GAMEPAD_LEN;
            *handle = g_ble_hid_attr_table[BLE_HID_IDX_GAMEPAD].handle;
            *value = g_ble_hid_gamepad_value;
            return g_ble_hid_gamepad_cccd;

        default:
            *length = 0u;
            *handle = 0u;
            *value = NULL;
            return NULL;
    }
}

bStatus_t BleHidService_AddService(void)
{
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, g_ble_hid_key_cccd);
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, g_ble_hid_mouse_cccd);
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, g_ble_hid_gamepad_cccd);
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, g_ble_hid_boot_key_cccd);
    GATTServApp_InitCharCfg(INVALID_CONNHANDLE, g_ble_hid_boot_mouse_cccd);
    g_ble_hid_protocol_value = BLE_HID_PROTOCOL_REPORT;
    linkDB_Register(BleHidService_HandleConnStatusCB);

    return GATTServApp_RegisterService(g_ble_hid_attr_table,
                                       GATT_NUM_ATTRS(g_ble_hid_attr_table),
                                       GATT_MAX_ENCRYPT_KEY_SIZE,
                                       &g_ble_hid_service_callbacks);
}

uint8_t BleHidService_IsReportNotifyEnabled(uint16_t connection_handle,
                                             uint8_t report_id)
{
    uint8_t boot_report;
    uint16_t handle;
    uint8_t *value;
    uint8_t length;
    gattCharCfg_t *cccd;

    cccd = BleHidService_CccdForReport(report_id, &boot_report, &handle,
                                       &value, &length);
    (void)handle;
    (void)value;
    (void)length;
    (void)boot_report;
    return (cccd != NULL) &&
           ((GATTServApp_ReadCharCfg(connection_handle, cccd) &
             GATT_CLIENT_CFG_NOTIFY) != 0u);
}

bStatus_t BleHidService_Notify(uint16_t connection_handle,
                               uint8_t report_id,
                               const uint8_t *data,
                               uint8_t length)
{
    uint8_t boot_report;
    uint8_t expected_length;
    uint8_t *unused_value;
    uint16_t handle;
    uint8_t i;
    gattCharCfg_t *cccd;
    attHandleValueNoti_t notification;
    bStatus_t status;

    cccd = BleHidService_CccdForReport(report_id, &boot_report, &handle,
                                       &unused_value, &expected_length);
    (void)boot_report;
    if((cccd == NULL) || (length != expected_length))
    {
        return bleInvalidRange;
    }
    if((GATTServApp_ReadCharCfg(connection_handle, cccd) &
        GATT_CLIENT_CFG_NOTIFY) == 0u)
    {
        return bleIncorrectMode;
    }

    /* WCH's GATT transport owns this packet buffer after SUCCESS.  It is a
     * protocol-stack pool allocation, not libc malloc; the application FIFO
     * and all report state remain statically allocated. */
    notification.pValue = GATT_bm_alloc(connection_handle,
                                        ATT_HANDLE_VALUE_NOTI,
                                        length,
                                        NULL,
                                        0u);
    if(notification.pValue == NULL)
    {
        return bleMemAllocError;
    }
    notification.handle = handle;
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
