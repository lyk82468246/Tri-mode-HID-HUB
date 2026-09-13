#ifndef TRI_MODE_HID_HUB_EVENT_ROUTER_TYPES_H
#define TRI_MODE_HID_HUB_EVENT_ROUTER_TYPES_H

#include <stdint.h>

#include "static_spsc_ring.h"

#define EVENT_ROUTER_ALIGN4             __attribute__((aligned(4)))

#define HID_KEYBOARD_REPORT_LEN         8u
#define HID_MOUSE_REPORT_LEN            4u
#define HID_GAMEPAD_REPORT_LEN          8u
#define STREAM_CHUNK_MAX_LEN            20u
#define ROUTER_EVENT_PAYLOAD_LEN        20u

typedef enum
{
    ROUTER_SRC_PS2_KEYBOARD = 0,
    ROUTER_SRC_PS2_MOUSE,
    ROUTER_SRC_USB_KEYBOARD,
    ROUTER_SRC_USB_MOUSE,
    ROUTER_SRC_UART,
    ROUTER_SRC_USB_CDC,
    ROUTER_SRC_TEST,
    ROUTER_SOURCE_COUNT
} RouterInputSource;

typedef enum
{
    ROUTER_EVENT_KEYBOARD_REPORT = 0,
    ROUTER_EVENT_MOUSE_REPORT,
    ROUTER_EVENT_GAMEPAD_REPORT,
    ROUTER_EVENT_STREAM_DATA,
    ROUTER_EVENT_SOURCE_UP,
    ROUTER_EVENT_SOURCE_DOWN,
    ROUTER_EVENT_SOURCE_RESYNC
} RouterEventKind;

typedef enum
{
    ROUTER_OUTPUT_NONE = 0,
    ROUTER_OUTPUT_USB = (1u << 0),
    ROUTER_OUTPUT_BLE = (1u << 1),
    ROUTER_OUTPUT_BOTH = ROUTER_OUTPUT_USB | ROUTER_OUTPUT_BLE
} RouterOutputMask;

typedef enum
{
    ROUTER_POLICY_USB_ONLY = 0,
    ROUTER_POLICY_BLE_ONLY,
    ROUTER_POLICY_USB_PREFERRED,
    ROUTER_POLICY_BLE_PREFERRED,
    ROUTER_POLICY_BOTH
} RouterOutputPolicy;

#define ROUTER_FLAG_SNAPSHOT            (1u << 0)
#define ROUTER_FLAG_RELEASE_ALL         (1u << 1)
#define ROUTER_FLAG_FROM_ISR            (1u << 2)
#define ROUTER_FLAG_OVERFLOW            (1u << 3)

typedef struct
{
    uint8_t bytes[HID_KEYBOARD_REPORT_LEN];
} HidKeyboardReport;

typedef struct
{
    uint8_t bytes[HID_MOUSE_REPORT_LEN];
} HidMouseReport;

typedef struct
{
    uint8_t bytes[HID_GAMEPAD_REPORT_LEN];
} HidGamepadReport;

typedef union
{
    HidKeyboardReport keyboard;
    HidMouseReport mouse;
    HidGamepadReport gamepad;
    uint8_t stream[STREAM_CHUNK_MAX_LEN];
    uint8_t raw[ROUTER_EVENT_PAYLOAD_LEN];
} RouterPayload;

/* 32 bytes on the CH58x RV32 ABI. */
typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint8_t source;
    uint8_t kind;
    uint8_t flags;
    uint8_t length;
    RouterPayload payload;
} RouterEvent;

/* Output queues own complete values until the USB/BLE backend consumes them. */
typedef struct
{
    uint8_t report_id;
    uint8_t kind;
    uint8_t length;
    uint8_t flags;
    uint8_t bytes[ROUTER_EVENT_PAYLOAD_LEN];
} HidTxFrame;

typedef struct
{
    uint8_t length;
    uint8_t flags;
    uint16_t reserved;
    uint8_t bytes[STREAM_CHUNK_MAX_LEN];
} StreamTxFrame;

typedef struct
{
    uint8_t modifiers;
    uint8_t keycodes[6];
    uint8_t key_count;
    uint8_t overflow;
} KeyboardState;

typedef struct
{
    uint8_t buttons;
    int8_t dx;
    int8_t dy;
    int8_t wheel;
} MouseState;

typedef struct
{
    uint8_t connected;
    uint8_t keyboard_valid;
    uint8_t mouse_valid;
    uint8_t reserved;
    KeyboardState keyboard;
    MouseState mouse;
    HidGamepadReport gamepad;
} InputSourceState;

typedef struct
{
    uint32_t ps2_overrun;
    uint32_t uart_overrun;
    uint32_t usb_report_drop;
    uint32_t router_event_drop;
    uint32_t hid_tx_drop;
    uint32_t stream_tx_drop;
    uint32_t parser_error;
    uint32_t resync_count;
} RouterStats;

typedef struct
{
    uint8_t task_id;
    uint8_t output_policy;
    uint8_t active_output_mask;
    uint8_t flags;
    uint16_t pending_events;
    uint16_t reserved;
    uint32_t next_sequence;

    InputSourceState source[ROUTER_SOURCE_COUNT];
    RouterStats stats;

    StaticSpscRing input_ring;
    StaticSpscRing usb_hid_tx_ring;
    StaticSpscRing ble_hid_tx_ring;
    StaticSpscRing stream_tx_ring;
} Event_Router;

#endif /* TRI_MODE_HID_HUB_EVENT_ROUTER_TYPES_H */
