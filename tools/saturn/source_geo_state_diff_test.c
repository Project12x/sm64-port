#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_source_geo_state.h"

typedef struct source_geo_domain_state {
    uint32_t animation;
    uint32_t painting;
    uint32_t warp;
    uint32_t water;
    uint32_t moving_texture;
    uint32_t camera_matrix_object;
    uint32_t lifecycle;
    uint32_t visibility;
} source_geo_domain_state_t;

typedef struct source_geo_domain_digest {
    uint32_t animation;
    uint32_t painting;
    uint32_t warp;
    uint32_t water;
    uint32_t moving_texture;
    uint32_t camera_matrix_object;
    uint32_t lifecycle;
    uint32_t visibility;
} source_geo_domain_digest_t;

static uint32_t digest_word(uint32_t domain, uint32_t value)
{
    return (UINT32_C(2166136261) ^ domain ^ value) * UINT32_C(16777619);
}

/* Reference effects are tied to executable source-site checks in
 * test_source_geo_state_contract.py. This fixture does not propose a second
 * geo implementation; it makes the observed normal-vs-old-skip state loss
 * explicit at the digest boundary required by this task. */
static void reference_render_walk(source_geo_domain_state_t *state)
{
    state->animation -= 1U;
    state->painting += 3U;
    /* Warp-transition state is updated outside the suppressed graph walk. */
    state->water += 5U;
    state->moving_texture += 7U;
    state->camera_matrix_object += 11U;
    state->lifecycle += 13U;
    state->visibility ^= UINT32_C(0x20);
}

static source_geo_domain_digest_t capture_domains(
    const source_geo_domain_state_t *state)
{
    source_geo_domain_digest_t digest;

    digest.animation = digest_word(1U, state->animation);
    digest.painting = digest_word(2U, state->painting);
    digest.warp = digest_word(3U, state->warp);
    digest.water = digest_word(4U, state->water);
    digest.moving_texture = digest_word(5U, state->moving_texture);
    digest.camera_matrix_object = digest_word(6U, state->camera_matrix_object);
    digest.lifecycle = digest_word(7U, state->lifecycle);
    digest.visibility = digest_word(8U, state->visibility);
    return digest;
}

static void test_old_root_suppression_changes_every_required_state_domain(void)
{
    const source_geo_domain_state_t initial = {
        .animation = 19U,
        .painting = 23U,
        .warp = 29U,
        .water = 31U,
        .moving_texture = 37U,
        .camera_matrix_object = 41U,
        .lifecycle = 43U,
        .visibility = 0U,
    };
    source_geo_domain_state_t normal = initial;
    const source_geo_domain_state_t old_suppressed = initial;
    source_geo_domain_digest_t normal_digest;
    source_geo_domain_digest_t suppressed_digest;

    reference_render_walk(&normal);
    normal_digest = capture_domains(&normal);
    suppressed_digest = capture_domains(&old_suppressed);

    assert(normal_digest.animation != suppressed_digest.animation);
    assert(normal_digest.painting != suppressed_digest.painting);
    assert(normal_digest.warp == suppressed_digest.warp);
    assert(normal_digest.water != suppressed_digest.water);
    assert(normal_digest.moving_texture != suppressed_digest.moving_texture);
    assert(normal_digest.camera_matrix_object != suppressed_digest.camera_matrix_object);
    assert(normal_digest.lifecycle != suppressed_digest.lifecycle);
    assert(normal_digest.visibility != suppressed_digest.visibility);
}

static void test_unproven_state_only_update_fails_closed(void)
{
    const sm64_saturn_source_geo_digest_t sentinel = {
        .generation = UINT32_C(0x10203040),
        .object_count = UINT32_C(0x50607080),
        .animation_digest = UINT32_C(0x90A0B0C0),
        .geo_state_digest = UINT32_C(0xD0E0F001),
        .visibility_digest = UINT32_C(0x12345678),
    };
    sm64_saturn_source_geo_digest_t digest = sentinel;

    assert(!sm64_saturn_source_geo_update_state(1U, &digest));
    assert(memcmp(&digest, &sentinel, sizeof(digest)) == 0);
}

static void test_invalid_requests_fail_closed(void)
{
    const sm64_saturn_source_geo_digest_t sentinel = {
        .generation = 7U,
        .object_count = 11U,
        .animation_digest = 13U,
        .geo_state_digest = 17U,
        .visibility_digest = 19U,
    };
    sm64_saturn_source_geo_digest_t digest = sentinel;

    assert(!sm64_saturn_source_geo_update_state(0U, &digest));
    assert(memcmp(&digest, &sentinel, sizeof(digest)) == 0);
    assert(!sm64_saturn_source_geo_update_state(1U, NULL));
}

int main(void)
{
    test_old_root_suppression_changes_every_required_state_domain();
    test_unproven_state_only_update_fails_closed();
    test_invalid_requests_fail_closed();
    return 0;
}
