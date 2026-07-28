/* See ztreme_hot_promotion.h for the GPL source and change notice. */
#include "ztreme_hot_promotion.h"

#include <string.h>

static size_t align_up(size_t value, size_t alignment) {
    const size_t mask = alignment - 1U;
    return (value + mask) & ~mask;
}

void saturn_hot_promotion_init(saturn_hot_promotion_t *promotion,
                               void *destination, size_t capacity) {
    promotion->destination = (uint8_t *)destination;
    promotion->capacity = capacity;
    promotion->used = 0U;
}

void *saturn_hot_promote(saturn_hot_promotion_t *promotion,
                         const void *source, size_t size, size_t alignment) {
    size_t offset;
    void *destination;

    if (promotion == NULL || source == NULL || size == 0U ||
        alignment == 0U || (alignment & (alignment - 1U)) != 0U)
        return NULL;
    offset = align_up(promotion->used, alignment);
    if (offset > promotion->capacity || size > promotion->capacity - offset)
        return NULL;
    destination = promotion->destination + offset;
    /* LWRAM is the cold source; only this bounded copy enters the hot arena.
     * The frame loop receives the returned HWRAM pointer and never follows
     * the source pointer after promotion. */
    memcpy(destination, source, size);
    promotion->used = offset + size;
    return destination;
}

size_t saturn_hot_promotion_used(const saturn_hot_promotion_t *promotion) {
    return promotion == NULL ? 0U : promotion->used;
}
