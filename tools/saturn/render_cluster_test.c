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

static sm64_saturn_render_view_t view_at(int32_t z, uint32_t generation)
{
    sm64_saturn_render_view_t view = {0};
    view.camera_position_q16[2] = z * 65536;
    view.generation = generation;
    return view;
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
    return 0;
}
