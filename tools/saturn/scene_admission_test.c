#include <assert.h>
#include <string.h>

#include "saturn_scene_admission.h"

static sm64_saturn_render_cluster_t make_cluster(int x0, int x1, int z0,
                                                  int z1, uint8_t mandatory)
{
    sm64_saturn_render_cluster_t cluster = {0};
    cluster.bounds_min_q16[0] = x0 * 65536;
    cluster.bounds_max_q16[0] = x1 * 65536;
    cluster.bounds_min_q16[1] = -4 * 65536;
    cluster.bounds_max_q16[1] = 4 * 65536;
    cluster.bounds_min_q16[2] = z0 * 65536;
    cluster.bounds_max_q16[2] = z1 * 65536;
    cluster.primitive_count = 1U;
    cluster.position_ref_count[0] = 1U;
    cluster.mandatory = mandatory;
    return cluster;
}

static void setup_scene(sm64_saturn_scene_admission_view_t *scene,
                        sm64_saturn_render_cluster_t *clusters,
                        sm64_saturn_scene_admission_node_t *nodes,
                        uint16_t *cluster_refs,
                        sm64_saturn_scene_admission_portal_window_t *portals,
                        uint16_t *portal_refs)
{
    memset(scene, 0, sizeof(*scene));
    *scene = (sm64_saturn_scene_admission_view_t){
        .metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION,
        .metadata_valid = 1U,
        .clusters = clusters, .cluster_count = 3U,
        .nodes = nodes, .node_count = 3U,
        .cluster_refs = cluster_refs, .cluster_ref_count = 3U,
        .portals = portals, .portal_count = 2U,
        .portal_refs = portal_refs, .portal_ref_count = 4U,
        .root_node = 0U,
    };
    scene->frustum.forward[2] = 65536;
    scene->frustum.right[0] = 65536;
    scene->frustum.up[1] = 65536;
    scene->frustum.near_depth = 1;
    scene->frustum.far_depth = 1000;
    scene->frustum.half_width = 100;
    scene->frustum.half_height = 100;
    scene->frustum.focal_length = 100;
    nodes[0] = (sm64_saturn_scene_admission_node_t){
        .bounds_min_q16 = {-20 * 65536, -8 * 65536, -30 * 65536},
        .bounds_max_q16 = {500 * 65536, 40 * 65536, 500 * 65536},
        .cluster_ref_first = 0U, .cluster_ref_count = 1U,
        .portal_ref_first = 0U, .portal_ref_count = 1U};
    nodes[1] = (sm64_saturn_scene_admission_node_t){
        .bounds_min_q16 = {20 * 65536, -8 * 65536, 20 * 65536},
        .bounds_max_q16 = {80 * 65536, 8 * 65536, 80 * 65536},
        .cluster_ref_first = 1U, .cluster_ref_count = 1U,
        .portal_ref_first = 1U, .portal_ref_count = 2U};
    nodes[2] = (sm64_saturn_scene_admission_node_t){
        .bounds_min_q16 = {390 * 65536, -8 * 65536, 120 * 65536},
        .bounds_max_q16 = {420 * 65536, 8 * 65536, 180 * 65536},
        .cluster_ref_first = 2U, .cluster_ref_count = 1U,
        .portal_ref_first = 3U, .portal_ref_count = 1U};
    cluster_refs[0] = 0U; cluster_refs[1] = 1U; cluster_refs[2] = 2U;
    portals[0] = (sm64_saturn_scene_admission_portal_window_t){
        .bounds_min_q16 = {16 * 65536, -4 * 65536, 16 * 65536},
        .bounds_max_q16 = {24 * 65536, 4 * 65536, 24 * 65536},
        .node_a = 0U, .node_b = 1U, .open = 1U};
    portals[1] = (sm64_saturn_scene_admission_portal_window_t){
        .bounds_min_q16 = {100 * 65536, -4 * 65536, 100 * 65536},
        .bounds_max_q16 = {110 * 65536, 4 * 65536, 110 * 65536},
        .node_a = 1U, .node_b = 2U, .open = 1U};
    portal_refs[0] = 0U; portal_refs[1] = 0U;
    portal_refs[2] = 1U; portal_refs[3] = 1U;
    clusters[0] = make_cluster(-4, 4, 20, 28, 0U);
    clusters[1] = make_cluster(30, 40, 30, 40, 0U);
    clusters[2] = make_cluster(400, 410, 150, 160, 0U);
}

static sm64_saturn_render_view_t view(void)
{
    sm64_saturn_render_view_t result = {0};
    result.view_forward_q16[2] = 65536;
    result.generation = 1U;
    return result;
}

int main(void)
{
    sm64_saturn_render_cluster_t clusters[3];
    sm64_saturn_scene_admission_node_t nodes[3];
    sm64_saturn_scene_admission_portal_window_t portals[2];
    uint16_t cluster_refs[3], portal_refs[4], admitted[4], admitted_portals[4];
    sm64_saturn_scene_admission_view_t scene;
    sm64_saturn_scene_admission_output_t output = {
        admitted, 4U, 0U, admitted_portals, 4U, 0U};
    sm64_saturn_scene_admission_stats_t stats;
    sm64_saturn_render_view_t camera = view();

    setup_scene(&scene, clusters, nodes, cluster_refs, portals, portal_refs);
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    assert(output.cluster_count == 2U && output.cluster_indices[0] == 0U &&
           output.cluster_indices[1] == 1U);
    assert(output.portal_count == 2U);

    /* Closed windows stop traversal, while a mandatory cluster survives an
     * outside frustum result. */
    portals[0].open = 0U;
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    assert(output.cluster_count == 1U && stats.portals_rejected_closed == 1U);
    clusters[0].mandatory = 1U;
    clusters[0].bounds_min_q16[2] = -20 * 65536;
    clusters[0].bounds_max_q16[2] = -10 * 65536;
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    assert(output.cluster_count == 1U && stats.mandatory_clusters_admitted == 1U);

    /* Wholly outside, behind-camera, near-plane intersection, and camera
     * inside bounds all use the same conservative frustum path. */
    portals[0].open = 1U;
    clusters[0] = make_cluster(300, 310, 20, 30, 0U);
    clusters[0].bounds_min_q16[2] = 0;
    clusters[0].bounds_max_q16[2] = 1 * 65536;
    nodes[0].bounds_min_q16[0] = -20 * 65536;
    nodes[0].bounds_max_q16[0] = 320 * 65536;
    portals[0].open = 0U;
    output.cluster_count = output.portal_count = 0U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    assert(stats.clusters_rejected_frustum != 0U);
    clusters[0] = make_cluster(-4, 4, -40, -20, 0U);
    output.cluster_count = output.portal_count = 0U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    clusters[0] = make_cluster(-4, 4, 0, 2, 0U);
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    clusters[0] = make_cluster(-4, 4, 20, 28, 0U);
    camera.camera_position_q16[2] = 24 * 65536;
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));

    /* Invalid node/ref/window and zero-cluster metadata fail closed before
     * traversal. */
    nodes[0].cluster_ref_count = 4U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    nodes[0].cluster_ref_count = 1U;
    cluster_refs[0] = 9U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    cluster_refs[0] = 0U;
    portals[0].node_b = 9U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    portals[0].node_b = 1U;
    scene.cluster_count = 0U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    scene.cluster_count = 3U;
    output.cluster_capacity = 0U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    assert(stats.output_exhausted != 0U);
    output.cluster_capacity = 4U;

    /* Yaw and pitch views use published lateral bases, not world Z. */
    setup_scene(&scene, clusters, nodes, cluster_refs, portals, portal_refs);
    scene.frustum.forward[0] = 46341; scene.frustum.forward[2] = 46341;
    scene.frustum.forward[1] = 0;
    scene.frustum.right[0] = 46341; scene.frustum.right[2] = -46341;
    memset(scene.frustum.right, 0, sizeof(scene.frustum.right));
    memset(scene.frustum.up, 0, sizeof(scene.frustum.up));
    clusters[0] = make_cluster(20, 30, -30, -20, 0U);
    nodes[0].bounds_min_q16[0] = 20 * 65536;
    nodes[0].bounds_max_q16[0] = 220 * 65536;
    camera = view(); camera.view_forward_q16[0] = 46341;
    camera.view_forward_q16[2] = 46341;
    camera.view_projection_q16[0][0] = 46341;
    camera.view_projection_q16[0][2] = -46341;
    camera.view_projection_q16[1][1] = 65536;
    output.cluster_capacity = 4U; output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    scene.frustum.forward[0] = 0; scene.frustum.forward[2] = 46341;
    scene.frustum.forward[1] = 46341;
    scene.frustum.right[0] = 65536; scene.frustum.right[2] = 0;
    clusters[0] = make_cluster(-4, 4, 20, 30, 0U);
    clusters[0].bounds_min_q16[1] = 20 * 65536;
    clusters[0].bounds_max_q16[1] = 30 * 65536;
    nodes[0].bounds_min_q16[1] = -8 * 65536;
    nodes[0].bounds_max_q16[1] = 30 * 65536;
    nodes[0].bounds_min_q16[0] = -8 * 65536;
    camera = view(); camera.view_forward_q16[1] = 46341;
    camera.view_forward_q16[2] = 46341;
    camera.view_projection_q16[0][0] = 65536;
    camera.view_projection_q16[1][1] = 46341;
    camera.view_projection_q16[1][2] = -46341;
    output.cluster_count = output.portal_count = 0U;
    assert(sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    return 0;
}
