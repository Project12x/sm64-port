#ifndef SM64_SATURN_MEMORY_ARENA_H
#define SM64_SATURN_MEMORY_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A bounded caller-owned arena for explicitly assigned Saturn memory tiers.
 * It never allocates backing memory and never falls back to another tier. */
typedef struct sm64_saturn_memory_arena {
    uint8_t *base;
    size_t capacity;
    size_t used;
    size_t peak;
    bool overflowed;
} sm64_saturn_memory_arena_t;

static inline void
sm64_saturn_memory_arena_init(sm64_saturn_memory_arena_t *arena,
                              void *base, size_t capacity)
{
    arena->base = (uint8_t *)base;
    arena->capacity = capacity;
    arena->used = 0;
    arena->peak = 0;
    arena->overflowed = false;
}

static inline void
sm64_saturn_memory_arena_reset(sm64_saturn_memory_arena_t *arena)
{
    arena->used = 0;
    arena->overflowed = false;
}

static inline void *
sm64_saturn_memory_arena_alloc(sm64_saturn_memory_arena_t *arena,
                               size_t size, size_t alignment)
{
    if (arena->base == NULL || alignment == 0 ||
        (alignment & (alignment - 1U)) != 0) {
        arena->overflowed = true;
        return NULL;
    }

    const size_t mask = alignment - 1U;
    if (arena->used > SIZE_MAX - mask) {
        arena->overflowed = true;
        return NULL;
    }
    const size_t aligned = (arena->used + mask) & ~mask;
    if (aligned > arena->capacity || size > arena->capacity - aligned) {
        arena->overflowed = true;
        return NULL;
    }

    void *const result = &arena->base[aligned];
    arena->used = aligned + size;
    if (arena->used > arena->peak) {
        arena->peak = arena->used;
    }
    return result;
}

#endif
