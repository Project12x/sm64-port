#include <stdint.h>
#include <stdio.h>

#include "saturn_geo_walk.h"

static int expect_event(const sm64_saturn_geo_walk_event_t *event,
                        uintptr_t node,
                        sm64_saturn_geo_walk_phase_t phase,
                        uint16_t action,
                        uint16_t matrix_depth,
                        uint16_t context_token,
                        int failure)
{
    if (event->node != node || event->phase != phase ||
        event->leave_action != action ||
        event->matrix_depth != matrix_depth ||
        event->context_token != context_token) {
        return failure;
    }
    return 0;
}

static int test_enter_leave_order(void)
{
    sm64_saturn_geo_walk_t walk;
    sm64_saturn_geo_walk_frame_t frames[4];
    sm64_saturn_geo_walk_event_t event;

    sm64_saturn_geo_walk_init(&walk, frames, 4U);
    if (!sm64_saturn_geo_walk_push_enter(&walk, 1U, 3U, 2U, 7U)) return 10;
    if (!sm64_saturn_geo_walk_push_leave(&walk, 1U, 4U, 7U)) return 11;
    if (!sm64_saturn_geo_walk_next(&walk, &event)) return 12;
    if (expect_event(&event, 1U, SM64_SATURN_GEO_WALK_LEAVE, 4U, 0U, 7U, 13)) return 13;
    if (!sm64_saturn_geo_walk_next(&walk, &event)) return 14;
    if (expect_event(&event, 1U, SM64_SATURN_GEO_WALK_ENTER, 3U, 2U, 7U, 15)) return 15;
    if (sm64_saturn_geo_walk_next(&walk, &event)) return 16;
    if (walk.depth != 0U || walk.high_water != 2U) return 17;
    return 0;
}

static int test_overflow_latches_and_stops(void)
{
    sm64_saturn_geo_walk_t walk;
    sm64_saturn_geo_walk_frame_t frames[1];
    sm64_saturn_geo_walk_event_t event;

    sm64_saturn_geo_walk_init(&walk, frames, 1U);
    if (!sm64_saturn_geo_walk_push_enter(&walk, 2U, 1U, 0U, 9U)) return 20;
    if (sm64_saturn_geo_walk_push_leave(&walk, 2U, 2U, 9U)) return 21;
    if (!walk.overflowed ||
        sm64_saturn_geo_walk_fail_reason(&walk) !=
            SM64_SATURN_GEO_WALK_OVERFLOW) return 22;
    if (sm64_saturn_geo_walk_next(&walk, &event)) return 23;
    return 0;
}

static int test_child_first_sibling_continuation_order(void)
{
    sm64_saturn_geo_walk_t walk;
    sm64_saturn_geo_walk_frame_t frames[4];
    sm64_saturn_geo_walk_event_t event;

    sm64_saturn_geo_walk_init(&walk, frames, 4U);
    /* Push reverse order: child enter/leave, then the sibling continuation. */
    if (!sm64_saturn_geo_walk_push_enter(&walk, 30U, 0U, 0U, 12U)) return 30;
    if (!sm64_saturn_geo_walk_push_leave(&walk, 10U, 9U, 12U)) return 31;
    if (!sm64_saturn_geo_walk_push_enter(&walk, 20U, 8U, 5U, 12U)) return 32;

    if (!sm64_saturn_geo_walk_next(&walk, &event)) return 33;
    if (expect_event(&event, 20U, SM64_SATURN_GEO_WALK_ENTER, 8U, 5U, 12U, 34)) return 34;
    if (!sm64_saturn_geo_walk_next(&walk, &event)) return 35;
    if (expect_event(&event, 10U, SM64_SATURN_GEO_WALK_LEAVE, 9U, 0U, 12U, 36)) return 36;
    if (!sm64_saturn_geo_walk_next(&walk, &event)) return 37;
    if (expect_event(&event, 30U, SM64_SATURN_GEO_WALK_ENTER, 0U, 0U, 12U, 38)) return 38;
    if (sm64_saturn_geo_walk_next(&walk, &event)) return 39;
    return walk.depth == 0U ? 0 : 40;
}

int main(void)
{
    int failure = test_enter_leave_order();
    if (failure != 0) return failure;
    failure = test_overflow_latches_and_stops();
    if (failure != 0) return failure;
    failure = test_child_first_sibling_continuation_order();
    if (failure != 0) return failure;
    printf("geo walk contract: PASS (frame=%zu walk=%zu)\n",
           sizeof(sm64_saturn_geo_walk_frame_t),
           sizeof(sm64_saturn_geo_walk_t));
    return 0;
}
