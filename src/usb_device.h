#ifndef TRI_MODE_HID_HUB_USB_DEVICE_H
#define TRI_MODE_HID_HUB_USB_DEVICE_H

#include <stdint.h>

#define USB_DEVICE_CDC_PACKET_MAX       64u

void UsbDevice_Init(void);
void UsbDevice_ProcessTask(void);

uint8_t UsbDevice_DequeueCdcRx(uint8_t *data, uint8_t *length);
uint8_t UsbDevice_GetConfiguration(void);
uint8_t UsbDevice_IsReady(void);
uint8_t UsbDevice_GetKeyboardLeds(void);

#endif /* TRI_MODE_HID_HUB_USB_DEVICE_H */
