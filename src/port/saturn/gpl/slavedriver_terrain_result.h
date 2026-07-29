/*
 * GPL-3.0-or-later. Close-port of the bounded SlaveDriver-Engine worker
 * result discipline from WALLS.C:1240-1408 at commit
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 *
 * This adaptation deliberately carries project-owned projected vertices and
 * material identities rather than SlaveDriver's sector/tile pointers. The
 * worker may reserve and fill only its caller-owned result span; VDP1 command,
 * texture, and Gouraud allocators remain master-owned.
 */
#ifndef SM64_SATURN_SLAVEDRIVER_TERRAIN_RESULT_H
#define SM64_SATURN_SLAVEDRIVER_TERRAIN_RESULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../gfx/saturn_projected_workarea.h"

#if defined(__sh__)
#include <yaul/scu/map.h>
#endif

#define SM64_SATURN_TERRAIN_RESULT_CORNERS 4U

typedef enum sm64_saturn_terrain_result_flags {
    SM64_SATURN_TERRAIN_RESULT_OPAQUE = 1U << 0,
    SM64_SATURN_TERRAIN_RESULT_TEXTURED = 1U << 1,
    SM64_SATURN_TERRAIN_RESULT_GOURAUD = 1U << 2,
    SM64_SATURN_TERRAIN_RESULT_CLIPPED = 1U << 3,
    SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL = 1U << 4,
    SM64_SATURN_TERRAIN_RESULT_LEAF_END = 1U << 5
} sm64_saturn_terrain_result_flags_t;

typedef struct sm64_saturn_terrain_result {
    sm64_saturn_projected_vertex_t corners[SM64_SATURN_TERRAIN_RESULT_CORNERS];
    uint16_t gouraud[SM64_SATURN_TERRAIN_RESULT_CORNERS];
    uint16_t primitive_id;
    uint16_t leaf_id;
    uint16_t material_id;
    uint16_t texture_slot;
    uint32_t painter_key;
    uint16_t flags;
    uint8_t corner_count;
    uint8_t clip_class;
} sm64_saturn_terrain_result_t;

typedef struct sm64_saturn_terrain_result_arena {
    sm64_saturn_terrain_result_t *records;
    uint16_t capacity;
    uint16_t count;
    uint16_t peak;
    uint16_t headroom;
    uint32_t reserve_rejects;
    uint32_t malformed_rejects;
} sm64_saturn_terrain_result_arena_t;

typedef struct sm64_saturn_terrain_result_spans {
    sm64_saturn_terrain_result_arena_t master;
    sm64_saturn_terrain_result_arena_t slave;
} sm64_saturn_terrain_result_spans_t;

/* The two result arenas live in the sourceboot LWRAM work area because a
 * complete compact stream is larger than the remaining HWRAM margin.  Keep
 * producer writes on the normal alias and use the uncached alias only at the
 * single join before the master copies the disjoint spans into its cacheable
 * merge stream.  This is the explicit cache-through boundary required by
 * SlaveDriver's worker-result contract without paying uncached traffic for
 * every record. */
static inline sm64_saturn_terrain_result_t *
sm64_saturn_terrain_result_records(
    sm64_saturn_terrain_result_t *records)
{
    return records;
}

static inline const sm64_saturn_terrain_result_t *
sm64_saturn_terrain_result_uncached_records(
    const sm64_saturn_terrain_result_t *records)
{
#if defined(__sh__)
    const uintptr_t physical =
        ((uintptr_t)records & ~((uintptr_t)CPU_ADDRESS_PARTITION_MASK)) -
        LWRAM(0);
    return (const sm64_saturn_terrain_result_t *)LWRAM_UNCACHED(physical);
#else
    return records;
#endif
}

_Static_assert(sizeof(sm64_saturn_terrain_result_t) <= 64U,
               "terrain result must remain a compact bounded record");
_Static_assert(offsetof(sm64_saturn_terrain_result_t, painter_key) % 4U == 0U,
               "painter key must be naturally aligned");

static inline void sm64_saturn_terrain_result_arena_init(
    sm64_saturn_terrain_result_arena_t *arena,
    sm64_saturn_terrain_result_t *records,
    uint16_t capacity,
    uint16_t headroom)
{
    if (arena == NULL) return;
    arena->records = records;
    arena->capacity = capacity;
    arena->count = 0U;
    arena->peak = 0U;
    arena->headroom = headroom > capacity ? capacity : headroom;
    arena->reserve_rejects = 0U;
    arena->malformed_rejects = 0U;
}

static inline void sm64_saturn_terrain_result_arena_reset(
    sm64_saturn_terrain_result_arena_t *arena)
{
    if (arena == NULL) return;
    arena->count = 0U;
    arena->peak = 0U;
    arena->reserve_rejects = 0U;
    arena->malformed_rejects = 0U;
}

/* Reserve the complete output count before any clipped fan writes. The
 * headroom matches SlaveDriver's +50 safety margin: callers may use it to
 * preserve a bounded fallback marker or future material recovery result. */
static inline bool sm64_saturn_terrain_result_reserve(
    sm64_saturn_terrain_result_arena_t *arena,
    uint16_t requested,
    sm64_saturn_terrain_result_t **out)
{
    if (out != NULL) *out = NULL;
    if (arena == NULL || requested == 0U || arena->records == NULL ||
        requested > (uint16_t)(arena->capacity - arena->headroom) ||
        arena->count > (uint16_t)(arena->capacity - arena->headroom) - requested) {
        if (arena != NULL) arena->reserve_rejects++;
        return false;
    }
    sm64_saturn_terrain_result_t *slot = &arena->records[arena->count];
    arena->count = (uint16_t)(arena->count + requested);
    if (arena->count > arena->peak) arena->peak = arena->count;
    if (out != NULL) *out = slot;
    return true;
}

static inline bool sm64_saturn_terrain_result_validate(
    const sm64_saturn_terrain_result_t *result)
{
    return result != NULL &&
           result->corner_count >= 3U &&
           result->corner_count <= SM64_SATURN_TERRAIN_RESULT_CORNERS;
}

static inline bool sm64_saturn_terrain_result_commit(
    sm64_saturn_terrain_result_arena_t *arena,
    sm64_saturn_terrain_result_t *result)
{
    if (!sm64_saturn_terrain_result_validate(result)) {
        if (arena != NULL) arena->malformed_rejects++;
        return false;
    }
    return true;
}

static inline void sm64_saturn_terrain_result_spans_init(
    sm64_saturn_terrain_result_spans_t *spans,
    sm64_saturn_terrain_result_t *master_records,
    uint16_t master_capacity,
    sm64_saturn_terrain_result_t *slave_records,
    uint16_t slave_capacity,
    uint16_t headroom)
{
    if (spans == NULL) return;
    sm64_saturn_terrain_result_arena_init(
        &spans->master, master_records, master_capacity, headroom);
    sm64_saturn_terrain_result_arena_init(
        &spans->slave, slave_records, slave_capacity, headroom);
}

#endif
