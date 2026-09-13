#ifndef TRI_MODE_HID_HUB_USB_HOST_HID_H
#define TRI_MODE_HID_HUB_USB_HOST_HID_H

#include <stdint.h>

#define USB_HOST_HID_MAX_INTERFACES          2u
#define USB_HOST_HID_MAX_CONFIG_DESCRIPTOR   256u
#define USB_HOST_HID_MAX_REPORT_DESCRIPTOR   256u
#define USB_HOST_HID_MAX_REPORT_PACKET       64u

typedef struct
{
    uint32_t attach_count;
    uint32_t detach_count;
    uint32_t enumeration_success;
    uint32_t enumeration_error;
    uint32_t transfer_timeout;
    uint32_t transfer_error;
    uint32_t report_descriptor_error;
    uint32_t report_parse_error;
    uint32_t report_drop;
    uint32_t report_count;
} UsbHostHidStats;

void UsbHostHid_Init(void);
void UsbHostHid_Process(void);
uint8_t UsbHostHid_IsReady(void);
uint8_t UsbHostHid_GetInterfaceCount(void);
void UsbHostHid_GetStats(UsbHostHidStats *stats);

#endif /* TRI_MODE_HID_HUB_USB_HOST_HID_H */
