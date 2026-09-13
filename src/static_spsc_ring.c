#include "static_spsc_ring.h"

static void StaticSpscRing_Copy(uint8_t *dst, const uint8_t *src, uint16_t len)
{
    uint16_t i;

    for(i = 0; i < len; ++i)
    {
        dst[i] = src[i];
    }
}

static void StaticSpscRing_MemoryBarrier(void)
{
    /* GCC emits a RISC-V fence for this builtin on the CH58x toolchain. */
    __sync_synchronize();
}

void StaticSpscRing_Init(StaticSpscRing *ring,
                         void *storage,
                         uint16_t item_size,
                         uint16_t capacity)
{
    ring->storage = (uint8_t *)storage;
    ring->item_size = item_size;
    ring->capacity = capacity;
    ring->head = 0;
    ring->tail = 0;
    ring->dropped = 0;
}

uint8_t StaticSpscRing_Push(StaticSpscRing *ring, const void *item)
{
    uint16_t head;
    uint16_t tail;
    uint16_t index;

    head = ring->head;
    tail = ring->tail;
    if((uint16_t)(head - tail) >= ring->capacity)
    {
        ring->dropped++;
        return 0;
    }

    /* All project capacities are powers of two; this avoids division. */
    index = (uint16_t)(head & (ring->capacity - 1u));
    StaticSpscRing_Copy(&ring->storage[index * ring->item_size],
                       (const uint8_t *)item,
                       ring->item_size);
    StaticSpscRing_MemoryBarrier();
    ring->head = (uint16_t)(head + 1u);
    return 1;
}

uint8_t StaticSpscRing_Pop(StaticSpscRing *ring, void *item)
{
    uint16_t head;
    uint16_t tail;
    uint16_t index;

    tail = ring->tail;
    head = ring->head;
    if(tail == head)
    {
        return 0;
    }

    index = (uint16_t)(tail & (ring->capacity - 1u));
    StaticSpscRing_MemoryBarrier();
    StaticSpscRing_Copy((uint8_t *)item,
                       &ring->storage[index * ring->item_size],
                       ring->item_size);
    StaticSpscRing_MemoryBarrier();
    ring->tail = (uint16_t)(tail + 1u);
    return 1;
}

uint8_t StaticSpscRing_Peek(const StaticSpscRing *ring, void *item)
{
    uint16_t tail;
    uint16_t index;
    uint8_t *source;

    tail = ring->tail;
    if(tail == ring->head)
    {
        return 0;
    }
    index = (uint16_t)(tail & (ring->capacity - 1u));
    StaticSpscRing_MemoryBarrier();
    source = &ring->storage[(uint32_t)index * ring->item_size];
    StaticSpscRing_Copy((uint8_t *)item, source, ring->item_size);
    return 1;
}

void StaticSpscRing_Clear(StaticSpscRing *ring)
{
    /* Only the consumer calls Clear.  Publishing tail makes all currently
     * queued values disposable while preserving the producer's head. */
    StaticSpscRing_MemoryBarrier();
    ring->tail = ring->head;
    StaticSpscRing_MemoryBarrier();
}

uint16_t StaticSpscRing_Count(const StaticSpscRing *ring)
{
    return (uint16_t)(ring->head - ring->tail);
}

uint8_t StaticSpscRing_IsEmpty(const StaticSpscRing *ring)
{
    return (ring->head == ring->tail) ? 1u : 0u;
}

uint8_t StaticSpscRing_IsFull(const StaticSpscRing *ring)
{
    return (StaticSpscRing_Count(ring) >= ring->capacity) ? 1u : 0u;
}
