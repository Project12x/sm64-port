#ifndef SM64_SATURN_RENDER_QUEUE_H
#define SM64_SATURN_RENDER_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum sm64_saturn_render_kind {
    SM64_SATURN_RENDER_WORLD = 0,
    SM64_SATURN_RENDER_ACTOR = 1,
    SM64_SATURN_RENDER_EFFECT = 2
} sm64_saturn_render_kind_t;

typedef enum sm64_saturn_render_pass {
    SM64_SATURN_PASS_OPAQUE = 0,
    SM64_SATURN_PASS_DECAL = 1,
    SM64_SATURN_PASS_TRANSLUCENT = 2
} sm64_saturn_render_pass_t;

/* Source identity and generated-IR identity deliberately travel together.
 * The backend consumes lowered_index; diagnostics and reference comparisons
 * retain source_bank/source_primitive. */
typedef struct sm64_saturn_render_item {
    int32_t depth_key;
    uint16_t source_bank;
    uint16_t source_primitive;
    uint16_t lowered_index;
    uint8_t kind;
    uint8_t pass;
} sm64_saturn_render_item_t;

typedef struct sm64_saturn_render_queue {
    sm64_saturn_render_item_t *items;
    uint16_t *order;
    uint16_t capacity;
    uint16_t count;
    uint16_t opaque_count;
    bool overflowed;
} sm64_saturn_render_queue_t;

static inline void
sm64_saturn_render_queue_init(sm64_saturn_render_queue_t *queue,
                              sm64_saturn_render_item_t *items,
                              uint16_t *order, uint16_t capacity)
{
    queue->items = items;
    queue->order = order;
    queue->capacity = capacity;
    queue->count = 0;
    queue->opaque_count = 0;
    queue->overflowed = false;
}

static inline void
sm64_saturn_render_queue_reset(sm64_saturn_render_queue_t *queue)
{
    queue->count = 0;
    queue->opaque_count = 0;
    queue->overflowed = false;
}

static inline bool
sm64_saturn_render_queue_push(sm64_saturn_render_queue_t *queue,
                              sm64_saturn_render_item_t item)
{
    if (queue->count >= queue->capacity) {
        queue->overflowed = true;
        return false;
    }
    const uint16_t slot = queue->count;
    queue->items[slot] = item;
    queue->order[slot] = slot;
    queue->count++;
    return true;
}

#endif
