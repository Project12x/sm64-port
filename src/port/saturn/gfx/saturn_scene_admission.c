#include "saturn_scene_admission.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

/* T2.9 (docs/saturn/evidence/reports/sprint2-t2_9-spatial-admit-audit.md).
 * Sub-span instrumentation only.  Every addition below is inside
 * #if SM64_SATURN_ADMIT_DIAG, and the product arm of every #else is the
 * pre-T2.9 statement verbatim, so a SATURN_DIAGNOSTIC_MODE=0 object is
 * byte-identical to HEAD's. */
#if defined(SATURN_DIAGNOSTIC_MODE) && SATURN_DIAGNOSTIC_MODE != 0 && \
    defined(__sh__)
#include "../runtime/saturn_prenotify_profile.h"
#define SM64_SATURN_ADMIT_DIAG 1
#else
#define SM64_SATURN_ADMIT_DIAG 0
#endif

/* Admission is completed by the master before either SH-2 receives the
 * render job. Its bounded traversal scratch is supplied by the caller so the
 * sourceboot renderer can reuse an otherwise-dead LWRAM result bank during
 * this phase instead of reserving another permanent 12 KiB allocation. */
static sm64_saturn_scene_admission_scratch_t *s_admission_scratch;
#define s_admission_visited (s_admission_scratch->visited)
#define s_admission_queued (s_admission_scratch->queued)
#define s_admission_queue (s_admission_scratch->queue)
#define s_admission_cluster_seen (s_admission_scratch->cluster_seen)

/* Each queue slot carries the node index and the frustum classification it
 * inherits from its parent, so the descent needs no second bank and no extra
 * memory (Sprint 2 T2.12). Two bits are enough for the tri-state result and
 * MAX_NODES leaves them free. */
#define ADMISSION_QUEUE_STATE_SHIFT 14U
#define ADMISSION_QUEUE_NODE_MASK 0x3FFFU
_Static_assert(SM64_SATURN_SCENE_ADMISSION_MAX_NODES <=
                   ADMISSION_QUEUE_NODE_MASK + 1U,
               "admission queue packs the node index below the state bits");
_Static_assert(SM64_SATURN_ZTREME_FRUSTUM_INSIDE <= 3,
               "admission queue packs the frustum result into two bits");

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
    /* Orientation is frame-owned. Package metadata may provide limits, but
     * it can never override the immutable view's forward basis. */
    frustum.forward[0] = view->view_forward_q16[0];
    frustum.forward[1] = view->view_forward_q16[1];
    frustum.forward[2] = view->view_forward_q16[2];
    if (view->view_projection_q16[0][0] != 0 ||
        view->view_projection_q16[0][1] != 0 ||
        view->view_projection_q16[0][2] != 0) {
        frustum.right[0] = view->view_projection_q16[0][0];
        frustum.right[1] = view->view_projection_q16[0][1];
        frustum.right[2] = view->view_projection_q16[0][2];
    }
    if (view->view_projection_q16[1][0] != 0 ||
        view->view_projection_q16[1][1] != 0 ||
        view->view_projection_q16[1][2] != 0) {
        frustum.up[0] = view->view_projection_q16[1][0];
        frustum.up[1] = view->view_projection_q16[1][1];
        frustum.up[2] = view->view_projection_q16[1][2];
    }
    if (view->view_projection_q16[0][0] == 0 &&
        view->view_projection_q16[0][1] == 0 &&
        view->view_projection_q16[0][2] == 0)
        memset(frustum.right, 0, sizeof(frustum.right));
    if (view->view_projection_q16[1][0] == 0 &&
        view->view_projection_q16[1][1] == 0 &&
        view->view_projection_q16[1][2] == 0)
        memset(frustum.up, 0, sizeof(frustum.up));
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

/* Hierarchy safety margin, in world units, applied to a node's bounds and to
 * nothing else (Sprint 2 T2.12).
 *
 * Pruning a subtree because its node tested OUTSIDE is only equivalent to
 * testing every cluster in it if OUTSIDE(node) implies OUTSIDE(cluster) for
 * every contained cluster. Containment of the *true* boxes is validated by
 * metadata_valid(), but the classifier does not test true boxes: it converts
 * each AABB to a centre/half-extent pair in integers
 * (ztreme_frustum.c:85-91) and then floors the projected centre and ceils the
 * projected support radius. Each of those steps is individually conservative
 * -- it can only widen a box -- but they widen the *cluster* and the *node*
 * independently, so a small cluster's widened projection can poke past a large
 * node's widened projection. The equivalence oracle found exactly that: 102
 * poses where the flat pass admitted a cluster whose node the hierarchy had
 * pruned, all of them within a couple of world units of the near plane.
 *
 * The bound: the centre/extent quantisation places the reconstructed box
 * inside [min - 1, max + 1] per axis, whose projection onto a Q16 unit basis
 * is at most sum|basis| <= sqrt(3) < 1.74 wider per side; the floor/ceil pair
 * adds at most 1 more. So a contained cluster's computed projected extent
 * exceeds the node's by less than 2.74 + 1 = 3.74 world units, and widening
 * the node by d raises its own projected extent by at least d (sum|basis| >= 1
 * for a unit basis). Eight units is that bound with better than a factor of
 * two in hand, against a scene that spans +-8,192.
 *
 * Widening is conservative in both directions and cannot admit less: a wider
 * node is harder to classify OUTSIDE, so fewer subtrees are pruned, and harder
 * to classify INSIDE, so fewer subtrees skip their own tests. Removing it is
 * killed by verify-admission-hierarchy. */
#define ADMISSION_NODE_MARGIN 8

static sm64_saturn_ztreme_frustum_result_t test_node_bounds(
    const sm64_saturn_ztreme_frustum_t *frustum,
    const int32_t minimum_q16[3], const int32_t maximum_q16[3])
{
    int32_t minimum[3], maximum[3];
    uint8_t axis;
    world_bounds(minimum_q16, maximum_q16, minimum, maximum);
    /* world_bounds() has already reduced Q16.16 to world units, so both
     * magnitudes are bounded by 32,768 and the margin cannot overflow. */
    for (axis = 0U; axis < 3U; axis++) {
        minimum[axis] -= ADMISSION_NODE_MARGIN;
        maximum[axis] += ADMISSION_NODE_MARGIN;
    }
    return sm64_saturn_ztreme_frustum_aabb(frustum, minimum, maximum);
}

/* Cluster membership in the admission output is an O(1) bit test against
 * s_admission_cluster_seen[], not a scan of the output list.
 *
 * T2.9 measured the scan this replaces at 27.7% of demo_spatial_admit() --
 * exactly K(K-1)/2 = 39,903 uint16 comparisons per frame for K = 283 admitted
 * clusters -- plus a further 6.6% for the 96 embedded scans the trailing
 * mandatory sweep performed, and it found zero duplicates in 1,330 frames.
 * The membership array was already allocated in the traversal scratch for
 * metadata_valid()'s coverage sweep and is dead for the rest of the call, so
 * this costs no memory.
 *
 * The invariant is that s_admission_cluster_seen[c] is non-zero exactly when
 * c already appears in output->cluster_indices[0 .. cluster_count). It is
 * established by clearing the array in the same place output->cluster_count
 * is known to be zero, and maintained by setting the byte at each of the two
 * append sites and nowhere else. Every index reaching it is below
 * scene->cluster_count, which metadata_valid() bounds by
 * SM64_SATURN_SCENE_ADMISSION_MAX_REFS -- the array's own size.
 *
 * Note that metadata_valid()'s own memset is NOT the clear this invariant
 * needs: that sweep leaves the array marked 1 for every referenced cluster.
 * The clear below is a separate statement in the admission path, which is
 * also what keeps the invariant true once metadata_valid() is memoised. */

static bool output_has_portal(const sm64_saturn_scene_admission_output_t *output,
                              uint16_t portal)
{
    uint16_t index;
    for (index = 0U; index < output->portal_count; index++)
        if (output->portal_indices[index] == portal) return true;
    return false;
}

/* Mirrors sm64_saturn_scene_admission_view_t up to, but not including,
 * `frustum`: that is every field metadata_valid() reads and no field it does
 * not. The static assertion below is the mechanical half of that claim --
 * adding a field to the view's prefix without adding it here fails the build
 * instead of quietly narrowing the memo key. If a future ABI ever pads the two
 * differently, revisit the key rather than deleting the assertion. */
typedef struct metadata_memo {
    uint16_t metadata_version;
    uint8_t metadata_valid;
    uint8_t reserved0;
    uint8_t metadata_immutable;
    uint8_t reserved2[3];
    const sm64_saturn_render_cluster_t *clusters;
    uint16_t cluster_count;
    const sm64_saturn_scene_admission_node_t *nodes;
    uint16_t node_count;
    const uint16_t *cluster_refs;
    uint16_t cluster_ref_count;
    const sm64_saturn_scene_admission_portal_window_t *portals;
    uint16_t portal_count;
    const uint16_t *portal_refs;
    uint16_t portal_ref_count;
    uint16_t root_node;
    uint16_t reserved1;
} metadata_memo_t;

_Static_assert(sizeof(metadata_memo_t) ==
                   offsetof(sm64_saturn_scene_admission_view_t, frustum),
               "metadata memo key must mirror every view field the validator "
               "reads");

/* Zero-initialised, and an all-zero key can never match a bindable view: a
 * valid one has metadata_immutable non-zero and clusters non-NULL. There is
 * therefore no separate liveness flag. Only successful validations are
 * recorded, so a rejected package is re-validated -- and re-reported through
 * stats -- on every attempt. */
static metadata_memo_t s_metadata_memo;

static bool metadata_memo_hit(const sm64_saturn_scene_admission_view_t *scene)
{
    /* An early-out, not the safety property: metadata_immutable is also a
     * member of the key below, so a view that has not opted in could not match
     * a stored key even without this line. */
    if (scene->metadata_immutable == 0U) return false;
    return s_metadata_memo.metadata_version == scene->metadata_version &&
           s_metadata_memo.metadata_valid == scene->metadata_valid &&
           s_metadata_memo.reserved0 == scene->reserved0 &&
           s_metadata_memo.metadata_immutable == scene->metadata_immutable &&
           s_metadata_memo.reserved2[0] == scene->reserved2[0] &&
           s_metadata_memo.reserved2[1] == scene->reserved2[1] &&
           s_metadata_memo.reserved2[2] == scene->reserved2[2] &&
           s_metadata_memo.clusters == scene->clusters &&
           s_metadata_memo.cluster_count == scene->cluster_count &&
           s_metadata_memo.nodes == scene->nodes &&
           s_metadata_memo.node_count == scene->node_count &&
           s_metadata_memo.cluster_refs == scene->cluster_refs &&
           s_metadata_memo.cluster_ref_count == scene->cluster_ref_count &&
           s_metadata_memo.portals == scene->portals &&
           s_metadata_memo.portal_count == scene->portal_count &&
           s_metadata_memo.portal_refs == scene->portal_refs &&
           s_metadata_memo.portal_ref_count == scene->portal_ref_count &&
           s_metadata_memo.root_node == scene->root_node &&
           s_metadata_memo.reserved1 == scene->reserved1;
}

static void metadata_memo_store(
    const sm64_saturn_scene_admission_view_t *scene)
{
    s_metadata_memo.metadata_version = scene->metadata_version;
    s_metadata_memo.metadata_valid = scene->metadata_valid;
    s_metadata_memo.reserved0 = scene->reserved0;
    s_metadata_memo.metadata_immutable = scene->metadata_immutable;
    s_metadata_memo.reserved2[0] = scene->reserved2[0];
    s_metadata_memo.reserved2[1] = scene->reserved2[1];
    s_metadata_memo.reserved2[2] = scene->reserved2[2];
    s_metadata_memo.clusters = scene->clusters;
    s_metadata_memo.cluster_count = scene->cluster_count;
    s_metadata_memo.nodes = scene->nodes;
    s_metadata_memo.node_count = scene->node_count;
    s_metadata_memo.cluster_refs = scene->cluster_refs;
    s_metadata_memo.cluster_ref_count = scene->cluster_ref_count;
    s_metadata_memo.portals = scene->portals;
    s_metadata_memo.portal_count = scene->portal_count;
    s_metadata_memo.portal_refs = scene->portal_refs;
    s_metadata_memo.portal_ref_count = scene->portal_ref_count;
    s_metadata_memo.root_node = scene->root_node;
    s_metadata_memo.reserved1 = scene->reserved1;
}

static bool metadata_valid(const sm64_saturn_scene_admission_view_t *scene,
                           sm64_saturn_scene_admission_stats_t *stats)
{
    uint16_t index;
    if (scene->metadata_version != SM64_SATURN_SCENE_ADMISSION_VERSION ||
        scene->metadata_valid == 0U || scene->cluster_count == 0U ||
        scene->reserved0 != 0U || scene->reserved1 != 0U ||
        scene->metadata_immutable > 1U || scene->reserved2[0] != 0U ||
        scene->reserved2[1] != 0U || scene->reserved2[2] != 0U ||
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
            cluster->reserved[0] != 0U || cluster->reserved[1] != 0U ||
            cluster->reserved[2] != 0U ||
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
        uint32_t child_end = (uint32_t)node->child_first + node->child_count;
        if (!bounds_valid(node->bounds_min_q16, node->bounds_max_q16) ||
            cluster_end > scene->cluster_ref_count ||
            portal_end > scene->portal_ref_count ||
            (node->child_count == 0U && node->child_first != 0U) ||
            child_end > scene->node_count ||
            (node->child_count != 0U && node->child_first <= index)) {
            stats->malformed_metadata = 1U;
            return false;
        }
        /* A child's bounds must lie inside its parent's. Together with the
         * per-node cluster containment below, this makes a node's bounds a
         * bound on its entire subtree by induction -- the property the
         * traversal's OUTSIDE prune and INSIDE short-circuit rest on. */
        for (uint16_t child = node->child_first; child < (uint16_t)child_end;
             child++) {
            const sm64_saturn_scene_admission_node_t *kid = &scene->nodes[child];
            for (uint8_t axis = 0U; axis < 3U; axis++)
                if (kid->bounds_min_q16[axis] < node->bounds_min_q16[axis] ||
                    kid->bounds_max_q16[axis] > node->bounds_max_q16[axis]) {
                    stats->malformed_metadata = 1U;
                    return false;
                }
        }
        for (uint16_t ref = 0U; ref < node->cluster_ref_count; ref++) {
            const uint16_t cluster_index = scene->cluster_refs[
                node->cluster_ref_first + ref];
            if (cluster_index >= scene->cluster_count) {
                stats->malformed_metadata = 1U;
                return false;
            }
            const sm64_saturn_render_cluster_t *cluster =
                &scene->clusters[cluster_index];
            for (uint8_t axis = 0U; axis < 3U; axis++)
                if (cluster->bounds_min_q16[axis] < node->bounds_min_q16[axis] ||
                    cluster->bounds_max_q16[axis] > node->bounds_max_q16[axis]) {
                    stats->malformed_metadata = 1U;
                    return false;
                }
        }
    }
    /* The hierarchy must be a forest rooted at root_node: no node is claimed
     * by two parents and the root is claimed by none. Without this, a node
     * reachable from an INSIDE parent and an OUTSIDE parent would be resolved
     * by whichever edge the queue happened to reach first, and the admitted
     * set would depend on traversal order. Borrows the visited[] bank, which
     * the admission path clears for itself before the traversal. */
    memset(s_admission_visited, 0, scene->node_count);
    for (index = 0U; index < scene->node_count; index++) {
        const sm64_saturn_scene_admission_node_t *node = &scene->nodes[index];
        for (uint16_t ref = 0U; ref < node->child_count; ref++) {
            const uint16_t child = (uint16_t)(node->child_first + ref);
            if (child == scene->root_node ||
                s_admission_visited[child] != 0U) {
                stats->malformed_metadata = 1U;
                return false;
            }
            s_admission_visited[child] = 1U;
        }
    }
    for (index = 0U; index < scene->cluster_ref_count; index++)
        if (scene->cluster_refs[index] >= scene->cluster_count) {
            stats->malformed_metadata = 1U;
            return false;
        }
    memset(s_admission_cluster_seen, 0, scene->cluster_count);
    for (index = 0U; index < scene->cluster_ref_count; index++)
        s_admission_cluster_seen[scene->cluster_refs[index]] = 1U;
    for (index = 0U; index < scene->cluster_count; index++)
        if (s_admission_cluster_seen[index] == 0U) {
            stats->malformed_metadata = 1U;
            return false;
        }
    for (index = 0U; index < scene->portal_count; index++) {
        const sm64_saturn_scene_admission_portal_window_t *portal =
            &scene->portals[index];
        if (!bounds_valid(portal->bounds_min_q16, portal->bounds_max_q16) ||
            portal->node_a >= scene->node_count ||
            portal->node_b >= scene->node_count || portal->node_a == portal->node_b ||
            portal->open > 1U ||
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
    for (index = 0U; index < scene->node_count; index++) {
        const sm64_saturn_scene_admission_node_t *node = &scene->nodes[index];
        uint16_t ref;
        for (ref = 0U; ref < node->portal_ref_count; ref++) {
            const uint16_t portal_index = scene->portal_refs[
                node->portal_ref_first + ref];
            const sm64_saturn_scene_admission_portal_window_t *portal =
                &scene->portals[portal_index];
            if (index != portal->node_a && index != portal->node_b) {
                stats->malformed_metadata = 1U;
                return false;
            }
        }
    }
    /* Every edge must be represented by both endpoint adjacency lists. This
     * removes the old traversal guess where a ref was interpreted as the
     * opposite endpoint even when the package omitted the relationship. */
    for (index = 0U; index < scene->portal_count; index++) {
        const sm64_saturn_scene_admission_portal_window_t *portal =
            &scene->portals[index];
        uint16_t endpoint, node_index;
        for (endpoint = 0U; endpoint < 2U; endpoint++) {
            const uint16_t node_id = endpoint == 0U ? portal->node_a : portal->node_b;
            bool found = false;
            for (node_index = 0U; node_index < scene->node_count; node_index++) {
                const sm64_saturn_scene_admission_node_t *node = &scene->nodes[node_index];
                if (node_index != node_id) continue;
                uint16_t ref;
                for (ref = 0U; ref < node->portal_ref_count; ref++)
                    if (scene->portal_refs[node->portal_ref_first + ref] == index)
                        found = true;
            }
            if (!found) {
                stats->malformed_metadata = 1U;
                return false;
            }
        }
    }
    return true;
}

bool sm64_saturn_scene_admit_with_scratch(
    const sm64_saturn_scene_admission_view_t *scene,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_scene_admission_output_t *output,
    sm64_saturn_scene_admission_stats_t *stats,
    sm64_saturn_scene_admission_scratch_t *scratch)
{
    sm64_saturn_ztreme_frustum_t frustum;
    uint16_t queue_head = 0U, queue_tail = 0U;
    uint16_t index;
    bool success = true;
    if (scratch == NULL) return false;
#if SM64_SATURN_ADMIT_DIAG
    sm64_saturn_prenotify_admit_t admit;
    uint16_t admit_entry;
    uint16_t admit_cursor;
    memset(&admit, 0, sizeof(admit));
    admit_entry = sm64_saturn_prenotify_profile_frt();
    admit_cursor = admit_entry;
#endif
    s_admission_scratch = scratch;
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
    /* T2.9 Finding C: metadata_valid() re-proves properties of data that is
     * static const in a generated header, and it measured a flat 1,700-1,701
     * FRT ticks in every one of 1,330 frames -- 10.6% of the stage, 0.485
     * VBlanks -- for ~3,470 loop iterations and ~19,400 uncached cartridge
     * reads. Bind-scoped memoisation removes all of it for callers that can
     * honour the immutability contract, and changes nothing for callers that
     * cannot: metadata_immutable defaults to zero and the key is only consulted
     * when it is set. */
    if (!metadata_memo_hit(scene)) {
        if (!metadata_valid(scene, stats)) {
#if SM64_SATURN_ADMIT_DIAG
            admit.malformed = 1U;
            admit_cursor = sm64_saturn_prenotify_profile_span(
                admit_cursor, &admit.validate_ticks, &admit.max_raw);
            admit.total_ticks = (uint16_t)(admit_cursor - admit_entry);
            sm64_saturn_prenotify_profile_publish_admit(&admit);
#endif
            return false;
        }
        if (scene->metadata_immutable != 0U) metadata_memo_store(scene);
    }
#if SM64_SATURN_ADMIT_DIAG
    /* Charges the entry guards and the whole per-frame revalidation of
     * static package metadata. */
    admit_cursor = sm64_saturn_prenotify_profile_span(
        admit_cursor, &admit.validate_ticks, &admit.max_raw);
#endif
    memset(s_admission_visited, 0, sizeof(s_admission_visited));
    memset(s_admission_queued, 0, sizeof(s_admission_queued));
    memset(s_admission_cluster_seen, 0, scene->cluster_count);
#if SM64_SATURN_ADMIT_DIAG
    admit_cursor = sm64_saturn_prenotify_profile_span(
        admit_cursor, &admit.scratch_ticks, &admit.max_raw);
#endif
    frustum = admission_frustum(scene, view);
#if SM64_SATURN_ADMIT_DIAG
    admit_cursor = sm64_saturn_prenotify_profile_span(
        admit_cursor, &admit.frustum_ticks, &admit.max_raw);
#endif
    s_admission_queue[queue_tail++] = scene->root_node;
    s_admission_queued[scene->root_node] = 1U;
    while (queue_head < queue_tail) {
        const uint16_t entry = s_admission_queue[queue_head++];
        const uint16_t node_index =
            (uint16_t)(entry & ADMISSION_QUEUE_NODE_MASK);
        const sm64_saturn_ztreme_frustum_result_t inherited =
            (sm64_saturn_ztreme_frustum_result_t)
                (entry >> ADMISSION_QUEUE_STATE_SHIFT);
        const sm64_saturn_scene_admission_node_t *node;
        sm64_saturn_ztreme_frustum_result_t node_state;
        if (s_admission_visited[node_index] != 0U) {
            stats->cycle_edges++;
            continue;
        }
        s_admission_visited[node_index] = 1U;
        node = &scene->nodes[node_index];
        stats->nodes_tested++;
        /* Z-Treme's hierarchical short-circuit (ZT_RENDERING.c:425, 486-492,
         * recorded in sprint2-t2_0-reference-sweep.md 4.3, and already written
         * in this repository at saturn_demo_render.c:772-785): a subtree of a
         * node that tested fully INSIDE is fully inside, so nothing below it
         * is tested again. Only INTERSECTS descends and re-tests; OUTSIDE
         * never reaches here because it is pruned below. */
        if (inherited == SM64_SATURN_ZTREME_FRUSTUM_INSIDE)
            node_state = SM64_SATURN_ZTREME_FRUSTUM_INSIDE;
        else
            node_state = test_node_bounds(&frustum, node->bounds_min_q16,
                                          node->bounds_max_q16);
#if SM64_SATURN_ADMIT_DIAG
        admit_cursor = sm64_saturn_prenotify_profile_span(
            admit_cursor, &admit.node_test_ticks, &admit.max_raw);
#endif
        if (node_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE) continue;
        stats->nodes_admitted++;
        for (index = 0U; index < node->cluster_ref_count; index++) {
            const uint16_t cluster_index = scene->cluster_refs[
                node->cluster_ref_first + index];
            const sm64_saturn_render_cluster_t *cluster =
                &scene->clusters[cluster_index];
            sm64_saturn_ztreme_frustum_result_t cluster_state;
            if (node_state == SM64_SATURN_ZTREME_FRUSTUM_INSIDE) {
                cluster_state = SM64_SATURN_ZTREME_FRUSTUM_INSIDE;
            } else {
                cluster_state = test_bounds(&frustum, cluster->bounds_min_q16,
                                            cluster->bounds_max_q16);
                /* Counts frustum tests actually performed, which is the cost
                 * this stage is measured by. It is no longer the scene's
                 * cluster count. */
                stats->clusters_tested++;
            }
#if SM64_SATURN_ADMIT_DIAG
            if (cluster_state == SM64_SATURN_ZTREME_FRUSTUM_INSIDE)
                admit.clusters_inside++;
            else if (cluster_state == SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS)
                admit.clusters_intersect++;
            /* This bucket also absorbs the loop overhead of every iteration
             * that continued before reaching a later probe -- see the ABI
             * note in saturn_prenotify_profile.h. */
            admit_cursor = sm64_saturn_prenotify_profile_span(
                admit_cursor, &admit.cluster_test_ticks, &admit.max_raw);
#endif
            if (cluster_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE &&
                cluster->mandatory == 0U) {
                stats->clusters_rejected_frustum++;
                continue;
            }
            const bool admit_duplicate =
                s_admission_cluster_seen[cluster_index] != 0U;
#if SM64_SATURN_ADMIT_DIAG
            /* dedup_compares stays zero by construction now; the T2.9 rig
             * publishes it, so the on-target witness for this change is that
             * counter falling from 39,903 to 0 while output_count holds. */
            admit.dedup_calls++;
            admit_cursor = sm64_saturn_prenotify_profile_span(
                admit_cursor, &admit.cluster_dedup_ticks, &admit.max_raw);
#endif
            if (admit_duplicate) {
                stats->duplicate_clusters++;
                continue;
            }
            s_admission_cluster_seen[cluster_index] = 1U;
            if (cluster->mandatory != 0U &&
                cluster_state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE)
                stats->mandatory_clusters_admitted++;
#if SM64_SATURN_ADMIT_DIAG
            admit_cursor = sm64_saturn_prenotify_profile_span(
                admit_cursor, &admit.cluster_emit_ticks, &admit.max_raw);
#endif
        }
        /* Descend, carrying this node's classification. A child of an
         * INTERSECTS node is enqueued with INTERSECTS, which is the "test me"
         * value, so nothing is skipped that the flat pass would have tested. */
        for (index = 0U; index < node->child_count; index++) {
            const uint16_t child = (uint16_t)(node->child_first + index);
            if (s_admission_visited[child] != 0U ||
                s_admission_queued[child] != 0U) {
                stats->cycle_edges++;
            } else if (queue_tail >= SM64_SATURN_SCENE_ADMISSION_MAX_NODES) {
                stats->output_exhausted = 1U;
                success = false;
            } else {
                s_admission_queue[queue_tail++] = (uint16_t)
                    (child | ((uint16_t)node_state <<
                              ADMISSION_QUEUE_STATE_SHIFT));
                s_admission_queued[child] = 1U;
            }
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
            if (s_admission_visited[destination] != 0U ||
                s_admission_queued[destination] != 0U) {
                stats->cycle_edges++;
            } else if (queue_tail >= SM64_SATURN_SCENE_ADMISSION_MAX_NODES) {
                stats->output_exhausted = 1U;
                success = false;
            } else {
                s_admission_queue[queue_tail++] = destination;
                s_admission_queued[destination] = 1U;
            }
        }
#if SM64_SATURN_ADMIT_DIAG
        admit_cursor = sm64_saturn_prenotify_profile_span(
            admit_cursor, &admit.portal_ticks, &admit.max_raw);
#endif
    }
    /* One ordered emission pass, which also discharges the mandatory
     * obligation. The traversal above only *marks*; the output is produced
     * here, in ascending cluster index, so it is a function of the admitted
     * set alone and not of the shape of the traversal that found it.
     *
     * That is what makes hierarchical admission provably equivalent to the
     * flat pass rather than merely equivalent as a set: the flat pass walked
     * BOB's ascending ref list and emitted in ascending order, and a
     * hierarchy walks the same clusters in a different order. Without this
     * pass, every mandatory cluster whose subtree was pruned would move from
     * its position in the sequence to the tail, and admission output order is
     * an input to the downstream depth-bin scatter's tie order.
     *
     * Mandatory records are unconditional package obligations: they are
     * retained even when their owning node is outside the current frustum,
     * which is why an unmarked mandatory cluster is emitted here. */
    for (index = 0U; index < scene->cluster_count; index++) {
        const sm64_saturn_render_cluster_t *cluster = &scene->clusters[index];
        if (s_admission_cluster_seen[index] == 0U) {
            if (cluster->mandatory == 0U) continue;
            stats->mandatory_clusters_admitted++;
            s_admission_cluster_seen[index] = 1U;
        }
        if (output->cluster_count >= output->cluster_capacity) {
            stats->output_exhausted = 1U;
            success = false;
            continue;
        }
        output->cluster_indices[output->cluster_count++] = index;
        stats->clusters_admitted++;
    }
#if SM64_SATURN_ADMIT_DIAG
    admit_cursor = sm64_saturn_prenotify_profile_span(
        admit_cursor, &admit.mandatory_ticks, &admit.max_raw);
    admit.total_ticks = (uint16_t)(admit_cursor - admit_entry);
    admit.nodes_tested = stats->nodes_tested;
    admit.nodes_admitted = stats->nodes_admitted;
    admit.clusters_tested = stats->clusters_tested;
    admit.clusters_admitted = stats->clusters_admitted;
    admit.clusters_rejected = stats->clusters_rejected_frustum;
    admit.clusters_duplicate = stats->duplicate_clusters;
    admit.portals_tested = stats->portals_tested;
    admit.output_count = output->cluster_count;
    sm64_saturn_prenotify_profile_publish_admit(&admit);
#endif
    return success && output->cluster_count != 0U;
}

#if defined(SM64_SATURN_SCENE_ADMISSION_REFERENCE)
/* Host-fixture only. A verbatim copy of the pre-T2.12 traversal, taken from
 * HEAD 3bf5f590 before any hierarchy existed: one BFS over portal edges, no
 * child descent, no inherited node state, every cluster of every admitted node
 * frustum-tested from scratch, emission in traversal order, and a trailing
 * mandatory sweep. It shares metadata_valid(), admission_frustum() and
 * test_bounds() with the shipped body on purpose -- T2.12 does not touch those
 * and a second copy would only pin a paraphrase. What this copy pins is the
 * one thing T2.12 replaces: which tests run and in what order clusters are
 * emitted. The diagnostic probes are omitted because SM64_SATURN_ADMIT_DIAG is
 * never set on a host build. */
bool sm64_saturn_scene_admit_reference_with_scratch(
    const sm64_saturn_scene_admission_view_t *scene,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_scene_admission_output_t *output,
    sm64_saturn_scene_admission_stats_t *stats,
    sm64_saturn_scene_admission_scratch_t *scratch)
{
    sm64_saturn_ztreme_frustum_t frustum;
    uint16_t queue_head = 0U, queue_tail = 0U;
    uint16_t index;
    bool success = true;
    if (scratch == NULL) return false;
    s_admission_scratch = scratch;
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
    memset(s_admission_visited, 0, sizeof(s_admission_visited));
    memset(s_admission_queued, 0, sizeof(s_admission_queued));
    memset(s_admission_cluster_seen, 0, scene->cluster_count);
    frustum = admission_frustum(scene, view);
    s_admission_queue[queue_tail++] = scene->root_node;
    s_admission_queued[scene->root_node] = 1U;
    while (queue_head < queue_tail) {
        const uint16_t node_index = s_admission_queue[queue_head++];
        const sm64_saturn_scene_admission_node_t *node;
        sm64_saturn_ztreme_frustum_result_t node_state;
        if (s_admission_visited[node_index] != 0U) {
            stats->cycle_edges++;
            continue;
        }
        s_admission_visited[node_index] = 1U;
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
            if (s_admission_cluster_seen[cluster_index] != 0U) {
                stats->duplicate_clusters++;
                continue;
            }
            if (output->cluster_count >= output->cluster_capacity) {
                stats->output_exhausted = 1U;
                success = false;
                continue;
            }
            output->cluster_indices[output->cluster_count++] = cluster_index;
            s_admission_cluster_seen[cluster_index] = 1U;
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
            if (s_admission_visited[destination] != 0U ||
                s_admission_queued[destination] != 0U) {
                stats->cycle_edges++;
            } else if (queue_tail >= SM64_SATURN_SCENE_ADMISSION_MAX_NODES) {
                stats->output_exhausted = 1U;
                success = false;
            } else {
                s_admission_queue[queue_tail++] = destination;
                s_admission_queued[destination] = 1U;
            }
        }
    }
    for (index = 0U; index < scene->cluster_count; index++) {
        const sm64_saturn_render_cluster_t *cluster = &scene->clusters[index];
        if (cluster->mandatory == 0U || s_admission_cluster_seen[index] != 0U)
            continue;
        if (output->cluster_count >= output->cluster_capacity) {
            stats->output_exhausted = 1U;
            success = false;
            continue;
        }
        output->cluster_indices[output->cluster_count++] = index;
        s_admission_cluster_seen[index] = 1U;
        stats->clusters_admitted++;
        stats->mandatory_clusters_admitted++;
    }
    return success && output->cluster_count != 0U;
}
#endif

#ifndef SATURN_SOURCEBOOT
static sm64_saturn_scene_admission_scratch_t s_admission_compat_scratch;
#endif

bool sm64_saturn_scene_admit(
    const sm64_saturn_scene_admission_view_t *scene,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_scene_admission_output_t *output,
    sm64_saturn_scene_admission_stats_t *stats)
{
#ifdef SATURN_SOURCEBOOT
    (void)scene;
    (void)view;
    (void)output;
    (void)stats;
    /* Sourceboot must pass a phase-owned bank explicitly. */
    return false;
#else
    return sm64_saturn_scene_admit_with_scratch(
        scene, view, output, stats, &s_admission_compat_scratch);
#endif
}
