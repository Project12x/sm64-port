#include <assert.h>
#include <stdint.h>

#include "saturn_frame_profile.h"
#include "saturn_gouraud.h"
#include "saturn_command_arena.h"
#include "saturn_memory_arena.h"
#include "saturn_projected_workarea.h"
#include "saturn_render_queue.h"
#include "saturn_transform.h"
#include "saturn_matrix.h"

static void test_identity_camera(void)
{
    const sm64_saturn_camera_transform_t camera = {
        .position = {10, 20, 30},
        .right = {1 << 16, 0, 0},
        .up = {0, 1 << 16, 0},
        .forward = {0, 0, 1 << 16}
    };
    const sm64_saturn_vec3i_t view = sm64_saturn_world_to_view(
        &camera, (sm64_saturn_vec3i_t){14, 26, 38});

    assert(view.x == 4);
    assert(view.y == 6);
    assert(view.z == 8);
}

static void test_q16_normalization(void)
{
    const sm64_saturn_vec3i_t normalized =
        sm64_saturn_vec3_normalize_q16((sm64_saturn_vec3i_t){3, 4, 0});

    assert(normalized.x == 39321);
    assert(normalized.y == 52428);
    assert(normalized.z == 0);
}

static void test_matrix_decode_identity(void)
{
    /* Real on-target encoding under GBI_FLOATS (F3DEX_GBI_2E=1, see
     * include/PR/gbi.h:90-94 and src/port/saturn/sourceboot/Makefile:74):
     * 16 consecutive row-major floats, matching gbi.h's `Mtx` struct
     * under that build configuration -- NOT the classic split s15.16
     * int32 GBI encoding. */
    const float gbi_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    sm64_saturn_mtx_t out;

    sm64_saturn_matrix_decode(gbi_floats, &out);

    assert(out.m[0][0] == (1 << 16) && out.m[0][1] == 0);
    assert(out.m[1][0] == 0 && out.m[1][1] == (1 << 16));
    assert(out.m[2][2] == (1 << 16));
    assert(out.m[3][3] == (1 << 16));
}

static void test_matrix_decode_translation(void)
{
    /* Row 3 = translation (16.0, -8.5, 0.25) in the reference's row-vector
     * convention (gfx_pc.c gfx_sp_vertex: translation lives in M[3][*]). */
    float gbi_floats[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        16.0f, -8.5f, 0.25f, 1.0f
    };
    sm64_saturn_mtx_t out;

    sm64_saturn_matrix_decode(gbi_floats, &out);

    assert(out.m[3][0] == ((int32_t)16 << 16));
    assert(out.m[3][1] == (int32_t)(-8.5f * 65536.0f)); /* -557056 */
    assert(out.m[3][2] == (int32_t)(0.25f * 65536.0f));  /* 16384 */
    assert(out.m[3][3] == (1 << 16));
}

static void test_frame_profile(void)
{
    sm64_saturn_frame_profile_t profile = {
        .update_ticks = 1,
        .sort_ticks = 2,
        .command_ticks = 3,
        .wait_ticks = 4,
        .vblank_ticks = 5
    };

    assert(sm64_saturn_frame_profile_render_total(&profile) == 10);
    assert(sm64_saturn_frame_profile_loop_total(&profile) == 15);
    assert(sm64_saturn_frame_profile_rate_x10(1500, profile.loop_ticks) == 100);
    assert(sm64_saturn_frame_profile_rate_x10(1500, 0) == 0);
}

static void test_bounded_memory_arena(void)
{
    uint8_t storage[32];
    sm64_saturn_memory_arena_t arena;
    sm64_saturn_memory_arena_init(&arena, storage, sizeof(storage));

    assert(sm64_saturn_memory_arena_alloc(&arena, 3, 8) == &storage[0]);
    assert(sm64_saturn_memory_arena_alloc(&arena, 8, 8) == &storage[8]);
    assert(arena.used == 16);
    assert(arena.peak == 16);
    assert(!arena.overflowed);
    assert(sm64_saturn_memory_arena_alloc(&arena, 17, 8) == NULL);
    assert(arena.overflowed);
    assert(arena.used == 16);

    sm64_saturn_memory_arena_reset(&arena);
    assert(arena.used == 0);
    assert(arena.peak == 16);
    assert(!arena.overflowed);
}

static void test_source_identified_render_queue(void)
{
    sm64_saturn_render_item_t items[2];
    uint16_t order[2];
    sm64_saturn_render_queue_t queue;
    sm64_saturn_render_queue_init(&queue, items, order, 2);

    assert(sm64_saturn_render_queue_push(&queue,
        (sm64_saturn_render_item_t){
            .depth_key = 900,
            .source_bank = 7,
            .source_primitive = 42,
            .lowered_index = 3,
            .kind = SM64_SATURN_RENDER_WORLD,
            .pass = SM64_SATURN_PASS_OPAQUE
        }));
    assert(sm64_saturn_render_queue_push(&queue,
        (sm64_saturn_render_item_t){
            .depth_key = 700,
            .source_bank = 9,
            .source_primitive = 11,
            .lowered_index = 5,
            .kind = SM64_SATURN_RENDER_ACTOR,
            .pass = SM64_SATURN_PASS_OPAQUE
        }));
    assert(queue.count == 2);
    assert(queue.order[0] == 0 && queue.order[1] == 1);
    assert(queue.items[0].source_primitive == 42);
    assert(queue.items[1].lowered_index == 5);
    assert(!sm64_saturn_render_queue_push(&queue,
        (sm64_saturn_render_item_t){0}));
    assert(queue.overflowed);

    sm64_saturn_render_queue_reset(&queue);
    assert(queue.count == 0);
    assert(!queue.overflowed);
}

static void test_projected_workarea(void)
{
    sm64_saturn_projected_vertex_t storage[4];
    sm64_saturn_projected_workarea_t workarea;
    uint16_t indices[4];
    const sm64_saturn_viewport_t viewport = {
        .left = 0,
        .top = 0,
        .right = 319,
        .bottom = 223
    };
    sm64_saturn_projected_quad_t quad;

    sm64_saturn_projected_workarea_init(&workarea, storage, 4);
    assert(workarea.count == 0);
    assert(workarea.peak == 0);
    assert(!workarea.overflowed);

    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){10, 10, 100}, &indices[0]));
    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){330, 10, 200}, &indices[1]));
    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){330, 200, 300}, &indices[2]));
    assert(sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){10, 200, 400}, &indices[3]));
    assert(workarea.count == 4);
    assert(workarea.peak == 4);
    assert(indices[0] == 0 && indices[3] == 3);

    assert(sm64_saturn_projected_quad_analyze(
        &workarea, indices, &viewport, &quad));
    assert(quad.min_x == 10 && quad.max_x == 330);
    assert(quad.min_y == 10 && quad.max_y == 200);
    assert(quad.min_z == 100 && quad.max_z == 400);
    assert(quad.center_z == 200);
    assert(quad.clip_and == SM64_SATURN_CLIP_NONE);
    assert(quad.clip_or == SM64_SATURN_CLIP_RIGHT);
    assert(sm64_saturn_projected_quad_is_visible(&quad, 64, 512, 512));
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 128, 512, 512));
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 64, 256, 512));
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 64, 512, 128));

    for (uint8_t corner = 0; corner < 4; corner++) {
        storage[corner].x = -10;
    }
    assert(sm64_saturn_projected_quad_analyze(
        &workarea, indices, &viewport, &quad));
    assert((quad.clip_and & SM64_SATURN_CLIP_LEFT) != 0);
    assert(!sm64_saturn_projected_quad_is_visible(&quad, 64, 512, 512));

    indices[3] = 4;
    assert(!sm64_saturn_projected_quad_analyze(
        &workarea, indices, &viewport, &quad));
    assert(!sm64_saturn_projected_workarea_push(&workarea,
        (sm64_saturn_projected_vertex_t){0, 0, 0}, NULL));
    assert(workarea.overflowed);

    sm64_saturn_projected_workarea_reset(&workarea);
    assert(workarea.count == 0);
    assert(workarea.peak == 4);
    assert(!workarea.overflowed);
}

static void test_bounded_command_arena(void)
{
    sm64_saturn_command_arena_t arena;
    uint16_t first;

    sm64_saturn_command_arena_init(&arena, 6, 2);
    assert(sm64_saturn_command_arena_begin(&arena) == 2);
    assert(sm64_saturn_command_arena_reserve(&arena, 2, &first));
    assert(first == 2);
    assert(sm64_saturn_command_arena_reserve(&arena, 1, &first));
    assert(first == 4);
    assert(!sm64_saturn_command_arena_reserve(&arena, 1, &first));
    assert(arena.overflowed);
    assert(sm64_saturn_command_arena_finish(&arena) == 5);
    assert(arena.live_count == 6);
    assert(arena.peak == 6);

    assert(sm64_saturn_command_arena_begin(&arena) == 5);
    assert(!arena.overflowed);
    assert(sm64_saturn_command_arena_reserve(&arena, 1, &first));
    assert(sm64_saturn_command_arena_finish(&arena) == 3);
    assert(arena.live_count == 4);
    assert(arena.peak == 6);
}

int main(void)
{
    assert(sm64_saturn_gouraud_neutral_color() == 0xC210U);
    test_identity_camera();
    test_q16_normalization();
    test_frame_profile();
    test_bounded_memory_arena();
    test_source_identified_render_queue();
    test_projected_workarea();
    test_bounded_command_arena();
    test_matrix_decode_identity();
    test_matrix_decode_translation();
    return 0;
}
