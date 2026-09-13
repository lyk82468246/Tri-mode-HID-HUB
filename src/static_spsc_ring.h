#ifndef TRI_MODE_HID_HUB_STATIC_SPSC_RING_H
#define TRI_MODE_HID_HUB_STATIC_SPSC_RING_H

#include <stdint.h>

/*
 * A fixed-capacity single-producer/single-consumer ring.
 *
 * The producer owns head and the consumer owns tail.  The implementation
 * deliberately stores values, rather than pointers into DMA or endpoint
 * memory, so an ISR can publish a complete item atomically from the point of
 * view of the consumer.
 */
typedef struct
{
    uint8_t *storage;
    uint16_t item_size;
    uint16_t capacity;
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t dropped;
} StaticSpscRing;

void StaticSpscRing_Init(StaticSpscRing *ring,
                         void *storage,
                         uint16_t item_size,
                         uint16_t capacity);

uint8_t StaticSpscRing_Push(StaticSpscRing *ring, const void *item);
uint8_t StaticSpscRing_Pop(StaticSpscRing *ring, void *item);
uint16_t StaticSpscRing_Count(const StaticSpscRing *ring);
uint8_t StaticSpscRing_IsEmpty(const StaticSpscRing *ring);
uint8_t StaticSpscRing_IsFull(const StaticSpscRing *ring);

#endif /* TRI_MODE_HID_HUB_STATIC_SPSC_RING_H */
