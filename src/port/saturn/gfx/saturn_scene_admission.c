#include "saturn_scene_admission.h"

#include <limits.h>
#include <string.h>

static int32_t floor_q16(int32_t value)
{
    if (value >= 0) return value >> 16;
    return -(int32_t)(((int64_t)(-(int64_t)value) + 65535LL) >> 16);
}

static int32_t ceil_q16(int32_t value)
{
    if (value >= 0) return (int32_t)(((int64_t)value + 65535LL) >> 16);
    return -(int32_t)((int64_t)(-(int64_t)value) >> 16);
}

static bool bounds_valid(const int32_t minimum[3], const int32_t maximum[3])
{
    uint8_t axis;
    for (axis = 0U; axis < 3U; axis++)
        if (minimum[axis] > maximum[axis]) return false;
    return true;
}

static void world_bounds(const int32_t minimum_q16[3],
                         const int32_t maximum_q16[3], int32_t minimum[3],
                         int32_t maximum[3])
{
    uint8_t axis;
    for (axis = 0U; axis < 3U; axis++) {
        minimum[axis] = floor_q16(minimum_q16[axis]);
        maximum[axis] = ceil_q16(maximum_q16[axis]);
    }
}

static bool has_lateral_basis(const sm64_saturn_ztreme_frustum_t *frustum)
{
    return frustum->right[0] != 0 || frustum->right[1] != 0 ||
           frustum->right[2] != 0 || frustum->up[0] != 0 ||
           frustum->up[1] != 0 || frustum->up[2] != 0;
}

static sm64_saturn_ztreme_frustum_t admission_frustum(
    const sm64_saturn_scene_admission_view_t *scene,
    const sm64_saturn_render_view_t *view)
{
    sm64_saturn_ztreme_frustum_t frustum = scene->frustum;
    uint8_t axis;
    for (axis = 0U; axis < 3U; axis++)
        frustum.position[axis] = floor_q16(view->camera_position_q16[axis]);
    if (frustum.forward[0] == 0 && frustum.forward[1] == 0 &&
        frustum.forward[2] == 0) {
        frustum.forward[0] = view->view_forward_q16[0];
        frustum.forward[1] = view->view_forward_q16[1];
        frustum.forward[2] = view->view_forward_q16[2];
    }
    if (frustum.near_depth <= 0) frustum.near_depth = 1;
    if (frustum.far_depth <= frustum.near_depth)
        frustum.far_depth = INT32_MAX;
    if (frustum.focal_length <= 0) frustum.focal_length = 1;
    /* A package may omit lateral planes while a camera is still yawed or
     * pitched. Infinite lateral limits are conservative: they can draw extra
     * geometry, but never discard visible geometry. */
    if (!has_lateral_basis(&frustum)) {
        frustum.half_width = INT32_MAX;
        frustum.half_height = INT32_MAX;
        frustum.right[0] = frustum.up[0] = 0;
        frustum.right[1] = frustum.up[1] = 0;
        frustum.right[2] = frustum.up[2] = 0;
    } else {
        if (frustum.half_width <= 0) frustum.half_width = 320;
        if (frustum.half_height <= 0) frustum.half_height = 240;
    }
    return frustum;
}

static sm64_saturn_ztreme_frustum_result_t test_bounds(
    const sm64_saturn_ztreme_frustum_t *frustum,
    const int32_t minimum_q16[3], const int32_t maximum_q16[3])
{
    int32_t minimum[3], maximum[3];
    world_bounds(minimum_q16, maximum_q16, minimum, maximum);
    return sm64_saturn_ztreme_frustum_aabb(frustum, minimum, maximum);
}

static bool output_has_cluster(const sm64_saturn_scene_admission_output_t *output,
                               uint16_t cluster)
{
    uint16_t index;
    for (index = 0U; index < output->cluster_count; index++)
        if (output->cluster_indices[index] == cluster) return true;
    return false;
}

static bool output_has_portal(const sm64_saturn_scene_admission_output_t *output,
                              uint16_t portal)
{
    uint16_t index;
    for (index = 0U; index < output->portal_count; index++)
        if (output->portal_indices[index] == portal) return true;
    return false;
}

static bool metadata_valid(const sm64_saturn_scene_admission_view_t *scene,
                           sm64_saturn_scene_admission_stats_t *stats)
{
    uint16_t index;
    if (scene->metadata_version != SM64_SATURN_SCENE_ADMISSION_VERSION ||
        scene->metadata_valid == 0U || scene->cluster_count == 0U ||
        scene->cluster_count > SM64_SATURN_SCENE_ADMISSION_MAX_REFS ||
        scene->node_count == 0U ||
        scene->node_count > SM64_SATURN_SCENE_ADMISSION_MAX_NODES ||
        scene->portal_count > SM64_SATURN_SCENE_ADMISSION_MAX_PORTALS ||
        scene->cluster_ref_count > SM64_SATURN_SCENE_ADMISSION_MAX_REFS ||
        scene->portal_ref_count > SM64_SATURN_SCENE_ADMISSION_MAX_REFS ||
        scene->root_node >= scene->node_count || scene->clusters == NULL ||
        scene->nodes == NULL ||
        (scene->cluster_ref_count != 0U && scene->cluster_refs == NULL) ||
        (scene->portal_count != 0U && scene->portals == NULL) ||
        (scene->portal_ref_count != 0U && scene->portal_refs == NULL)) {
        stats->malformed_metadata = 1U;
        stats->zero_clusters = scene->cluster_count == 0U;
        return false;
    }
    for (index = 0U; index < scene->cluster_count; index++) {
        const sm64_saturn_render_cluster_t *cluster = &scene->clusters[index];
        if (!bounds_valid(cluster->bounds_min_q16, cluster->bounds_max_q16) ||
            cluster->primitive_count == 0U ||
            (cluster->position_ref_count[SATURN_LOD_NEAR] == 0U &&
             cluster->mandatory == 0U)) {
            stats->malformed_metadata = 1U;
            return false;
        }
    }
    for (index = 0U; index < scene->node_count; index++) {
        const sm64_saturn_scene_admission_node_t *node = &scene->nodes[index];
        uint32_t cluster_end = (uint32_t)node->cluster_ref_first +
                               node->cluster_ref_count;
        uint32_t portal_end = (uint32_t)node->portal_ref_first +
                              node->portal_ref_count;
        if (!bounds_valid(node->bounds_min_q16, node->bounds_max_q16) ||
            cluster_end > scene->cluster_ref_count ||
            portal_end > scene->portal_ref_count) {
            stats->malformed_metadata = 1U;
            return false;
        }
    }
    for (index = 0U; index < scene->cluster_ref_count; index++)
        if (scene->cluster_refs[index] >= scene->cluster_count) {
            stats->malformed_metadata = 1U;
            return false;
        }
    for (index = 0U; index < scene->portal_count; index++) {
        const sm64_saturn_scene_admission_portal_window_t *portal =
            &scene->portals[index];
        if (!bounds_valid(portal->bounds_min_q16, portal->bounds_max_q16) ||
            portal->node_a >= scene->node_count ||
            portal->node_b >= scene->node_count || portal->node_a == portal->node_b ||
            portal->reserved[0] != 0U || portal->reserved[1] != 0U ||
            portal->reserved[2] != 0U) {
            stats->malformed_metadata = 1U;
            return false;
        }
    }
    for (index = 0U; index < scene->portal_ref_count; index++)
        if (scene->portal_refs[index] >= scene->portal_count) {
            stats->malformed_metadata = 1U;
            return false;
        }
    return true;
}

bool sm64_saturn_scene_admit(
    const sm64_saturn_scene_admission_view_t *scene,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_scene_admission_output_t *output,
    sm64_saturn_scene_admission_stats_t *stats)
{
    sm64_saturn_ztreme_frustum_t frustum;
    uint8_t visited[SM64_SATURN_SCENE_ADMISSION_MAX_NODES];
    uint16_t queue[SM64_SATURN_SCENE_ADMISSION_MAX_NODES];
    uint16_t queue_head = 0U, queue_tail = 0U;
    uint16_t index;
    bool success = true;
    if (stats != NULL) memset(stats, 0, sizeof(*stats));
    if (output != NULL) {
        output->cluster_count = 0U;
        output->portal_count = 0U;
    }
    if (scene == NULL || view == NULL || output == NULL || stats == NULL ||
        output->cluster_indices == NULL ||
        (output->portal_capacity != 0U && output->portal_indices == NULL) ||
        view->generation == 0U ||
        (view->view_forward_q16[0] == 0 && view->view_forward_q16[1] == 0 &&
         view->view_forward_q16[2] == 0)) {
        if (stats != NULL) stats->malformed_metadata = 1U;
        return false;
    }
    if (!metadata_valid(scene, stats)) return false;
    memset(visited, 0, sizeof(visited));
    frustum = admission_frustum(scene, view);
    queue[queue_tail++] = scene->root_node;
    while (queue_head < queue_tail) {
        const uint16_t node_index = queue[queue_head++];
        const sm64_saturn_scene_admission_node_t *node;
        sm64_saturn_ztreme_frustum_result_t node_state;
        if (visited[node_index] != 0U) {
            stats->cycle_edges++;
            continue;
        }
        visited[node_index] = 1U;
        node = &scene->nodes[node_index];
        stats->nodes_tested++;
        node_state = test_bounds(&frustum, node->bounds_min_q16,
                                 node->bounds_max_q16);
        if (node_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE) continue;
        stats->nodes_admitted++;
        for (index = 0U; index < node->cluster_ref_count; index++) {
            const uint16_t cluster_index = scene->cluster_refs[
                node->cluster_ref_first + index];
            const sm64_saturn_render_cluster_t *cluster =
                &scene->clusters[cluster_index];
            const sm64_saturn_ztreme_frustum_result_t cluster_state =
                test_bounds(&frustum, cluster->bounds_min_q16,
                            cluster->bounds_max_q16);
            stats->clusters_tested++;
            if (cluster_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE &&
                cluster->mandatory == 0U) {
                stats->clusters_rejected_frustum++;
                continue;
            }
            if (output_has_cluster(output, cluster_index)) {
                stats->duplicate_clusters++;
                continue;
            }
            if (output->cluster_count >= output->cluster_capacity) {
                stats->output_exhausted = 1U;
                success = false;
                continue;
            }
            output->cluster_indices[output->cluster_count++] = cluster_index;
            stats->clusters_admitted++;
            if (cluster->mandatory != 0U &&
                cluster_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE)
                stats->mandatory_clusters_admitted++;
        }
        for (index = 0U; index < node->portal_ref_count; index++) {
            const uint16_t portal_index = scene->portal_refs[
                node->portal_ref_first + index];
            const sm64_saturn_scene_admission_portal_window_t *portal =
                &scene->portals[portal_index];
            uint16_t destination;
            sm64_saturn_ztreme_frustum_result_t portal_state;
            stats->portals_tested++;
            if (portal->open == 0U) {
                stats->portals_rejected_closed++;
                continue;
            }
            stats->portals_open++;
            portal_state = test_bounds(&frustum, portal->bounds_min_q16,
                                       portal->bounds_max_q16);
            if (portal_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE) {
                stats->portals_rejected_frustum++;
                continue;
            }
            if (!output_has_portal(output, portal_index)) {
                if (output->portal_count >= output->portal_capacity) {
                    stats->output_exhausted = 1U;
                    success = false;
                } else {
                    output->portal_indices[output->portal_count++] = portal_index;
                }
            }
            destination = portal->node_a == node_index ? portal->node_b :
                         portal->node_a;
            if (visited[destination] != 0U) {
                stats->cycle_edges++;
            } else if (queue_tail >= SM64_SATURN_SCENE_ADMISSION_MAX_NODES) {
                stats->output_exhausted = 1U;
                success = false;
            } else {
                queue[queue_tail++] = destination;
            }
        }
    }
    return success && output->cluster_count != 0U;
}
