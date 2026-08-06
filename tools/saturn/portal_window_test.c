#include <assert.h>
#include <string.h>

#include "saturn_scene_admission.h"

int main(void)
{
    sm64_saturn_render_cluster_t cluster = {0};
    sm64_saturn_scene_admission_node_t nodes[3] = {0};
    sm64_saturn_scene_admission_portal_window_t portal = {0};
    uint16_t cluster_ref[1] = {0U}, portal_ref[3] = {0U};
    uint16_t clusters_out[2], portals_out[2];
    sm64_saturn_scene_admission_view_t scene = {0};
    sm64_saturn_scene_admission_output_t output = {
        clusters_out, 2U, 0U, portals_out, 2U, 0U};
    sm64_saturn_scene_admission_stats_t stats;
    sm64_saturn_render_view_t view = {0};

    cluster.bounds_min_q16[2] = 20 * 65536;
    cluster.bounds_max_q16[2] = 28 * 65536;
    cluster.position_ref_count[0] = 1U;
    cluster.primitive_count = 1U;
    nodes[0].bounds_min_q16[0] = -4 * 65536;
    nodes[0].bounds_max_q16[0] = 4 * 65536;
    nodes[0].bounds_min_q16[2] = 1 * 65536;
    nodes[0].bounds_max_q16[2] = 40 * 65536;
    nodes[0].cluster_ref_count = 1U;
    nodes[0].portal_ref_count = 1U;
    nodes[1] = nodes[0];
    nodes[1].cluster_ref_first = 0U;
    nodes[1].portal_ref_first = 0U;
    nodes[2] = nodes[0];
    nodes[2].portal_ref_first = 2U;
    nodes[2].portal_ref_count = 0U;
    portal.bounds_min_q16[0] = -2 * 65536;
    portal.bounds_max_q16[0] = 2 * 65536;
    portal.bounds_min_q16[2] = 4 * 65536;
    portal.bounds_max_q16[2] = 8 * 65536;
    portal.node_a = 0U; portal.node_b = 1U;
    scene.metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION;
    scene.metadata_valid = 1U;
    scene.clusters = &cluster; scene.cluster_count = 1U;
    scene.nodes = nodes; scene.node_count = 3U;
    scene.cluster_refs = cluster_ref; scene.cluster_ref_count = 1U;
    scene.portals = &portal; scene.portal_count = 1U;
    scene.portal_refs = portal_ref; scene.portal_ref_count = 3U;
    scene.frustum.forward[2] = 65536;
    scene.frustum.right[0] = 65536;
    scene.frustum.up[1] = 65536;
    scene.frustum.near_depth = 1; scene.frustum.far_depth = 100;
    scene.frustum.half_width = 100; scene.frustum.half_height = 100;
    scene.frustum.focal_length = 100;
    view.view_forward_q16[2] = 65536; view.generation = 1U;
    view.view_projection_q16[0][0] = 65536;
    view.view_projection_q16[1][1] = 65536;

    portal.open = 0U;
    assert(sm64_saturn_scene_admit(&scene, &view, &output, &stats));
    assert(output.cluster_count == 1U && output.portal_count == 0U);
    assert(stats.portals_rejected_closed == 1U);
    portal.open = 1U;
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &view, &output, &stats));
    assert(output.portal_count == 1U && stats.cycle_edges != 0U);
    portal.bounds_min_q16[0] = 1000 * 65536;
    portal.bounds_max_q16[0] = 1001 * 65536;
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &view, &output, &stats));
    assert(output.portal_count == 0U && stats.portals_rejected_frustum == 1U);
    nodes[2].portal_ref_count = 1U;
    nodes[2].portal_ref_count = 1U;
    output.cluster_count = output.portal_count = 0U;
    assert(!sm64_saturn_scene_admit(&scene, &view, &output, &stats));
    return 0;
}
