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

void sm64_saturn_texture_residency_init(
    sm64_saturn_texture_residency_t *residency,
    const vdp1_vram_partitions_t *partitions);

void sm64_saturn_texture_residency_init_region(
    sm64_saturn_texture_residency_t *residency,
    void *base, size_t capacity);

bool sm64_saturn_texture_residency_upload(
    sm64_saturn_texture_residency_t *residency, size_t offset,
    const void *source, size_t bytes);

#endif
