#include "CH58x_common.h"
#include "CH58x_usbhost.h"

#include "board_pins.h"
#include "event_router.h"
#include "static_spsc_ring.h"
#include "usb_host_hid.h"

/*
 * M4 deliberately does not call the blocking transaction/control helpers
 * from CH58x_usb2hostBase.c.  Those official helpers contain busy loops and
 * delay calls.  This module only uses the WCH
 * register definitions and USB2_HostInit(), then advances one hardware
 * transaction per TMOS service turn.
 */
#define USB_HOST_TRANSFER_TIMEOUT_TICKS     20u  /* 40 ms at a 2 ms service */
#define USB_HOST_CONTROL_TIMEOUT_TICKS      100u /* 200 ms per request */
#define USB_HOST_RESET_TICKS                8u   /* 16 ms bus reset */
#define USB_HOST_RESET_SETTLE_TICKS         1u
#define USB_HOST_ADDRESS_SETTLE_TICKS       5u   /* 10 ms after SET_ADDRESS */
#define USB_HOST_ERROR_RETRY_TICKS          50u  /* 100 ms before re-enumerate */
#define USB_HOST_REPORT_QUEUE_CAPACITY      4u
#define USB_HOST_PROCESS_REPORT_BUDGET      4u

#define USB_HOST_HID_CLASS                  0x03u
#define USB_HOST_HID_SUBCLASS_BOOT          0x01u
#define USB_HOST_HID_PROTOCOL_KEYBOARD      0x01u
#define USB_HOST_HID_PROTOCOL_MOUSE         0x02u

#define USB_HOST_HID_INPUT                  0x80u
#define USB_HOST_HID_GET_DESCRIPTOR         0x06u
#define USB_HOST_HID_SET_CONFIGURATION      0x09u
#define USB_HOST_HID_SET_ADDRESS            0x05u
#define USB_HOST_HID_SET_IDLE               0x0Au
#define USB_HOST_HID_SET_PROTOCOL           0x0Bu
#define USB_HOST_HID_DESCRIPTOR_REPORT      0x22u

#define USB_HOST_HID_REPORT_ID_COUNT        8u
#define USB_HOST_HID_FIELD_COUNT            24u
#define USB_HOST_HID_LOCAL_USAGE_COUNT      16u
#define USB_HOST_HID_GLOBAL_STACK_DEPTH     2u

#define USB_HOST_HID_USAGE_PAGE_GENERIC     0x01u
#define USB_HOST_HID_USAGE_KEYBOARD         0x06u
#define USB_HOST_HID_USAGE_MOUSE            0x02u
#define USB_HOST_HID_USAGE_X                0x30u
#define USB_HOST_HID_USAGE_Y                0x31u
#define USB_HOST_HID_USAGE_WHEEL            0x38u
#define USB_HOST_HID_USAGE_BUTTON_PAGE      0x09u
#define USB_HOST_HID_USAGE_KEY_PAGE         0x07u

#define USB_HOST_HID_KIND_NONE              0u
#define USB_HOST_HID_KIND_KEYBOARD          1u
#define USB_HOST_HID_KIND_MOUSE             2u

#define USB_HOST_HID_ITEM_MAIN              0u
#define USB_HOST_HID_ITEM_GLOBAL            1u
#define USB_HOST_HID_ITEM_LOCAL             2u

#define USB_HOST_HID_MAIN_INPUT             0x08u
#define USB_HOST_HID_MAIN_OUTPUT            0x09u
#define USB_HOST_HID_MAIN_COLLECTION        0x0Au
#define USB_HOST_HID_MAIN_FEATURE           0x0Bu
#define USB_HOST_HID_MAIN_END_COLLECTION    0x0Cu

#define USB_HOST_HID_GLOBAL_USAGE_PAGE      0x00u
#define USB_HOST_HID_GLOBAL_LOGICAL_MIN     0x01u
#define USB_HOST_HID_GLOBAL_LOGICAL_MAX     0x02u
#define USB_HOST_HID_GLOBAL_REPORT_SIZE     0x07u
#define USB_HOST_HID_GLOBAL_REPORT_ID       0x08u
#define USB_HOST_HID_GLOBAL_REPORT_COUNT    0x09u
#define USB_HOST_HID_GLOBAL_PUSH             0x0Au
#define USB_HOST_HID_GLOBAL_POP              0x0Bu

#define USB_HOST_HID_LOCAL_USAGE            0x00u
#define USB_HOST_HID_LOCAL_USAGE_MIN        0x01u
#define USB_HOST_HID_LOCAL_USAGE_MAX        0x02u

#define USB_HOST_HID_FLAG_CONSTANT          0x01u
#define USB_HOST_HID_FLAG_VARIABLE          0x02u
#define USB_HOST_HID_FLAG_RELATIVE          0x04u

typedef struct
{
    uint8_t report_id;
    uint8_t flags;
    uint8_t bit_size;
    uint8_t report_count;
    uint16_t bit_offset;
    uint16_t usage_page;
    uint16_t usage;
    uint16_t usage_min;
    uint16_t usage_max;
    int32_t logical_min;
    int32_t logical_max;
} UsbHostHidField;

typedef struct
{
    uint8_t report_id;
    uint8_t kind;
    uint16_t input_bits;
} UsbHostHidReportInfo;

typedef struct
{
    uint8_t valid;
    uint8_t report_id_present;
    uint8_t kind;
    uint8_t collection_depth;
    uint8_t active_collection_kind;
    uint16_t top_usage_page;
    uint16_t top_usage;
    uint8_t report_info_count;
    uint8_t field_count;
    UsbHostHidReportInfo report_info[USB_HOST_HID_REPORT_ID_COUNT];
    UsbHostHidField fields[USB_HOST_HID_FIELD_COUNT];
} UsbHostHidParser;

typedef struct
{
    uint8_t valid;
    uint8_t interface_number;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t endpoint_address;
    uint8_t endpoint_size;
    uint8_t interval;
    uint8_t data_toggle;
    uint16_t report_descriptor_length;
    uint8_t report_descriptor_valid;
    uint8_t absolute_x_valid;
    uint8_t absolute_y_valid;
    int32_t absolute_x;
    int32_t absolute_y;
    UsbHostHidParser parser;
} UsbHostHidInterface;

typedef struct
{
    uint8_t interface_index;
    uint8_t length;
    uint8_t reserved[2];
    uint8_t bytes[USB_HOST_HID_MAX_REPORT_PACKET];
} UsbHostHidRawReport;

typedef enum
{
    USB_HOST_STATE_WAIT_ATTACH = 0,
    USB_HOST_STATE_RESET,
    USB_HOST_STATE_RESET_SETTLE,
    USB_HOST_STATE_ENUMERATING,
    USB_HOST_STATE_ADDRESS_SETTLE,
    USB_HOST_STATE_READY,
    USB_HOST_STATE_ERROR
} UsbHostHidState;

typedef enum
{
    USB_HOST_TRANSFER_IDLE = 0,
    USB_HOST_TRANSFER_CONTROL,
    USB_HOST_TRANSFER_INTERRUPT
} UsbHostTransferKind;

typedef enum
{
    USB_HOST_TRANSFER_PENDING = 0,
    USB_HOST_TRANSFER_SUCCESS,
    USB_HOST_TRANSFER_NAK,
    USB_HOST_TRANSFER_ERROR,
    USB_HOST_TRANSFER_TIMEOUT,
    USB_HOST_TRANSFER_DISCONNECT
} UsbHostTransferResult;

typedef enum
{
    USB_HOST_CONTROL_SETUP = 0,
    USB_HOST_CONTROL_DATA,
    USB_HOST_CONTROL_STATUS
} UsbHostControlPhase;

typedef enum
{
    USB_HOST_ENUM_GET_DEVICE_8 = 1,
    USB_HOST_ENUM_GET_DEVICE_FULL,
    USB_HOST_ENUM_SET_ADDRESS,
    USB_HOST_ENUM_GET_CONFIG_9,
    USB_HOST_ENUM_GET_CONFIG_FULL,
    USB_HOST_ENUM_SET_CONFIG,
    USB_HOST_ENUM_GET_REPORT,
    USB_HOST_ENUM_SET_PROTOCOL,
    USB_HOST_ENUM_SET_IDLE
} UsbHostEnumStep;

typedef struct
{
    uint8_t active;
    uint8_t kind;
    uint8_t endpoint;
    uint8_t pid;
    uint8_t wait_ticks;
    uint8_t rx_length;
} UsbHostTransfer;

typedef struct
{
    uint8_t running;
    uint8_t operation;
    uint8_t phase;
    uint8_t direction_in;
    uint8_t status_in;
    uint8_t data_toggle;
    uint8_t chunk_length;
    uint8_t reserved;
    uint16_t requested;
    uint16_t transferred;
    uint16_t received;
    uint8_t *destination;
    uint8_t timeout_ticks;
} UsbHostControl;

static uint8_t g_usb_host_rx_dma[USB_HOST_HID_MAX_REPORT_PACKET]
    EVENT_ROUTER_ALIGN4;
static uint8_t g_usb_host_tx_dma[USB_HOST_HID_MAX_REPORT_PACKET]
    EVENT_ROUTER_ALIGN4;
static uint8_t g_usb_host_device_descriptor[18u] EVENT_ROUTER_ALIGN4;
static uint8_t g_usb_host_config_descriptor[USB_HOST_HID_MAX_CONFIG_DESCRIPTOR]
    EVENT_ROUTER_ALIGN4;
static uint8_t g_usb_host_report_descriptor[USB_HOST_HID_MAX_INTERFACES]
                                      [USB_HOST_HID_MAX_REPORT_DESCRIPTOR]
    EVENT_ROUTER_ALIGN4;

static UsbHostHidRawReport g_usb_host_report_storage[USB_HOST_REPORT_QUEUE_CAPACITY]
    EVENT_ROUTER_ALIGN4;
static StaticSpscRing g_usb_host_report_ring;
static UsbHostHidRawReport g_usb_host_report_work EVENT_ROUTER_ALIGN4;
static HidKeyboardReport g_usb_host_keyboard_work EVENT_ROUTER_ALIGN4;
static HidMouseReport g_usb_host_mouse_work EVENT_ROUTER_ALIGN4;

static UsbHostHidInterface g_usb_host_interfaces[USB_HOST_HID_MAX_INTERFACES]
    EVENT_ROUTER_ALIGN4;
static UsbHostTransfer g_usb_host_transfer EVENT_ROUTER_ALIGN4;
static UsbHostControl g_usb_host_control EVENT_ROUTER_ALIGN4;
static UsbHostHidStats g_usb_host_stats EVENT_ROUTER_ALIGN4;

static UsbHostHidState g_usb_host_state;
static uint8_t g_usb_host_attached;
static uint8_t g_usb_host_ready;
static uint8_t g_usb_host_interface_count;
static uint8_t g_usb_host_enum_interface;
static uint8_t g_usb_host_poll_interface;
static uint8_t g_usb_host_poll_due_ticks;
static uint8_t g_usb_host_state_ticks;
static uint8_t g_usb_host_error_ticks;
static uint16_t g_usb_host_config_length;
static uint8_t g_usb_host_keyboard_seen;
static uint8_t g_usb_host_mouse_seen;
static uint8_t g_usb_host_device_address;
static uint8_t g_usb_host_configuration_value;

static void UsbHostHid_Copy(uint8_t *dst, const uint8_t *src, uint16_t length)
{
    uint16_t i;

    for(i = 0u; i < length; ++i)
    {
        dst[i] = src[i];
    }
}

static void UsbHostHid_ClearBytes(uint8_t *dst, uint16_t length)
{
    uint16_t i;

    for(i = 0u; i < length; ++i)
    {
        dst[i] = 0u;
    }
}

static uint16_t UsbHostHid_ReadLe16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint8_t UsbHostHid_IsAttached(void)
{
    return (R8_USB2_MIS_ST & RB_UMS_DEV_ATTACH) ? 1u : 0u;
}

static uint8_t UsbHostHid_IsBootInterface(const UsbHostHidInterface *interface)
{
    return (interface->protocol == USB_HOST_HID_PROTOCOL_KEYBOARD ||
            interface->protocol == USB_HOST_HID_PROTOCOL_MOUSE) ? 1u : 0u;
}

static void UsbHostHid_StopTransfer(void)
{
    R8_U2H_EP_PID = 0u;
    g_usb_host_transfer.active = 0u;
}

static void UsbHostHid_StartTransaction(uint8_t endpoint_pid,
                                        uint8_t rx_control,
                                        uint8_t tx_control,
                                        uint8_t tx_length,
                                        uint8_t kind,
                                        uint8_t endpoint)
{
    R8_U2H_RX_CTRL = rx_control;
    R8_U2H_TX_CTRL = tx_control;
    R8_U2H_TX_LEN = tx_length;
    R8_U2H_EP_PID = endpoint_pid;
    R8_USB2_INT_FG = RB_UIF_TRANSFER;

    g_usb_host_transfer.active = 1u;
    g_usb_host_transfer.kind = kind;
    g_usb_host_transfer.endpoint = endpoint;
    g_usb_host_transfer.pid = (uint8_t)(endpoint_pid >> 4);
    g_usb_host_transfer.wait_ticks = USB_HOST_TRANSFER_TIMEOUT_TICKS;
    g_usb_host_transfer.rx_length = 0u;
}

static UsbHostTransferResult UsbHostHid_PollTransaction(void)
{
    uint8_t flags;
    uint8_t status;
    uint8_t response;

    if(!g_usb_host_transfer.active)
    {
        return USB_HOST_TRANSFER_ERROR;
    }
    if(!UsbHostHid_IsAttached())
    {
        UsbHostHid_StopTransfer();
        return USB_HOST_TRANSFER_DISCONNECT;
    }

    flags = R8_USB2_INT_FG;
    if(flags & RB_UIF_DETECT)
    {
        R8_USB2_INT_FG = RB_UIF_DETECT;
        if(!UsbHostHid_IsAttached())
        {
            UsbHostHid_StopTransfer();
            return USB_HOST_TRANSFER_DISCONNECT;
        }
    }
    if(!(flags & RB_UIF_TRANSFER))
    {
        if(g_usb_host_transfer.wait_ticks != 0u)
        {
            --g_usb_host_transfer.wait_ticks;
            return USB_HOST_TRANSFER_PENDING;
        }
        UsbHostHid_StopTransfer();
        return USB_HOST_TRANSFER_TIMEOUT;
    }

    status = R8_USB2_INT_ST;
    g_usb_host_transfer.rx_length = R8_USB2_RX_LEN;
    UsbHostHid_StopTransfer();
    R8_USB2_INT_FG = RB_UIF_TRANSFER;

    if(status & RB_UIS_TOG_OK)
    {
        return USB_HOST_TRANSFER_SUCCESS;
    }
    response = status & MASK_UIS_H_RES;
    if(response == USB_PID_NAK)
    {
        return USB_HOST_TRANSFER_NAK;
    }
    ++g_usb_host_stats.transfer_error;
    return USB_HOST_TRANSFER_ERROR;
}

static uint8_t UsbHostHid_ReportInfoIndex(const UsbHostHidParser *parser,
                                          uint8_t report_id,
                                          uint8_t *index)
{
    uint16_t i;

    for(i = 0u; i < parser->report_info_count; ++i)
    {
        if(parser->report_info[i].report_id == report_id)
        {
            *index = i;
            return 1u;
        }
    }
    return 0u;
}

static uint8_t UsbHostHid_EnsureReportInfo(UsbHostHidParser *parser,
                                           uint8_t report_id,
                                           uint8_t *index)
{
    if(UsbHostHid_ReportInfoIndex(parser, report_id, index))
    {
        return 1u;
    }
    if(parser->report_info_count >= USB_HOST_HID_REPORT_ID_COUNT)
    {
        return 0u;
    }
    *index = parser->report_info_count++;
    parser->report_info[*index].report_id = report_id;
    parser->report_info[*index].kind = parser->active_collection_kind;
    parser->report_info[*index].input_bits = 0u;
    return 1u;
}

static uint32_t UsbHostHid_ItemUnsigned(const uint8_t *data, uint8_t length)
{
    uint32_t value = 0u;
    uint8_t i;

    for(i = 0u; i < length; ++i)
    {
        value |= ((uint32_t)data[i]) << (8u * i);
    }
    return value;
}

static int32_t UsbHostHid_ItemSigned(const uint8_t *data, uint8_t length)
{
    uint32_t value = UsbHostHid_ItemUnsigned(data, length);

    if(length == 1u && (value & 0x80u))
    {
        value |= 0xFFFFFF00u;
    }
    else if(length == 2u && (value & 0x8000u))
    {
        value |= 0xFFFF0000u;
    }
    return (int32_t)value;
}

typedef struct
{
    uint16_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    uint8_t report_size;
    uint8_t report_id;
    uint8_t report_count;
} UsbHostHidGlobalState;

typedef struct
{
    uint16_t usage_page;
    int32_t logical_min;
    int32_t logical_max;
    uint8_t report_size;
    uint8_t report_id;
    uint8_t report_count;
} UsbHostHidGlobalStackState;

typedef struct
{
    uint16_t values[USB_HOST_HID_LOCAL_USAGE_COUNT];
    uint8_t count;
    uint8_t has_min;
    uint8_t has_max;
    uint16_t usage_min;
    uint16_t usage_max;
} UsbHostHidLocalState;

static void UsbHostHid_LocalReset(UsbHostHidLocalState *local)
{
    local->count = 0u;
    local->has_min = 0u;
    local->has_max = 0u;
    local->usage_min = 0u;
    local->usage_max = 0u;
}

static uint16_t UsbHostHid_LocalUsage(const UsbHostHidLocalState *local,
                                     uint8_t index)
{
    if(index < local->count)
    {
        return local->values[index];
    }
    if(local->has_min && local->has_max)
    {
        return (uint16_t)(local->usage_min + index);
    }
    return 0u;
}

static uint8_t UsbHostHid_AddVariableFields(UsbHostHidParser *parser,
                                            const UsbHostHidGlobalState *global,
                                            const UsbHostHidLocalState *local,
                                            UsbHostHidReportInfo *report,
                                            uint8_t flags)
{
    uint8_t i;
    UsbHostHidField *field;

    if(global->report_count > USB_HOST_HID_FIELD_COUNT)
    {
        return 0u;
    }
    for(i = 0u; i < global->report_count; ++i)
    {
        if(parser->field_count >= USB_HOST_HID_FIELD_COUNT)
        {
            return 0u;
        }
        field = &parser->fields[parser->field_count++];
        field->report_id = global->report_id;
        field->flags = flags;
        field->bit_size = global->report_size;
        field->report_count = 1u;
        field->bit_offset = report->input_bits;
        field->usage_page = global->usage_page;
        field->usage = UsbHostHid_LocalUsage(local, i);
        field->usage_min = local->has_min ? local->usage_min : field->usage;
        field->usage_max = local->has_max ? local->usage_max : field->usage;
        field->logical_min = global->logical_min;
        field->logical_max = global->logical_max;
        report->input_bits = (uint16_t)(report->input_bits + global->report_size);
        if(report->input_bits > (USB_HOST_HID_MAX_REPORT_PACKET * 8u))
        {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t UsbHostHid_AddArrayField(UsbHostHidParser *parser,
                                        const UsbHostHidGlobalState *global,
                                        const UsbHostHidLocalState *local,
                                        UsbHostHidReportInfo *report,
                                        uint8_t flags)
{
    UsbHostHidField *field;

    if(parser->field_count >= USB_HOST_HID_FIELD_COUNT)
    {
        return 0u;
    }
    field = &parser->fields[parser->field_count++];
    field->report_id = global->report_id;
    field->flags = flags;
    field->bit_size = global->report_size;
    field->report_count = global->report_count;
    field->bit_offset = report->input_bits;
    field->usage_page = global->usage_page;
    field->usage = 0u;
    field->usage_min = local->has_min ? local->usage_min : 0u;
    field->usage_max = local->has_max ? local->usage_max : 0xFFFFu;
    field->logical_min = global->logical_min;
    field->logical_max = global->logical_max;
    report->input_bits = (uint16_t)(report->input_bits +
                                    (uint16_t)global->report_size *
                                    global->report_count);
    return (report->input_bits <= (USB_HOST_HID_MAX_REPORT_PACKET * 8u)) ? 1u : 0u;
}

static uint8_t UsbHostHid_ParseReportDescriptor(UsbHostHidParser *parser,
                                                const uint8_t *data,
                                                uint16_t length)
{
    UsbHostHidGlobalState global;
    UsbHostHidGlobalStackState stack[USB_HOST_HID_GLOBAL_STACK_DEPTH];
    UsbHostHidLocalState local;
    uint8_t stack_depth = 0u;
    uint16_t position = 0u;
    uint8_t prefix;
    uint8_t item_size;
    uint8_t item_type;
    uint8_t item_tag;
    uint32_t item_value;
    uint16_t usage;
    uint8_t report_index;
    UsbHostHidReportInfo *report;
    uint8_t flags;
    uint8_t i;

    UsbHostHid_ClearBytes((uint8_t *)parser, sizeof(*parser));
    global.usage_page = 0u;
    global.logical_min = 0;
    global.logical_max = 0;
    global.report_size = 0u;
    global.report_id = 0u;
    global.report_count = 0u;
    UsbHostHid_LocalReset(&local);
    while(position < length)
    {
        prefix = data[position++];
        if(prefix == 0xFEu)
        {
            if((uint16_t)(length - position) < 2u)
            {
                return 0u;
            }
            item_size = data[position];
            position = (uint16_t)(position + 2u);
            if((uint16_t)(length - position) < item_size)
            {
                return 0u;
            }
            position = (uint16_t)(position + item_size);
            UsbHostHid_LocalReset(&local);
            continue;
        }

        item_size = prefix & 0x03u;
        if(item_size == 3u)
        {
            item_size = 4u;
        }
        item_type = (prefix >> 2) & 0x03u;
        item_tag = (prefix >> 4) & 0x0Fu;
        if((uint16_t)(length - position) < item_size)
        {
            return 0u;
        }
        item_value = UsbHostHid_ItemUnsigned(&data[position], item_size);
        position = (uint16_t)(position + item_size);

        if(item_type == USB_HOST_HID_ITEM_GLOBAL)
        {
            switch(item_tag)
            {
                case USB_HOST_HID_GLOBAL_USAGE_PAGE:
                    global.usage_page = (uint16_t)item_value;
                    break;
                case USB_HOST_HID_GLOBAL_LOGICAL_MIN:
                    global.logical_min = UsbHostHid_ItemSigned(&data[position - item_size],
                                                               item_size);
                    break;
                case USB_HOST_HID_GLOBAL_LOGICAL_MAX:
                    global.logical_max = UsbHostHid_ItemSigned(&data[position - item_size],
                                                               item_size);
                    break;
                case USB_HOST_HID_GLOBAL_REPORT_SIZE:
                    if(item_value > 32u)
                    {
                        return 0u;
                    }
                    global.report_size = (uint8_t)item_value;
                    break;
                case USB_HOST_HID_GLOBAL_REPORT_ID:
                    if(item_value == 0u || item_value > 255u ||
                       !UsbHostHid_EnsureReportInfo(parser,
                                                    (uint8_t)item_value,
                                                    &report_index))
                    {
                        return 0u;
                    }
                    global.report_id = (uint8_t)item_value;
                    parser->report_id_present = 1u;
                    break;
                case USB_HOST_HID_GLOBAL_REPORT_COUNT:
                    if(item_value > 255u)
                    {
                        return 0u;
                    }
                    global.report_count = (uint8_t)item_value;
                    break;
                case USB_HOST_HID_GLOBAL_PUSH:
                    if(stack_depth >= USB_HOST_HID_GLOBAL_STACK_DEPTH)
                    {
                        return 0u;
                    }
                    stack[stack_depth].usage_page = global.usage_page;
                    stack[stack_depth].logical_min = global.logical_min;
                    stack[stack_depth].logical_max = global.logical_max;
                    stack[stack_depth].report_size = global.report_size;
                    stack[stack_depth].report_id = global.report_id;
                    stack[stack_depth].report_count = global.report_count;
                    ++stack_depth;
                    break;
                case USB_HOST_HID_GLOBAL_POP:
                    if(stack_depth == 0u)
                    {
                        return 0u;
                    }
                    --stack_depth;
                    global.usage_page = stack[stack_depth].usage_page;
                    global.logical_min = stack[stack_depth].logical_min;
                    global.logical_max = stack[stack_depth].logical_max;
                    global.report_size = stack[stack_depth].report_size;
                    global.report_id = stack[stack_depth].report_id;
                    global.report_count = stack[stack_depth].report_count;
                    break;
                default:
                    break;
            }
            continue;
        }

        if(item_type == USB_HOST_HID_ITEM_LOCAL)
        {
            switch(item_tag)
            {
                case USB_HOST_HID_LOCAL_USAGE:
                    if(local.count < USB_HOST_HID_LOCAL_USAGE_COUNT)
                    {
                        local.values[local.count++] = (uint16_t)item_value;
                    }
                    break;
                case USB_HOST_HID_LOCAL_USAGE_MIN:
                    local.usage_min = (uint16_t)item_value;
                    local.has_min = 1u;
                    break;
                case USB_HOST_HID_LOCAL_USAGE_MAX:
                    local.usage_max = (uint16_t)item_value;
                    local.has_max = 1u;
                    break;
                default:
                    break;
            }
            continue;
        }

        if(item_type != USB_HOST_HID_ITEM_MAIN)
        {
            continue;
        }

        switch(item_tag)
        {
            case USB_HOST_HID_MAIN_COLLECTION:
                usage = UsbHostHid_LocalUsage(&local, 0u);
                if(parser->collection_depth == 0u)
                {
                    parser->top_usage_page = global.usage_page;
                    parser->top_usage = usage;
                    if(global.usage_page == USB_HOST_HID_USAGE_PAGE_GENERIC &&
                       usage == USB_HOST_HID_USAGE_KEYBOARD)
                    {
                        parser->active_collection_kind = USB_HOST_HID_KIND_KEYBOARD;
                        if(parser->kind == USB_HOST_HID_KIND_NONE)
                        {
                            parser->kind = USB_HOST_HID_KIND_KEYBOARD;
                        }
                    }
                    else if(global.usage_page == USB_HOST_HID_USAGE_PAGE_GENERIC &&
                            usage == USB_HOST_HID_USAGE_MOUSE)
                    {
                        parser->active_collection_kind = USB_HOST_HID_KIND_MOUSE;
                        if(parser->kind == USB_HOST_HID_KIND_NONE)
                        {
                            parser->kind = USB_HOST_HID_KIND_MOUSE;
                        }
                    }
                    else
                    {
                        parser->active_collection_kind = USB_HOST_HID_KIND_NONE;
                    }
                }
                if(parser->collection_depth == 255u)
                {
                    return 0u;
                }
                ++parser->collection_depth;
                break;

            case USB_HOST_HID_MAIN_END_COLLECTION:
                if(parser->collection_depth == 0u)
                {
                    return 0u;
                }
                --parser->collection_depth;
                if(parser->collection_depth == 0u)
                {
                    parser->active_collection_kind = USB_HOST_HID_KIND_NONE;
                }
                break;

            case USB_HOST_HID_MAIN_INPUT:
                if(!UsbHostHid_EnsureReportInfo(parser,
                                                global.report_id,
                                                &report_index))
                {
                    return 0u;
                }
                report = &parser->report_info[report_index];
                if(report->kind == USB_HOST_HID_KIND_NONE)
                {
                    report->kind = parser->active_collection_kind;
                }
                flags = (uint8_t)item_value;
                if(global.report_size == 0u || global.report_count == 0u)
                {
                    return 0u;
                }
                if(!(flags & USB_HOST_HID_FLAG_CONSTANT))
                {
                    if(flags & USB_HOST_HID_FLAG_VARIABLE)
                    {
                        if(!UsbHostHid_AddVariableFields(parser,
                                                         &global,
                                                         &local,
                                                         report,
                                                         flags))
                        {
                            return 0u;
                        }
                    }
                    else if(!UsbHostHid_AddArrayField(parser,
                                                      &global,
                                                      &local,
                                                      report,
                                                      flags))
                    {
                        return 0u;
                    }
                }
                else
                {
                    report->input_bits = (uint16_t)(report->input_bits +
                                                    (uint16_t)global.report_size *
                                                    global.report_count);
                    if(report->input_bits > (USB_HOST_HID_MAX_REPORT_PACKET * 8u))
                    {
                        return 0u;
                    }
                }
                break;

            case USB_HOST_HID_MAIN_OUTPUT:
            case USB_HOST_HID_MAIN_FEATURE:
                /* Output/Feature bits do not affect the input report layout. */
                break;

            default:
                break;
        }
        UsbHostHid_LocalReset(&local);
    }

    if(parser->collection_depth != 0u || parser->field_count == 0u)
    {
        return 0u;
    }
    for(i = 0u; i < parser->report_info_count; ++i)
    {
        if(parser->report_info[i].input_bits == 0u ||
           (uint16_t)((parser->report_info[i].input_bits + 7u) / 8u) +
                   (parser->report_id_present ? 1u : 0u) >
               USB_HOST_HID_MAX_REPORT_PACKET)
        {
            return 0u;
        }
    }
    parser->valid = 1u;
    return 1u;
}

static uint32_t UsbHostHid_ReadBits(const uint8_t *data,
                                    uint16_t bit_offset,
                                    uint8_t bit_size)
{
    uint32_t value = 0u;
    uint8_t i;

    if(bit_size > 32u)
    {
        return 0u;
    }
    for(i = 0u; i < bit_size; ++i)
    {
        if(data[(bit_offset + i) >> 3] & (uint8_t)(1u << ((bit_offset + i) & 7u)))
        {
            value |= (uint32_t)1u << i;
        }
    }
    return value;
}

static int32_t UsbHostHid_SignExtend(uint32_t value, uint8_t bit_size)
{
    if(bit_size == 0u)
    {
        return 0;
    }
    if(bit_size < 32u && (value & ((uint32_t)1u << (bit_size - 1u))))
    {
        value |= ~((uint32_t)0xFFFFFFFFu >> (32u - bit_size));
    }
    return (int32_t)value;
}

static uint8_t UsbHostHid_GetReportPayload(const UsbHostHidParser *parser,
                                           const uint8_t *report,
                                           uint8_t length,
                                           const uint8_t **payload,
                                           uint8_t *payload_length,
                                           uint8_t *report_id)
{
    uint8_t index;
    uint16_t expected_length;

    if(parser->report_id_present)
    {
        if(length == 0u)
        {
            return 0u;
        }
        *report_id = report[0];
        *payload = &report[1];
        *payload_length = (uint8_t)(length - 1u);
    }
    else
    {
        *report_id = 0u;
        *payload = report;
        *payload_length = length;
    }
    if(!UsbHostHid_ReportInfoIndex(parser, *report_id, &index))
    {
        return 0u;
    }
    expected_length = (uint16_t)((parser->report_info[index].input_bits + 7u) / 8u);
    return (*payload_length >= expected_length) ? 1u : 0u;
}

static uint8_t UsbHostHid_GetReportKind(const UsbHostHidParser *parser,
                                        uint8_t report_id)
{
    uint8_t index;

    if(!UsbHostHid_ReportInfoIndex(parser, report_id, &index))
    {
        return USB_HOST_HID_KIND_NONE;
    }
    return parser->report_info[index].kind;
}

static uint8_t UsbHostHid_KeyboardAdd(HidKeyboardReport *report,
                                      uint8_t usage,
                                      uint8_t *count)
{
    uint8_t i;

    if(usage == 0u || usage == 1u)
    {
        return 1u;
    }
    for(i = 0u; i < *count; ++i)
    {
        if(report->bytes[2u + i] == usage)
        {
            return 1u;
        }
    }
    if(*count >= 6u)
    {
        return 0u;
    }
    report->bytes[2u + *count] = usage;
    ++(*count);
    return 1u;
}

static uint8_t UsbHostHid_ParseKeyboard(const UsbHostHidInterface *interface,
                                        const uint8_t *data,
                                        uint8_t length,
                                        HidKeyboardReport *report)
{
    const uint8_t *payload;
    uint8_t payload_length;
    uint8_t report_id;
    uint8_t i;
    uint8_t j;
    uint8_t count = 0u;
    uint8_t value;
    uint32_t raw;
    const UsbHostHidField *field;

    UsbHostHid_ClearBytes(report->bytes, HID_KEYBOARD_REPORT_LEN);
    if(interface->protocol == USB_HOST_HID_PROTOCOL_KEYBOARD)
    {
        i = 0u;
        if(length < HID_KEYBOARD_REPORT_LEN)
        {
            return 0u;
        }
        for(j = 0u; j < HID_KEYBOARD_REPORT_LEN; ++j)
        {
            report->bytes[j] = data[i + j];
        }
        return 1u;
    }
    if(!interface->parser.valid ||
       !UsbHostHid_GetReportPayload(&interface->parser,
                                    data,
                                    length,
                                    &payload,
                                    &payload_length,
                                    &report_id))
    {
        return 0u;
    }
    (void)payload_length;
    for(i = 0u; i < interface->parser.field_count; ++i)
    {
        field = &interface->parser.fields[i];
        if(field->report_id != report_id || field->usage_page != USB_HOST_HID_USAGE_KEY_PAGE)
        {
            continue;
        }
        if(field->flags & USB_HOST_HID_FLAG_VARIABLE)
        {
            raw = UsbHostHid_ReadBits(payload,
                                      field->bit_offset,
                                      field->bit_size);
            value = (uint8_t)raw;
            if(field->usage >= 0xE0u && field->usage <= 0xE7u && value != 0u)
            {
                report->bytes[0] |= (uint8_t)(1u << (field->usage - 0xE0u));
            }
        }
        else
        {
            for(j = 0u; j < field->report_count; ++j)
            {
                raw = UsbHostHid_ReadBits(payload,
                                          (uint16_t)(field->bit_offset +
                                                     j * field->bit_size),
                                          field->bit_size);
                value = (uint8_t)raw;
                if(value >= field->usage_min && value <= field->usage_max &&
                   !UsbHostHid_KeyboardAdd(report, value, &count))
                {
                    report->bytes[2] = 0x01u;
                    report->bytes[3] = 0x01u;
                    report->bytes[4] = 0x01u;
                    report->bytes[5] = 0x01u;
                    report->bytes[6] = 0x01u;
                    report->bytes[7] = 0x01u;
                    return 1u;
                }
            }
        }
    }
    return 1u;
}

static int8_t UsbHostHid_SaturateInt8(int32_t value)
{
    if(value > 127)
    {
        return 127;
    }
    if(value < -128)
    {
        return -128;
    }
    return (int8_t)value;
}

static int32_t UsbHostHid_AbsoluteDelta(int32_t current,
                                        int32_t *previous,
                                        uint8_t *valid)
{
    int64_t delta;

    if(!*valid)
    {
        *previous = current;
        *valid = 1u;
        return 0;
    }
    delta = (int64_t)current - (int64_t)*previous;
    *previous = current;
    if(delta > 127)
    {
        return 127;
    }
    if(delta < -128)
    {
        return -128;
    }
    return (int32_t)delta;
}

static uint8_t UsbHostHid_ParseMouse(UsbHostHidInterface *interface,
                                     const uint8_t *data,
                                     uint8_t length,
                                     HidMouseReport *report)
{
    const uint8_t *payload;
    uint8_t payload_length;
    uint8_t report_id;
    uint8_t i;
    uint8_t j;
    uint32_t raw;
    int32_t value;
    int32_t absolute_value;
    const UsbHostHidField *field;

    report->bytes[0] = 0u;
    report->bytes[1] = 0u;
    report->bytes[2] = 0u;
    report->bytes[3] = 0u;
    if(interface->protocol == USB_HOST_HID_PROTOCOL_MOUSE)
    {
        i = 0u;
        if(length < 3u)
        {
            return 0u;
        }
        report->bytes[0] = data[i] & 0x07u;
        report->bytes[1] = data[i + 1u];
        report->bytes[2] = data[i + 2u];
        if(length > (uint8_t)(i + 3u))
        {
            report->bytes[3] = data[i + 3u];
        }
        return 1u;
    }
    if(!interface->parser.valid ||
       !UsbHostHid_GetReportPayload(&interface->parser,
                                    data,
                                    length,
                                    &payload,
                                    &payload_length,
                                    &report_id))
    {
        return 0u;
    }
    (void)payload_length;
    for(i = 0u; i < interface->parser.field_count; ++i)
    {
        field = &interface->parser.fields[i];
        if(field->report_id != report_id)
        {
            continue;
        }
        if(!(field->flags & USB_HOST_HID_FLAG_VARIABLE))
        {
            continue;
        }
        for(j = 0u; j < field->report_count; ++j)
        {
            raw = UsbHostHid_ReadBits(payload,
                                      (uint16_t)(field->bit_offset +
                                                 j * field->bit_size),
                                      field->bit_size);
            value = ((field->flags & USB_HOST_HID_FLAG_RELATIVE) ||
                     field->logical_min < 0) ?
                        UsbHostHid_SignExtend(raw, field->bit_size) :
                        (int32_t)raw;
            if(field->usage_page == USB_HOST_HID_USAGE_BUTTON_PAGE &&
               field->usage + j >= 1u && field->usage + j <= 8u && value != 0)
            {
                report->bytes[0] |= (uint8_t)(1u << (field->usage + j - 1u));
            }
            else if(field->usage_page == USB_HOST_HID_USAGE_PAGE_GENERIC &&
                    field->usage + j == USB_HOST_HID_USAGE_X)
            {
                if(!(field->flags & USB_HOST_HID_FLAG_RELATIVE))
                {
                    absolute_value = value;
                    value = UsbHostHid_AbsoluteDelta(absolute_value,
                                                     &interface->absolute_x,
                                                     &interface->absolute_x_valid);
                }
                report->bytes[1] = (uint8_t)UsbHostHid_SaturateInt8(value);
            }
            else if(field->usage_page == USB_HOST_HID_USAGE_PAGE_GENERIC &&
                    field->usage + j == USB_HOST_HID_USAGE_Y)
            {
                if(!(field->flags & USB_HOST_HID_FLAG_RELATIVE))
                {
                    absolute_value = value;
                    value = UsbHostHid_AbsoluteDelta(absolute_value,
                                                     &interface->absolute_y,
                                                     &interface->absolute_y_valid);
                }
                report->bytes[2] = (uint8_t)UsbHostHid_SaturateInt8(value);
            }
            else if(field->usage_page == USB_HOST_HID_USAGE_PAGE_GENERIC &&
                    field->usage + j == USB_HOST_HID_USAGE_WHEEL)
            {
                report->bytes[3] = (uint8_t)UsbHostHid_SaturateInt8(value);
            }
        }
    }
    return 1u;
}

static void UsbHostHid_ResetInterfaceTable(void)
{
    UsbHostHid_ClearBytes((uint8_t *)g_usb_host_interfaces,
                          sizeof(g_usb_host_interfaces));
    g_usb_host_interface_count = 0u;
}

static uint8_t UsbHostHid_ParseConfiguration(void)
{
    uint16_t position = 0u;
    uint16_t total_length;
    uint8_t descriptor_length;
    uint8_t descriptor_type;
    int8_t active_interface = -1;
    uint16_t i;
    uint16_t packet_size;
    UsbHostHidInterface *interface;

    if(g_usb_host_config_length < 9u ||
       g_usb_host_config_descriptor[0] < 9u ||
       g_usb_host_config_descriptor[1] != USB_DESCR_TYP_CONFIG)
    {
        return 0u;
    }
    total_length = UsbHostHid_ReadLe16(&g_usb_host_config_descriptor[2]);
    if(total_length < 9u || total_length > USB_HOST_HID_MAX_CONFIG_DESCRIPTOR ||
       total_length > g_usb_host_config_length)
    {
        return 0u;
    }
    UsbHostHid_ResetInterfaceTable();
    while(position < total_length)
    {
        if((uint16_t)(total_length - position) < 2u)
        {
            return 0u;
        }
        descriptor_length = g_usb_host_config_descriptor[position];
        descriptor_type = g_usb_host_config_descriptor[position + 1u];
        if(descriptor_length < 2u ||
           (uint16_t)(position + descriptor_length) > total_length)
        {
            return 0u;
        }

        if(descriptor_type == USB_DESCR_TYP_INTERF && descriptor_length >= 9u)
        {
            active_interface = -1;
            if(g_usb_host_config_descriptor[position + 5u] == USB_HOST_HID_CLASS &&
               g_usb_host_config_descriptor[position + 3u] == 0u &&
               (g_usb_host_config_descriptor[position + 7u] == 0u ||
                g_usb_host_config_descriptor[position + 7u] ==
                    USB_HOST_HID_PROTOCOL_KEYBOARD ||
                g_usb_host_config_descriptor[position + 7u] ==
                    USB_HOST_HID_PROTOCOL_MOUSE))
            {
                if(g_usb_host_interface_count >= USB_HOST_HID_MAX_INTERFACES)
                {
                    ++g_usb_host_stats.report_descriptor_error;
                }
                else
                {
                    interface = &g_usb_host_interfaces[g_usb_host_interface_count];
                    interface->valid = 1u;
                    interface->interface_number = g_usb_host_config_descriptor[position + 2u];
                    interface->subclass = g_usb_host_config_descriptor[position + 6u];
                    interface->protocol = g_usb_host_config_descriptor[position + 7u];
                    interface->data_toggle = 0u;
                    active_interface = (int8_t)g_usb_host_interface_count;
                    ++g_usb_host_interface_count;
                }
            }
        }
        else if(descriptor_type == USB_DESCR_TYP_HID &&
                active_interface >= 0 && descriptor_length >= 9u)
        {
            interface = &g_usb_host_interfaces[(uint8_t)active_interface];
            /* bNumDescriptors is byte 5; each subordinate descriptor entry
             * starts at byte 6 and contains type + little-endian length. */
            for(i = 0u; i + 2u < (uint16_t)(descriptor_length - 6u); i += 3u)
            {
                if(g_usb_host_config_descriptor[position + 6u + i] ==
                       USB_HOST_HID_DESCRIPTOR_REPORT)
                {
                    interface->report_descriptor_length =
                        (uint16_t)g_usb_host_config_descriptor[position + 7u + i] |
                        ((uint16_t)g_usb_host_config_descriptor[position + 8u + i] << 8);
                    break;
                }
            }
        }
        else if(descriptor_type == USB_DESCR_TYP_ENDP &&
                active_interface >= 0 && descriptor_length >= 7u)
        {
            interface = &g_usb_host_interfaces[(uint8_t)active_interface];
            packet_size = UsbHostHid_ReadLe16(&g_usb_host_config_descriptor[position + 4u]);
            packet_size &= 0x07FFu;
            if((g_usb_host_config_descriptor[position + 3u] & USB_ENDP_TYPE_MASK) ==
                   USB_ENDP_TYPE_INTER &&
               (g_usb_host_config_descriptor[position + 2u] & USB_ENDP_DIR_MASK) &&
               packet_size != 0u && packet_size <= USB_HOST_HID_MAX_REPORT_PACKET &&
               interface->endpoint_address == 0u)
            {
                interface->endpoint_address = g_usb_host_config_descriptor[position + 2u];
                interface->endpoint_size = (uint8_t)packet_size;
                interface->interval = g_usb_host_config_descriptor[position + 6u];
                if(interface->interval == 0u)
                {
                    interface->interval = 1u;
                }
            }
        }
        position = (uint16_t)(position + descriptor_length);
    }

    if(g_usb_host_interface_count == 0u)
    {
        return 0u;
    }
    for(i = 0u; i < g_usb_host_interface_count; ++i)
    {
        interface = &g_usb_host_interfaces[i];
        if(!interface->valid || interface->endpoint_address == 0u ||
           (interface->report_descriptor_length == 0u &&
            !UsbHostHid_IsBootInterface(interface)))
        {
            return 0u;
        }
    }
    return 1u;
}

static void UsbHostHid_SetError(void)
{
    UsbHostHid_StopTransfer();
    g_usb_host_control.running = 0u;
    g_usb_host_ready = 0u;
    g_usb_host_state = USB_HOST_STATE_ERROR;
    g_usb_host_error_ticks = USB_HOST_ERROR_RETRY_TICKS;
    ++g_usb_host_stats.enumeration_error;
}

static void UsbHostHid_InjectReleaseAll(void)
{
    HidKeyboardReport keyboard;
    HidMouseReport mouse;

    if(g_usb_host_keyboard_seen)
    {
        UsbHostHid_ClearBytes(keyboard.bytes, HID_KEYBOARD_REPORT_LEN);
        (void)EventRouter_InjectKeyboardReport(ROUTER_SRC_USB_KEYBOARD, &keyboard);
    }
    if(g_usb_host_mouse_seen)
    {
        UsbHostHid_ClearBytes(mouse.bytes, HID_MOUSE_REPORT_LEN);
        (void)EventRouter_InjectMouseReport(ROUTER_SRC_USB_MOUSE, &mouse);
    }
    g_usb_host_keyboard_seen = 0u;
    g_usb_host_mouse_seen = 0u;
}

static void UsbHostHid_HandleDetach(void)
{
    UsbHostHid_InjectReleaseAll();
    UsbHostHid_StopTransfer();
    R8_U2HOST_CTRL &= (uint8_t)~(RB_UH_PORT_EN | RB_UH_LOW_SPEED);
    R8_USB2_INT_FG = 0xFFu;
    g_usb_host_control.running = 0u;
    g_usb_host_ready = 0u;
    g_usb_host_attached = 0u;
    g_usb_host_state = USB_HOST_STATE_WAIT_ATTACH;
    g_usb_host_interface_count = 0u;
    g_usb_host_device_address = 0u;
    ++g_usb_host_stats.detach_count;
}

static void UsbHostHid_StartControl(uint8_t operation,
                                    uint8_t request_type,
                                    uint8_t request,
                                    uint16_t value,
                                    uint16_t index,
                                    uint8_t *destination,
                                    uint16_t length)
{
    g_usb_host_tx_dma[0] = request_type;
    g_usb_host_tx_dma[1] = request;
    g_usb_host_tx_dma[2] = (uint8_t)value;
    g_usb_host_tx_dma[3] = (uint8_t)(value >> 8);
    g_usb_host_tx_dma[4] = (uint8_t)index;
    g_usb_host_tx_dma[5] = (uint8_t)(index >> 8);
    g_usb_host_tx_dma[6] = (uint8_t)length;
    g_usb_host_tx_dma[7] = (uint8_t)(length >> 8);

    g_usb_host_control.running = 1u;
    g_usb_host_control.operation = operation;
    g_usb_host_control.phase = USB_HOST_CONTROL_SETUP;
    g_usb_host_control.direction_in = (request_type & USB_HOST_HID_INPUT) ? 1u : 0u;
    g_usb_host_control.status_in = g_usb_host_control.direction_in ? 0u : 1u;
    g_usb_host_control.data_toggle = 1u;
    g_usb_host_control.chunk_length = 0u;
    g_usb_host_control.requested = length;
    g_usb_host_control.transferred = 0u;
    g_usb_host_control.received = 0u;
    g_usb_host_control.destination = destination;
    g_usb_host_control.timeout_ticks = USB_HOST_CONTROL_TIMEOUT_TICKS;
}

static void UsbHostHid_StartControlPhase(void)
{
    uint8_t toggle;
    uint8_t chunk;
    uint16_t remaining;

    if(g_usb_host_control.phase == USB_HOST_CONTROL_SETUP)
    {
        UsbHostHid_StartTransaction((uint8_t)(USB_PID_SETUP << 4),
                                    0u,
                                    0u,
                                    sizeof(USB_SETUP_REQ),
                                    USB_HOST_TRANSFER_CONTROL,
                                    0u);
        return;
    }

    if(g_usb_host_control.phase == USB_HOST_CONTROL_DATA)
    {
        toggle = g_usb_host_control.data_toggle ? RB_UH_R_TOG : 0u;
        if(g_usb_host_control.direction_in)
        {
            UsbHostHid_StartTransaction((uint8_t)(USB_PID_IN << 4),
                                        toggle,
                                        0u,
                                        0u,
                                        USB_HOST_TRANSFER_CONTROL,
                                        0u);
        }
        else
        {
            remaining = (uint16_t)(g_usb_host_control.requested -
                                   g_usb_host_control.transferred);
            if(remaining > Usb2DevEndp0Size)
            {
                chunk = Usb2DevEndp0Size;
            }
            else
            {
                chunk = (uint8_t)remaining;
            }
            UsbHostHid_Copy(g_usb_host_tx_dma,
                            &g_usb_host_control.destination[g_usb_host_control.transferred],
                            chunk);
            g_usb_host_control.chunk_length = chunk;
            UsbHostHid_StartTransaction((uint8_t)(USB_PID_OUT << 4),
                                        0u,
                                        g_usb_host_control.data_toggle ? RB_UH_T_TOG : 0u,
                                        chunk,
                                        USB_HOST_TRANSFER_CONTROL,
                                        0u);
        }
        return;
    }

    /* USB control status is always DATA1 and travels in the opposite
     * direction to the request data phase. */
    if(g_usb_host_control.status_in)
    {
        UsbHostHid_StartTransaction((uint8_t)(USB_PID_IN << 4),
                                    RB_UH_R_TOG,
                                    0u,
                                    1u,
                                    USB_HOST_TRANSFER_CONTROL,
                                    0u);
    }
    else
    {
        UsbHostHid_StartTransaction((uint8_t)(USB_PID_OUT << 4),
                                    0u,
                                    RB_UH_T_TOG,
                                    0u,
                                    USB_HOST_TRANSFER_CONTROL,
                                    0u);
    }
}

static void UsbHostHid_StartGetDevice8(void)
{
    g_usb_host_state = USB_HOST_STATE_ENUMERATING;
    g_usb_host_device_address = 0u;
    SetHostUsb2Addr(0u);
    g_usb_host_control.operation = USB_HOST_ENUM_GET_DEVICE_8;
    UsbHostHid_StartControl(USB_HOST_ENUM_GET_DEVICE_8,
                            USB_REQ_TYP_IN,
                            USB_GET_DESCRIPTOR,
                            (uint16_t)(USB_DESCR_TYP_DEVICE << 8),
                            0u,
                            g_usb_host_device_descriptor,
                            8u);
}

static void UsbHostHid_StartReportForCurrentInterface(void);
static void UsbHostHid_AdvanceInterface(void);

static void UsbHostHid_StartOptionalBootRequests(void)
{
    UsbHostHidInterface *interface =
        &g_usb_host_interfaces[g_usb_host_enum_interface];

    if(UsbHostHid_IsBootInterface(interface))
    {
        g_usb_host_control.operation = USB_HOST_ENUM_SET_PROTOCOL;
        UsbHostHid_StartControl(USB_HOST_ENUM_SET_PROTOCOL,
                                USB_REQ_TYP_OUT | USB_REQ_RECIP_INTERF,
                                USB_HOST_HID_SET_PROTOCOL,
                                0u,
                                interface->interface_number,
                                NULL,
                                0u);
    }
    else
    {
        UsbHostHid_AdvanceInterface();
    }
}

static void UsbHostHid_AdvanceInterface(void)
{
    ++g_usb_host_enum_interface;
    UsbHostHid_StartReportForCurrentInterface();
}

static void UsbHostHid_StartReportForCurrentInterface(void)
{
    UsbHostHidInterface *interface;

    while(g_usb_host_enum_interface < g_usb_host_interface_count)
    {
        interface = &g_usb_host_interfaces[g_usb_host_enum_interface];
        if(interface->report_descriptor_length != 0u &&
           interface->report_descriptor_length <= USB_HOST_HID_MAX_REPORT_DESCRIPTOR)
        {
            g_usb_host_control.operation = USB_HOST_ENUM_GET_REPORT;
            UsbHostHid_StartControl(USB_HOST_ENUM_GET_REPORT,
                                    USB_REQ_TYP_IN | USB_REQ_RECIP_INTERF,
                                    USB_GET_DESCRIPTOR,
                                    (uint16_t)(USB_HOST_HID_DESCRIPTOR_REPORT << 8),
                                    interface->interface_number,
                                    g_usb_host_report_descriptor[g_usb_host_enum_interface],
                                    interface->report_descriptor_length);
            return;
        }
        if(interface->protocol == USB_HOST_HID_PROTOCOL_KEYBOARD ||
           interface->protocol == USB_HOST_HID_PROTOCOL_MOUSE)
        {
            ++g_usb_host_stats.report_descriptor_error;
            UsbHostHid_StartOptionalBootRequests();
            return;
        }
        ++g_usb_host_stats.report_descriptor_error;
        UsbHostHid_SetError();
        return;
    }

    g_usb_host_state = USB_HOST_STATE_READY;
    g_usb_host_ready = 1u;
    g_usb_host_poll_interface = 0u;
    g_usb_host_poll_due_ticks = 0u;
    ++g_usb_host_stats.enumeration_success;
}

static void UsbHostHid_ControlSucceeded(void)
{
    uint8_t operation = g_usb_host_control.operation;
    UsbHostHidInterface *interface;
    uint8_t max_packet;
    uint16_t copy_length;
    uint16_t config_total;

    if(g_usb_host_control.phase == USB_HOST_CONTROL_SETUP)
    {
        if(g_usb_host_control.requested != 0u)
        {
            g_usb_host_control.phase = USB_HOST_CONTROL_DATA;
        }
        else
        {
            g_usb_host_control.phase = USB_HOST_CONTROL_STATUS;
        }
        g_usb_host_control.data_toggle = 1u;
        return;
    }
    if(g_usb_host_control.phase == USB_HOST_CONTROL_DATA)
    {
        if(g_usb_host_control.direction_in)
        {
            if(g_usb_host_transfer.rx_length != 0u &&
               g_usb_host_control.destination != NULL)
            {
                copy_length = g_usb_host_transfer.rx_length;
                if(copy_length > (uint16_t)(g_usb_host_control.requested -
                                            g_usb_host_control.received))
                {
                    copy_length = (uint16_t)(g_usb_host_control.requested -
                                             g_usb_host_control.received);
                }
                UsbHostHid_Copy(&g_usb_host_control.destination[g_usb_host_control.received],
                                g_usb_host_rx_dma,
                                copy_length);
                g_usb_host_control.received = (uint16_t)(g_usb_host_control.received +
                                                         copy_length);
            }
            g_usb_host_control.data_toggle ^= 1u;
            if(g_usb_host_control.received >= g_usb_host_control.requested ||
               g_usb_host_transfer.rx_length < Usb2DevEndp0Size)
            {
                g_usb_host_control.phase = USB_HOST_CONTROL_STATUS;
            }
        }
        else
        {
            g_usb_host_control.transferred = (uint16_t)(g_usb_host_control.transferred +
                                                        g_usb_host_control.chunk_length);
            g_usb_host_control.data_toggle ^= 1u;
            if(g_usb_host_control.transferred >= g_usb_host_control.requested)
            {
                g_usb_host_control.phase = USB_HOST_CONTROL_STATUS;
            }
        }
        return;
    }

    g_usb_host_control.running = 0u;
    switch(operation)
    {
        case USB_HOST_ENUM_GET_DEVICE_8:
            if(g_usb_host_control.received < 8u)
            {
                UsbHostHid_SetError();
                return;
            }
            max_packet = g_usb_host_device_descriptor[7];
            if(max_packet != 8u && max_packet != 16u && max_packet != 32u &&
               max_packet != 64u)
            {
                UsbHostHid_SetError();
                return;
            }
            Usb2DevEndp0Size = max_packet;
            g_usb_host_control.operation = USB_HOST_ENUM_GET_DEVICE_FULL;
            UsbHostHid_StartControl(USB_HOST_ENUM_GET_DEVICE_FULL,
                                    USB_REQ_TYP_IN,
                                    USB_GET_DESCRIPTOR,
                                    (uint16_t)(USB_DESCR_TYP_DEVICE << 8),
                                    0u,
                                    g_usb_host_device_descriptor,
                                    sizeof(g_usb_host_device_descriptor));
            break;

        case USB_HOST_ENUM_GET_DEVICE_FULL:
            if(g_usb_host_control.received < sizeof(g_usb_host_device_descriptor))
            {
                UsbHostHid_SetError();
                return;
            }
            g_usb_host_device_address = 1u;
            g_usb_host_control.operation = USB_HOST_ENUM_SET_ADDRESS;
            UsbHostHid_StartControl(USB_HOST_ENUM_SET_ADDRESS,
                                    USB_REQ_TYP_OUT,
                                    USB_HOST_HID_SET_ADDRESS,
                                    g_usb_host_device_address,
                                    0u,
                                    NULL,
                                    0u);
            break;

        case USB_HOST_ENUM_SET_ADDRESS:
            SetHostUsb2Addr(g_usb_host_device_address);
            g_usb_host_state = USB_HOST_STATE_ADDRESS_SETTLE;
            g_usb_host_state_ticks = USB_HOST_ADDRESS_SETTLE_TICKS;
            break;

        case USB_HOST_ENUM_GET_CONFIG_9:
            if(g_usb_host_control.received < 9u)
            {
                UsbHostHid_SetError();
                return;
            }
            g_usb_host_config_length = 9u;
            config_total = UsbHostHid_ReadLe16(&g_usb_host_config_descriptor[2]);
            if(config_total < 9u || config_total > USB_HOST_HID_MAX_CONFIG_DESCRIPTOR)
            {
                UsbHostHid_SetError();
                return;
            }
            g_usb_host_control.operation = USB_HOST_ENUM_GET_CONFIG_FULL;
            UsbHostHid_StartControl(USB_HOST_ENUM_GET_CONFIG_FULL,
                                    USB_REQ_TYP_IN,
                                    USB_GET_DESCRIPTOR,
                                    (uint16_t)(USB_DESCR_TYP_CONFIG << 8),
                                    0u,
                                    g_usb_host_config_descriptor,
                                    config_total);
            break;

        case USB_HOST_ENUM_GET_CONFIG_FULL:
            g_usb_host_config_length = g_usb_host_control.received;
            if(!UsbHostHid_ParseConfiguration())
            {
                UsbHostHid_SetError();
                return;
            }
            g_usb_host_configuration_value = g_usb_host_config_descriptor[5];
            g_usb_host_control.operation = USB_HOST_ENUM_SET_CONFIG;
            UsbHostHid_StartControl(USB_HOST_ENUM_SET_CONFIG,
                                    USB_REQ_TYP_OUT,
                                    USB_HOST_HID_SET_CONFIGURATION,
                                    g_usb_host_configuration_value,
                                    0u,
                                    NULL,
                                    0u);
            break;

        case USB_HOST_ENUM_SET_CONFIG:
            g_usb_host_enum_interface = 0u;
            UsbHostHid_StartReportForCurrentInterface();
            break;

        case USB_HOST_ENUM_GET_REPORT:
            interface = &g_usb_host_interfaces[g_usb_host_enum_interface];
            if(g_usb_host_control.received == 0u ||
               !UsbHostHid_ParseReportDescriptor(&interface->parser,
                                                 g_usb_host_report_descriptor[g_usb_host_enum_interface],
                                                 g_usb_host_control.received))
            {
                interface->report_descriptor_valid = 0u;
                ++g_usb_host_stats.report_parse_error;
                if(!UsbHostHid_IsBootInterface(interface))
                {
                    UsbHostHid_SetError();
                    return;
                }
            }
            else
            {
                interface->report_descriptor_valid = 1u;
            }
            UsbHostHid_StartOptionalBootRequests();
            break;

        case USB_HOST_ENUM_SET_PROTOCOL:
            g_usb_host_control.operation = USB_HOST_ENUM_SET_IDLE;
            interface = &g_usb_host_interfaces[g_usb_host_enum_interface];
            UsbHostHid_StartControl(USB_HOST_ENUM_SET_IDLE,
                                    USB_REQ_TYP_OUT | USB_REQ_RECIP_INTERF,
                                    USB_HOST_HID_SET_IDLE,
                                    0u,
                                    interface->interface_number,
                                    NULL,
                                    0u);
            break;

        case USB_HOST_ENUM_SET_IDLE:
            UsbHostHid_AdvanceInterface();
            break;

        default:
            UsbHostHid_SetError();
            break;
    }
}

static void UsbHostHid_ControlFailed(UsbHostTransferResult result)
{
    uint8_t operation = g_usb_host_control.operation;
    UsbHostHidInterface *interface;

    g_usb_host_control.running = 0u;
    if(result == USB_HOST_TRANSFER_TIMEOUT)
    {
        ++g_usb_host_stats.transfer_timeout;
    }
    if(operation == USB_HOST_ENUM_GET_REPORT)
    {
        interface = &g_usb_host_interfaces[g_usb_host_enum_interface];
        ++g_usb_host_stats.report_descriptor_error;
        if(UsbHostHid_IsBootInterface(interface))
        {
            interface->report_descriptor_valid = 0u;
            UsbHostHid_StartOptionalBootRequests();
            return;
        }
    }
    if(operation == USB_HOST_ENUM_SET_PROTOCOL)
    {
        /* Boot requests are optional for devices that already default to
         * boot protocol or reject SET_PROTOCOL.  Continue with SET_IDLE. */
        interface = &g_usb_host_interfaces[g_usb_host_enum_interface];
        UsbHostHid_StartControl(USB_HOST_ENUM_SET_IDLE,
                                USB_REQ_TYP_OUT | USB_REQ_RECIP_INTERF,
                                USB_HOST_HID_SET_IDLE,
                                0u,
                                interface->interface_number,
                                NULL,
                                0u);
        return;
    }
    if(operation == USB_HOST_ENUM_SET_IDLE)
    {
        UsbHostHid_AdvanceInterface();
        return;
    }
    UsbHostHid_SetError();
}

static void UsbHostHid_ControlProcess(void)
{
    UsbHostTransferResult result;

    if(!g_usb_host_control.running)
    {
        return;
    }
    if(g_usb_host_control.timeout_ticks == 0u)
    {
        UsbHostHid_ControlFailed(USB_HOST_TRANSFER_TIMEOUT);
        return;
    }
    --g_usb_host_control.timeout_ticks;

    if(g_usb_host_transfer.active)
    {
        result = UsbHostHid_PollTransaction();
        if(result == USB_HOST_TRANSFER_PENDING || result == USB_HOST_TRANSFER_NAK)
        {
            return;
        }
        if(result == USB_HOST_TRANSFER_SUCCESS)
        {
            UsbHostHid_ControlSucceeded();
        }
        else
        {
            UsbHostHid_ControlFailed(result);
        }
        return;
    }
    UsbHostHid_StartControlPhase();
}

static void UsbHostHid_StartReset(void)
{
    UsbHostHid_StopTransfer();
    g_usb_host_control.running = 0u;
    g_usb_host_ready = 0u;
    g_usb_host_keyboard_seen = 0u;
    g_usb_host_mouse_seen = 0u;
    g_usb_host_interface_count = 0u;
    g_usb_host_enum_interface = 0u;
    g_usb_host_poll_interface = 0u;
    StaticSpscRing_Clear(&g_usb_host_report_ring);
    SetHostUsb2Addr(0u);
    Usb2DevEndp0Size = DEFAULT_ENDP0_SIZE;
    R8_U2HOST_CTRL &= (uint8_t)~(RB_UH_PORT_EN | RB_UH_LOW_SPEED);
    /* WCH's root-port sequence applies bus reset at full speed, then
     * selects low/full speed from the sampled attach level before enabling
     * the port. */
    SetUsb2Speed(1u);
    R8_U2HOST_CTRL |= RB_UH_BUS_RESET;
    g_usb_host_state = USB_HOST_STATE_RESET;
    g_usb_host_state_ticks = USB_HOST_RESET_TICKS;
}

static void UsbHostHid_ProcessReset(void)
{
    if(g_usb_host_state_ticks != 0u)
    {
        --g_usb_host_state_ticks;
        return;
    }
    R8_U2HOST_CTRL &= (uint8_t)~RB_UH_BUS_RESET;
    g_usb_host_state = USB_HOST_STATE_RESET_SETTLE;
    g_usb_host_state_ticks = USB_HOST_RESET_SETTLE_TICKS;
}

static void UsbHostHid_ProcessResetSettle(void)
{
    if(g_usb_host_state_ticks != 0u)
    {
        --g_usb_host_state_ticks;
        return;
    }
    if(R8_USB2_MIS_ST & RB_UMS_DM_LEVEL)
    {
        SetUsb2Speed(0u);
        R8_U2HOST_CTRL |= RB_UH_LOW_SPEED;
    }
    else
    {
        SetUsb2Speed(1u);
        R8_U2HOST_CTRL &= (uint8_t)~RB_UH_LOW_SPEED;
    }
    R8_U2HOST_CTRL |= RB_UH_PORT_EN;
    R8_USB2_INT_FG = 0xFFu;
    UsbHostHid_StartGetDevice8();
}

static void UsbHostHid_ProcessAddressSettle(void)
{
    if(g_usb_host_state_ticks != 0u)
    {
        --g_usb_host_state_ticks;
        return;
    }
    g_usb_host_control.operation = USB_HOST_ENUM_GET_CONFIG_9;
    UsbHostHid_StartControl(USB_HOST_ENUM_GET_CONFIG_9,
                            USB_REQ_TYP_IN,
                            USB_GET_DESCRIPTOR,
                            (uint16_t)(USB_DESCR_TYP_CONFIG << 8),
                            0u,
                            g_usb_host_config_descriptor,
                            9u);
}

static void UsbHostHid_StartInterruptPoll(void)
{
    UsbHostHidInterface *interface;
    uint8_t toggle;

    if(g_usb_host_interface_count == 0u ||
       g_usb_host_poll_interface >= g_usb_host_interface_count)
    {
        g_usb_host_poll_interface = 0u;
    }
    interface = &g_usb_host_interfaces[g_usb_host_poll_interface];
    toggle = interface->data_toggle ? RB_UH_R_TOG : 0u;
    UsbHostHid_StartTransaction((uint8_t)((USB_PID_IN << 4) |
                                          (interface->endpoint_address & USB_ENDP_ADDR_MASK)),
                                toggle,
                                0u,
                                0u,
                                USB_HOST_TRANSFER_INTERRUPT,
                                (uint8_t)(interface->endpoint_address & USB_ENDP_ADDR_MASK));
}

static void UsbHostHid_ProcessReady(void)
{
    UsbHostTransferResult result;
    UsbHostHidInterface *interface;
    uint8_t i;
    uint8_t interval_ticks;

    if(g_usb_host_transfer.active)
    {
        result = UsbHostHid_PollTransaction();
        if(result == USB_HOST_TRANSFER_PENDING)
        {
            return;
        }
        interface = &g_usb_host_interfaces[g_usb_host_poll_interface];
        if(result == USB_HOST_TRANSFER_SUCCESS)
        {
            g_usb_host_report_work.interface_index = g_usb_host_poll_interface;
            g_usb_host_report_work.length = g_usb_host_transfer.rx_length;
            g_usb_host_report_work.reserved[0] = 0u;
            g_usb_host_report_work.reserved[1] = 0u;
            for(i = 0u; i < USB_HOST_HID_MAX_REPORT_PACKET; ++i)
            {
                g_usb_host_report_work.bytes[i] =
                    (i < g_usb_host_report_work.length) ? g_usb_host_rx_dma[i] : 0u;
            }
            if(!StaticSpscRing_Push(&g_usb_host_report_ring,
                                    &g_usb_host_report_work))
            {
                ++g_usb_host_stats.report_drop;
            }
            else
            {
                ++g_usb_host_stats.report_count;
            }
            interface->data_toggle ^= 1u;
        }
        else if(result == USB_HOST_TRANSFER_NAK)
        {
            /* NAK is the normal idle response for an interrupt endpoint. */
        }
        else if(result == USB_HOST_TRANSFER_DISCONNECT)
        {
            return;
        }
        else
        {
            if(result == USB_HOST_TRANSFER_TIMEOUT)
            {
                ++g_usb_host_stats.transfer_timeout;
            }
            UsbHostHid_SetError();
            return;
        }
        interval_ticks = (uint8_t)((interface->interval + 1u) / 2u);
        if(interval_ticks == 0u)
        {
            interval_ticks = 1u;
        }
        g_usb_host_poll_due_ticks = (uint8_t)(interval_ticks - 1u);
        ++g_usb_host_poll_interface;
        if(g_usb_host_poll_interface >= g_usb_host_interface_count)
        {
            g_usb_host_poll_interface = 0u;
        }
        return;
    }
    if(g_usb_host_poll_due_ticks != 0u)
    {
        --g_usb_host_poll_due_ticks;
        return;
    }
    UsbHostHid_StartInterruptPoll();
}

static void UsbHostHid_DrainReports(void)
{
    UsbHostHidInterface *interface;
    uint8_t report_kind;
    uint8_t processed = 0u;

    while(processed < USB_HOST_PROCESS_REPORT_BUDGET &&
          StaticSpscRing_Pop(&g_usb_host_report_ring, &g_usb_host_report_work))
    {
        ++processed;
        if(g_usb_host_report_work.interface_index >= g_usb_host_interface_count)
        {
            continue;
        }
        interface = &g_usb_host_interfaces[g_usb_host_report_work.interface_index];
        if(interface->protocol == USB_HOST_HID_PROTOCOL_KEYBOARD)
        {
            report_kind = USB_HOST_HID_KIND_KEYBOARD;
        }
        else if(interface->protocol == USB_HOST_HID_PROTOCOL_MOUSE)
        {
            report_kind = USB_HOST_HID_KIND_MOUSE;
        }
        else if(interface->parser.report_id_present &&
                g_usb_host_report_work.length != 0u)
        {
            report_kind = UsbHostHid_GetReportKind(&interface->parser,
                                                   g_usb_host_report_work.bytes[0]);
        }
        else
        {
            report_kind = interface->parser.kind;
        }
        if(report_kind == USB_HOST_HID_KIND_KEYBOARD)
        {
            if(UsbHostHid_ParseKeyboard(interface,
                                        g_usb_host_report_work.bytes,
                                        g_usb_host_report_work.length,
                                        &g_usb_host_keyboard_work))
            {
                g_usb_host_keyboard_seen = 1u;
                if(!EventRouter_InjectKeyboardReport(ROUTER_SRC_USB_KEYBOARD,
                                                      &g_usb_host_keyboard_work))
                {
                    ++g_usb_host_stats.report_drop;
                }
            }
            else
            {
                ++g_usb_host_stats.report_parse_error;
            }
        }
        else if(report_kind == USB_HOST_HID_KIND_MOUSE)
        {
            if(UsbHostHid_ParseMouse(interface,
                                     g_usb_host_report_work.bytes,
                                     g_usb_host_report_work.length,
                                     &g_usb_host_mouse_work))
            {
                g_usb_host_mouse_seen = 1u;
                if(!EventRouter_InjectMouseReport(ROUTER_SRC_USB_MOUSE,
                                                   &g_usb_host_mouse_work))
                {
                    ++g_usb_host_stats.report_drop;
                }
            }
            else
            {
                ++g_usb_host_stats.report_parse_error;
            }
        }
    }
}

void UsbHostHid_Init(void)
{
    /* The PCB uses PB6/HOST_EN for the USB-A load switch.  A development
     * board may override the mask to 0 when its USB host VBUS is already
     * powered externally. */
    GPIOB_SetBits(BOARD_USB_HOST_ENABLE_PIN);
    GPIOB_ModeCfg(BOARD_USB_HOST_ENABLE_PIN, GPIO_ModeOut_PP_5mA);
    pU2HOST_RX_RAM_Addr = g_usb_host_rx_dma;
    pU2HOST_TX_RAM_Addr = g_usb_host_tx_dma;
    USB2_HostInit();

    StaticSpscRing_Init(&g_usb_host_report_ring,
                        g_usb_host_report_storage,
                        sizeof(g_usb_host_report_storage[0]),
                        USB_HOST_REPORT_QUEUE_CAPACITY);
    UsbHostHid_ClearBytes((uint8_t *)&g_usb_host_transfer,
                          sizeof(g_usb_host_transfer));
    UsbHostHid_ClearBytes((uint8_t *)&g_usb_host_control,
                          sizeof(g_usb_host_control));
    UsbHostHid_ClearBytes((uint8_t *)&g_usb_host_stats,
                          sizeof(g_usb_host_stats));
    g_usb_host_state = USB_HOST_STATE_WAIT_ATTACH;
    g_usb_host_attached = 0u;
    g_usb_host_ready = 0u;
    g_usb_host_interface_count = 0u;
    g_usb_host_poll_due_ticks = 0u;
    g_usb_host_keyboard_seen = 0u;
    g_usb_host_mouse_seen = 0u;
}

void UsbHostHid_Process(void)
{
    uint8_t attached = UsbHostHid_IsAttached();

    if(R8_USB2_INT_FG & RB_UIF_DETECT)
    {
        R8_USB2_INT_FG = RB_UIF_DETECT;
    }
    if(!attached)
    {
        if(g_usb_host_attached)
        {
            UsbHostHid_HandleDetach();
        }
        UsbHostHid_DrainReports();
        return;
    }
    if(!g_usb_host_attached)
    {
        g_usb_host_attached = 1u;
        ++g_usb_host_stats.attach_count;
        UsbHostHid_StartReset();
        return;
    }

    switch(g_usb_host_state)
    {
        case USB_HOST_STATE_RESET:
            UsbHostHid_ProcessReset();
            break;
        case USB_HOST_STATE_RESET_SETTLE:
            UsbHostHid_ProcessResetSettle();
            break;
        case USB_HOST_STATE_ADDRESS_SETTLE:
            UsbHostHid_ProcessAddressSettle();
            break;
        case USB_HOST_STATE_ENUMERATING:
            UsbHostHid_ControlProcess();
            break;
        case USB_HOST_STATE_READY:
            UsbHostHid_ProcessReady();
            break;
        case USB_HOST_STATE_ERROR:
            if(g_usb_host_error_ticks != 0u)
            {
                --g_usb_host_error_ticks;
            }
            else
            {
                UsbHostHid_StartReset();
            }
            break;
        case USB_HOST_STATE_WAIT_ATTACH:
        default:
            UsbHostHid_StartReset();
            break;
    }
    UsbHostHid_DrainReports();
}

uint8_t UsbHostHid_IsReady(void)
{
    return g_usb_host_ready;
}

uint8_t UsbHostHid_GetInterfaceCount(void)
{
    return g_usb_host_interface_count;
}

void UsbHostHid_GetStats(UsbHostHidStats *stats)
{
    if(stats == NULL)
    {
        return;
    }
    *stats = g_usb_host_stats;
}
