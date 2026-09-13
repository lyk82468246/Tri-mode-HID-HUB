#ifndef TRI_MODE_HID_HUB_BLE_OUTPUT_H
#define TRI_MODE_HID_HUB_BLE_OUTPUT_H

#include <stdint.h>

void BleOutput_Init(void);
void BleOutput_ProcessInput(void);
void BleOutput_Process(void);

uint8_t BleOutput_IsConnected(void);
uint8_t BleOutput_IsReady(void);
uint8_t BleOutput_IsHidReady(void);
uint8_t BleOutput_GetHidReportNotifyMask(void);
uint8_t BleOutput_IsStreamReady(void);
uint16_t BleOutput_GetConnectionHandle(void);

#endif /* TRI_MODE_HID_HUB_BLE_OUTPUT_H */
