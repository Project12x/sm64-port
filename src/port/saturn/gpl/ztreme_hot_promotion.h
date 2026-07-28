/*
 * GPL-3.0. Close-port of the hot vertex promotion pattern in
 * Maxime-XL2/SONIC-Z-TREME, cff75451, Projects/SONIC Z-TREME/ZTE/
 * ZT_LOADING.c:320-353.
 *
 * Material change: the Saturn port exposes a bounded offset-free bank API
 * instead of mutating SGL mesh globals; callers own the LWRAM source and the
 * HWRAM destination arena. See docs/saturn/PROVENANCE.md.
 */
#ifndef SM64_SATURN_ZTREME_HOT_PROMOTION_H
#define SM64_SATURN_ZTREME_HOT_PROMOTION_H

#include <stddef.h>
#include <stdint.h>

typedef struct saturn_hot_promotion {
    uint8_t *destination;
    size_t capacity;
    size_t used;
} saturn_hot_promotion_t;

void saturn_hot_promotion_init(saturn_hot_promotion_t *promotion,
                               void *destination, size_t capacity);
void *saturn_hot_promote(saturn_hot_promotion_t *promotion,
                         const void *source, size_t size, size_t alignment);
size_t saturn_hot_promotion_used(const saturn_hot_promotion_t *promotion);

#endif
