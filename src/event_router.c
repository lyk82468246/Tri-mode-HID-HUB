#include "event_router.h"

#define ROUTER_INPUT_CAPACITY       16u
#define ROUTER_HID_TX_CAPACITY       4u
#define ROUTER_STREAM_TX_CAPACITY    8u
#define ROUTER_PROCESS_BUDGET        4u

static RouterEvent g_router_input_storage[ROUTER_INPUT_CAPACITY] EVENT_ROUTER_ALIGN4;
static HidTxFrame g_usb_hid_tx_storage[ROUTER_HID_TX_CAPACITY] EVENT_ROUTER_ALIGN4;
static HidTxFrame g_ble_hid_tx_storage[ROUTER_HID_TX_CAPACITY] EVENT_ROUTER_ALIGN4;
static StreamTxFrame g_stream_tx_storage[ROUTER_STREAM_TX_CAPACITY] EVENT_ROUTER_ALIGN4;

static Event_Router g_event_router EVENT_ROUTER_ALIGN4;

static uint8_t EventRouter_CopyPayload(uint8_t *dst,
                                       const uint8_t *src,
                                       uint8_t length,
                                       uint8_t limit)
{
    uint8_t i;

    if(length > limit)
    {
        length = limit;
    }
    for(i = 0; i < length; ++i)
    {
        dst[i] = src[i];
    }
    return length;
}

void EventRouter_Init(void)
{
    StaticSpscRing_Init(&g_event_router.input_ring,
                        g_router_input_storage,
                        sizeof(g_router_input_storage[0]),
                        ROUTER_INPUT_CAPACITY);
    StaticSpscRing_Init(&g_event_router.usb_hid_tx_ring,
                        g_usb_hid_tx_storage,
                        sizeof(g_usb_hid_tx_storage[0]),
                        ROUTER_HID_TX_CAPACITY);
    StaticSpscRing_Init(&g_event_router.ble_hid_tx_ring,
                        g_ble_hid_tx_storage,
                        sizeof(g_ble_hid_tx_storage[0]),
                        ROUTER_HID_TX_CAPACITY);
    StaticSpscRing_Init(&g_event_router.stream_tx_ring,
                        g_stream_tx_storage,
                        sizeof(g_stream_tx_storage[0]),
                        ROUTER_STREAM_TX_CAPACITY);

    g_event_router.output_policy = ROUTER_POLICY_USB_ONLY;
    g_event_router.active_output_mask = ROUTER_OUTPUT_USB;
}

uint8_t EventRouter_Post(const RouterEvent *event)
{
    RouterEvent queued;
    uint8_t i;

    queued.sequence = event->sequence;
    if(queued.sequence == 0)
    {
        queued.sequence = ++g_event_router.next_sequence;
    }
    else if(queued.sequence > g_event_router.next_sequence)
    {
        g_event_router.next_sequence = queued.sequence;
    }
    queued.timestamp_ms = event->timestamp_ms;
    queued.source = event->source;
    queued.kind = event->kind;
    queued.flags = event->flags;
    queued.length = event->length;
    for(i = 0; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
    {
        queued.payload.raw[i] = event->payload.raw[i];
    }

    if(!StaticSpscRing_Push(&g_event_router.input_ring, &queued))
    {
        g_event_router.stats.router_event_drop++;
        return 0;
    }
    return 1;
}

static uint8_t EventRouter_InjectFixed(uint8_t source,
                                       uint8_t kind,
                                       const uint8_t *data,
                                       uint8_t length)
{
    RouterEvent event;
    uint8_t i;

    event.sequence = 0;
    event.timestamp_ms = 0;
    event.source = source;
    event.kind = kind;
    event.flags = ROUTER_FLAG_SNAPSHOT;
    event.length = length;
    for(i = 0; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
    {
        event.payload.raw[i] = 0;
    }
    EventRouter_CopyPayload(event.payload.raw,
                            data,
                            length,
                            ROUTER_EVENT_PAYLOAD_LEN);
    return EventRouter_Post(&event);
}

uint8_t EventRouter_InjectKeyboardReport(uint8_t source,
                                         const HidKeyboardReport *report)
{
    return EventRouter_InjectFixed(source,
                                   ROUTER_EVENT_KEYBOARD_REPORT,
                                   report->bytes,
                                   HID_KEYBOARD_REPORT_LEN);
}

uint8_t EventRouter_InjectMouseReport(uint8_t source,
                                      const HidMouseReport *report)
{
    return EventRouter_InjectFixed(source,
                                   ROUTER_EVENT_MOUSE_REPORT,
                                   report->bytes,
                                   HID_MOUSE_REPORT_LEN);
}

uint8_t EventRouter_InjectGamepadReport(uint8_t source,
                                        const HidGamepadReport *report)
{
    return EventRouter_InjectFixed(source,
                                   ROUTER_EVENT_GAMEPAD_REPORT,
                                   report->bytes,
                                   HID_GAMEPAD_REPORT_LEN);
}

uint8_t EventRouter_InjectStreamData(uint8_t source,
                                     const uint8_t *data,
                                     uint8_t length)
{
    uint8_t accepted = 1;
    uint8_t chunk;

    while(length != 0)
    {
        chunk = (length > STREAM_CHUNK_MAX_LEN) ? STREAM_CHUNK_MAX_LEN : length;
        if(!EventRouter_InjectFixed(source,
                                    ROUTER_EVENT_STREAM_DATA,
                                    data,
                                    chunk))
        {
            accepted = 0;
        }
        data += chunk;
        length = (uint8_t)(length - chunk);
    }
    return accepted;
}

static void EventRouter_QueueHid(const RouterEvent *event,
                                 uint8_t report_id,
                                 uint8_t length)
{
    HidTxFrame frame;
    uint8_t i;

    frame.report_id = report_id;
    frame.kind = event->kind;
    frame.length = length;
    frame.flags = event->flags;
    for(i = 0; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
    {
        frame.bytes[i] = 0;
    }
    EventRouter_CopyPayload(frame.bytes,
                            event->payload.raw,
                            event->length,
                            length);

    if(!StaticSpscRing_Push(&g_event_router.usb_hid_tx_ring, &frame))
    {
        g_event_router.stats.hid_tx_drop++;
    }
}

static void EventRouter_QueueStream(const RouterEvent *event)
{
    StreamTxFrame frame;
    uint8_t i;

    frame.length = EventRouter_CopyPayload(frame.bytes,
                                           event->payload.raw,
                                           event->length,
                                           STREAM_CHUNK_MAX_LEN);
    frame.flags = event->flags;
    frame.reserved = 0;
    for(i = frame.length; i < STREAM_CHUNK_MAX_LEN; ++i)
    {
        frame.bytes[i] = 0;
    }

    if(!StaticSpscRing_Push(&g_event_router.stream_tx_ring, &frame))
    {
        g_event_router.stats.stream_tx_drop++;
    }
}

void EventRouter_Process(void)
{
    RouterEvent event;
    uint8_t processed = 0;

    while((processed < ROUTER_PROCESS_BUDGET) &&
          StaticSpscRing_Pop(&g_event_router.input_ring, &event))
    {
        if(!(g_event_router.active_output_mask & ROUTER_OUTPUT_USB))
        {
            ++processed;
            continue;
        }

        switch(event.kind)
        {
            case ROUTER_EVENT_KEYBOARD_REPORT:
                EventRouter_QueueHid(&event,
                                     1u,
                                     HID_KEYBOARD_REPORT_LEN);
                break;

            case ROUTER_EVENT_MOUSE_REPORT:
                EventRouter_QueueHid(&event,
                                     2u,
                                     HID_MOUSE_REPORT_LEN);
                break;

            case ROUTER_EVENT_GAMEPAD_REPORT:
                EventRouter_QueueHid(&event,
                                     3u,
                                     HID_GAMEPAD_REPORT_LEN);
                break;

            case ROUTER_EVENT_STREAM_DATA:
                EventRouter_QueueStream(&event);
                break;

            case ROUTER_EVENT_SOURCE_RESYNC:
            case ROUTER_EVENT_SOURCE_UP:
            case ROUTER_EVENT_SOURCE_DOWN:
            default:
                /* Source state handling is completed in Milestone 5. */
                break;
        }
        ++processed;
    }
}

uint8_t EventRouter_DequeueUsbHidFrame(HidTxFrame *frame)
{
    return StaticSpscRing_Pop(&g_event_router.usb_hid_tx_ring, frame);
}

uint8_t EventRouter_DequeueUsbStreamFrame(StreamTxFrame *frame)
{
    return StaticSpscRing_Pop(&g_event_router.stream_tx_ring, frame);
}

Event_Router *EventRouter_GetContext(void)
{
    return &g_event_router;
}
