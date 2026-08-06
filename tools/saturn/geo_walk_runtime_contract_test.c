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

struct trace {
    uint8_t values[16];
    uint8_t count;
};

static bool trace_enter(uintptr_t node,
                        sm64_saturn_geo_walk_runtime_enter_t *result,
                        void *user)
{
    struct trace *trace = user;
    trace->values[trace->count++] = (uint8_t)node;
    result->admitted = true;
    result->child = node == 1U ? 2U : (node == 2U ? 4U : 0U);
    result->sibling = node == 1U ? 3U : 0U;
    result->defer_dispatch = node == 1U;
    result->leave_required = node == 1U;
    result->leave_action = 9U;
    result->matrix_depth = 5U;
    result->context_token = 6U;
    return true;
}

static void trace_dispatch(uintptr_t node, void *user)
{
    struct trace *trace = user;
    trace->values[trace->count++] = (uint8_t)(node + 10U);
}

static void trace_leave(uintptr_t node, uint16_t action, uint16_t matrix_depth,
                        uint16_t context_token, void *user)
{
    struct trace *trace = user;
    if (node != 1U || action != 9U || matrix_depth != 5U || context_token != 6U) {
        trace->values[trace->count++] = 0xFFU;
        return;
    }
    trace->values[trace->count++] = 0xEEU;
}

static int test_depth_first_deferred_dispatch(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[8];
    struct trace trace = { { 0 }, 0U };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        trace_enter, trace_dispatch, trace_leave
    };
    const uint8_t expected[] = { 1U, 2U, 4U, 11U, 0xEEU, 3U };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 8U);
    if (!sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &trace)) return 30;
    if (trace.count != sizeof(expected)) return 31;
    for (uint8_t i = 0U; i < trace.count; i++) {
        if (trace.values[i] != expected[i]) return 32;
    }
    return 0;
}

int main(void)
{
    int failure = test_sibling_cursor_is_preserved();
    if (failure != 0) return failure;
    failure = test_overflow_stops_without_partial_event();
    if (failure != 0) return failure;
    failure = test_depth_first_deferred_dispatch();
    if (failure != 0) return failure;
    puts("geo walk runtime contract: PASS");
    return 0;
}
