#ifndef SM64_SATURN_COMMAND_ARENA_H
#define SM64_SATURN_COMMAND_ARENA_H

#include <stdbool.h>
#include <stdint.h>

/* Bounded, scene-neutral ownership for a variable-length VDP1 command list.
 * The target backend owns command encoding; this record owns indices and
 * guarantees that one slot is always retained for the terminating command. */
typedef struct sm64_saturn_command_arena {
    uint16_t capacity;
    uint16_t setup_count;
    uint16_t cursor;
    uint16_t previous_end;
    uint16_t live_count;
    uint16_t peak;
    bool overflowed;
} sm64_saturn_command_arena_t;

static inline void
sm64_saturn_command_arena_init(sm64_saturn_command_arena_t *arena,
                               uint16_t capacity, uint16_t setup_count)
{
    arena->capacity = capacity;
    arena->setup_count = setup_count;
    arena->cursor = setup_count;
    arena->previous_end = setup_count;
    arena->live_count = setup_count + 1U;
    arena->peak = arena->live_count;
    arena->overflowed = capacity <= setup_count;
}

/* Returns the prior END index so the backend can clear exactly that command. */
static inline uint16_t
sm64_saturn_command_arena_begin(sm64_saturn_command_arena_t *arena)
{
    const uint16_t previous_end = arena->previous_end;
    arena->cursor = arena->setup_count;
    arena->live_count = arena->setup_count + 1U;
    arena->overflowed = arena->capacity <= arena->setup_count;
    return previous_end;
}

/* Reserve encoded draw commands while preserving one final END slot. */
static inline bool
sm64_saturn_command_arena_reserve(sm64_saturn_command_arena_t *arena,
                                  uint16_t count, uint16_t *first)
{
    const uint32_t end = (uint32_t)arena->cursor + count;
    if (end >= arena->capacity) {
        arena->overflowed = true;
        return false;
    }
    *first = arena->cursor;
    arena->cursor = (uint16_t)end;
    return true;
}

/* Draw slots still available while preserving the final END command. */
static inline uint16_t
sm64_saturn_command_arena_available(
    const sm64_saturn_command_arena_t *arena)
{
    if (arena->cursor >= arena->capacity)
        return 0U;
    return (uint16_t)(arena->capacity - arena->cursor - 1U);
}

/* Budget for the current producer after protecting a later producer's
 * all-or-nothing tail reservation. This is the command-list equivalent of
 * SlaveDriver's MAXNMSLAVEPOLYS guard margin (WALLS.C:1278,1380): optional
 * world work must not consume the space needed by the essential final pass. */
static inline uint16_t
sm64_saturn_command_arena_budget_before_tail(
    const sm64_saturn_command_arena_t *arena, uint16_t reserved_tail)
{
    const uint16_t available =
        sm64_saturn_command_arena_available(arena);
    return available > reserved_tail
        ? (uint16_t)(available - reserved_tail) : 0U;
}

/* Returns the slot where the backend must encode END. */
static inline uint16_t
sm64_saturn_command_arena_finish(sm64_saturn_command_arena_t *arena)
{
    arena->previous_end = arena->cursor;
    arena->live_count = arena->cursor + 1U;
    if (arena->live_count > arena->peak)
        arena->peak = arena->live_count;
    return arena->previous_end;
}

#endif
