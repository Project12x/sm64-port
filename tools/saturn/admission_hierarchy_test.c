/* Sprint 2 T2.12 equivalence oracle for hierarchical scene admission.
 *
 * The claim under test is that giving admission a real node hierarchy changes
 * *which frustum tests run* and nothing else: the admitted cluster index array
 * must stay byte-identical to the flat single-node traversal's, index for
 * index, over a pose corpus.
 *
 * Two statements per pose:
 *
 *   reference -- sm64_saturn_scene_admit_reference_with_scratch(), the pinned
 *                pre-T2.12 flat traversal, compiled into
 *                saturn_scene_admission.c only under
 *                SM64_SATURN_SCENE_ADMISSION_REFERENCE, which only the
 *                verify-admission-hierarchy recipe defines. It is driven with
 *                the flat scene: one root node holding all 867 BOB cluster
 *                refs in ascending order -- exactly what
 *                emit_bob_scene.py published before T2.12.
 *   shipped   -- whatever sm64_saturn_scene_admit_with_scratch() currently is,
 *                driven with the hierarchical scene.
 *
 * The cluster bank is the real generated BOB bank (867 clusters), not a
 * synthetic one, so the corner cases the route actually contains are in the
 * corpus by construction. Synthetic scenes cover the shapes BOB cannot
 * produce: empty nodes, single-cluster nodes, a camera inside a node, a node
 * straddling the near plane, and views that admit everything or nothing.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saturn_scene_admission.h"
#include "bob_scene.h"

#define POSE_MAX 4096U
#define CLUSTER_MAX SM64_SATURN_BOB_CLUSTER_COUNT

static sm64_saturn_scene_admission_scratch_t g_scratch_reference;
static sm64_saturn_scene_admission_scratch_t g_scratch_shipped;

static uint16_t g_admitted_reference[CLUSTER_MAX];
static uint16_t g_admitted_shipped[CLUSTER_MAX];

static unsigned long g_poses;
static unsigned long g_admissions;
static unsigned long g_divergences;
static uint64_t g_digest = 1469598103934665603ULL;

static unsigned long g_tested_reference_total;
static unsigned long g_tested_shipped_total;
static unsigned long g_nodes_shipped_total;
static uint32_t g_tested_shipped_min = UINT32_MAX;
static uint32_t g_tested_shipped_max;
static uint32_t g_admitted_min = UINT32_MAX;
static uint32_t g_admitted_max;

static void digest_u32(uint32_t value)
{
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        g_digest ^= (uint64_t)((value >> shift) & 0xFFU);
        g_digest *= 1099511628211ULL;
    }
}

/* ---------------------------------------------------------------- scenes -- */

static sm64_saturn_scene_admission_node_t g_flat_node[1];
static uint16_t g_flat_refs[CLUSTER_MAX];

static void build_flat_scene(sm64_saturn_scene_admission_view_t *scene,
                             const sm64_saturn_render_cluster_t *clusters,
                             uint16_t cluster_count)
{
    memset(scene, 0, sizeof(*scene));
    memset(g_flat_node, 0, sizeof(g_flat_node));
    for (uint16_t index = 0U; index < cluster_count; index++)
        g_flat_refs[index] = index;
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        int32_t minimum = clusters[0].bounds_min_q16[axis];
        int32_t maximum = clusters[0].bounds_max_q16[axis];
        for (uint16_t index = 1U; index < cluster_count; index++) {
            if (clusters[index].bounds_min_q16[axis] < minimum)
                minimum = clusters[index].bounds_min_q16[axis];
            if (clusters[index].bounds_max_q16[axis] > maximum)
                maximum = clusters[index].bounds_max_q16[axis];
        }
        g_flat_node[0].bounds_min_q16[axis] = minimum;
        g_flat_node[0].bounds_max_q16[axis] = maximum;
    }
    g_flat_node[0].cluster_ref_first = 0U;
    g_flat_node[0].cluster_ref_count = cluster_count;
    scene->metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION;
    scene->metadata_valid = 1U;
    scene->clusters = clusters;
    scene->cluster_count = cluster_count;
    scene->nodes = g_flat_node;
    scene->node_count = 1U;
    scene->cluster_refs = g_flat_refs;
    scene->cluster_ref_count = cluster_count;
    scene->root_node = 0U;
}

/* ------------------------------------------------------- host tree builder -- */

/* A median split on the widest axis, the same shape emit_bob_scene.py bakes.
 * Built here as well so the gate covers the *traversal* over hierarchies the
 * generator does not currently produce -- every leaf size from 1 upward, which
 * is where degenerate node shapes (single-cluster leaves, deep chains) live. */
#define NODE_MAX 2048U

static sm64_saturn_scene_admission_node_t g_tree_nodes[NODE_MAX];
static uint16_t g_tree_refs[CLUSTER_MAX];
static uint16_t g_tree_perm[CLUSTER_MAX];
static uint16_t g_tree_node_count;
static uint16_t g_tree_leaf_count;
static uint16_t g_tree_depth_max;

static const sm64_saturn_render_cluster_t *g_sort_clusters;
static uint8_t g_sort_axis;

static int compare_center(const void *left, const void *right)
{
    const uint16_t a = *(const uint16_t *)left;
    const uint16_t b = *(const uint16_t *)right;
    const int64_t ca = (int64_t)g_sort_clusters[a].bounds_min_q16[g_sort_axis] +
                       g_sort_clusters[a].bounds_max_q16[g_sort_axis];
    const int64_t cb = (int64_t)g_sort_clusters[b].bounds_min_q16[g_sort_axis] +
                       g_sort_clusters[b].bounds_max_q16[g_sort_axis];
    if (ca < cb) return -1;
    if (ca > cb) return 1;
    return a < b ? -1 : (a > b ? 1 : 0);
}

typedef struct pending { uint16_t node, lo, hi, depth; } pending_t;

static void node_bounds(const sm64_saturn_render_cluster_t *clusters,
                        uint16_t lo, uint16_t hi,
                        sm64_saturn_scene_admission_node_t *node)
{
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        int32_t minimum = clusters[g_tree_perm[lo]].bounds_min_q16[axis];
        int32_t maximum = clusters[g_tree_perm[lo]].bounds_max_q16[axis];
        for (uint16_t i = (uint16_t)(lo + 1U); i < hi; i++) {
            const sm64_saturn_render_cluster_t *c = &clusters[g_tree_perm[i]];
            if (c->bounds_min_q16[axis] < minimum) minimum = c->bounds_min_q16[axis];
            if (c->bounds_max_q16[axis] > maximum) maximum = c->bounds_max_q16[axis];
        }
        node->bounds_min_q16[axis] = minimum;
        node->bounds_max_q16[axis] = maximum;
    }
}

static void build_host_tree(sm64_saturn_scene_admission_view_t *scene,
                            const sm64_saturn_render_cluster_t *clusters,
                            uint16_t cluster_count, uint16_t leaf_max)
{
    static pending_t queue[NODE_MAX];
    uint16_t head = 0U, tail = 0U;
    memset(g_tree_nodes, 0, sizeof(g_tree_nodes));
    for (uint16_t i = 0U; i < cluster_count; i++) g_tree_perm[i] = i;
    g_tree_node_count = 1U;
    g_tree_leaf_count = 0U;
    g_tree_depth_max = 0U;
    queue[tail++] = (pending_t){0U, 0U, cluster_count, 0U};
    while (head < tail) {
        const pending_t item = queue[head++];
        sm64_saturn_scene_admission_node_t *node = &g_tree_nodes[item.node];
        const uint16_t span = (uint16_t)(item.hi - item.lo);
        node_bounds(clusters, item.lo, item.hi, node);
        if (item.depth > g_tree_depth_max) g_tree_depth_max = item.depth;
        if (span <= leaf_max || (uint32_t)g_tree_node_count + 2U > NODE_MAX) {
            node->cluster_ref_first = item.lo;
            node->cluster_ref_count = span;
            g_tree_leaf_count++;
            continue;
        }
        for (uint8_t axis = 0U; axis < 3U; axis++) {
            const int64_t extent = (int64_t)node->bounds_max_q16[axis] -
                                   node->bounds_min_q16[axis];
            const int64_t widest = (int64_t)node->bounds_max_q16[g_sort_axis] -
                                   node->bounds_min_q16[g_sort_axis];
            if (axis == 0U || extent > widest) g_sort_axis = axis;
        }
        g_sort_clusters = clusters;
        qsort(&g_tree_perm[item.lo], span, sizeof(g_tree_perm[0]),
              compare_center);
        node->child_first = g_tree_node_count;
        node->child_count = 2U;
        g_tree_node_count = (uint16_t)(g_tree_node_count + 2U);
        queue[tail++] = (pending_t){node->child_first, item.lo,
                                    (uint16_t)(item.lo + span / 2U),
                                    (uint16_t)(item.depth + 1U)};
        queue[tail++] = (pending_t){(uint16_t)(node->child_first + 1U),
                                    (uint16_t)(item.lo + span / 2U), item.hi,
                                    (uint16_t)(item.depth + 1U)};
    }
    for (uint16_t i = 0U; i < cluster_count; i++)
        g_tree_refs[i] = g_tree_perm[i];
    memset(scene, 0, sizeof(*scene));
    scene->metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION;
    scene->metadata_valid = 1U;
    scene->clusters = clusters;
    scene->cluster_count = cluster_count;
    scene->nodes = g_tree_nodes;
    scene->node_count = g_tree_node_count;
    scene->cluster_refs = g_tree_refs;
    scene->cluster_ref_count = cluster_count;
    scene->root_node = 0U;
}

/* ------------------------------------------------------------ pose corpus -- */

typedef struct pose {
    int32_t position[3];
    int32_t forward[3];
    int32_t right[3];
    int32_t up[3];
} pose_t;

static pose_t g_poses_table[POSE_MAX];
static uint16_t g_pose_count;

/* Q16 sine table on 16 steps of a full turn; cos(a) = sin(a + quarter). */
static const int32_t k_sin16[16] = {
        0,  25080,  46341,  60547,  65536,  60547,  46341,  25080,
        0, -25080, -46341, -60547, -65536, -60547, -46341, -25080};

static void push_pose(int32_t x, int32_t y, int32_t z, unsigned yaw,
                      int pitch_step)
{
    pose_t *pose;
    const int32_t sine = k_sin16[yaw & 15U];
    const int32_t cosine = k_sin16[(yaw + 4U) & 15U];
    if (g_pose_count >= POSE_MAX) return;
    pose = &g_poses_table[g_pose_count++];
    pose->position[0] = x;
    pose->position[1] = y;
    pose->position[2] = z;
    /* Yaw about world Y, then an approximate pitch by tilting forward and up
     * on the vertical axis. The basis does not have to be exactly orthonormal:
     * admission consumes it as published, and both statements consume the same
     * numbers. */
    pose->forward[0] = sine;
    pose->forward[1] = (int32_t)pitch_step * 32768;
    pose->forward[2] = cosine;
    pose->right[0] = cosine;
    pose->right[1] = 0;
    pose->right[2] = -sine;
    pose->up[0] = 0;
    pose->up[1] = 65536;
    pose->up[2] = -(int32_t)pitch_step * 32768;
}

static void build_pose_corpus(void)
{
    static const int32_t k_x[] = {-8192, -4096, 0, 4096, 8192, 1024};
    static const int32_t k_y[] = {-512, 512, 2048, 4608};
    static const int32_t k_z[] = {-8192, -2048, 0, 2048, 8192};
    g_pose_count = 0U;
    for (unsigned xi = 0U; xi < sizeof(k_x) / sizeof(k_x[0]); xi++)
        for (unsigned yi = 0U; yi < sizeof(k_y) / sizeof(k_y[0]); yi++)
            for (unsigned zi = 0U; zi < sizeof(k_z) / sizeof(k_z[0]); zi++)
                for (unsigned yaw = 0U; yaw < 16U; yaw += 2U)
                    push_pose(k_x[xi], k_y[yi], k_z[zi], yaw,
                              ((int)(xi + zi) % 3) - 1);
    /* Two poses far outside the scene looking away from it: the "admits
     * nothing but the mandatory set" corner. */
    push_pose(1 << 20, 1 << 18, 1 << 20, 8U, 0);
    push_pose(-(1 << 20), -(1 << 18), -(1 << 20), 0U, 0);
    /* Camera exactly on the scene origin, which is inside the root bounds. */
    push_pose(0, 0, 0, 0U, 0);
    push_pose(0, 0, 0, 8U, 0);
}

/* -------------------------------------------------------------- frustums -- */

typedef struct limits {
    const char *name;
    int32_t near_depth;
    int32_t far_depth;
    int32_t half_width;
    int32_t half_height;
    int32_t focal_length;
} limits_t;

/* The production BOB frustum is saturn_demo_render.c:134-139. The rest bracket
 * it: one that admits essentially everything, one that admits essentially
 * nothing, and one whose near plane sits deep inside the scene so cluster
 * bounds straddle it. */
static const limits_t k_limits[] = {
    {"production", 128, 8192, 160, 112, 256},
    {"wide", 1, 1 << 24, 1 << 20, 1 << 20, 256},
    {"narrow", 128, 384, 16, 12, 256},
    {"near-deep", 3072, 8192, 160, 112, 256},
    {"shallow-focal", 128, 8192, 160, 112, 16},
};

/* ------------------------------------------------------------- comparison -- */

static sm64_saturn_render_view_t make_view(const pose_t *pose)
{
    sm64_saturn_render_view_t view;
    memset(&view, 0, sizeof(view));
    for (uint8_t axis = 0U; axis < 3U; axis++) {
        view.camera_position_q16[axis] = pose->position[axis] * 65536;
        view.view_forward_q16[axis] = pose->forward[axis];
        view.view_projection_q16[0][axis] = pose->right[axis];
        view.view_projection_q16[1][axis] = pose->up[axis];
    }
    view.generation = 1U;
    return view;
}

/* Drives both statements over one pose and requires the admitted index arrays
 * to be byte-equal. Returns the number of divergences found. */
static unsigned compare_pose(const char *label,
                             const sm64_saturn_scene_admission_view_t *flat,
                             const sm64_saturn_scene_admission_view_t *tree,
                             const pose_t *pose, const limits_t *limits,
                             uint16_t capacity)
{
    sm64_saturn_scene_admission_view_t flat_scene = *flat;
    sm64_saturn_scene_admission_view_t tree_scene = *tree;
    sm64_saturn_scene_admission_stats_t stats_reference, stats_shipped;
    sm64_saturn_scene_admission_output_t out_reference = {
        g_admitted_reference, capacity, 0U, NULL, 0U, 0U};
    sm64_saturn_scene_admission_output_t out_shipped = {
        g_admitted_shipped, capacity, 0U, NULL, 0U, 0U};
    sm64_saturn_render_view_t view = make_view(pose);
    bool ok_reference, ok_shipped;
    unsigned divergences = 0U;

    flat_scene.frustum.near_depth = limits->near_depth;
    flat_scene.frustum.far_depth = limits->far_depth;
    flat_scene.frustum.half_width = limits->half_width;
    flat_scene.frustum.half_height = limits->half_height;
    flat_scene.frustum.focal_length = limits->focal_length;
    tree_scene.frustum = flat_scene.frustum;

    memset(g_admitted_reference, 0xFF, sizeof(g_admitted_reference));
    memset(g_admitted_shipped, 0xFF, sizeof(g_admitted_shipped));
    ok_reference = sm64_saturn_scene_admit_reference_with_scratch(
        &flat_scene, &view, &out_reference, &stats_reference,
        &g_scratch_reference);
    ok_shipped = sm64_saturn_scene_admit_with_scratch(
        &tree_scene, &view, &out_shipped, &stats_shipped, &g_scratch_shipped);

    g_poses++;
    g_admissions += out_reference.cluster_count;
    g_tested_reference_total += stats_reference.clusters_tested;
    g_tested_shipped_total += stats_shipped.clusters_tested;
    g_nodes_shipped_total += stats_shipped.nodes_tested;
    if (stats_shipped.clusters_tested < g_tested_shipped_min)
        g_tested_shipped_min = stats_shipped.clusters_tested;
    if (stats_shipped.clusters_tested > g_tested_shipped_max)
        g_tested_shipped_max = stats_shipped.clusters_tested;
    if (out_reference.cluster_count < g_admitted_min)
        g_admitted_min = out_reference.cluster_count;
    if (out_reference.cluster_count > g_admitted_max)
        g_admitted_max = out_reference.cluster_count;
    digest_u32(out_reference.cluster_count);
    for (uint16_t index = 0U; index < out_reference.cluster_count; index++)
        digest_u32(g_admitted_reference[index]);

    if (ok_reference != ok_shipped) {
        printf("DIVERGENCE %s: return %d vs %d\n", label, (int)ok_reference,
               (int)ok_shipped);
        divergences++;
    }
    if (out_reference.cluster_count != out_shipped.cluster_count) {
        printf("DIVERGENCE %s: admitted %u vs %u\n", label,
               out_reference.cluster_count, out_shipped.cluster_count);
        divergences++;
    } else if (memcmp(g_admitted_reference, g_admitted_shipped,
                      (size_t)out_reference.cluster_count *
                          sizeof(uint16_t)) != 0) {
        for (uint16_t index = 0U; index < out_reference.cluster_count; index++)
            if (g_admitted_reference[index] != g_admitted_shipped[index]) {
                printf("DIVERGENCE %s: slot %u is %u vs %u\n", label, index,
                       g_admitted_reference[index], g_admitted_shipped[index]);
                break;
            }
        divergences++;
    }
    if (stats_reference.clusters_admitted != stats_shipped.clusters_admitted) {
        printf("DIVERGENCE %s: clusters_admitted %u vs %u\n", label,
               stats_reference.clusters_admitted,
               stats_shipped.clusters_admitted);
        divergences++;
    }
    g_divergences += divergences;
    return divergences;
}

static void sweep(const char *label,
                  const sm64_saturn_scene_admission_view_t *flat,
                  const sm64_saturn_scene_admission_view_t *tree,
                  uint16_t capacity)
{
    const unsigned long before_poses = g_poses;
    const unsigned long before_divergences = g_divergences;
    const unsigned long before_reference = g_tested_reference_total;
    const unsigned long before_shipped = g_tested_shipped_total;
    const unsigned long before_nodes = g_nodes_shipped_total;
    char name[128];
    g_tested_shipped_min = UINT32_MAX;
    g_tested_shipped_max = 0U;
    for (unsigned li = 0U; li < sizeof(k_limits) / sizeof(k_limits[0]); li++)
        for (uint16_t pi = 0U; pi < g_pose_count; pi++) {
            snprintf(name, sizeof(name), "%s/%s/pose%u", label,
                     k_limits[li].name, (unsigned)pi);
            (void)compare_pose(name, flat, tree, &g_poses_table[pi],
                               &k_limits[li], capacity);
        }
    {
        const unsigned long poses = g_poses - before_poses;
        const unsigned long reference = g_tested_reference_total - before_reference;
        const unsigned long shipped = g_tested_shipped_total - before_shipped;
        const unsigned long nodes = g_nodes_shipped_total - before_nodes;
        printf("  %-26s nodes %5u leaves %5u depth %2u | tests/pose "
               "flat %6.1f tree %6.1f+n%5.1f (%5.1f%%) range %u..%u | div %lu\n",
               label, (unsigned)tree->node_count, (unsigned)g_tree_leaf_count,
               (unsigned)g_tree_depth_max,
               (double)reference / (double)poses, (double)shipped / (double)poses,
               (double)nodes / (double)poses,
               reference ? 100.0 * (double)(shipped + nodes) / (double)reference : 0.0,
               g_tested_shipped_min, g_tested_shipped_max,
               g_divergences - before_divergences);
    }
}

/* ------------------------------------------------------------------ main -- */

int main(void)
{
    sm64_saturn_scene_admission_view_t bob_flat;

    build_pose_corpus();
    build_flat_scene(&bob_flat, sm64_saturn_bob_render_clusters,
                     SM64_SATURN_BOB_CLUSTER_COUNT);

    /* Harness check first: the flat scene against itself. Both statements are
     * different function bodies over the same data, so a zero here proves the
     * comparison, the corpus and the reference hook are wired to something
     * real before any hierarchy is introduced. */
    printf("admission hierarchy: %u poses x %u frustum templates per sweep\n",
           (unsigned)g_pose_count,
           (unsigned)(sizeof(k_limits) / sizeof(k_limits[0])));
    g_tree_leaf_count = 1U;
    g_tree_depth_max = 0U;
    sweep("bob-flat", &bob_flat, &bob_flat, CLUSTER_MAX);

    /* Host-built median-split hierarchies over the same 867 clusters, from
     * one cluster per leaf upward. Leaf size 1 is the degenerate shape --
     * 1,733 nodes, single-cluster leaves -- and the largest is barely a tree
     * at all; both must admit exactly what the flat pass admits. */
    {
        static const uint16_t k_leaf[] = {1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U};
        char label[64];
        for (unsigned li = 0U; li < sizeof(k_leaf) / sizeof(k_leaf[0]); li++) {
            sm64_saturn_scene_admission_view_t tree;
            build_host_tree(&tree, sm64_saturn_bob_render_clusters,
                            SM64_SATURN_BOB_CLUSTER_COUNT, k_leaf[li]);
            snprintf(label, sizeof(label), "bob-tree-leaf%u",
                     (unsigned)k_leaf[li]);
            sweep(label, &bob_flat, &tree, CLUSTER_MAX);
        }
    }

    /* The hierarchy the generator actually bakes, exactly as the target will
     * traverse it. */
    {
        sm64_saturn_scene_admission_view_t baked;
        memset(&baked, 0, sizeof(baked));
        baked.metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION;
        baked.metadata_valid = 1U;
        baked.clusters = sm64_saturn_bob_render_clusters;
        baked.cluster_count = SM64_SATURN_BOB_CLUSTER_COUNT;
        baked.nodes = sm64_saturn_bob_scene_admission_nodes;
        baked.node_count = SM64_SATURN_BOB_ADMISSION_NODE_COUNT;
        baked.cluster_refs = sm64_saturn_bob_scene_admission_cluster_refs;
        baked.cluster_ref_count = SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT;
        baked.root_node = 0U;
        g_tree_leaf_count = 0U;
        g_tree_depth_max = 0U;
        sweep("bob-generated", &bob_flat, &baked, CLUSTER_MAX);
    }

    printf("admission hierarchy: %lu poses, %lu cluster admissions, "
           "%lu divergences\n", g_poses, g_admissions, g_divergences);
    printf("admission hierarchy: admitted per pose min %u max %u\n",
           g_admitted_min, g_admitted_max);
    printf("admission hierarchy: clusters_tested reference %lu, shipped %lu "
           "(min %u max %u)\n", g_tested_reference_total, g_tested_shipped_total,
           g_tested_shipped_min, g_tested_shipped_max);
    printf("admission hierarchy: admitted-set digest %016llx\n",
           (unsigned long long)g_digest);
    if (g_divergences != 0UL) {
        printf("admission hierarchy fixture: FAIL\n");
        return 1;
    }
    printf("admission hierarchy fixture: PASS\n");
    return 0;
}
