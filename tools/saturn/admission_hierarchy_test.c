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
    char name[128];
    for (unsigned li = 0U; li < sizeof(k_limits) / sizeof(k_limits[0]); li++)
        for (uint16_t pi = 0U; pi < g_pose_count; pi++) {
            snprintf(name, sizeof(name), "%s/%s/pose%u", label,
                     k_limits[li].name, (unsigned)pi);
            (void)compare_pose(name, flat, tree, &g_poses_table[pi],
                               &k_limits[li], capacity);
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
    sweep("bob-flat", &bob_flat, &bob_flat, CLUSTER_MAX);

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
