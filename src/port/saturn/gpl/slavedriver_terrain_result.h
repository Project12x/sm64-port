/*
 * GPL-3.0-or-later. Close-port of the bounded SlaveDriver-Engine worker
 * result discipline from WALLS.C:1240-1408 at commit
 * a8986591557b6e680550d3c23970284d3b38ff8f.
 *
 * This adaptation carries compact project-owned primitive/command identities
 * rather than SlaveDriver's sector/tile pointers. The worker may reserve and
 * fill only its caller-owned descriptor and command spans; texture residency,
 * Gouraud allocation, final command concatenation, and VDP1 remain
 * master-owned.
 */
#ifndef SM64_SATURN_SLAVEDRIVER_TERRAIN_RESULT_H
#define SM64_SATURN_SLAVEDRIVER_TERRAIN_RESULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__sh__)
#include <cpu/cache.h>
#include <yaul/scu/map.h>
#endif

#define SM64_SATURN_TERRAIN_RESULT_CORNERS 4U
#define SM64_SATURN_TERRAIN_COMMAND_BYTES 32U
#define SM64_SATURN_TERRAIN_RESULT_TEMPLATE_PATCHED 0x80U
#define SM64_SATURN_TERRAIN_RESULT_CORNER_COUNT_MASK 0x7FU

typedef enum sm64_saturn_terrain_result_flags {
    SM64_SATURN_TERRAIN_RESULT_OPAQUE = 1U << 0,
    SM64_SATURN_TERRAIN_RESULT_TEXTURED = 1U << 1,
    SM64_SATURN_TERRAIN_RESULT_GOURAUD = 1U << 2,
    SM64_SATURN_TERRAIN_RESULT_CLIPPED = 1U << 3,
    SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL = 1U << 4,
    SM64_SATURN_TERRAIN_RESULT_LEAF_END = 1U << 5,
    /* A material may remain geometrically present while its 32x32 texture
     * is intentionally downgraded for a mid/far LOD.  The master emitter
     * must honor this bit rather than consulting the source primitive alone.
     */
    SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED = 1U << 6
} sm64_saturn_terrain_result_flags_t;

/* The worker publishes identity and ordering only. Screen coordinates already
 * live in its private 32-byte command image; material, texture, flags, and
 * colors remain in the immutable primitive/template banks. The high bit of
 * corner_count records whether immutable command state was patched, leaving
 * the descriptor at twelve bytes without a parallel validity stream. */
typedef struct sm64_saturn_visible_terrain {
    uint16_t primitive_id;
    uint16_t command_index;
    uint32_t painter_key;
    uint16_t bsp_leaf;
    uint8_t corner_count;
    uint8_t clip_class;
} sm64_saturn_visible_terrain_t;

typedef sm64_saturn_visible_terrain_t sm64_saturn_terrain_result_t;

typedef struct sm64_saturn_terrain_result_arena {
    sm64_saturn_terrain_result_t *records;
    uint8_t *commands;
    uint16_t capacity;
    uint16_t count;
    uint16_t peak;
    uint16_t headroom;
    uint32_t reserve_rejects;
    uint32_t malformed_rejects;
    volatile uint32_t published_sequence;
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

static inline uint8_t *sm64_saturn_terrain_result_commands(
    uint8_t commands[][SM64_SATURN_TERRAIN_COMMAND_BYTES])
{
    return commands == NULL ? NULL : &commands[0][0];
}

static inline const uint8_t *sm64_saturn_terrain_result_uncached_commands(
    const uint8_t *commands)
{
#if defined(__sh__)
    const uintptr_t physical =
        ((uintptr_t)commands & ~((uintptr_t)CPU_ADDRESS_PARTITION_MASK)) -
        LWRAM(0);
    return (const uint8_t *)LWRAM_UNCACHED(physical);
#else
    return commands;
#endif
}

_Static_assert(sizeof(sm64_saturn_terrain_result_t) == 12U,
               "visible terrain descriptor must remain twelve bytes");
_Static_assert(offsetof(sm64_saturn_terrain_result_t, painter_key) % 4U == 0U,
               "painter key must be naturally aligned");

static inline void sm64_saturn_terrain_result_arena_init(
    sm64_saturn_terrain_result_arena_t *arena,
    sm64_saturn_terrain_result_t *records,
    uint8_t *commands,
    uint16_t capacity,
    uint16_t headroom)
{
    if (arena == NULL) return;
    arena->records = records;
    arena->commands = commands;
    arena->capacity = capacity;
    arena->count = 0U;
    arena->peak = 0U;
    arena->headroom = headroom > capacity ? capacity : headroom;
    arena->reserve_rejects = 0U;
    arena->malformed_rejects = 0U;
    arena->published_sequence = 0U;
}

static inline void sm64_saturn_terrain_result_arena_reset(
    sm64_saturn_terrain_result_arena_t *arena)
{
    if (arena == NULL) return;
    arena->count = 0U;
    arena->peak = 0U;
    arena->reserve_rejects = 0U;
    arena->malformed_rejects = 0U;
    arena->published_sequence = 0U;
}

/* Reserve the complete output count before any clipped fan writes. The
 * headroom matches SlaveDriver's +50 safety margin: callers may use it to
 * preserve a bounded fallback marker or future material recovery result. */
static inline bool sm64_saturn_terrain_result_reserve(
    sm64_saturn_terrain_result_arena_t *arena,
    uint16_t requested,
    sm64_saturn_terrain_result_t **out,
    uint8_t **out_commands)
{
    if (out != NULL) *out = NULL;
    if (out_commands != NULL) *out_commands = NULL;
    if (arena == NULL || requested == 0U || arena->records == NULL ||
        arena->commands == NULL ||
        requested > (uint16_t)(arena->capacity - arena->headroom) ||
        arena->count > (uint16_t)(arena->capacity - arena->headroom) - requested) {
        if (arena != NULL) arena->reserve_rejects++;
        return false;
    }
    sm64_saturn_terrain_result_t *slot = &arena->records[arena->count];
    arena->count = (uint16_t)(arena->count + requested);
    if (arena->count > arena->peak) arena->peak = arena->count;
    if (out != NULL) *out = slot;
    if (out_commands != NULL)
        *out_commands = arena->commands +
            (size_t)(arena->count - requested) *
                SM64_SATURN_TERRAIN_COMMAND_BYTES;
    return true;
}

static inline bool sm64_saturn_terrain_result_validate(
    const sm64_saturn_terrain_result_t *result)
{
    return result != NULL &&
           (result->corner_count &
            SM64_SATURN_TERRAIN_RESULT_CORNER_COUNT_MASK) >= 3U &&
           (result->corner_count &
            SM64_SATURN_TERRAIN_RESULT_CORNER_COUNT_MASK) <=
               SM64_SATURN_TERRAIN_RESULT_CORNERS;
}

static inline uint8_t sm64_saturn_terrain_result_corner_count(
    const sm64_saturn_terrain_result_t *result)
{
    return result == NULL ? 0U :
        (uint8_t)(result->corner_count &
                  SM64_SATURN_TERRAIN_RESULT_CORNER_COUNT_MASK);
}

static inline bool sm64_saturn_terrain_result_template_patched(
    const sm64_saturn_terrain_result_t *result)
{
    return result != NULL &&
        (result->corner_count &
         SM64_SATURN_TERRAIN_RESULT_TEMPLATE_PATCHED) != 0U;
}

static inline uint8_t sm64_saturn_terrain_result_clip_class(
    const sm64_saturn_terrain_result_t *result)
{
    return result == NULL ? 0U :
        (uint8_t)(result->clip_class &
            ~(SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL |
              SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED));
}

static inline bool sm64_saturn_terrain_result_recovery(
    const sm64_saturn_terrain_result_t *result)
{
    return result != NULL &&
        (result->clip_class &
         SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL) != 0U;
}

static inline bool sm64_saturn_terrain_result_texture_suppressed(
    const sm64_saturn_terrain_result_t *result)
{
    return result != NULL &&
        (result->clip_class &
         SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED) != 0U;
}

static inline void sm64_saturn_terrain_result_arena_seal(
    sm64_saturn_terrain_result_arena_t *arena, uint32_t sequence)
{
    if (arena != NULL) {
#if defined(__GNUC__)
        __asm__ volatile("" ::: "memory");
#endif
        arena->published_sequence = sequence;
    }
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
    uint8_t *master_commands,
    uint16_t master_capacity,
    sm64_saturn_terrain_result_t *slave_records,
    uint8_t *slave_commands,
    uint16_t slave_capacity,
    uint16_t headroom)
{
    if (spans == NULL) return;
    sm64_saturn_terrain_result_arena_init(
        &spans->master, master_records, master_commands,
        master_capacity, headroom);
    sm64_saturn_terrain_result_arena_init(
        &spans->slave, slave_records, slave_commands,
        slave_capacity, headroom);
}

#endif
