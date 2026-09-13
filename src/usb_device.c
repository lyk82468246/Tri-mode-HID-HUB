#include "CH58x_common.h"
#include "CH58x_usbdev.h"

#include "event_router.h"
#include "usb_device.h"

#define USB_DEVICE_EP0_SIZE             64u
#define USB_DEVICE_HID_PACKET_SIZE      16u
#define USB_DEVICE_INTERFACE_HID         0u
#define USB_DEVICE_INTERFACE_CDC_CTRL    1u
#define USB_DEVICE_INTERFACE_CDC_DATA    2u

#define USB_CONTROL_OUT_NONE             0u
#define USB_CONTROL_OUT_HID_SET_REPORT   1u
#define USB_CONTROL_OUT_CDC_LINE_CODING  2u

#define CDC_REQ_SET_LINE_CODING          0x20u
#define CDC_REQ_GET_LINE_CODING          0x21u
#define CDC_REQ_SET_CONTROL_LINE_STATE   0x22u
#define CDC_REQ_SEND_BREAK               0x23u

#define USB_DEVICE_REMOTE_WAKEUP         0x01u

/*
 * The VID/PID are development identifiers only.  A production device must
 * use an assigned VID/PID pair before it is released or distributed.
 */
#define USB_DEVICE_VID                   0x1209u
#define USB_DEVICE_PID                   0x5820u

static const uint8_t g_usb_device_descriptor[] =
{
    0x12, 0x01, 0x00, 0x02, 0xEF, 0x02, 0x01, USB_DEVICE_EP0_SIZE,
    (uint8_t)(USB_DEVICE_VID & 0xFF), (uint8_t)(USB_DEVICE_VID >> 8),
    (uint8_t)(USB_DEVICE_PID & 0xFF), (uint8_t)(USB_DEVICE_PID >> 8),
    0x00, 0x01, 0x01, 0x02, 0x03, 0x01
};

/* HID + CDC ACM; total length is 100 bytes (0x0064). */
static const uint8_t g_usb_configuration_descriptor[] =
{
    /* Configuration */
    0x09, 0x02, 0x64, 0x00, 0x03, 0x01, 0x00, 0x80, 0x32,

    /* Interface 0: one HID interface with three Report IDs */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0xC1, 0x00,
    0x07, 0x05, 0x81, 0x03, USB_DEVICE_HID_PACKET_SIZE, 0x00, 0x01,

    /* CDC IAD, interface 1: communication/control */
    0x08, 0x0B, 0x01, 0x02, 0x02, 0x02, 0x01, 0x00,
    0x09, 0x04, 0x01, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x02,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x01, 0x02,
    0x07, 0x05, 0x83, 0x03, 0x08, 0x00, 0x10,

    /* Interface 2: CDC data */
    0x09, 0x04, 0x02, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
    0x07, 0x05, 0x82, 0x02, 0x40, 0x00, 0x00
};

static const uint8_t g_usb_hid_report_descriptor[] =
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
    0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x36, 0x15, 0x81, 0x25,
    0x7F, 0x75, 0x08, 0x95, 0x05, 0x81, 0x02, 0xC0
};

static const uint8_t g_usb_string_language[] = {0x04, 0x03, 0x09, 0x04};

static const uint8_t g_usb_string_manufacturer[] =
{
    0x22, 0x03,
    'T', 0, 'r', 0, 'i', 0, '-', 0, 'm', 0, 'o', 0, 'd', 0, 'e', 0,
    ' ', 0, 'H', 0, 'I', 0, 'D', 0, ' ', 0, 'H', 0, 'u', 0, 'b', 0
};

static const uint8_t g_usb_string_product[] =
{
    0x22, 0x03,
    'C', 0, 'H', 0, '5', 0, '8', 0, '2', 0, 'M', 0, ' ', 0,
    'M', 0, 'u', 0, 'l', 0, 't', 0, 'i', 0, ' ', 0,
    'H', 0, 'I', 0, 'D', 0
};

static const uint8_t g_usb_string_serial[] =
{
    0x0E, 0x03, 'M', 0, '1', 0, '-', 0, 'D', 0, 'E', 0, 'V', 0
};

typedef struct
{
    uint8_t length;
    uint8_t reserved[3];
    uint8_t bytes[USB_DEVICE_CDC_PACKET_MAX];
} UsbCdcRxPacket;

/* WCH USB DMA layout: EP0/4 = 192 bytes, EP1/2/3 = 128 bytes each. */
static uint8_t g_ep0_ram[64u + 64u + 64u] EVENT_ROUTER_ALIGN4;
static uint8_t g_ep1_ram[64u + 64u] EVENT_ROUTER_ALIGN4;
static uint8_t g_ep2_ram[64u + 64u] EVENT_ROUTER_ALIGN4;
static uint8_t g_ep3_ram[64u + 64u] EVENT_ROUTER_ALIGN4;

/* These symbols are declared by CH58x_usbdev.h.  The WCH driver source is
 * excluded from the project because this file owns the composite controller. */
uint8_t *pEP0_RAM_Addr;
uint8_t *pEP1_RAM_Addr;
uint8_t *pEP2_RAM_Addr;
uint8_t *pEP3_RAM_Addr;

static UsbCdcRxPacket g_cdc_rx_storage[4] EVENT_ROUTER_ALIGN4;
static StaticSpscRing g_cdc_rx_ring;

static volatile uint8_t g_usb_configuration;
static volatile uint8_t g_usb_suspended;
static uint8_t g_setup_request;
static uint8_t g_setup_request_type;
static uint16_t g_setup_length;
static const uint8_t *g_setup_descriptor;
static uint8_t g_pending_address;
static uint8_t g_control_out_request;
static uint8_t g_control_out_expected;
static uint8_t g_control_out_received;
static uint8_t g_control_out_data[USB_DEVICE_EP0_SIZE] EVENT_ROUTER_ALIGN4;
static uint8_t g_hid_idle;
static uint8_t g_hid_protocol = 1u;
static uint8_t g_hid_keyboard_leds;
static uint8_t g_cdc_control_line_state;
static uint8_t g_cdc_line_coding[7] = {0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08};
static uint8_t g_usb_remote_wakeup;

static void UsbDevice_Copy(uint8_t *dst, const uint8_t *src, uint16_t length)
{
    uint16_t i;

    for(i = 0; i < length; ++i)
    {
        dst[i] = src[i];
    }
}

static uint8_t UsbDevice_Min64(uint16_t length)
{
    return (length > USB_DEVICE_EP0_SIZE) ? USB_DEVICE_EP0_SIZE : (uint8_t)length;
}

static void UsbDevice_StallControl(void)
{
    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                   UEP_R_RES_STALL | UEP_T_RES_STALL;
}

static uint8_t UsbDevice_SelectDescriptor(uint8_t descriptor_type,
                                          uint8_t descriptor_index,
                                          uint16_t descriptor_interface,
                                          const uint8_t **descriptor)
{
    switch(descriptor_type)
    {
        case USB_DESCR_TYP_DEVICE:
            *descriptor = g_usb_device_descriptor;
            return sizeof(g_usb_device_descriptor);

        case USB_DESCR_TYP_CONFIG:
            *descriptor = g_usb_configuration_descriptor;
            return sizeof(g_usb_configuration_descriptor);

        case USB_DESCR_TYP_HID:
            if((descriptor_interface & 0xFFu) != USB_DEVICE_INTERFACE_HID)
            {
                return 0;
            }
            /* Interface 0 HID descriptor starts at offset 18. */
            *descriptor = &g_usb_configuration_descriptor[18];
            return 9u;

        case USB_DESCR_TYP_REPORT:
            if((descriptor_interface & 0xFFu) != USB_DEVICE_INTERFACE_HID)
            {
                return 0;
            }
            *descriptor = g_usb_hid_report_descriptor;
            return sizeof(g_usb_hid_report_descriptor);

        case USB_DESCR_TYP_STRING:
            switch(descriptor_index)
            {
                case 0:
                    *descriptor = g_usb_string_language;
                    return sizeof(g_usb_string_language);
                case 1:
                    *descriptor = g_usb_string_manufacturer;
                    return sizeof(g_usb_string_manufacturer);
                case 2:
                    *descriptor = g_usb_string_product;
                    return sizeof(g_usb_string_product);
                case 3:
                    *descriptor = g_usb_string_serial;
                    return sizeof(g_usb_string_serial);
                default:
                    return 0;
            }

        default:
            return 0;
    }
}

static void UsbDevice_SetEndpointHalt(uint16_t endpoint, uint8_t halt)
{
    volatile uint8_t *control = 0;
    uint8_t in = (endpoint & USB_ENDP_DIR_MASK) ? 1u : 0u;

    switch(endpoint & USB_ENDP_ADDR_MASK)
    {
        case 1:
            control = &R8_UEP1_CTRL;
            break;
        case 2:
            control = &R8_UEP2_CTRL;
            break;
        case 3:
            control = &R8_UEP3_CTRL;
            break;
        default:
            return;
    }

    if(in)
    {
        if(halt)
        {
            *control = (*control & ~MASK_UEP_T_RES) | UEP_T_RES_STALL;
        }
        else
        {
            *control = (*control & ~(RB_UEP_T_TOG | MASK_UEP_T_RES)) | UEP_T_RES_NAK;
        }
    }
    else
    {
        if(halt)
        {
            *control = (*control & ~MASK_UEP_R_RES) | UEP_R_RES_STALL;
        }
        else
        {
            *control = (*control & ~(RB_UEP_R_TOG | MASK_UEP_R_RES)) | UEP_R_RES_ACK;
        }
    }
}

static uint8_t UsbDevice_EndpointHalted(uint16_t endpoint)
{
    uint8_t control;

    switch(endpoint & USB_ENDP_ADDR_MASK)
    {
        case 1:
            control = R8_UEP1_CTRL;
            break;
        case 2:
            control = R8_UEP2_CTRL;
            break;
        case 3:
            control = R8_UEP3_CTRL;
            break;
        default:
            return 0;
    }
    return (endpoint & USB_ENDP_DIR_MASK) ?
               (((control & MASK_UEP_T_RES) == UEP_T_RES_STALL) ? 1u : 0u) :
               (((control & MASK_UEP_R_RES) == UEP_R_RES_STALL) ? 1u : 0u);
}

static uint8_t UsbDevice_HidReportSize(uint8_t report_id)
{
    switch(report_id)
    {
        case 1:
            return HID_KEYBOARD_REPORT_LEN;
        case 2:
            return HID_MOUSE_REPORT_LEN;
        case 3:
            return HID_GAMEPAD_REPORT_LEN;
        default:
            return 0;
    }
}

static uint8_t UsbDevice_PrepareHidGetReport(uint8_t report_id)
{
    uint8_t length = UsbDevice_HidReportSize(report_id);
    uint8_t i;

    if(length == 0)
    {
        return 0;
    }
    pEP0_DataBuf[0] = report_id;
    for(i = 1; i <= length; ++i)
    {
        pEP0_DataBuf[i] = 0;
    }
    return (uint8_t)(length + 1u);
}

static void UsbDevice_ApplyControlOut(void)
{
    if(g_control_out_request == USB_CONTROL_OUT_HID_SET_REPORT)
    {
        if((g_control_out_received >= 2u) && (g_control_out_data[0] == 1u))
        {
            g_hid_keyboard_leds = g_control_out_data[1];
        }
    }
    else if(g_control_out_request == USB_CONTROL_OUT_CDC_LINE_CODING)
    {
        if(g_control_out_received >= sizeof(g_cdc_line_coding))
        {
            UsbDevice_Copy(g_cdc_line_coding,
                           g_control_out_data,
                           sizeof(g_cdc_line_coding));
        }
    }
    g_control_out_request = USB_CONTROL_OUT_NONE;
}

static void UsbDevice_ResetEndpointState(void)
{
    R8_USB_DEV_AD = 0;
    g_usb_configuration = 0;
    g_usb_suspended = 0;
    g_pending_address = 0;
    g_control_out_request = USB_CONTROL_OUT_NONE;
    g_control_out_expected = 0;
    g_control_out_received = 0;
    g_usb_remote_wakeup = 0;
    g_hid_idle = 0;
    g_hid_protocol = 1u;
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
}

/* This is the WCH CH58x Device controller setup used by the application. */
void USB_DeviceInit(void)
{
    R8_USB_CTRL = 0x00;
    R8_UEP4_1_MOD = RB_UEP4_RX_EN | RB_UEP4_TX_EN |
                   RB_UEP1_RX_EN | RB_UEP1_TX_EN;
    R8_UEP2_3_MOD = RB_UEP2_RX_EN | RB_UEP2_TX_EN |
                   RB_UEP3_RX_EN | RB_UEP3_TX_EN;

    R16_UEP0_DMA = (uint16_t)(uint32_t)pEP0_RAM_Addr;
    R16_UEP1_DMA = (uint16_t)(uint32_t)pEP1_RAM_Addr;
    R16_UEP2_DMA = (uint16_t)(uint32_t)pEP2_RAM_Addr;
    R16_UEP3_DMA = (uint16_t)(uint32_t)pEP3_RAM_Addr;

    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP4_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;

    R8_USB_DEV_AD = 0x00;
    R8_USB_CTRL = RB_UC_DEV_PU_EN | RB_UC_INT_BUSY | RB_UC_DMA_EN;
    R16_PIN_ANALOG_IE |= RB_PIN_USB_IE | RB_PIN_USB_DP_PU;
    R8_USB_INT_FG = 0xFF;
    R8_UDEV_CTRL = RB_UD_PD_DIS | RB_UD_PORT_EN;
    R8_USB_INT_EN = RB_UIE_SUSPEND | RB_UIE_BUS_RST | RB_UIE_TRANSFER;
}

void DevEP1_IN_Deal(uint8_t length)
{
    R8_UEP1_T_LEN = length;
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

void DevEP2_IN_Deal(uint8_t length)
{
    R8_UEP2_T_LEN = length;
    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

void DevEP3_IN_Deal(uint8_t length)
{
    R8_UEP3_T_LEN = length;
    R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

void DevEP4_IN_Deal(uint8_t length)
{
    R8_UEP4_T_LEN = length;
    R8_UEP4_CTRL = (R8_UEP4_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

void DevEP1_OUT_Deal(uint8_t length)
{
    (void)length;
    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
}

void DevEP2_OUT_Deal(uint8_t length)
{
    (void)length;
    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
}

void DevEP3_OUT_Deal(uint8_t length)
{
    (void)length;
    R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
}

void DevEP4_OUT_Deal(uint8_t length)
{
    (void)length;
    R8_UEP4_CTRL = (R8_UEP4_CTRL & ~MASK_UEP_R_RES) | UEP_R_RES_ACK;
}

void USB_DevTransProcess(void)
{
    uint8_t intflag = R8_USB_INT_FG;

    if(intflag & RB_UIF_TRANSFER)
    {
        uint8_t int_status = R8_USB_INT_ST;
        uint8_t token = int_status & (MASK_UIS_TOKEN | MASK_UIS_ENDP);
        uint8_t length;

        if(!(int_status & RB_UIS_SETUP_ACT))
        {
            switch(token)
            {
                case UIS_TOKEN_IN:
                    if((g_setup_request == USB_GET_DESCRIPTOR) &&
                       (g_setup_length != 0))
                    {
                        length = UsbDevice_Min64(g_setup_length);
                        UsbDevice_Copy(pEP0_DataBuf,
                                       g_setup_descriptor,
                                       length);
                        g_setup_descriptor += length;
                        g_setup_length = (uint16_t)(g_setup_length - length);
                        R8_UEP0_T_LEN = length;
                        R8_UEP0_CTRL ^= RB_UEP_T_TOG;
                    }
                    else if(g_setup_request == USB_SET_ADDRESS)
                    {
                        R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) |
                                        g_pending_address;
                        R8_UEP0_T_LEN = 0;
                        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                    }
                    else
                    {
                        R8_UEP0_T_LEN = 0;
                        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                    }
                    break;

                case UIS_TOKEN_OUT:
                    length = R8_USB_RX_LEN;
                    if((g_control_out_request != USB_CONTROL_OUT_NONE) &&
                       (g_control_out_received < g_control_out_expected))
                    {
                        uint8_t copy_length = length;
                        uint8_t remaining = (uint8_t)(g_control_out_expected -
                                                       g_control_out_received);
                        if(copy_length > remaining)
                        {
                            copy_length = remaining;
                        }
                        if(copy_length != 0)
                        {
                            UsbDevice_Copy(&g_control_out_data[g_control_out_received],
                                           pEP0_DataBuf,
                                           copy_length);
                            g_control_out_received = (uint8_t)(g_control_out_received +
                                                               copy_length);
                        }
                        if(g_control_out_received >= g_control_out_expected)
                        {
                            UsbDevice_ApplyControlOut();
                        }
                    }
                    R8_UEP0_T_LEN = 0;
                    R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                                   UEP_R_RES_ACK | UEP_T_RES_ACK;
                    break;

                case UIS_TOKEN_OUT | 1:
                    if(int_status & RB_UIS_TOG_OK)
                    {
                        R8_UEP1_CTRL ^= RB_UEP_R_TOG;
                        length = R8_USB_RX_LEN;
                        if((length >= 2u) && (pEP1_OUT_DataBuf[0] == 1u))
                        {
                            g_hid_keyboard_leds = pEP1_OUT_DataBuf[1];
                        }
                        DevEP1_OUT_Deal(length);
                    }
                    break;

                case UIS_TOKEN_IN | 1:
                    R8_UEP1_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) |
                                   UEP_T_RES_NAK;
                    break;

                case UIS_TOKEN_OUT | 2:
                    if(int_status & RB_UIS_TOG_OK)
                    {
                        UsbCdcRxPacket packet;
                        R8_UEP2_CTRL ^= RB_UEP_R_TOG;
                        length = R8_USB_RX_LEN;
                        packet.length = length;
                        packet.reserved[0] = 0;
                        packet.reserved[1] = 0;
                        packet.reserved[2] = 0;
                        UsbDevice_Copy(packet.bytes, pEP2_OUT_DataBuf, length);
                        (void)StaticSpscRing_Push(&g_cdc_rx_ring, &packet);
                        DevEP2_OUT_Deal(length);
                    }
                    break;

                case UIS_TOKEN_IN | 2:
                    R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) |
                                   UEP_T_RES_NAK;
                    break;

                case UIS_TOKEN_OUT | 3:
                    if(int_status & RB_UIS_TOG_OK)
                    {
                        R8_UEP3_CTRL ^= RB_UEP_R_TOG;
                        DevEP3_OUT_Deal(R8_USB_RX_LEN);
                    }
                    break;

                case UIS_TOKEN_IN | 3:
                    R8_UEP3_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) |
                                   UEP_T_RES_NAK;
                    break;

                case UIS_TOKEN_OUT | 4:
                    if(int_status & RB_UIS_TOG_OK)
                    {
                        R8_UEP4_CTRL ^= RB_UEP_R_TOG;
                        DevEP4_OUT_Deal(R8_USB_RX_LEN);
                    }
                    break;

                case UIS_TOKEN_IN | 4:
                    R8_UEP4_CTRL ^= RB_UEP_T_TOG;
                    R8_UEP4_CTRL = (R8_UEP4_CTRL & ~MASK_UEP_T_RES) |
                                   UEP_T_RES_NAK;
                    break;

                default:
                    break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }

        if(R8_USB_INT_ST & RB_UIS_SETUP_ACT)
        {
            uint8_t descriptor_length;
            uint8_t descriptor_type;
            uint8_t error = 0;

            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                           UEP_R_RES_ACK | UEP_T_RES_NAK;
            g_setup_request = pSetupReqPak->bRequest;
            g_setup_request_type = pSetupReqPak->bRequestType;
            g_setup_length = pSetupReqPak->wLength;
            g_setup_descriptor = 0;
            g_control_out_request = USB_CONTROL_OUT_NONE;
            g_control_out_expected = 0;
            g_control_out_received = 0;

            if((g_setup_request_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD)
            {
                switch(g_setup_request)
                {
                    case USB_GET_DESCRIPTOR:
                        descriptor_type = (uint8_t)(pSetupReqPak->wValue >> 8);
                        descriptor_length = UsbDevice_SelectDescriptor(
                            descriptor_type,
                            (uint8_t)(pSetupReqPak->wValue & 0xFFu),
                            pSetupReqPak->wIndex,
                            &g_setup_descriptor);
                        if(descriptor_length == 0)
                        {
                            error = 1;
                        }
                        else
                        {
                            if(g_setup_length > descriptor_length)
                            {
                                g_setup_length = descriptor_length;
                            }
                        }
                        break;

                    case USB_SET_ADDRESS:
                        g_pending_address = (uint8_t)(pSetupReqPak->wValue & 0x7Fu);
                        g_setup_length = 0;
                        break;

                    case USB_GET_CONFIGURATION:
                        pEP0_DataBuf[0] = g_usb_configuration;
                        g_setup_length = (g_setup_length > 1u) ? 1u : g_setup_length;
                        break;

                    case USB_SET_CONFIGURATION:
                        g_usb_configuration = (uint8_t)(pSetupReqPak->wValue & 0xFFu);
                        g_setup_length = 0;
                        break;

                    case USB_GET_INTERFACE:
                        pEP0_DataBuf[0] = 0;
                        g_setup_length = (g_setup_length > 1u) ? 1u : g_setup_length;
                        break;

                    case USB_SET_INTERFACE:
                        g_setup_length = 0;
                        break;

                    case USB_GET_STATUS:
                        pEP0_DataBuf[0] = 0;
                        pEP0_DataBuf[1] = 0;
                        if((g_setup_request_type & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            pEP0_DataBuf[0] = UsbDevice_EndpointHalted(
                                pSetupReqPak->wIndex);
                        }
                        else if((g_setup_request_type & USB_REQ_RECIP_MASK) ==
                                USB_REQ_RECIP_DEVICE)
                        {
                            pEP0_DataBuf[0] = g_usb_remote_wakeup ? 0x02u : 0u;
                        }
                        g_setup_length = (g_setup_length > 2u) ? 2u : g_setup_length;
                        break;

                    case USB_CLEAR_FEATURE:
                    case USB_SET_FEATURE:
                        if((g_setup_request_type & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                        {
                            if(pSetupReqPak->wValue != 0u)
                            {
                                error = 1;
                            }
                            else
                            {
                                UsbDevice_SetEndpointHalt(pSetupReqPak->wIndex,
                                                           (g_setup_request == USB_SET_FEATURE));
                            }
                        }
                        else if(((g_setup_request_type & USB_REQ_RECIP_MASK) ==
                                 USB_REQ_RECIP_DEVICE) &&
                                (pSetupReqPak->wValue == USB_DEVICE_REMOTE_WAKEUP))
                        {
                            g_usb_remote_wakeup = (g_setup_request == USB_SET_FEATURE) ? 1u : 0u;
                        }
                        else
                        {
                            error = 1;
                        }
                        g_setup_length = 0;
                        break;

                    default:
                        error = 1;
                        break;
                }
            }
            else if((g_setup_request_type & USB_REQ_TYP_MASK) == USB_REQ_TYP_CLASS)
            {
                if((g_setup_request_type & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_INTERF)
                {
                    if((pSetupReqPak->wIndex & 0xFFu) == USB_DEVICE_INTERFACE_HID)
                    {
                        switch(g_setup_request)
                        {
                            case HID_GET_IDLE:
                                pEP0_DataBuf[0] = g_hid_idle;
                                g_setup_length = (g_setup_length > 1u) ? 1u : g_setup_length;
                                break;
                            case HID_SET_IDLE:
                                g_hid_idle = (uint8_t)(pSetupReqPak->wValue >> 8);
                                g_setup_length = 0;
                                break;
                            case HID_GET_PROTOCOL:
                                pEP0_DataBuf[0] = g_hid_protocol;
                                g_setup_length = (g_setup_length > 1u) ? 1u : g_setup_length;
                                break;
                            case HID_SET_PROTOCOL:
                                g_hid_protocol = (uint8_t)(pSetupReqPak->wValue & 0xFFu);
                                g_setup_length = 0;
                                break;
                            case HID_GET_REPORT:
                                g_setup_length = UsbDevice_PrepareHidGetReport(
                                    (uint8_t)(pSetupReqPak->wValue & 0xFFu));
                                if(g_setup_length == 0)
                                {
                                    error = 1;
                                }
                                else if(g_setup_length > pSetupReqPak->wLength)
                                {
                                    g_setup_length = pSetupReqPak->wLength;
                                }
                                break;
                            case HID_SET_REPORT:
                                if(pSetupReqPak->wLength > USB_DEVICE_EP0_SIZE)
                                {
                                    error = 1;
                                }
                                else
                                {
                                    g_control_out_request = USB_CONTROL_OUT_HID_SET_REPORT;
                                    g_control_out_expected = (uint8_t)pSetupReqPak->wLength;
                                    g_setup_length = 0;
                                }
                                break;
                            default:
                                error = 1;
                                break;
                        }
                    }
                    else if((pSetupReqPak->wIndex & 0xFFu) == USB_DEVICE_INTERFACE_CDC_CTRL)
                    {
                        switch(g_setup_request)
                        {
                            case CDC_REQ_SET_LINE_CODING:
                                if(pSetupReqPak->wLength != sizeof(g_cdc_line_coding))
                                {
                                    error = 1;
                                }
                                else
                                {
                                    g_control_out_request = USB_CONTROL_OUT_CDC_LINE_CODING;
                                    g_control_out_expected = sizeof(g_cdc_line_coding);
                                    g_setup_length = 0;
                                }
                                break;
                            case CDC_REQ_GET_LINE_CODING:
                                UsbDevice_Copy(pEP0_DataBuf,
                                               g_cdc_line_coding,
                                               sizeof(g_cdc_line_coding));
                                g_setup_length = (g_setup_length > sizeof(g_cdc_line_coding)) ?
                                                      sizeof(g_cdc_line_coding) : g_setup_length;
                                break;
                            case CDC_REQ_SET_CONTROL_LINE_STATE:
                                g_cdc_control_line_state = (uint8_t)(pSetupReqPak->wValue & 0xFFu);
                                g_setup_length = 0;
                                break;
                            case CDC_REQ_SEND_BREAK:
                                g_setup_length = 0;
                                break;
                            default:
                                error = 1;
                                break;
                        }
                    }
                    else
                    {
                        error = 1;
                    }
                }
                else
                {
                    error = 1;
                }
            }
            else
            {
                error = 1;
            }

            if(error)
            {
                UsbDevice_StallControl();
            }
            else
            {
                if(g_setup_request_type & USB_REQ_TYP_IN)
                {
                    length = UsbDevice_Min64(g_setup_length);
                    if((g_setup_request == USB_GET_DESCRIPTOR) && (length != 0))
                    {
                        UsbDevice_Copy(pEP0_DataBuf, g_setup_descriptor, length);
                        g_setup_descriptor += length;
                    }
                    g_setup_length = (uint16_t)(g_setup_length - length);
                }
                else
                {
                    length = 0;
                }
                R8_UEP0_T_LEN = length;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG |
                               UEP_R_RES_ACK | UEP_T_RES_ACK;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }
    }
    else if(intflag & RB_UIF_BUS_RST)
    {
        UsbDevice_ResetEndpointState();
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    }
    else if(intflag & RB_UIF_SUSPEND)
    {
        g_usb_suspended = (R8_USB_MIS_ST & RB_UMS_SUSPEND) ? 1u : 0u;
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    }
    else
    {
        R8_USB_INT_FG = intflag;
    }
}

void UsbDevice_Init(void)
{
    g_usb_configuration = 0;
    g_usb_suspended = 0;
    g_setup_request = 0;
    g_setup_request_type = 0;
    g_setup_length = 0;
    g_setup_descriptor = 0;
    g_pending_address = 0;
    g_control_out_request = USB_CONTROL_OUT_NONE;
    g_control_out_expected = 0;
    g_control_out_received = 0;
    g_hid_idle = 0;
    g_hid_protocol = 1u;
    g_hid_keyboard_leds = 0;
    g_cdc_control_line_state = 0;
    g_usb_remote_wakeup = 0;
    StaticSpscRing_Init(&g_cdc_rx_ring,
                        g_cdc_rx_storage,
                        sizeof(g_cdc_rx_storage[0]),
                        4u);

    pEP0_RAM_Addr = g_ep0_ram;
    pEP1_RAM_Addr = g_ep1_ram;
    pEP2_RAM_Addr = g_ep2_ram;
    pEP3_RAM_Addr = g_ep3_ram;
    USB_DeviceInit();
    PFIC_EnableIRQ(USB_IRQn);
}

uint8_t UsbDevice_DequeueCdcRx(uint8_t *data, uint8_t *length)
{
    UsbCdcRxPacket packet;

    if(!StaticSpscRing_Pop(&g_cdc_rx_ring, &packet))
    {
        return 0;
    }
    *length = (packet.length > USB_DEVICE_CDC_PACKET_MAX) ?
                  USB_DEVICE_CDC_PACKET_MAX : packet.length;
    UsbDevice_Copy(data, packet.bytes, *length);
    return 1;
}

void UsbDevice_ProcessTask(void)
{
    HidTxFrame hid_frame;
    StreamTxFrame stream_frame;
    uint8_t wire_length;
    uint8_t payload_length;

    if(g_usb_configuration == 0u || g_usb_suspended)
    {
        return;
    }

    PFIC_DisableIRQ(USB_IRQn);
    if(((R8_UEP1_CTRL & MASK_UEP_T_RES) == UEP_T_RES_NAK) &&
       EventRouter_DequeueUsbHidFrame(&hid_frame))
    {
        payload_length = (hid_frame.length > ROUTER_EVENT_PAYLOAD_LEN) ?
                             ROUTER_EVENT_PAYLOAD_LEN : hid_frame.length;
        wire_length = (uint8_t)(payload_length + 1u);
        pEP1_IN_DataBuf[0] = hid_frame.report_id;
        UsbDevice_Copy(&pEP1_IN_DataBuf[1], hid_frame.bytes, payload_length);
        DevEP1_IN_Deal(wire_length);
    }
    PFIC_EnableIRQ(USB_IRQn);

    PFIC_DisableIRQ(USB_IRQn);
    if(((R8_UEP2_CTRL & MASK_UEP_T_RES) == UEP_T_RES_NAK) &&
       EventRouter_DequeueUsbStreamFrame(&stream_frame))
    {
        if(stream_frame.length > USB_DEVICE_CDC_PACKET_MAX)
        {
            stream_frame.length = USB_DEVICE_CDC_PACKET_MAX;
        }
        UsbDevice_Copy(pEP2_IN_DataBuf, stream_frame.bytes, stream_frame.length);
        DevEP2_IN_Deal(stream_frame.length);
    }
    PFIC_EnableIRQ(USB_IRQn);
}

uint8_t UsbDevice_GetConfiguration(void)
{
    return g_usb_configuration;
}

uint8_t UsbDevice_IsReady(void)
{
    return (g_usb_configuration != 0u && g_usb_suspended == 0u) ? 1u : 0u;
}

uint8_t UsbDevice_GetKeyboardLeds(void)
{
    return g_hid_keyboard_leds;
}

__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void)
{
    USB_DevTransProcess();
}
