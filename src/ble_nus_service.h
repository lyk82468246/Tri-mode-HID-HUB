#ifndef TRI_MODE_HID_HUB_BLE_NUS_SERVICE_H
#define TRI_MODE_HID_HUB_BLE_NUS_SERVICE_H

#include "CH58xBLE_LIB.h"

#define BLE_NUS_MAX_PAYLOAD              20u

typedef struct
{
    uint8_t length;
    uint8_t reserved[3];
    uint8_t bytes[BLE_NUS_MAX_PAYLOAD];
} BleNusRxFrame;

bStatus_t BleNusService_AddService(void);

uint8_t BleNusService_DequeueRx(BleNusRxFrame *frame);
uint32_t BleNusService_GetRxDropCount(void);
uint8_t BleNusService_IsTxNotifyEnabled(uint16_t connection_handle);

bStatus_t BleNusService_Notify(uint16_t connection_handle,
                               const uint8_t *data,
                               uint8_t length);

#endif /* TRI_MODE_HID_HUB_BLE_NUS_SERVICE_H */
