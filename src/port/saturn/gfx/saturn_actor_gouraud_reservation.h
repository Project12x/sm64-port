#ifndef SM64_SATURN_ACTOR_GOURAUD_RESERVATION_H
#define SM64_SATURN_ACTOR_GOURAUD_RESERVATION_H

#include <stdbool.h>
#include <stdint.h>

/* The generic-actor phase's claim on the shared Gouraud staging array.
 *
 * Extracted from saturn_demo_render.c's finalize path so the contract can be
 * tested directly. It is arithmetic with two obligations that are easy to get
 * subtly wrong and impossible to observe in a rendered frame:
 *
 *   - **Disjointness.** The reservation must begin at or after everything
 *     already allocated, so actor tables never overwrite terrain's.
 *   - **Cache-line alignment.** sm64_saturn_gouraud_table_t is 8 bytes, so TWO
 *     entries share every 16-byte SH-2 cache line. The actor range sits
 *     directly against terrain's later allocations; if either edge lands on an
 *     odd index it shares a line with a neighbour's entry, and once the two
 *     phases run on different CPUs -- which is the whole point of the emit
 *     offload -- each CPU's write-back discards the other's half. The
 *     corruption would be silent and would not reproduce under the default
 *     caches-off harness.
 *
 * Both edges are therefore rounded to even. The cost is at most two 8-byte
 * entries. */
typedef struct sm64_saturn_actor_gouraud_reservation {
    uint16_t first;  /* first table index owned by the actor phase */
    uint16_t limit;  /* one past the last; also the new bank cursor */
} sm64_saturn_actor_gouraud_reservation_t;

/* Entries per 16-byte SH-2 cache line, at sizeof(gouraud_table_t) == 8. */
#define SM64_SATURN_ACTOR_GOURAUD_LINE_ENTRIES 2U

/* Reserve `count` tables for the actor phase out of a bank with `capacity`
 * entries of which `used` are already taken. Returns false, writing nothing,
 * when the aligned request does not fit -- fail closed rather than hand back a
 * range that overruns the bank. */
static inline bool sm64_saturn_actor_gouraud_reserve(
    uint16_t used, uint16_t capacity, uint16_t count,
    sm64_saturn_actor_gouraud_reservation_t *out)
{
    if (out == NULL) return false;
    /* Round up in uint32 so a `used` or `count` near UINT16_MAX cannot wrap
     * back to a small value and silently produce an overlapping range. */
    const uint32_t aligned_first =
        ((uint32_t)used + 1U) & ~(uint32_t)1U;
    const uint32_t aligned_count =
        ((uint32_t)count + 1U) & ~(uint32_t)1U;
    if (aligned_first > (uint32_t)capacity ||
        aligned_count > (uint32_t)capacity - aligned_first)
        return false;
    out->first = (uint16_t)aligned_first;
    out->limit = (uint16_t)(aligned_first + aligned_count);
    return true;
}

#endif /* SM64_SATURN_ACTOR_GOURAUD_RESERVATION_H */
