#ifndef TRI_MODE_HID_HUB_EVENT_ROUTER_H
#define TRI_MODE_HID_HUB_EVENT_ROUTER_H

#include "event_router_types.h"

void EventRouter_Init(void);
void EventRouter_Process(void);
void EventRouter_SetOutputMask(uint8_t output_mask);
void EventRouter_SetOutputPolicy(uint8_t output_policy);
void EventRouter_SetOutputAvailability(uint8_t available_mask);
void EventRouter_SetHidReportAvailability(uint8_t usb_report_mask,
                                          uint8_t ble_report_mask);
void EventRouter_SetStreamAvailability(uint8_t available_mask);
uint8_t EventRouter_GetActiveOutputMask(void);
uint8_t EventRouter_GetActiveStreamOutputMask(void);
uint8_t EventRouter_GetOutputAvailability(void);

uint8_t EventRouter_Post(const RouterEvent *event);

uint8_t EventRouter_InjectKeyboardReport(uint8_t source,
                                         const HidKeyboardReport *report);
uint8_t EventRouter_InjectMouseReport(uint8_t source,
                                      const HidMouseReport *report);
uint8_t EventRouter_InjectGamepadReport(uint8_t source,
                                        const HidGamepadReport *report);
uint8_t EventRouter_InjectStreamData(uint8_t source,
                                     const uint8_t *data,
                                     uint8_t length);
uint8_t EventRouter_InjectSourceEvent(uint8_t source,
                                      uint8_t kind,
                                      uint8_t flags);

uint8_t EventRouter_DequeueUsbHidFrame(HidTxFrame *frame);
uint8_t EventRouter_DequeueUsbStreamFrame(StreamTxFrame *frame);
uint8_t EventRouter_DequeueBleHidFrame(HidTxFrame *frame);
uint8_t EventRouter_DequeueBleStreamFrame(StreamTxFrame *frame);
uint8_t EventRouter_PeekBleHidFrame(HidTxFrame *frame);
uint8_t EventRouter_PeekBleStreamFrame(StreamTxFrame *frame);

Event_Router *EventRouter_GetContext(void);

#endif /* TRI_MODE_HID_HUB_EVENT_ROUTER_H */
