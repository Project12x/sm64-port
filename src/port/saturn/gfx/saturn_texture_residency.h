#ifndef SM64_SATURN_TEXTURE_RESIDENCY_H
#define SM64_SATURN_TEXTURE_RESIDENCY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yaul.h>

/* Bounded VDP1 texture-bank destination shared by diagnostic and game
 * clients. Source policy (linked data, disc staging, or 4 MiB cart) remains
 * above this layer; every route terminates in the same checked SCU transfer. */
typedef struct sm64_saturn_texture_residency {
    uint8_t *base;
    size_t capacity;
    size_t used;
    size_t peak;
    bool overflowed;
} sm64_saturn_texture_residency_t;

static inline void
sm64_saturn_texture_residency_init(
    sm64_saturn_texture_residency_t *residency,
    const vdp1_vram_partitions_t *partitions)
{
    residency->base = (uint8_t *)partitions->texture_base;
    residency->capacity = partitions->texture_size;
    residency->used = 0;
    residency->peak = 0;
    residency->overflowed = false;
}

static inline bool
sm64_saturn_texture_residency_upload(
    sm64_saturn_texture_residency_t *residency, size_t offset,
    const void *source, size_t bytes)
{
    if (source == NULL || offset > residency->capacity ||
        bytes > residency->capacity - offset) {
        residency->overflowed = true;
        return false;
    }

    scu_dma_transfer(0, residency->base + offset, source, bytes);
    scu_dma_transfer_wait(0);
    const size_t end = offset + bytes;
    if (end > residency->used)
        residency->used = end;
    if (residency->used > residency->peak)
        residency->peak = residency->used;
    return true;
}

#endif
