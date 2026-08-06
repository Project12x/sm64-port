#include <stdint.h>
#include <stdio.h>

#include "saturn_geo_walk_runtime.h"

static int test_sibling_cursor_is_preserved(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[4];
    sm64_saturn_geo_walk_runtime_frame_t event;

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 4U);
    if (!sm64_saturn_geo_walk_runtime_push_enter(&walk, 1U, 3U, 2U, 7U)) return 10;
    if (!sm64_saturn_geo_walk_runtime_next(&walk, &event)) return 11;
    if (event.node != 1U || event.sibling != 3U ||
        event.phase != SM64_SATURN_GEO_WALK_RUNTIME_ENTER ||
        event.matrix_depth != 2U || event.context_token != 7U) return 12;
    if (!sm64_saturn_geo_walk_runtime_push_leave(&walk, 1U, 4U, 7U)) return 13;
    if (!sm64_saturn_geo_walk_runtime_next(&walk, &event)) return 14;
    if (event.node != 1U || event.phase != SM64_SATURN_GEO_WALK_RUNTIME_LEAVE ||
        event.leave_action != 4U) return 15;
    return walk.depth == 0U && walk.high_water == 1U ? 0 : 16;
}

static int test_overflow_stops_without_partial_event(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[1];
    sm64_saturn_geo_walk_runtime_frame_t event;

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 1U);
    if (!sm64_saturn_geo_walk_runtime_push_enter(&walk, 1U, 0U, 0U, 0U)) return 20;
    if (sm64_saturn_geo_walk_runtime_push_enter(&walk, 2U, 0U, 0U, 0U)) return 21;
    if (sm64_saturn_geo_walk_runtime_next(&walk, &event)) return 22;
    return walk.overflowed &&
           walk.fail_reason == SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW ? 0 : 23;
}

int main(void)
{
    int failure = test_sibling_cursor_is_preserved();
    if (failure != 0) return failure;
    failure = test_overflow_stops_without_partial_event();
    if (failure != 0) return failure;
    puts("geo walk runtime contract: PASS");
    return 0;
}
