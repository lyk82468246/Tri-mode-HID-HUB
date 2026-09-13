#include "event_router.h"

#define ROUTER_INPUT_CAPACITY       16u
#define ROUTER_HID_TX_CAPACITY      4u
#define ROUTER_STREAM_TX_CAPACITY   8u
#define ROUTER_PROCESS_BUDGET       4u

#define ROUTER_REPORT_ID_KEYBOARD   1u
#define ROUTER_REPORT_ID_MOUSE      2u
#define ROUTER_REPORT_ID_GAMEPAD    3u

static RouterEvent g_router_input_storage[ROUTER_INPUT_CAPACITY] EVENT_ROUTER_ALIGN4;
static HidTxFrame g_usb_hid_tx_storage[ROUTER_HID_TX_CAPACITY] EVENT_ROUTER_ALIGN4;
static HidTxFrame g_ble_hid_tx_storage[ROUTER_HID_TX_CAPACITY] EVENT_ROUTER_ALIGN4;
static StreamTxFrame g_usb_stream_tx_storage[ROUTER_STREAM_TX_CAPACITY] EVENT_ROUTER_ALIGN4;
static StreamTxFrame g_ble_stream_tx_storage[ROUTER_STREAM_TX_CAPACITY] EVENT_ROUTER_ALIGN4;

static Event_Router g_event_router EVENT_ROUTER_ALIGN4;

static void EventRouter_ClearBytes(uint8_t *data, uint16_t length)
{
    uint16_t i;

    for(i = 0u; i < length; ++i)
    {
        data[i] = 0u;
    }
}

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
    for(i = 0u; i < length; ++i)
    {
        dst[i] = src[i];
    }
    return length;
}

static uint8_t EventRouter_IsValidSource(uint8_t source)
{
    return (source < ROUTER_SOURCE_COUNT) ? 1u : 0u;
}

static uint8_t EventRouter_IsValidKind(uint8_t kind)
{
    return (kind <= ROUTER_EVENT_SOURCE_RESYNC) ? 1u : 0u;
}

static uint8_t EventRouter_IsValidEventLength(uint8_t kind,
                                              uint8_t length)
{
    switch(kind)
    {
        case ROUTER_EVENT_KEYBOARD_REPORT:
            return (length == HID_KEYBOARD_REPORT_LEN) ? 1u : 0u;

        case ROUTER_EVENT_MOUSE_REPORT:
            return (length == HID_MOUSE_REPORT_LEN) ? 1u : 0u;

        case ROUTER_EVENT_GAMEPAD_REPORT:
            return (length == HID_GAMEPAD_REPORT_LEN) ? 1u : 0u;

        case ROUTER_EVENT_STREAM_DATA:
            return (length <= STREAM_CHUNK_MAX_LEN) ? 1u : 0u;

        case ROUTER_EVENT_SOURCE_UP:
        case ROUTER_EVENT_SOURCE_DOWN:
        case ROUTER_EVENT_SOURCE_RESYNC:
            return (length == 0u) ? 1u : 0u;

        default:
            return 0u;
    }
}

static uint8_t EventRouter_OutputBitForSlot(uint8_t slot)
{
    return (slot == ROUTER_OUTPUT_INDEX_BLE) ?
               ROUTER_OUTPUT_BLE : ROUTER_OUTPUT_USB;
}

static uint8_t EventRouter_OutputSlotForBit(uint8_t output_bit)
{
    return (output_bit == ROUTER_OUTPUT_BLE) ?
               ROUTER_OUTPUT_INDEX_BLE : ROUTER_OUTPUT_INDEX_USB;
}

static uint8_t EventRouter_HidReportMaskForId(uint8_t report_id)
{
    switch(report_id)
    {
        case ROUTER_REPORT_ID_KEYBOARD:
            return ROUTER_HID_REPORT_MASK_KEYBOARD;

        case ROUTER_REPORT_ID_MOUSE:
            return ROUTER_HID_REPORT_MASK_MOUSE;

        case ROUTER_REPORT_ID_GAMEPAD:
            return ROUTER_HID_REPORT_MASK_GAMEPAD;

        default:
            return ROUTER_HID_REPORT_MASK_NONE;
    }
}

static uint8_t EventRouter_IsHidReportAvailable(uint8_t output_bit,
                                                uint8_t report_id)
{
    uint8_t slot = EventRouter_OutputSlotForBit(output_bit);
    uint8_t report_mask = EventRouter_HidReportMaskForId(report_id);

    return ((g_event_router.hid_report_available[slot] & report_mask) != 0u) ?
               1u : 0u;
}

static void EventRouter_ClearHidQueues(uint8_t output_mask)
{
    /* All output consumers run after EventRouter_Process in the same TMOS
     * service callback.  Clearing here therefore cannot race a dequeue. */
    if((output_mask & ROUTER_OUTPUT_USB) != 0u)
    {
        StaticSpscRing_Clear(&g_event_router.usb_hid_tx_ring);
    }
    if((output_mask & ROUTER_OUTPUT_BLE) != 0u)
    {
        StaticSpscRing_Clear(&g_event_router.ble_hid_tx_ring);
    }
}

static void EventRouter_ClearStreamQueues(uint8_t output_mask)
{
    if((output_mask & ROUTER_OUTPUT_USB) != 0u)
    {
        StaticSpscRing_Clear(&g_event_router.usb_stream_tx_ring);
    }
    if((output_mask & ROUTER_OUTPUT_BLE) != 0u)
    {
        StaticSpscRing_Clear(&g_event_router.ble_stream_tx_ring);
    }
}

static void EventRouter_ResetPendingMouse(uint8_t slot)
{
    if(slot >= ROUTER_OUTPUT_SLOT_COUNT)
    {
        return;
    }
    g_event_router.pending_mouse[slot].buttons = 0u;
    g_event_router.pending_mouse[slot].dx = 0;
    g_event_router.pending_mouse[slot].dy = 0;
    g_event_router.pending_mouse[slot].wheel = 0;
}

static void EventRouter_MarkOutputResync(uint8_t output_bit)
{
    uint8_t slot = EventRouter_OutputSlotForBit(output_bit);

    g_event_router.pending_keyboard_mask |= output_bit;
    g_event_router.pending_mouse_mask |= output_bit;
    g_event_router.pending_gamepad_mask |= output_bit;
    g_event_router.pending_mouse[slot].buttons =
        g_event_router.merged_mouse.buttons;
    g_event_router.pending_mouse[slot].dx = 0;
    g_event_router.pending_mouse[slot].dy = 0;
    g_event_router.pending_mouse[slot].wheel = 0;
    g_event_router.stats.resync_count++;
}

static void EventRouter_MarkAllActiveHidPending(void)
{
    uint8_t slot;
    uint8_t output_bit;

    g_event_router.pending_keyboard_mask |=
        g_event_router.active_output_mask;
    g_event_router.pending_mouse_mask |=
        g_event_router.active_output_mask;
    g_event_router.pending_gamepad_mask |=
        g_event_router.active_output_mask;

    for(slot = 0u; slot < ROUTER_OUTPUT_SLOT_COUNT; ++slot)
    {
        output_bit = EventRouter_OutputBitForSlot(slot);
        if((g_event_router.active_output_mask & output_bit) != 0u)
        {
            g_event_router.pending_mouse[slot].buttons =
                g_event_router.merged_mouse.buttons;
            g_event_router.pending_mouse[slot].dx = 0;
            g_event_router.pending_mouse[slot].dy = 0;
            g_event_router.pending_mouse[slot].wheel = 0;
        }
    }
}

static uint8_t EventRouter_SelectActiveOutput(uint8_t available_mask)
{
    uint8_t available = (uint8_t)(available_mask & ROUTER_OUTPUT_BOTH);
    uint8_t requested = ROUTER_OUTPUT_NONE;

    switch(g_event_router.output_policy)
    {
        case ROUTER_POLICY_USB_ONLY:
            requested = ROUTER_OUTPUT_USB;
            break;

        case ROUTER_POLICY_BLE_ONLY:
            requested = ROUTER_OUTPUT_BLE;
            break;

        case ROUTER_POLICY_USB_PREFERRED:
            if((available & ROUTER_OUTPUT_USB) != 0u)
            {
                requested = ROUTER_OUTPUT_USB;
            }
            else if((available & ROUTER_OUTPUT_BLE) != 0u)
            {
                requested = ROUTER_OUTPUT_BLE;
            }
            break;

        case ROUTER_POLICY_BLE_PREFERRED:
            if((available & ROUTER_OUTPUT_BLE) != 0u)
            {
                requested = ROUTER_OUTPUT_BLE;
            }
            else if((available & ROUTER_OUTPUT_USB) != 0u)
            {
                requested = ROUTER_OUTPUT_USB;
            }
            break;

        case ROUTER_POLICY_BOTH:
            requested = ROUTER_OUTPUT_BOTH;
            break;

        case ROUTER_POLICY_NONE:
        default:
            requested = ROUTER_OUTPUT_NONE;
            break;
    }

    return (uint8_t)(requested & available);
}

static void EventRouter_ApplyActiveOutputMask(uint8_t active_output_mask)
{
    uint8_t previous_mask = g_event_router.active_output_mask;
    uint8_t output_bit;
    uint8_t slot;

    if(previous_mask == active_output_mask)
    {
        return;
    }

    for(output_bit = ROUTER_OUTPUT_USB;
        output_bit <= ROUTER_OUTPUT_BLE;
        output_bit = (uint8_t)(output_bit << 1))
    {
        if(((previous_mask & output_bit) != 0u) &&
           ((active_output_mask & output_bit) == 0u))
        {
            EventRouter_ClearHidQueues(output_bit);
            g_event_router.pending_keyboard_mask &= (uint8_t)~output_bit;
            g_event_router.pending_mouse_mask &= (uint8_t)~output_bit;
            g_event_router.pending_gamepad_mask &= (uint8_t)~output_bit;
            slot = EventRouter_OutputSlotForBit(output_bit);
            EventRouter_ResetPendingMouse(slot);
        }
    }

    g_event_router.active_output_mask = active_output_mask;

    for(output_bit = ROUTER_OUTPUT_USB;
        output_bit <= ROUTER_OUTPUT_BLE;
        output_bit = (uint8_t)(output_bit << 1))
    {
        if(((previous_mask & output_bit) == 0u) &&
           ((active_output_mask & output_bit) != 0u))
        {
            EventRouter_MarkOutputResync(output_bit);
        }
    }

    g_event_router.stats.output_switches++;
}

static void EventRouter_ApplyActiveStreamOutputMask(uint8_t active_output_mask)
{
    uint8_t previous_mask = g_event_router.active_stream_output_mask;
    uint8_t output_bit;

    if(previous_mask == active_output_mask)
    {
        return;
    }

    for(output_bit = ROUTER_OUTPUT_USB;
        output_bit <= ROUTER_OUTPUT_BLE;
        output_bit = (uint8_t)(output_bit << 1))
    {
        if(((previous_mask & output_bit) != 0u) &&
           ((active_output_mask & output_bit) == 0u))
        {
            EventRouter_ClearStreamQueues(output_bit);
        }
    }
    g_event_router.active_stream_output_mask = active_output_mask;
    g_event_router.stats.output_switches++;
}

void EventRouter_Init(void)
{
    EventRouter_ClearBytes((uint8_t *)&g_event_router,
                           (uint16_t)sizeof(g_event_router));

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
    StaticSpscRing_Init(&g_event_router.usb_stream_tx_ring,
                        g_usb_stream_tx_storage,
                        sizeof(g_usb_stream_tx_storage[0]),
                        ROUTER_STREAM_TX_CAPACITY);
    StaticSpscRing_Init(&g_event_router.ble_stream_tx_ring,
                        g_ble_stream_tx_storage,
                        sizeof(g_ble_stream_tx_storage[0]),
                        ROUTER_STREAM_TX_CAPACITY);

    /* Automatic USB-preferred selection is the normal product behavior. */
    g_event_router.output_policy = ROUTER_POLICY_USB_PREFERRED;
    g_event_router.output_available_mask = ROUTER_OUTPUT_NONE;
    g_event_router.active_output_mask = ROUTER_OUTPUT_NONE;
    g_event_router.stream_available_mask = ROUTER_OUTPUT_NONE;
    g_event_router.active_stream_output_mask = ROUTER_OUTPUT_NONE;
    g_event_router.gamepad_source = ROUTER_SOURCE_COUNT;
    g_event_router.hid_report_available[ROUTER_OUTPUT_INDEX_USB] =
        ROUTER_HID_REPORT_MASK_NONE;
    g_event_router.hid_report_available[ROUTER_OUTPUT_INDEX_BLE] =
        ROUTER_HID_REPORT_MASK_NONE;
}

void EventRouter_SetOutputPolicy(uint8_t output_policy)
{
    if(output_policy > ROUTER_POLICY_NONE)
    {
        g_event_router.stats.parser_error++;
        return;
    }
    g_event_router.output_policy = output_policy;
    EventRouter_ApplyActiveOutputMask(EventRouter_SelectActiveOutput(
        g_event_router.output_available_mask));
    EventRouter_ApplyActiveStreamOutputMask(EventRouter_SelectActiveOutput(
        g_event_router.stream_available_mask));
}

void EventRouter_SetOutputAvailability(uint8_t available_mask)
{
    uint8_t masked_availability =
        (uint8_t)(available_mask & ROUTER_OUTPUT_BOTH);

    /* Preserve the legacy output-level API by treating an available HID
     * endpoint as capable of carrying all three logical reports. */
    EventRouter_SetHidReportAvailability(
        ((masked_availability & ROUTER_OUTPUT_USB) != 0u) ?
            ROUTER_HID_REPORT_MASK_ALL : ROUTER_HID_REPORT_MASK_NONE,
        ((masked_availability & ROUTER_OUTPUT_BLE) != 0u) ?
            ROUTER_HID_REPORT_MASK_ALL : ROUTER_HID_REPORT_MASK_NONE);
}

void EventRouter_SetHidReportAvailability(uint8_t usb_report_mask,
                                          uint8_t ble_report_mask)
{
    uint8_t report_masks[ROUTER_OUTPUT_SLOT_COUNT];
    uint8_t slot;
    uint8_t output_bit;
    uint8_t output_mask = ROUTER_OUTPUT_NONE;

    report_masks[ROUTER_OUTPUT_INDEX_USB] =
        (uint8_t)(usb_report_mask & ROUTER_HID_REPORT_MASK_ALL);
    report_masks[ROUTER_OUTPUT_INDEX_BLE] =
        (uint8_t)(ble_report_mask & ROUTER_HID_REPORT_MASK_ALL);

    for(slot = 0u; slot < ROUTER_OUTPUT_SLOT_COUNT; ++slot)
    {
        output_bit = EventRouter_OutputBitForSlot(slot);
        if(g_event_router.hid_report_available[slot] != report_masks[slot])
        {
            g_event_router.hid_report_available[slot] = report_masks[slot];
            if((g_event_router.active_output_mask & output_bit) != 0u)
            {
                /* A mixed HID queue cannot safely retain frames for a
                 * report whose CCCD just changed.  Rebuild all reports from
                 * the canonical state on the next service pass. */
                EventRouter_ClearHidQueues(output_bit);
                g_event_router.pending_keyboard_mask |= output_bit;
                g_event_router.pending_mouse_mask |= output_bit;
                g_event_router.pending_gamepad_mask |= output_bit;
                g_event_router.pending_mouse[slot].buttons =
                    g_event_router.merged_mouse.buttons;
                g_event_router.pending_mouse[slot].dx = 0;
                g_event_router.pending_mouse[slot].dy = 0;
                g_event_router.pending_mouse[slot].wheel = 0;
                g_event_router.stats.resync_count++;
            }
        }
        if(report_masks[slot] != ROUTER_HID_REPORT_MASK_NONE)
        {
            output_mask |= output_bit;
        }
    }

    g_event_router.output_available_mask = output_mask;
    EventRouter_ApplyActiveOutputMask(EventRouter_SelectActiveOutput(
        g_event_router.output_available_mask));
}

void EventRouter_SetStreamAvailability(uint8_t available_mask)
{
    g_event_router.stream_available_mask =
        (uint8_t)(available_mask & ROUTER_OUTPUT_BOTH);
    EventRouter_ApplyActiveStreamOutputMask(EventRouter_SelectActiveOutput(
        g_event_router.stream_available_mask));
}

void EventRouter_SetOutputMask(uint8_t output_mask)
{
    switch(output_mask & ROUTER_OUTPUT_BOTH)
    {
        case ROUTER_OUTPUT_USB:
            EventRouter_SetOutputPolicy(ROUTER_POLICY_USB_ONLY);
            break;

        case ROUTER_OUTPUT_BLE:
            EventRouter_SetOutputPolicy(ROUTER_POLICY_BLE_ONLY);
            break;

        case ROUTER_OUTPUT_BOTH:
            EventRouter_SetOutputPolicy(ROUTER_POLICY_BOTH);
            break;

        case ROUTER_OUTPUT_NONE:
        default:
            EventRouter_SetOutputPolicy(ROUTER_POLICY_NONE);
            break;
    }
}

uint8_t EventRouter_GetActiveOutputMask(void)
{
    return g_event_router.active_output_mask;
}

uint8_t EventRouter_GetActiveStreamOutputMask(void)
{
    return g_event_router.active_stream_output_mask;
}

uint8_t EventRouter_GetOutputAvailability(void)
{
    return g_event_router.output_available_mask;
}

uint8_t EventRouter_Post(const RouterEvent *event)
{
    RouterEvent queued;
    uint8_t i;

    if(event == 0)
    {
        g_event_router.stats.parser_error++;
        return 0u;
    }
    if(!EventRouter_IsValidSource(event->source) ||
       !EventRouter_IsValidKind(event->kind) ||
       !EventRouter_IsValidEventLength(event->kind, event->length) ||
       (event->length > ROUTER_EVENT_PAYLOAD_LEN))
    {
        g_event_router.stats.parser_error++;
        return 0u;
    }

    queued.sequence = event->sequence;
    if(queued.sequence == 0u)
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
    for(i = 0u; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
    {
        queued.payload.raw[i] = event->payload.raw[i];
    }

    if(!StaticSpscRing_Push(&g_event_router.input_ring, &queued))
    {
        g_event_router.stats.router_event_drop++;
        return 0u;
    }
    g_event_router.pending_events = StaticSpscRing_Count(
        &g_event_router.input_ring);
    return 1u;
}

static uint8_t EventRouter_InjectFixed(uint8_t source,
                                       uint8_t kind,
                                       const uint8_t *data,
                                       uint8_t length)
{
    RouterEvent event;
    uint8_t i;

    if((data == 0) && (length != 0u))
    {
        return 0u;
    }

    event.sequence = 0u;
    event.timestamp_ms = 0u;
    event.source = source;
    event.kind = kind;
    event.flags = ROUTER_FLAG_SNAPSHOT;
    event.length = length;
    for(i = 0u; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
    {
        event.payload.raw[i] = 0u;
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
    if(report == 0)
    {
        return 0u;
    }
    return EventRouter_InjectFixed(source,
                                   ROUTER_EVENT_KEYBOARD_REPORT,
                                   report->bytes,
                                   HID_KEYBOARD_REPORT_LEN);
}

uint8_t EventRouter_InjectMouseReport(uint8_t source,
                                      const HidMouseReport *report)
{
    if(report == 0)
    {
        return 0u;
    }
    return EventRouter_InjectFixed(source,
                                   ROUTER_EVENT_MOUSE_REPORT,
                                   report->bytes,
                                   HID_MOUSE_REPORT_LEN);
}

uint8_t EventRouter_InjectGamepadReport(uint8_t source,
                                        const HidGamepadReport *report)
{
    if(report == 0)
    {
        return 0u;
    }
    return EventRouter_InjectFixed(source,
                                   ROUTER_EVENT_GAMEPAD_REPORT,
                                   report->bytes,
                                   HID_GAMEPAD_REPORT_LEN);
}

uint8_t EventRouter_InjectStreamData(uint8_t source,
                                     const uint8_t *data,
                                     uint8_t length)
{
    uint8_t accepted = 1u;
    uint8_t chunk;

    if((data == 0) && (length != 0u))
    {
        return 0u;
    }
    while(length != 0u)
    {
        chunk = (length > STREAM_CHUNK_MAX_LEN) ?
                    STREAM_CHUNK_MAX_LEN : length;
        if(!EventRouter_InjectFixed(source,
                                    ROUTER_EVENT_STREAM_DATA,
                                    data,
                                    chunk))
        {
            accepted = 0u;
        }
        data += chunk;
        length = (uint8_t)(length - chunk);
    }
    return accepted;
}

uint8_t EventRouter_InjectSourceEvent(uint8_t source,
                                      uint8_t kind,
                                      uint8_t flags)
{
    RouterEvent event;
    uint8_t i;

    if((kind != ROUTER_EVENT_SOURCE_UP) &&
       (kind != ROUTER_EVENT_SOURCE_DOWN) &&
       (kind != ROUTER_EVENT_SOURCE_RESYNC))
    {
        return 0u;
    }

    event.sequence = 0u;
    event.timestamp_ms = 0u;
    event.source = source;
    event.kind = kind;
    event.flags = flags;
    event.length = 0u;
    for(i = 0u; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
    {
        event.payload.raw[i] = 0u;
    }
    return EventRouter_Post(&event);
}

static uint8_t EventRouter_KeyboardStateEqual(const KeyboardState *left,
                                              const KeyboardState *right)
{
    uint8_t i;

    if((left->modifiers != right->modifiers) ||
       (left->key_count != right->key_count) ||
       (left->overflow != right->overflow))
    {
        return 0u;
    }
    for(i = 0u; i < 6u; ++i)
    {
        if(left->keycodes[i] != right->keycodes[i])
        {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t EventRouter_KeyAlreadyPresent(const KeyboardState *state,
                                             uint8_t keycode)
{
    uint8_t i;

    for(i = 0u; i < state->key_count; ++i)
    {
        if(state->keycodes[i] == keycode)
        {
            return 1u;
        }
    }
    return 0u;
}

static void EventRouter_ParseKeyboardState(const uint8_t *data,
                                           KeyboardState *state)
{
    uint8_t i;
    uint8_t keycode;

    state->modifiers = data[0];
    state->key_count = 0u;
    state->overflow = 0u;
    for(i = 0u; i < 6u; ++i)
    {
        state->keycodes[i] = 0u;
    }

    for(i = 0u; i < 6u; ++i)
    {
        keycode = data[(uint8_t)(2u + i)];
        if(keycode == 0u)
        {
            continue;
        }
        if(keycode == 0x01u)
        {
            state->overflow = 1u;
            continue;
        }
        if(EventRouter_KeyAlreadyPresent(state, keycode))
        {
            continue;
        }
        if(state->key_count >= 6u)
        {
            state->overflow = 1u;
            continue;
        }
        state->keycodes[state->key_count] = keycode;
        state->key_count++;
    }
}

static void EventRouter_RecomputeKeyboard(void)
{
    KeyboardState merged;
    InputSourceState *source;
    uint8_t source_index;
    uint8_t key_index;
    uint8_t keycode;
    uint8_t was_overflow = g_event_router.merged_keyboard.overflow;

    merged.modifiers = 0u;
    merged.key_count = 0u;
    merged.overflow = 0u;
    for(key_index = 0u; key_index < 6u; ++key_index)
    {
        merged.keycodes[key_index] = 0u;
    }

    for(source_index = 0u;
        source_index < ROUTER_SOURCE_COUNT;
        ++source_index)
    {
        source = &g_event_router.source[source_index];
        if((source->connected == 0u) || (source->keyboard_valid == 0u))
        {
            continue;
        }

        merged.modifiers |= source->keyboard.modifiers;
        if(source->keyboard.overflow != 0u)
        {
            merged.overflow = 1u;
        }
        for(key_index = 0u; key_index < source->keyboard.key_count; ++key_index)
        {
            keycode = source->keyboard.keycodes[key_index];
            if(EventRouter_KeyAlreadyPresent(&merged, keycode))
            {
                continue;
            }
            if(merged.key_count >= 6u)
            {
                merged.overflow = 1u;
                continue;
            }
            merged.keycodes[merged.key_count] = keycode;
            merged.key_count++;
        }
    }

    if((merged.overflow != 0u) && (was_overflow == 0u))
    {
        g_event_router.stats.keyboard_merge_overflow++;
    }
    if(!EventRouter_KeyboardStateEqual(&merged,
                                       &g_event_router.merged_keyboard))
    {
        g_event_router.merged_keyboard = merged;
        g_event_router.pending_keyboard_mask |=
            g_event_router.active_output_mask;
    }
}

static void EventRouter_RecomputeMouseButtons(void)
{
    uint8_t buttons = 0u;
    uint8_t source_index;
    InputSourceState *source;
    uint8_t slot;
    uint8_t output_bit;

    for(source_index = 0u;
        source_index < ROUTER_SOURCE_COUNT;
        ++source_index)
    {
        source = &g_event_router.source[source_index];
        if((source->connected != 0u) && (source->mouse_valid != 0u))
        {
            buttons |= source->mouse.buttons;
        }
    }

    if(buttons == g_event_router.merged_mouse.buttons)
    {
        return;
    }

    g_event_router.merged_mouse.buttons = buttons;
    for(slot = 0u; slot < ROUTER_OUTPUT_SLOT_COUNT; ++slot)
    {
        output_bit = EventRouter_OutputBitForSlot(slot);
        if((g_event_router.active_output_mask & output_bit) != 0u)
        {
            g_event_router.pending_mouse[slot].buttons = buttons;
            g_event_router.pending_mouse_mask |= output_bit;
        }
    }
}

static int8_t EventRouter_SaturatingAdd(int8_t current,
                                        int8_t delta)
{
    int16_t sum = (int16_t)current + (int16_t)delta;

    if(sum > 127)
    {
        g_event_router.stats.mouse_delta_saturation++;
        return 127;
    }
    if(sum < -128)
    {
        g_event_router.stats.mouse_delta_saturation++;
        return -128;
    }
    return (int8_t)sum;
}

static void EventRouter_AccumulateMouseDelta(int8_t dx,
                                             int8_t dy,
                                             int8_t wheel)
{
    uint8_t slot;
    uint8_t output_bit;

    if((dx == 0) && (dy == 0) && (wheel == 0))
    {
        return;
    }

    if(g_event_router.active_output_mask == ROUTER_OUTPUT_NONE)
    {
        g_event_router.stats.mouse_unavailable_drop++;
        return;
    }

    for(slot = 0u; slot < ROUTER_OUTPUT_SLOT_COUNT; ++slot)
    {
        output_bit = EventRouter_OutputBitForSlot(slot);
        if((g_event_router.active_output_mask & output_bit) == 0u)
        {
            continue;
        }
        g_event_router.pending_mouse[slot].buttons =
            g_event_router.merged_mouse.buttons;
        g_event_router.pending_mouse[slot].dx =
            EventRouter_SaturatingAdd(g_event_router.pending_mouse[slot].dx,
                                       dx);
        g_event_router.pending_mouse[slot].dy =
            EventRouter_SaturatingAdd(g_event_router.pending_mouse[slot].dy,
                                       dy);
        g_event_router.pending_mouse[slot].wheel =
            EventRouter_SaturatingAdd(g_event_router.pending_mouse[slot].wheel,
                                       wheel);
        g_event_router.pending_mouse_mask |= output_bit;
    }
}

static uint8_t EventRouter_GamepadEqual(const HidGamepadReport *left,
                                        const HidGamepadReport *right)
{
    uint8_t i;

    for(i = 0u; i < HID_GAMEPAD_REPORT_LEN; ++i)
    {
        if(left->bytes[i] != right->bytes[i])
        {
            return 0u;
        }
    }
    return 1u;
}

static void EventRouter_RecomputeGamepad(void)
{
    HidGamepadReport merged;
    uint8_t selected = ROUTER_SOURCE_COUNT;
    uint8_t source_index;
    uint8_t i;
    uint8_t changed;
    InputSourceState *source;

    if((g_event_router.gamepad_source < ROUTER_SOURCE_COUNT) &&
       (g_event_router.source[g_event_router.gamepad_source].connected != 0u) &&
       (g_event_router.source[g_event_router.gamepad_source].gamepad_valid != 0u))
    {
        selected = g_event_router.gamepad_source;
    }
    else
    {
        for(source_index = 0u;
            source_index < ROUTER_SOURCE_COUNT;
            ++source_index)
        {
            source = &g_event_router.source[source_index];
            if((source->connected != 0u) && (source->gamepad_valid != 0u))
            {
                selected = source_index;
                break;
            }
        }
    }

    for(i = 0u; i < HID_GAMEPAD_REPORT_LEN; ++i)
    {
        merged.bytes[i] = 0u;
    }
    if(selected < ROUTER_SOURCE_COUNT)
    {
        for(i = 0u; i < HID_GAMEPAD_REPORT_LEN; ++i)
        {
            merged.bytes[i] =
                g_event_router.source[selected].gamepad.bytes[i];
        }
    }

    changed = (g_event_router.merged_gamepad_valid !=
               ((selected < ROUTER_SOURCE_COUNT) ? 1u : 0u));
    if(!changed)
    {
        changed = !EventRouter_GamepadEqual(&merged,
                                            &g_event_router.merged_gamepad);
    }
    g_event_router.gamepad_source = selected;
    if(changed)
    {
        g_event_router.merged_gamepad = merged;
        g_event_router.merged_gamepad_valid =
            (selected < ROUTER_SOURCE_COUNT) ? 1u : 0u;
        g_event_router.pending_gamepad_mask |=
            g_event_router.active_output_mask;
    }
}

static void EventRouter_ClearSource(InputSourceState *source)
{
    EventRouter_ClearBytes((uint8_t *)source,
                           (uint16_t)sizeof(InputSourceState));
}

static void EventRouter_ApplyKeyboard(const RouterEvent *event)
{
    InputSourceState *source = &g_event_router.source[event->source];

    if(event->length != HID_KEYBOARD_REPORT_LEN)
    {
        g_event_router.stats.parser_error++;
    }
    source->connected = 1u;
    source->keyboard_valid = 1u;
    EventRouter_ParseKeyboardState(event->payload.raw,
                                   &source->keyboard);
    if((event->flags & ROUTER_FLAG_OVERFLOW) != 0u)
    {
        source->keyboard.overflow = 1u;
    }
    if((event->flags & ROUTER_FLAG_RELEASE_ALL) != 0u)
    {
        source->keyboard.modifiers = 0u;
        source->keyboard.key_count = 0u;
        source->keyboard.overflow = 0u;
        EventRouter_ClearBytes(source->keyboard.keycodes, 6u);
    }
    EventRouter_RecomputeKeyboard();
}

static void EventRouter_ApplyMouse(const RouterEvent *event)
{
    InputSourceState *source = &g_event_router.source[event->source];
    int8_t dx = (int8_t)event->payload.raw[1];
    int8_t dy = (int8_t)event->payload.raw[2];
    int8_t wheel = (int8_t)event->payload.raw[3];

    if(event->length != HID_MOUSE_REPORT_LEN)
    {
        g_event_router.stats.parser_error++;
    }
    source->connected = 1u;
    source->mouse_valid = 1u;
    source->mouse.buttons = event->payload.raw[0];
    source->mouse.dx = dx;
    source->mouse.dy = dy;
    source->mouse.wheel = wheel;
    if((event->flags & ROUTER_FLAG_RELEASE_ALL) != 0u)
    {
        source->mouse.buttons = 0u;
        source->mouse.dx = 0;
        source->mouse.dy = 0;
        source->mouse.wheel = 0;
        dx = 0;
        dy = 0;
        wheel = 0;
    }
    EventRouter_RecomputeMouseButtons();
    EventRouter_AccumulateMouseDelta(dx, dy, wheel);
}

static void EventRouter_ApplyGamepad(const RouterEvent *event)
{
    InputSourceState *source = &g_event_router.source[event->source];
    uint8_t i;

    if(event->length != HID_GAMEPAD_REPORT_LEN)
    {
        g_event_router.stats.parser_error++;
    }
    source->connected = 1u;
    source->gamepad_valid = 1u;
    for(i = 0u; i < HID_GAMEPAD_REPORT_LEN; ++i)
    {
        source->gamepad.bytes[i] = event->payload.raw[i];
    }
    if((event->flags & ROUTER_FLAG_RELEASE_ALL) != 0u)
    {
        EventRouter_ClearBytes(source->gamepad.bytes,
                               HID_GAMEPAD_REPORT_LEN);
    }
    g_event_router.gamepad_source = event->source;
    EventRouter_RecomputeGamepad();
}

static void EventRouter_ApplySourceEvent(const RouterEvent *event)
{
    InputSourceState *source = &g_event_router.source[event->source];

    if(event->kind == ROUTER_EVENT_SOURCE_UP)
    {
        EventRouter_ClearSource(source);
        if(g_event_router.gamepad_source == event->source)
        {
            g_event_router.gamepad_source = ROUTER_SOURCE_COUNT;
        }
    }
    else if(event->kind == ROUTER_EVENT_SOURCE_DOWN)
    {
        EventRouter_ClearSource(source);
        source->connected = 1u;
        if(g_event_router.gamepad_source == event->source)
        {
            g_event_router.gamepad_source = ROUTER_SOURCE_COUNT;
        }
    }
    else
    {
        source->connected = 1u;
        if((event->flags & ROUTER_FLAG_RELEASE_ALL) != 0u)
        {
            EventRouter_ClearSource(source);
            source->connected = 1u;
            if(g_event_router.gamepad_source == event->source)
            {
                g_event_router.gamepad_source = ROUTER_SOURCE_COUNT;
            }
        }
    }

    EventRouter_RecomputeKeyboard();
    EventRouter_RecomputeMouseButtons();
    EventRouter_RecomputeGamepad();
    EventRouter_MarkAllActiveHidPending();
    g_event_router.stats.resync_count++;
}

static uint8_t EventRouter_HandleControlFrame(const RouterEvent *event)
{
    uint8_t command;
    uint8_t argument;

    if((event->source != ROUTER_SRC_USB_CDC) &&
       (event->source != ROUTER_SRC_BLE_NUS))
    {
        return 0u;
    }
    if((event->length != ROUTER_CONTROL_FRAME_LEN) ||
       (event->payload.raw[0] != ROUTER_CONTROL_MAGIC_0) ||
       (event->payload.raw[1] != ROUTER_CONTROL_MAGIC_1))
    {
        return 0u;
    }

    command = event->payload.raw[2];
    argument = event->payload.raw[3];
    switch(command)
    {
        case ROUTER_CONTROL_SET_POLICY:
            if(argument > ROUTER_POLICY_NONE)
            {
                g_event_router.stats.parser_error++;
            }
            else
            {
                EventRouter_SetOutputPolicy(argument);
            }
            break;

        case ROUTER_CONTROL_SET_MASK:
            if((argument & (uint8_t)~ROUTER_OUTPUT_BOTH) != 0u)
            {
                g_event_router.stats.parser_error++;
            }
            else
            {
                EventRouter_SetOutputMask(argument);
            }
            break;

        default:
            g_event_router.stats.parser_error++;
            break;
    }
    return 1u;
}

static uint8_t EventRouter_TryPushHid(uint8_t output_bit,
                                      const HidTxFrame *frame)
{
    StaticSpscRing *ring;

    ring = (output_bit == ROUTER_OUTPUT_BLE) ?
               &g_event_router.ble_hid_tx_ring :
               &g_event_router.usb_hid_tx_ring;
    if(StaticSpscRing_IsFull(ring))
    {
        /* The pending bit retains the newest state; this is backpressure,
         * not a lost key snapshot. */
        return 0u;
    }
    if(!StaticSpscRing_Push(ring, frame))
    {
        g_event_router.stats.hid_tx_drop++;
        return 0u;
    }
    return 1u;
}

static uint8_t EventRouter_TryPushStream(uint8_t output_bit,
                                         const StreamTxFrame *frame)
{
    StaticSpscRing *ring;

    ring = (output_bit == ROUTER_OUTPUT_BLE) ?
               &g_event_router.ble_stream_tx_ring :
               &g_event_router.usb_stream_tx_ring;
    if(StaticSpscRing_IsFull(ring))
    {
        g_event_router.stats.stream_tx_drop++;
        return 0u;
    }
    if(!StaticSpscRing_Push(ring, frame))
    {
        g_event_router.stats.stream_tx_drop++;
        return 0u;
    }
    return 1u;
}

static void EventRouter_ServicePendingHid(void)
{
    HidTxFrame frame;
    uint8_t slot;
    uint8_t output_bit;
    uint8_t i;

    for(slot = 0u; slot < ROUTER_OUTPUT_SLOT_COUNT; ++slot)
    {
        output_bit = EventRouter_OutputBitForSlot(slot);
        if((g_event_router.active_output_mask & output_bit) == 0u)
        {
            continue;
        }

        if(((g_event_router.pending_keyboard_mask & output_bit) != 0u) &&
           EventRouter_IsHidReportAvailable(output_bit,
                                            ROUTER_REPORT_ID_KEYBOARD))
        {
            frame.report_id = ROUTER_REPORT_ID_KEYBOARD;
            frame.kind = ROUTER_EVENT_KEYBOARD_REPORT;
            frame.length = HID_KEYBOARD_REPORT_LEN;
            frame.flags = ROUTER_FLAG_SNAPSHOT;
            if(g_event_router.merged_keyboard.overflow != 0u)
            {
                frame.flags |= ROUTER_FLAG_OVERFLOW;
            }
            EventRouter_ClearBytes(frame.bytes, ROUTER_EVENT_PAYLOAD_LEN);
            frame.bytes[0] = g_event_router.merged_keyboard.modifiers;
            if(g_event_router.merged_keyboard.overflow != 0u)
            {
                for(i = 0u; i < 6u; ++i)
                {
                    frame.bytes[(uint8_t)(2u + i)] = 0x01u;
                }
            }
            else
            {
                for(i = 0u; i < g_event_router.merged_keyboard.key_count; ++i)
                {
                    frame.bytes[(uint8_t)(2u + i)] =
                        g_event_router.merged_keyboard.keycodes[i];
                }
            }
            if(EventRouter_TryPushHid(output_bit, &frame))
            {
                g_event_router.pending_keyboard_mask &= (uint8_t)~output_bit;
            }
        }

        if(((g_event_router.pending_mouse_mask & output_bit) != 0u) &&
           EventRouter_IsHidReportAvailable(output_bit,
                                            ROUTER_REPORT_ID_MOUSE))
        {
            frame.report_id = ROUTER_REPORT_ID_MOUSE;
            frame.kind = ROUTER_EVENT_MOUSE_REPORT;
            frame.length = HID_MOUSE_REPORT_LEN;
            frame.flags = ROUTER_FLAG_SNAPSHOT;
            EventRouter_ClearBytes(frame.bytes, ROUTER_EVENT_PAYLOAD_LEN);
            frame.bytes[0] = g_event_router.pending_mouse[slot].buttons;
            frame.bytes[1] = (uint8_t)g_event_router.pending_mouse[slot].dx;
            frame.bytes[2] = (uint8_t)g_event_router.pending_mouse[slot].dy;
            frame.bytes[3] = (uint8_t)g_event_router.pending_mouse[slot].wheel;
            if(EventRouter_TryPushHid(output_bit, &frame))
            {
                g_event_router.pending_mouse_mask &= (uint8_t)~output_bit;
                g_event_router.pending_mouse[slot].dx = 0;
                g_event_router.pending_mouse[slot].dy = 0;
                g_event_router.pending_mouse[slot].wheel = 0;
            }
        }

        if(((g_event_router.pending_gamepad_mask & output_bit) != 0u) &&
           EventRouter_IsHidReportAvailable(output_bit,
                                            ROUTER_REPORT_ID_GAMEPAD))
        {
            frame.report_id = ROUTER_REPORT_ID_GAMEPAD;
            frame.kind = ROUTER_EVENT_GAMEPAD_REPORT;
            frame.length = HID_GAMEPAD_REPORT_LEN;
            frame.flags = ROUTER_FLAG_SNAPSHOT;
            for(i = 0u; i < ROUTER_EVENT_PAYLOAD_LEN; ++i)
            {
                frame.bytes[i] = 0u;
            }
            if(g_event_router.merged_gamepad_valid != 0u)
            {
                for(i = 0u; i < HID_GAMEPAD_REPORT_LEN; ++i)
                {
                    frame.bytes[i] = g_event_router.merged_gamepad.bytes[i];
                }
            }
            if(EventRouter_TryPushHid(output_bit, &frame))
            {
                g_event_router.pending_gamepad_mask &= (uint8_t)~output_bit;
            }
        }
    }
}

static void EventRouter_QueueStream(const RouterEvent *event)
{
    StreamTxFrame frame;
    uint8_t output_bit;
    uint8_t i;

    frame.length = EventRouter_CopyPayload(frame.bytes,
                                           event->payload.raw,
                                           event->length,
                                           STREAM_CHUNK_MAX_LEN);
    frame.flags = event->flags;
    frame.reserved = 0u;
    for(i = frame.length; i < STREAM_CHUNK_MAX_LEN; ++i)
    {
        frame.bytes[i] = 0u;
    }

    if(g_event_router.active_stream_output_mask == ROUTER_OUTPUT_NONE)
    {
        g_event_router.stats.stream_unavailable_drop++;
        return;
    }

    for(output_bit = ROUTER_OUTPUT_USB;
        output_bit <= ROUTER_OUTPUT_BLE;
        output_bit = (uint8_t)(output_bit << 1))
    {
        if((g_event_router.active_stream_output_mask & output_bit) != 0u)
        {
            (void)EventRouter_TryPushStream(output_bit, &frame);
        }
    }
}

void EventRouter_Process(void)
{
    RouterEvent event;
    uint8_t processed = 0u;

    while((processed < ROUTER_PROCESS_BUDGET) &&
          StaticSpscRing_Pop(&g_event_router.input_ring, &event))
    {
        switch(event.kind)
        {
            case ROUTER_EVENT_KEYBOARD_REPORT:
                EventRouter_ApplyKeyboard(&event);
                break;

            case ROUTER_EVENT_MOUSE_REPORT:
                EventRouter_ApplyMouse(&event);
                break;

            case ROUTER_EVENT_GAMEPAD_REPORT:
                EventRouter_ApplyGamepad(&event);
                break;

            case ROUTER_EVENT_STREAM_DATA:
                if(!EventRouter_HandleControlFrame(&event))
                {
                    EventRouter_QueueStream(&event);
                }
                break;

            case ROUTER_EVENT_SOURCE_RESYNC:
            case ROUTER_EVENT_SOURCE_UP:
            case ROUTER_EVENT_SOURCE_DOWN:
                EventRouter_ApplySourceEvent(&event);
                break;

            default:
                g_event_router.stats.parser_error++;
                break;
        }
        ++processed;
    }

    EventRouter_ServicePendingHid();
    g_event_router.pending_events = StaticSpscRing_Count(
        &g_event_router.input_ring);
}

uint8_t EventRouter_DequeueUsbHidFrame(HidTxFrame *frame)
{
    return StaticSpscRing_Pop(&g_event_router.usb_hid_tx_ring, frame);
}

uint8_t EventRouter_DequeueUsbStreamFrame(StreamTxFrame *frame)
{
    return StaticSpscRing_Pop(&g_event_router.usb_stream_tx_ring, frame);
}

uint8_t EventRouter_DequeueBleHidFrame(HidTxFrame *frame)
{
    return StaticSpscRing_Pop(&g_event_router.ble_hid_tx_ring, frame);
}

uint8_t EventRouter_DequeueBleStreamFrame(StreamTxFrame *frame)
{
    return StaticSpscRing_Pop(&g_event_router.ble_stream_tx_ring, frame);
}

uint8_t EventRouter_PeekBleHidFrame(HidTxFrame *frame)
{
    return StaticSpscRing_Peek(&g_event_router.ble_hid_tx_ring, frame);
}

uint8_t EventRouter_PeekBleStreamFrame(StreamTxFrame *frame)
{
    return StaticSpscRing_Peek(&g_event_router.ble_stream_tx_ring, frame);
}

Event_Router *EventRouter_GetContext(void)
{
    return &g_event_router;
}
