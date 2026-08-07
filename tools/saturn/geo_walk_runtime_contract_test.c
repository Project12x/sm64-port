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

/* ---- second_child (two-subtree node) coverage ----
 *
 * Shared fixture for every second_child case below. The tree is fixed:
 *
 *   node 1 (root): child = cfg.root_child, second_child = cfg.root_second_child
 *   node 2:        leaf (stands in for "child"'s subtree)
 *   node 3:        child = 4, sibling = 5 (stands in for "second_child"'s
 *                   own subtree -- proves second_child is walked as a real
 *                   subtree, with its own child AND its own sibling
 *                   continuation, not just visited as a single node)
 *   node 4, 5:     leaves
 *
 * Only node 1's enter() result varies per test (via cfg); nodes 2-5 always
 * report the same fixed shape above.
 */
struct two_child_config {
    uintptr_t root_child;
    uintptr_t root_second_child;
    bool boundary_required;
    bool leave_required;
    bool defer_dispatch;
};

struct two_child_ctx {
    struct trace trace;
    struct two_child_config cfg;
};

static bool two_child_enter(uintptr_t node,
                            sm64_saturn_geo_walk_runtime_enter_t *result,
                            void *user)
{
    struct two_child_ctx *ctx = user;
    ctx->trace.values[ctx->trace.count++] = (uint8_t) node;
    result->admitted = true;
    result->matrix_depth = 5U;
    result->context_token = 6U;
    result->leave_action = 9U;
    result->boundary_action = 8U;
    if (node == 1U) {
        result->child = ctx->cfg.root_child;
        result->second_child = ctx->cfg.root_second_child;
        result->boundary_required = ctx->cfg.boundary_required;
        result->leave_required = ctx->cfg.leave_required;
        result->defer_dispatch = ctx->cfg.defer_dispatch;
    } else if (node == 3U) {
        result->child = 4U;
        result->sibling = 5U;
    }
    /* Nodes 2, 4, 5, and any sentinel node id used to prove second_child is
     * ignored when child == 0 are leaves: no child/sibling/second_child. */
    return true;
}

static void two_child_dispatch(uintptr_t node, void *user)
{
    struct two_child_ctx *ctx = user;
    ctx->trace.values[ctx->trace.count++] = (uint8_t) (node + 20U);
}

static void two_child_leave(uintptr_t node, uint16_t action, uint16_t matrix_depth,
                            uint16_t context_token, void *user)
{
    struct two_child_ctx *ctx = user;
    if (node != 1U || matrix_depth != 5U || context_token != 6U) {
        ctx->trace.values[ctx->trace.count++] = 0xFFU;
        return;
    }
    if (action == 8U) {
        ctx->trace.values[ctx->trace.count++] = 0xB0U;
    } else if (action == 9U) {
        ctx->trace.values[ctx->trace.count++] = 0xEEU;
    } else {
        ctx->trace.values[ctx->trace.count++] = 0xFFU;
    }
}

static int test_two_child_walks_child_then_second_child(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[8];
    struct two_child_ctx ctx = { { { 0 }, 0U }, { 2U, 3U, false, false, false } };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        two_child_enter, two_child_dispatch, two_child_leave
    };
    /* [node-enter, child-subtree-events..., second_child-subtree-events...] */
    const uint8_t expected[] = { 1U, 2U, 3U, 4U, 5U };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 8U);
    if (!sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &ctx)) return 40;
    if (ctx.trace.count != sizeof(expected)) return 41;
    for (uint8_t i = 0U; i < ctx.trace.count; i++) {
        if (ctx.trace.values[i] != expected[i]) return 42;
    }
    return 0;
}

static int test_two_child_boundary_leave_fires_between_subtrees(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[8];
    struct two_child_ctx ctx = { { { 0 }, 0U }, { 2U, 3U, true, false, false } };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        two_child_enter, two_child_dispatch, two_child_leave
    };
    /* boundary leave (0xB0) fires after child(2) drains, before
     * second_child(3)'s first event. */
    const uint8_t expected[] = { 1U, 2U, 0xB0U, 3U, 4U, 5U };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 8U);
    if (!sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &ctx)) return 50;
    if (ctx.trace.count != sizeof(expected)) return 51;
    for (uint8_t i = 0U; i < ctx.trace.count; i++) {
        if (ctx.trace.values[i] != expected[i]) return 52;
    }
    return 0;
}

static int test_two_child_boundary_then_final_leave_order(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[8];
    struct two_child_ctx ctx = { { { 0 }, 0U }, { 2U, 3U, true, true, false } };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        two_child_enter, two_child_dispatch, two_child_leave
    };
    /* Both leave callbacks fire: boundary (0xB0) between the two subtrees,
     * final (0xEE) last, after second_child(3) fully drains. */
    const uint8_t expected[] = { 1U, 2U, 0xB0U, 3U, 4U, 5U, 0xEEU };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 8U);
    if (!sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &ctx)) return 60;
    if (ctx.trace.count != sizeof(expected)) return 61;
    for (uint8_t i = 0U; i < ctx.trace.count; i++) {
        if (ctx.trace.values[i] != expected[i]) return 62;
    }
    return 0;
}

static int test_two_child_ignored_when_child_zero(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[8];
    /* second_child set to a distinctive sentinel node id (99) that must
     * NEVER be entered when child == 0 -- an easy invariant to get
     * backwards (e.g. guarding only on second_child != 0). If node 99 were
     * wrongly entered, its enter() call would append 99 to the trace and
     * the length check below would catch it. */
    struct two_child_ctx ctx = { { { 0 }, 0U }, { 0U, 99U, false, false, false } };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        two_child_enter, two_child_dispatch, two_child_leave
    };
    const uint8_t expected[] = { 1U };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 8U);
    if (!sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &ctx)) return 70;
    if (ctx.trace.count != sizeof(expected)) return 71;
    if (ctx.trace.values[0] != expected[0]) return 72;
    return 0;
}

static int test_two_child_defer_dispatch_between_child_and_boundary(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[8];
    struct two_child_ctx ctx = { { { 0 }, 0U }, { 2U, 3U, false, false, true } };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        two_child_enter, two_child_dispatch, two_child_leave
    };
    /* Deferred dispatch (node 1 + 20 = 21) fires after child(2) drains,
     * before second_child(3)'s first event -- same slot boundary_leave
     * would occupy, exercising the push order's DISPATCH-before-boundary
     * step (Step 3 item 5 pops before item 4 in this configuration, since
     * boundary_required is false here and only dispatch is pushed). */
    const uint8_t expected[] = { 1U, 2U, 21U, 3U, 4U, 5U };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 8U);
    if (!sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &ctx)) return 90;
    if (ctx.trace.count != sizeof(expected)) return 91;
    for (uint8_t i = 0U; i < ctx.trace.count; i++) {
        if (ctx.trace.values[i] != expected[i]) return 92;
    }
    return 0;
}

static int test_two_child_overflow_reports_capacity(void)
{
    sm64_saturn_geo_walk_runtime_t walk;
    sm64_saturn_geo_walk_runtime_frame_t frames[2];
    /* Same shape as test_two_child_boundary_then_final_leave_order, but a
     * capacity of 2 is far too small to hold the additional second_child
     * bookkeeping frames (final-leave, second_child-ENTER, boundary-leave,
     * child-ENTER) this path pushes. Must report overflow cleanly rather
     * than corrupt walk state or return success. */
    struct two_child_ctx ctx = { { { 0 }, 0U }, { 2U, 3U, true, true, false } };
    const sm64_saturn_geo_walk_runtime_ops_t ops = {
        two_child_enter, two_child_dispatch, two_child_leave
    };

    sm64_saturn_geo_walk_runtime_init(&walk, frames, 2U);
    if (sm64_saturn_geo_walk_runtime_run(&walk, 1U, &ops, &ctx)) return 80;
    return walk.overflowed &&
           walk.fail_reason == SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW ? 0 : 81;
}

int main(void)
{
    int failure = test_sibling_cursor_is_preserved();
    if (failure != 0) return failure;
    failure = test_overflow_stops_without_partial_event();
    if (failure != 0) return failure;
    failure = test_depth_first_deferred_dispatch();
    if (failure != 0) return failure;
    failure = test_two_child_walks_child_then_second_child();
    if (failure != 0) return failure;
    failure = test_two_child_boundary_leave_fires_between_subtrees();
    if (failure != 0) return failure;
    failure = test_two_child_boundary_then_final_leave_order();
    if (failure != 0) return failure;
    failure = test_two_child_ignored_when_child_zero();
    if (failure != 0) return failure;
    failure = test_two_child_defer_dispatch_between_child_and_boundary();
    if (failure != 0) return failure;
    failure = test_two_child_overflow_reports_capacity();
    if (failure != 0) return failure;
    puts("geo walk runtime contract: PASS");
    return 0;
}
