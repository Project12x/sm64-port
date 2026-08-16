#include <assert.h>
#include <stdio.h>
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
    result.view_projection_q16[0][0] = 65536;
    result.view_projection_q16[1][1] = 65536;
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
    cluster_refs[2] = 1U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    cluster_refs[2] = 2U;
    clusters[0].reserved[0] = 1U;
    assert(!sm64_saturn_scene_admit(&scene, &camera, &output, &stats));
    clusters[0].reserved[0] = 0U;
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

    /* A cluster reachable from two nodes must be emitted once. The rest of
     * this fixture -- and BOB itself, where cluster_ref_count equals
     * cluster_count and every cluster is referenced exactly once -- never
     * produces a duplicate, so without this case the duplicate test is dead
     * code in every gate that covers this module. */
    {
        sm64_saturn_render_cluster_t shared_clusters[3];
        sm64_saturn_scene_admission_node_t shared_nodes[3];
        sm64_saturn_scene_admission_portal_window_t shared_portals[2];
        uint16_t shared_refs[4], shared_portal_refs[4];
        uint16_t shared_admitted[8], shared_admitted_portals[4];
        sm64_saturn_scene_admission_view_t shared_scene;
        sm64_saturn_scene_admission_output_t shared_output = {
            shared_admitted, 8U, 0U, shared_admitted_portals, 4U, 0U};
        sm64_saturn_scene_admission_stats_t shared_stats;
        sm64_saturn_render_view_t shared_camera = view();

        setup_scene(&shared_scene, shared_clusters, shared_nodes, shared_refs,
                    shared_portals, shared_portal_refs);
        /* refs become {0, 1, 1, 2}: node 0 owns clusters 0 and 1, node 1 owns
         * cluster 1 as well, node 2 owns cluster 2. Coverage still holds. */
        shared_refs[0] = 0U;
        shared_refs[1] = 1U;
        shared_refs[2] = 1U;
        shared_refs[3] = 2U;
        shared_scene.cluster_ref_count = 4U;
        shared_nodes[0].cluster_ref_first = 0U;
        shared_nodes[0].cluster_ref_count = 2U;
        shared_nodes[1].cluster_ref_first = 2U;
        shared_nodes[1].cluster_ref_count = 1U;
        shared_nodes[2].cluster_ref_first = 3U;
        shared_nodes[2].cluster_ref_count = 1U;
        assert(sm64_saturn_scene_admit(&shared_scene, &shared_camera,
                                       &shared_output, &shared_stats));
        assert(shared_output.cluster_count == 2U);
        assert(shared_output.cluster_indices[0] == 0U &&
               shared_output.cluster_indices[1] == 1U);
        assert(shared_stats.duplicate_clusters == 1U);
        assert(shared_stats.clusters_admitted == 2U);

        /* The same cluster, made mandatory and moved behind the camera, is
         * still emitted once: the trailing mandatory sweep must see the
         * traversal's own emission. */
        shared_clusters[1].mandatory = 1U;
        shared_clusters[1].bounds_min_q16[2] = -40 * 65536;
        shared_clusters[1].bounds_max_q16[2] = -30 * 65536;
        shared_nodes[0].bounds_min_q16[2] = -40 * 65536;
        shared_nodes[1].bounds_min_q16[2] = -40 * 65536;
        shared_output.cluster_count = shared_output.portal_count = 0U;
        assert(sm64_saturn_scene_admit(&shared_scene, &shared_camera,
                                       &shared_output, &shared_stats));
        assert(shared_output.cluster_count == 2U);
        assert(shared_stats.duplicate_clusters == 1U);
    }

    /* Bind-scoped metadata memoisation (T2.10 item 2). The opt-in is
     * fail-closed: every case above ran with metadata_immutable zero and
     * therefore revalidated on every call, which is why they still reject the
     * malformed variants they always did. These cases exercise the opt-in. */
    {
        sm64_saturn_render_cluster_t memo_clusters[3], other_clusters[3];
        sm64_saturn_scene_admission_node_t memo_nodes[3];
        sm64_saturn_scene_admission_portal_window_t memo_portals[2];
        uint16_t memo_refs[3], memo_portal_refs[4];
        uint16_t memo_admitted[8], memo_admitted_portals[4];
        sm64_saturn_scene_admission_view_t memo_scene, other_scene;
        sm64_saturn_scene_admission_output_t memo_output = {
            memo_admitted, 8U, 0U, memo_admitted_portals, 4U, 0U};
        sm64_saturn_scene_admission_stats_t memo_stats;
        sm64_saturn_render_view_t memo_camera = view();

        setup_scene(&memo_scene, memo_clusters, memo_nodes, memo_refs,
                    memo_portals, memo_portal_refs);
        memo_scene.metadata_immutable = 1U;
        assert(sm64_saturn_scene_admit(&memo_scene, &memo_camera, &memo_output,
                                       &memo_stats));
        assert(memo_output.cluster_count == 2U);
        const uint16_t first_count = memo_output.cluster_count;

        /* Binding the same immutable view again must produce the same result
         * from the memo. */
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(sm64_saturn_scene_admit(&memo_scene, &memo_camera, &memo_output,
                                       &memo_stats));
        assert(memo_output.cluster_count == first_count);

        /* The memo must be observable, or it could be silently dead and no
         * gate would notice. This mutates the metadata behind an unchanged
         * binding, which the immutability opt-in explicitly promises will not
         * happen; the point is to pin the contract's consequence, not to
         * endorse doing it. A caller that cannot make that promise leaves
         * metadata_immutable zero and gets the checks above instead. */
        memo_clusters[0].reserved[0] = 1U;
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(sm64_saturn_scene_admit(&memo_scene, &memo_camera, &memo_output,
                                       &memo_stats));
        memo_clusters[0].reserved[0] = 0U;

        /* A second immutable binding that differs from the memo only in its
         * cluster pointer must be validated, not served from the memo. */
        memcpy(other_clusters, memo_clusters, sizeof(other_clusters));
        other_clusters[0].reserved[0] = 1U;
        other_scene = memo_scene;
        other_scene.clusters = other_clusters;
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(!sm64_saturn_scene_admit(&other_scene, &memo_camera,
                                        &memo_output, &memo_stats));
        assert(memo_stats.malformed_metadata != 0U);

        /* Rejection must not be cached: the same malformed binding has to be
         * re-validated, and re-reported, on every attempt. */
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(!sm64_saturn_scene_admit(&other_scene, &memo_camera,
                                        &memo_output, &memo_stats));
        assert(memo_stats.malformed_metadata != 0U);

        /* Differing only in a count, and only in the metadata_valid byte, must
         * likewise miss the memo. */
        other_scene = memo_scene;
        other_scene.cluster_count = 2U;
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(!sm64_saturn_scene_admit(&other_scene, &memo_camera,
                                        &memo_output, &memo_stats));
        other_scene = memo_scene;
        other_scene.metadata_valid = 0U;
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(!sm64_saturn_scene_admit(&other_scene, &memo_camera,
                                        &memo_output, &memo_stats));

        /* An out-of-range opt-in value is malformed metadata, not a truthy
         * flag. */
        other_scene = memo_scene;
        other_scene.metadata_immutable = 2U;
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(!sm64_saturn_scene_admit(&other_scene, &memo_camera,
                                        &memo_output, &memo_stats));

        /* And the original binding still works after all of that. */
        memo_output.cluster_count = memo_output.portal_count = 0U;
        assert(sm64_saturn_scene_admit(&memo_scene, &memo_camera, &memo_output,
                                       &memo_stats));
        assert(memo_output.cluster_count == first_count);
    }

    /* Hierarchy metadata (Sprint 2 T2.12). The traversal prunes an OUTSIDE
     * subtree and admits an INSIDE one without testing it, so every structural
     * property that argument rests on has to fail closed rather than be
     * assumed of the baker. Without these cases the five new checks in
     * metadata_valid() would be untested by any gate. */
    {
        sm64_saturn_render_cluster_t tree_clusters[3];
        sm64_saturn_scene_admission_node_t tree_nodes[3];
        sm64_saturn_scene_admission_portal_window_t tree_portals[2];
        uint16_t tree_refs[3], tree_portal_refs[4];
        uint16_t tree_admitted[8], tree_admitted_portals[4];
        sm64_saturn_scene_admission_view_t tree_scene;
        sm64_saturn_scene_admission_output_t tree_output = {
            tree_admitted, 8U, 0U, tree_admitted_portals, 4U, 0U};
        sm64_saturn_scene_admission_stats_t tree_stats;
        sm64_saturn_render_view_t tree_camera = view();
        uint16_t flat_count;

        /* Baseline: the flat arrangement this fixture has used throughout. */
        setup_scene(&tree_scene, tree_clusters, tree_nodes, tree_refs,
                    tree_portals, tree_portal_refs);
        assert(sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                       &tree_stats));
        flat_count = tree_output.cluster_count;

        /* The same three nodes as a hierarchy: node 0 is the parent of 1 and
         * 2, whose bounds are already inside its own. Portal edges stay, so
         * this also covers a node reachable both as a child and through a
         * window. The admitted set must not move. */
        tree_nodes[0].child_first = 1U;
        tree_nodes[0].child_count = 2U;
        tree_output.cluster_count = tree_output.portal_count = 0U;
        assert(sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                       &tree_stats));
        assert(tree_output.cluster_count == flat_count);
        assert(tree_stats.nodes_tested >= 3U);

        /* A child index at or below its parent's would let the descent
         * revisit an ancestor. Node 2 is given node 1 as a child, with node 2
         * widened to contain it, so this case is rejected by the ordering
         * rule alone and not by containment or by the forest rule. */
        tree_nodes[0].child_first = 0U;
        tree_nodes[0].child_count = 0U;
        tree_nodes[2].bounds_min_q16[0] = 20 * 65536;
        tree_nodes[2].bounds_min_q16[2] = 20 * 65536;
        tree_nodes[2].child_first = 1U;
        tree_nodes[2].child_count = 1U;
        assert(!sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                        &tree_stats));
        assert(tree_stats.malformed_metadata != 0U);
        tree_nodes[2].bounds_min_q16[0] = 390 * 65536;
        tree_nodes[2].bounds_min_q16[2] = 120 * 65536;
        tree_nodes[2].child_first = 0U;
        tree_nodes[2].child_count = 0U;
        tree_nodes[0].child_first = 1U;
        tree_nodes[0].child_count = 2U;

        /* A child range past the node count. */
        tree_nodes[0].child_first = 2U;
        tree_nodes[0].child_count = 2U;
        assert(!sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                        &tree_stats));

        /* A leaf must publish a zero child_first: this is the word that used
         * to be `reserved`, and a stale value must not read as a child. */
        tree_nodes[0].child_first = 1U;
        tree_nodes[0].child_count = 0U;
        assert(!sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                        &tree_stats));

        /* Child bounds must be contained in the parent's. This is the
         * property that makes a node's bounds a bound on its subtree, and
         * therefore the property the OUTSIDE prune rests on. */
        tree_nodes[0].child_first = 1U;
        tree_nodes[0].child_count = 2U;
        tree_nodes[1].bounds_max_q16[0] = 900 * 65536;
        assert(!sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                        &tree_stats));
        tree_nodes[1].bounds_max_q16[0] = 80 * 65536;

        /* Two parents claiming one child would make the admitted set depend
         * on which edge the queue reached first. Node 1 is widened to contain
         * node 2 so that the double claim, and not containment, is what this
         * case rejects. */
        tree_nodes[1].bounds_max_q16[0] = 430 * 65536;
        tree_nodes[1].bounds_max_q16[2] = 190 * 65536;
        tree_nodes[1].child_first = 2U;
        tree_nodes[1].child_count = 1U;
        assert(!sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                        &tree_stats));
        assert(tree_stats.malformed_metadata != 0U);
        tree_nodes[1].bounds_max_q16[0] = 80 * 65536;
        tree_nodes[1].bounds_max_q16[2] = 80 * 65536;
        tree_nodes[1].child_first = 0U;
        tree_nodes[1].child_count = 0U;

        /* And the root may not be anyone's child. */
        tree_nodes[2].child_first = 0U;
        tree_nodes[2].child_count = 1U;
        assert(!sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                        &tree_stats));
        tree_nodes[2].child_first = 0U;
        tree_nodes[2].child_count = 0U;

        /* Back to the valid hierarchy, which must still work. */
        tree_output.cluster_count = tree_output.portal_count = 0U;
        assert(sm64_saturn_scene_admit(&tree_scene, &tree_camera, &tree_output,
                                       &tree_stats));
        assert(tree_output.cluster_count == flat_count);
    }

    printf("scene admission fixture: PASS\n");
    return 0;
}
