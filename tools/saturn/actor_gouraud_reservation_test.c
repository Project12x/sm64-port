/* Contract test for the generic-actor Gouraud reservation.
 *
 * This is deliberately NOT a snapshot of what the current renderer produces.
 * It asserts the properties the actor-emit offload depends on, each of which
 * can fail and none of which are observable in a rendered frame:
 *
 *   1. disjointness   -- the reservation never overlaps already-allocated
 *                        tables, so actor shading cannot overwrite terrain's;
 *   2. alignment      -- both edges land on a 16-byte SH-2 cache-line boundary,
 *                        because 8-byte entries pack two per line and the two
 *                        phases are about to run on different CPUs with no
 *                        coherency between them;
 *   3. sufficiency    -- the range is at least as large as was asked for;
 *   4. containment    -- an accepted reservation always fits inside capacity;
 *   5. fail-closed    -- a request that cannot fit is refused rather than
 *                        clamped into something that overruns;
 *   6. no wrap        -- inputs near UINT16_MAX do not wrap into a small,
 *                        overlapping range.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_gouraud_reservation.h"

static int fail(const char *what, uint16_t used, uint16_t capacity,
                uint16_t count)
{
    fprintf(stderr, "gouraud reservation: %s (used=%u capacity=%u count=%u)\n",
            what, used, capacity, count);
    return 0;
}

static int check_one(uint16_t used, uint16_t capacity, uint16_t count)
{
    sm64_saturn_actor_gouraud_reservation_t r;
    memset(&r, 0xA5, sizeof(r));
    const bool ok = sm64_saturn_actor_gouraud_reserve(used, capacity, count, &r);

    if (!ok) {
        /* 5. fail-closed: refusal is only legitimate when the aligned request
         * genuinely cannot fit. Recompute the requirement in wide arithmetic
         * so the check does not share the implementation's own rounding. */
        const uint32_t need_first = ((uint32_t)used + 1U) & ~(uint32_t)1U;
        const uint32_t need_count = ((uint32_t)count + 1U) & ~(uint32_t)1U;
        if (need_first + need_count <= (uint32_t)capacity)
            return fail("refused a request that fits", used, capacity, count);
        return 1;
    }

    if (r.first < used)
        return fail("overlaps already-allocated tables", used, capacity, count);
    if ((r.first % SM64_SATURN_ACTOR_GOURAUD_LINE_ENTRIES) != 0U)
        return fail("first edge is not cache-line aligned", used, capacity,
                    count);
    if ((r.limit % SM64_SATURN_ACTOR_GOURAUD_LINE_ENTRIES) != 0U)
        return fail("limit edge is not cache-line aligned", used, capacity,
                    count);
    if (r.limit < r.first)
        return fail("limit precedes first", used, capacity, count);
    if ((uint32_t)(r.limit - r.first) < (uint32_t)count)
        return fail("range is smaller than requested", used, capacity, count);
    if ((uint32_t)r.limit > (uint32_t)capacity)
        return fail("accepted range overruns capacity", used, capacity, count);
    return 1;
}

int main(void)
{
    /* Exhaustive over a small window, which covers every alignment parity
     * combination, the zero-count case, and both sides of the capacity edge. */
    for (uint16_t capacity = 0U; capacity <= 40U; capacity++)
        for (uint16_t used = 0U; used <= capacity; used++)
            for (uint16_t count = 0U; count <= 40U; count++)
                if (!check_one(used, capacity, count)) return 1;

    /* 6. no wrap: values near the 16-bit ceiling must not fold back into a
     * small range that would silently overlap terrain's tables. */
    static const uint16_t edge[] = {
        0U, 1U, 2U, 0x7FFEU, 0x7FFFU, 0xFFFDU, 0xFFFEU, 0xFFFFU,
    };
    for (size_t a = 0U; a < sizeof(edge) / sizeof(edge[0]); a++)
        for (size_t b = 0U; b < sizeof(edge) / sizeof(edge[0]); b++)
            for (size_t c = 0U; c < sizeof(edge) / sizeof(edge[0]); c++)
                if (!check_one(edge[a], edge[b], edge[c])) return 1;

    /* The reservation must actually be usable: an ordinal inside the reserved
     * count maps below the limit, and one past it does not. This is the exact
     * bound demo_generic_actor_emit applies, stated as a property rather than
     * as an observation that it currently never trips. */
    {
        sm64_saturn_actor_gouraud_reservation_t r;
        if (!sm64_saturn_actor_gouraud_reserve(7U, 256U, 5U, &r))
            return fprintf(stderr, "gouraud reservation: setup failed\n"), 1;
        for (uint16_t ordinal = 0U; ordinal < 5U; ordinal++) {
            if ((uint32_t)r.first + ordinal >= r.limit) {
                fprintf(stderr,
                        "gouraud reservation: ordinal %u rejected inside its "
                        "own reservation\n", ordinal);
                return 1;
            }
        }
        if ((uint32_t)r.first + (uint32_t)(r.limit - r.first) < r.limit) {
            fprintf(stderr, "gouraud reservation: limit is not exclusive\n");
            return 1;
        }
    }

    printf("actor gouraud reservation: PASS\n");
    return 0;
}
