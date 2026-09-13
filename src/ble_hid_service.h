#ifndef TRI_MODE_HID_HUB_BLE_HID_SERVICE_H
#define TRI_MODE_HID_HUB_BLE_HID_SERVICE_H

#include "CH58xBLE_LIB.h"

#define BLE_HID_REPORT_ID_KEYBOARD       1u
#define BLE_HID_REPORT_ID_MOUSE          2u
#define BLE_HID_REPORT_ID_GAMEPAD        3u

#define BLE_HID_REPORT_NOTIFY_MASK_KEYBOARD (1u << 0)
#define BLE_HID_REPORT_NOTIFY_MASK_MOUSE    (1u << 1)
#define BLE_HID_REPORT_NOTIFY_MASK_GAMEPAD  (1u << 2)

#define BLE_HID_PROTOCOL_BOOT            0u
#define BLE_HID_PROTOCOL_REPORT          1u

#define BLE_HID_REPORT_TYPE_INPUT        1u
#define BLE_HID_REPORT_TYPE_OUTPUT       2u

bStatus_t BleHidService_AddService(void);

uint8_t BleHidService_IsReportNotifyEnabled(uint16_t connection_handle,
                                             uint8_t report_id);
uint8_t BleHidService_GetReportNotifyMask(uint16_t connection_handle);
uint8_t BleHidService_IsAnyReportNotifyEnabled(uint16_t connection_handle);

bStatus_t BleHidService_Notify(uint16_t connection_handle,
                               uint8_t report_id,
                               const uint8_t *data,
                               uint8_t length);

#endif /* TRI_MODE_HID_HUB_BLE_HID_SERVICE_H */
