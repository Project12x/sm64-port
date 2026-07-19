#include <assert.h>
#include <stdint.h>

#include "saturn_frame_profile.h"
#include "saturn_memory_arena.h"
#include "saturn_transform.h"

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

int main(void)
{
    test_identity_camera();
    test_q16_normalization();
    test_frame_profile();
    test_bounded_memory_arena();
    return 0;
}
