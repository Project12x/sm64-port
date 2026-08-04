#include <assert.h>
#include <string.h>

#include "saturn_render_cluster.h"

static sm64_saturn_render_cluster_t cluster(void)
{
    return (sm64_saturn_render_cluster_t){
        .bounds_min_q16 = {-16 * 65536, -16 * 65536, 64 * 65536},
        .bounds_max_q16 = {16 * 65536, 16 * 65536, 96 * 65536},
        .primitive_first = 7U,
        .primitive_count = 3U,
        .position_ref_first = {0U, 8U, 12U},
        .position_ref_count = {8U, 4U, 2U},
        .mandatory = 0U,
    };
}

static sm64_saturn_render_view_t view_with_forward(
    int32_t x, int32_t y, int32_t z, int32_t forward_x, int32_t forward_y,
    int32_t forward_z, uint32_t generation)
{
    sm64_saturn_render_view_t view = {0};
    view.camera_position_q16[0] = x * 65536;
    view.camera_position_q16[1] = y * 65536;
    view.camera_position_q16[2] = z * 65536;
    view.view_forward_q16[0] = forward_x;
    view.view_forward_q16[1] = forward_y;
    view.view_forward_q16[2] = forward_z;
    view.generation = generation;
    return view;
}

static sm64_saturn_render_view_t view_at(int32_t z, uint32_t generation)
{
    return view_with_forward(0, 0, z, 0, 0, 65536, generation);
}

int main(void)
{
    sm64_saturn_render_cluster_t subject = cluster();
    sm64_saturn_render_view_t view = view_at(0, 9U);
    sm64_saturn_render_lod_state_t lod = {
        .thresholds = {32, 24, 80, 72, 0, 0, 0, 0},
        .previous = SATURN_LOD_NEAR,
    };
    sm64_saturn_render_cluster_result_t result;

    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 1U);
    assert(result.generation == 9U);
    assert(result.lod_tier == SATURN_LOD_MID);
    assert(result.position_ref_first == 8U && result.position_ref_count == 4U);

    view = view_at(-80, 10U);
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.lod_tier == SATURN_LOD_FAR);
    assert(result.position_ref_count == 2U);

    subject.mandatory = 1U;
    subject.bounds_min_q16[2] = -96;
    subject.bounds_max_q16[2] = -64;
    view = view_at(0, 11U);
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 1U && result.lod_tier == SATURN_LOD_NEAR);

    subject.mandatory = 0U;
    view = view_at(0, 12U);
    assert(!sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 0U);

    subject = cluster();
    subject.position_ref_count[SATURN_LOD_FAR] = 0U;
    lod.previous = SATURN_LOD_FAR;
    view = view_at(-80, 13U);
    assert(!sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 0U);

    view = view_at(0, 1U);
    assert(!sm64_saturn_render_cluster_admit(NULL, &view, &lod, &result));
    assert(!sm64_saturn_render_cluster_admit(&subject, NULL, &lod, &result));
    assert(!sm64_saturn_render_cluster_admit(&subject, &view, NULL, &result));
    assert(!sm64_saturn_render_cluster_admit(&subject, &view, &lod, NULL));

    /* A non-axis-aligned yaw must use the immutable view forward vector, not
     * world Z. The cluster is in front at 45 degrees despite negative Z. */
    subject = cluster();
    subject.bounds_min_q16[0] = 120 * 65536;
    subject.bounds_max_q16[0] = 136 * 65536;
    subject.bounds_min_q16[2] = -64 * 65536;
    subject.bounds_max_q16[2] = -48 * 65536;
    lod.previous = SATURN_LOD_NEAR;
    view = view_with_forward(0, 0, 0, 46341, 0, 46341, 14U);
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 1U && result.generation == 14U);
    assert(result.lod_tier == SATURN_LOD_MID);
    assert(result.position_ref_first == 8U && result.position_ref_count == 4U);

    /* The reverse yaw case has positive world Z but lies behind the camera. */
    subject.bounds_min_q16[0] = -192 * 65536;
    subject.bounds_max_q16[0] = -176 * 65536;
    subject.bounds_min_q16[2] = 64 * 65536;
    subject.bounds_max_q16[2] = 80 * 65536;
    assert(!sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 0U);

    /* Mandatory behind-camera work retains its exact near compact span. */
    subject.mandatory = 1U;
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.lod_tier == SATURN_LOD_NEAR);
    assert(result.position_ref_first == 0U && result.position_ref_count == 8U);

    /* MID hysteresis also follows yawed view depth: 28 units stays MID. */
    subject = cluster();
    subject.bounds_min_q16[0] = 104 * 65536;
    subject.bounds_max_q16[0] = 120 * 65536;
    subject.bounds_min_q16[2] = -64 * 65536;
    subject.bounds_max_q16[2] = -48 * 65536;
    lod.previous = SATURN_LOD_MID;
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.lod_tier == SATURN_LOD_MID);
    assert(result.position_ref_first == 8U && result.position_ref_count == 4U);

    /* A pitched view has the same contract: positive elevation can outweigh
     * negative world Z, and the reverse remains behind despite positive Z. */
    subject = cluster();
    subject.bounds_min_q16[1] = 120 * 65536;
    subject.bounds_max_q16[1] = 136 * 65536;
    subject.bounds_min_q16[2] = -64 * 65536;
    subject.bounds_max_q16[2] = -48 * 65536;
    lod.previous = SATURN_LOD_NEAR;
    view = view_with_forward(0, 0, 0, 0, 46341, 46341, 15U);
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 1U && result.generation == 15U);
    assert(result.lod_tier == SATURN_LOD_MID);
    assert(result.position_ref_first == 8U && result.position_ref_count == 4U);

    subject.bounds_min_q16[1] = -192 * 65536;
    subject.bounds_max_q16[1] = -176 * 65536;
    subject.bounds_min_q16[2] = 64 * 65536;
    subject.bounds_max_q16[2] = 80 * 65536;
    assert(!sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.admitted == 0U);

    /* A transform sequence never publishes zero. Admission must use the same
     * normalized value, including the UINT32_MAX wrap, and a scene reset must
     * discard the previous MID hysteresis tier. */
    subject = cluster();
    subject.bounds_min_q16[0] = 104 * 65536;
    subject.bounds_max_q16[0] = 120 * 65536;
    subject.bounds_min_q16[2] = -64 * 65536;
    subject.bounds_max_q16[2] = -48 * 65536;
    lod.previous = SATURN_LOD_MID;
    assert(sm64_saturn_render_generation_next(0U) == 1U);
    assert(sm64_saturn_render_generation_next(UINT32_MAX - 1U) == UINT32_MAX);
    view = view_with_forward(
        0, 0, 0, 46341, 0, 46341,
        sm64_saturn_render_generation_next(UINT32_MAX - 1U));
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.generation == UINT32_MAX);
    assert(result.lod_tier == SATURN_LOD_MID);

    lod.previous = SATURN_LOD_NEAR;
    view.generation = sm64_saturn_render_generation_next(result.generation);
    assert(view.generation == 1U);
    assert(sm64_saturn_render_cluster_admit(&subject, &view, &lod, &result));
    assert(result.generation == 1U);
    assert(result.lod_tier == SATURN_LOD_NEAR);
    assert(result.position_ref_first == 0U && result.position_ref_count == 8U);
    return 0;
}
