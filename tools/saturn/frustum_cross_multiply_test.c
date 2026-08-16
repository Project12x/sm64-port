/* Sprint 2 T2.10 item 3 equivalence oracle.
 *
 * T2.9 (docs/saturn/evidence/reports/sprint2-t2_9-spatial-admit-audit.md,
 * Finding E) established that every admission AABB test spends four SH-2
 * hardware 64/32 divisions inside sm64_saturn_ztreme_frustum_aabb() to derive
 * projected lateral limits, 3,472 per frame. Replacing them with
 * cross-multiplication is the only item in T2.10 that can move the admitted
 * set, so this fixture is written and landed BEFORE the change, following the
 * pattern T2.6 used for the meshlet depth kernel
 * (docs/saturn/evidence/reports/sprint2-t2_6-meshlet-arithmetic.md sections 3
 * and 5): three independent statements per case, plus a set-level check.
 *
 * The three statements are
 *
 *   reference  the pinned pre-T2.10 divided body, compiled into
 *              ztreme_frustum.c only when SM64_SATURN_ZTREME_FRUSTUM_REFERENCE
 *              is defined, which only this target does;
 *   shipped    whatever sm64_saturn_ztreme_frustum_aabb() actually is today;
 *   model      an exact, unclamped restatement written here from the algebra
 *              and evaluated in 128-bit. The model *divides* where the
 *              cross-multiplied form multiplies, and it never clamps, so it is
 *              not a paraphrase of either implementation.
 *
 * The identity under test, for integer a, integer N >= 0 and integer F > 0:
 *
 *     a >  trunc(N/F)   <=>   a*F >  N
 *     a <  -trunc(N/F)  <=>   a*F <  -N
 *     a >= -trunc(N/F)  <=>   a*F >= -N
 *     a <=  trunc(N/F)  <=>   a*F <=  N
 *
 * N >= 0 makes trunc == floor, and that is what makes all four exact. The
 * divided form truncates toward zero, and for a negative numerator the
 * equivalence fails outright: N = -5, F = 2, a = -2 gives a > trunc(N/F)
 * false while a*F > N is true. The domain predicate below therefore excludes
 * negative numerators, non-positive focal lengths, operands that would
 * overflow the int64 products, and every case where the divided form's int32
 * clamp is active.
 *
 * Two things this oracle deliberately does not do:
 *   - it does not model the prologue (centre/extent/dot/support radius)
 *     independently, because T2.10 does not touch it; the verbatim reference
 *     copy is what covers that half;
 *   - it does not stop at per-box verdicts. sweep_admitted_sets() drives the
 *     real sm64_saturn_scene_admit() with the classifier switched underneath
 *     it and requires the emitted cluster index arrays to be byte-equal.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "saturn_scene_admission.h"

#if !defined(__SIZEOF_INT128__)
#error "this oracle needs __int128 for its exact statement"
#endif

typedef __int128 wide_t;

/* ----------------------------------------------------------------- model - */

typedef struct model_state {
    int64_t z, z_radius;
    int64_t x, x_radius;
    int64_t y, y_radius;
    int64_t near_limit, far_limit;
    int64_t near_z, far_z;
} model_state_t;

static bool wide_fits_i64(wide_t value)
{
    return value >= (wide_t)INT64_MIN && value <= (wide_t)INT64_MAX;
}

static bool fits_i32(int64_t value)
{
    return value >= INT32_MIN && value <= INT32_MAX;
}

static wide_t model_dot(const int32_t basis[3], const wide_t value[3])
{
    return (wide_t)basis[0] * value[0] + (wide_t)basis[1] * value[1] +
           (wide_t)basis[2] * value[2];
}

static wide_t model_support(const int32_t basis[3], const wide_t extent[3])
{
    wide_t radius = 0;
    for (unsigned axis = 0U; axis < 3U; axis++) {
        const wide_t component = basis[axis];
        const wide_t magnitude = component < 0 ? -component : component;
        radius += magnitude * extent[axis];
    }
    return (radius + 0xFFFF) >> 16;
}

/* Recomputes the shipped prologue in 128 bits and reports whether every
 * intermediate the int64 implementations compute is representable. A case that
 * would overflow int64 inside the pinned reference is pre-existing undefined
 * behaviour in the code under test; the sweep skips it rather than comparing
 * two undefined results and calling the agreement evidence. */
static bool model_prologue(const sm64_saturn_ztreme_frustum_t *frustum,
                           const int32_t minimum[3], const int32_t maximum[3],
                           model_state_t *out)
{
    wide_t centre[3], extent[3], delta[3];
    for (unsigned axis = 0U; axis < 3U; axis++) {
        centre[axis] = ((wide_t)minimum[axis] + maximum[axis]) / 2;
        extent[axis] = ((wide_t)maximum[axis] - minimum[axis] + 1) / 2;
        delta[axis] = centre[axis] - frustum->position[axis];
    }
    const wide_t z = model_dot(frustum->forward, delta) >> 16;
    const wide_t x = model_dot(frustum->right, delta) >> 16;
    const wide_t y = model_dot(frustum->up, delta) >> 16;
    const wide_t z_radius = model_support(frustum->forward, extent);
    const wide_t x_radius = model_support(frustum->right, extent);
    const wide_t y_radius = model_support(frustum->up, extent);
    if (!wide_fits_i64(z) || !wide_fits_i64(x) || !wide_fits_i64(y) ||
        !wide_fits_i64(z_radius) || !wide_fits_i64(x_radius) ||
        !wide_fits_i64(y_radius))
        return false;
    if (!wide_fits_i64(z + z_radius) || !wide_fits_i64(z - z_radius) ||
        !wide_fits_i64(x + x_radius) || !wide_fits_i64(x - x_radius) ||
        !wide_fits_i64(y + y_radius) || !wide_fits_i64(y - y_radius))
        return false;
    out->z = (int64_t)z;
    out->x = (int64_t)x;
    out->y = (int64_t)y;
    out->z_radius = (int64_t)z_radius;
    out->x_radius = (int64_t)x_radius;
    out->y_radius = (int64_t)y_radius;
    out->near_limit = frustum->near_depth;
    out->far_limit = frustum->far_depth;
    out->far_z = out->z + out->z_radius > out->near_limit
        ? out->z + out->z_radius : out->near_limit;
    out->near_z = out->z - out->z_radius > out->near_limit
        ? out->z - out->z_radius : out->near_limit;
    /* The pinned reference forms these products in int64 before dividing. */
    if (!wide_fits_i64((wide_t)out->far_z * frustum->half_width) ||
        !wide_fits_i64((wide_t)out->far_z * frustum->half_height) ||
        !wide_fits_i64((wide_t)out->near_z * frustum->half_width) ||
        !wide_fits_i64((wide_t)out->near_z * frustum->half_height))
        return false;
    return true;
}

/* The domain on which cross-multiplication is exactly the divided form. This
 * is an independent restatement of the guard the shipped code applies. The
 * sweep requires both sides of it to be populated, so a shipped guard that
 * drifted away from this predicate surfaces as a divergence rather than as a
 * quietly narrowed test. */
static bool model_domain(const sm64_saturn_ztreme_frustum_t *frustum,
                         const model_state_t *state)
{
    if (frustum->focal_length <= 0) return false;
    if (frustum->half_width < 0 || frustum->half_height < 0) return false;
    if (state->far_z < 0 || state->near_z < 0) return false;
    if (!fits_i32(state->far_z) || !fits_i32(state->near_z)) return false;
    if (!fits_i32(state->x - state->x_radius) ||
        !fits_i32(state->x + state->x_radius) ||
        !fits_i32(state->y - state->y_radius) ||
        !fits_i32(state->y + state->y_radius))
        return false;
    /* The divided form clamps a limit that will not fit int32 to INT32_MAX.
     * Where that clamp is active the two forms genuinely differ, and it is the
     * divided form that is then the non-conservative one, so the domain
     * excludes it and the shipped code stays on the divided form there. */
    const wide_t bound = (wide_t)INT32_MAX * frustum->focal_length;
    if ((wide_t)state->far_z * frustum->half_width > bound) return false;
    if ((wide_t)state->far_z * frustum->half_height > bound) return false;
    if ((wide_t)state->near_z * frustum->half_width > bound) return false;
    if ((wide_t)state->near_z * frustum->half_height > bound) return false;
    return true;
}

static wide_t model_floor_div(wide_t numerator, wide_t denominator)
{
    wide_t quotient = numerator / denominator;
    if ((numerator % denominator) != 0 &&
        ((numerator < 0) != (denominator < 0)))
        quotient -= 1;
    return quotient;
}

static bool model_interval_outside(wide_t centre, wide_t radius, wide_t low,
                                   wide_t high)
{
    return centre + radius < low || centre - radius > high;
}

static bool model_interval_inside(wide_t centre, wide_t radius, wide_t low,
                                  wide_t high)
{
    return centre - radius >= low && centre + radius <= high;
}

/* The exact statement, by division, in a type wide enough that neither the
 * shipped int64 products nor the reference's int32 clamp can reach it. */
static sm64_saturn_ztreme_frustum_result_t model_classify(
    const sm64_saturn_ztreme_frustum_t *frustum, const model_state_t *state)
{
    const wide_t focal = frustum->focal_length;
    const wide_t far_width =
        model_floor_div((wide_t)state->far_z * frustum->half_width, focal);
    const wide_t far_height =
        model_floor_div((wide_t)state->far_z * frustum->half_height, focal);
    const wide_t near_width =
        model_floor_div((wide_t)state->near_z * frustum->half_width, focal);
    const wide_t near_height =
        model_floor_div((wide_t)state->near_z * frustum->half_height, focal);
    if (model_interval_outside(state->z, state->z_radius, state->near_limit,
                               state->far_limit) ||
        model_interval_outside(state->x, state->x_radius, -far_width,
                               far_width) ||
        model_interval_outside(state->y, state->y_radius, -far_height,
                               far_height))
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    if (model_interval_inside(state->z, state->z_radius, state->near_limit,
                              state->far_limit) &&
        model_interval_inside(state->x, state->x_radius, -near_width,
                              near_width) &&
        model_interval_inside(state->y, state->y_radius, -near_height,
                              near_height))
        return SM64_SATURN_ZTREME_FRUSTUM_INSIDE;
    return SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS;
}

/* ---------------------------------------------------------------- corpus - */

static uint32_t rng_state = 0x13579BDFu;

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 1;
}

static int32_t rng_span(int32_t magnitude)
{
    const uint32_t raw = rng_next();
    const int64_t value = (int64_t)(raw % (uint32_t)(2 * magnitude + 1));
    return (int32_t)(value - magnitude);
}

typedef struct basis_case {
    int32_t forward[3];
    int32_t right[3];
    int32_t up[3];
} basis_case_t;

/* 46341 == round(65536 / sqrt(2)), the same 45 degree Q16 basis the standing
 * scene-admission fixture uses. Basis magnitudes stay at or below 2^17 so the
 * int64 dot products in the code under test cannot overflow on any box the
 * corpus generates. */
static const basis_case_t k_bases[] = {
    {{0, 0, 65536}, {65536, 0, 0}, {0, 65536, 0}},
    {{46341, 0, 46341}, {46341, 0, -46341}, {0, 65536, 0}},
    {{-46341, 0, 46341}, {46341, 0, 46341}, {0, 65536, 0}},
    {{0, 46341, 46341}, {65536, 0, 0}, {0, 46341, -46341}},
    {{0, 0, -65536}, {-65536, 0, 0}, {0, 65536, 0}},
    {{20000, 30000, 50000}, {50000, -20000, -30000}, {-30000, 50000, -20000}},
    {{0, 0, 65536}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 131072}, {131072, 0, 0}, {0, 131072, 0}},
};

typedef struct limit_case {
    int32_t near_depth, far_depth, half_width, half_height, focal_length;
} limit_case_t;

static const limit_case_t k_limits[] = {
    {128, 8192, 160, 112, 256},        /* the production BOB frustum */
    {1, 1000, 100, 100, 100},          /* the standing fixture frustum */
    {1, 16, 1, 1, 1},                  /* tight */
    {128, 8192, INT32_MAX, INT32_MAX, 1},   /* clamp active everywhere */
    {1, 1 << 20, 715827882, 715827882, 3},  /* 3 * 715827882 hugs INT32_MAX */
    {1, 1 << 20, 715827883, 715827883, 3},  /* one step past it */
    {128, 8192, INT32_MAX, 1, 1},      /* clamp active on width only */
    {128, 8192, 1, INT32_MAX, 1},      /* clamp active on height only */
    {128, 8192, 160, 112, INT32_MAX},  /* limits collapse to zero */
    {128, 8192, 160, 112, 0},          /* division would fail */
    {128, 8192, 160, 112, -256},       /* negative focal length */
    {-4096, 8192, 160, 112, 256},      /* near plane behind the camera */
    {128, 8192, 0, 0, 256},            /* zero lateral extent */
    {128, 8192, -160, -112, 256},      /* negative lateral extent */
    {128, INT32_MAX, 160, 112, 256},   /* unbounded far plane */
    {INT32_MAX, INT32_MAX, 160, 112, 256},
    {8192, 128, 160, 112, 256},        /* inverted depth range */
};

static const int32_t k_positions[][3] = {
    {0, 0, 0},
    {1024, 256, -2048},
    {-32768, -1024, 32767},
    {100000, -100000, 100000},
};

/* Boxes the random bands do not reliably produce: degenerate volumes, odd
 * sized edges that exercise the ceil in the half extent, signed mirrors, boxes
 * straddling the near and far planes, and coordinates on the int32 rails. */
static const int32_t k_fixed_boxes[][6] = {
    {0, 0, 0, 0, 0, 0},
    {-1, -1, -1, 1, 1, 1},
    {0, 0, 100, 1, 1, 101},
    {-3, -3, -3, 4, 4, 4},
    {-5, -5, 127, 5, 5, 129},
    {-5, -5, 8191, 5, 5, 8193},
    {-5, -5, -8193, 5, 5, -8191},
    {160, 112, 256, 161, 113, 257},
    {-161, -113, 256, -160, -112, 257},
    {INT32_MAX - 1, INT32_MAX - 1, INT32_MAX - 1, INT32_MAX, INT32_MAX,
     INT32_MAX},
    {INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN + 1, INT32_MIN + 1,
     INT32_MIN + 1},
    {INT32_MIN, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MAX},
    {-1073741824, -1073741824, 1073741824, -1073741823, -1073741823,
     1073741825},
    {1073741824, -16, 1073741824, 1073741826, 16, 1073741826},
    /* Chosen so that, under the axis-aligned basis with the camera at the
     * origin, the lateral low edge lands exactly on INT32_MIN: centre is
     * INT32_MIN + 32 and the half extent is 32. That is the only place where
     * the divided form's int32 clamp is observable inside the rest of the
     * domain, so it is what makes the clamp half of the guard load-bearing
     * rather than defensive. The second entry is the same construction on the
     * vertical axis. */
    {INT32_MIN, -16, 200, INT32_MIN + 63, 16, 300},
    {-16, INT32_MIN, 200, 16, INT32_MIN + 63, 300},
};

static const int32_t k_random_bands[] = {512, 8192, 1 << 20, 1 << 30};
#define RANDOM_BOXES_PER_BAND 220U

/* ---------------------------------------------------------- direct sweep - */

static unsigned long g_cases;
static unsigned long g_domain_cases;
static unsigned long g_outside_domain_cases;
static unsigned long g_skipped_cases;
static unsigned long g_verdicts[3];
static unsigned long g_shipped_vs_reference;
static unsigned long g_shipped_vs_model;

static void compare_case(const sm64_saturn_ztreme_frustum_t *frustum,
                         const int32_t minimum[3], const int32_t maximum[3])
{
    model_state_t state;
    int32_t ordered_min[3], ordered_max[3];
    for (unsigned axis = 0U; axis < 3U; axis++) {
        ordered_min[axis] = minimum[axis] < maximum[axis] ? minimum[axis]
                                                          : maximum[axis];
        ordered_max[axis] = minimum[axis] < maximum[axis] ? maximum[axis]
                                                          : minimum[axis];
    }
    if (!model_prologue(frustum, ordered_min, ordered_max, &state)) {
        g_skipped_cases++;
        return;
    }
    g_cases++;
    sm64_saturn_ztreme_frustum_force_reference = 0;
    const sm64_saturn_ztreme_frustum_result_t shipped =
        sm64_saturn_ztreme_frustum_aabb(frustum, ordered_min, ordered_max);
    const sm64_saturn_ztreme_frustum_result_t reference =
        sm64_saturn_ztreme_frustum_aabb_reference(frustum, ordered_min,
                                                  ordered_max);
    g_verdicts[(unsigned)shipped]++;
    if (shipped != reference) {
        g_shipped_vs_reference++;
        fprintf(stderr,
                "frustum divergence (shipped %d, reference %d) "
                "box [%d %d %d]..[%d %d %d]\n",
                (int)shipped, (int)reference, ordered_min[0], ordered_min[1],
                ordered_min[2], ordered_max[0], ordered_max[1],
                ordered_max[2]);
    }
    if (model_domain(frustum, &state)) {
        g_domain_cases++;
        const sm64_saturn_ztreme_frustum_result_t model =
            model_classify(frustum, &state);
        if (shipped != model) {
            g_shipped_vs_model++;
            fprintf(stderr,
                    "model divergence (shipped %d, model %d) "
                    "box [%d %d %d]..[%d %d %d]\n",
                    (int)shipped, (int)model, ordered_min[0], ordered_min[1],
                    ordered_min[2], ordered_max[0], ordered_max[1],
                    ordered_max[2]);
        }
    } else {
        g_outside_domain_cases++;
    }
}

static void sweep_classifier(void)
{
    const size_t limit_count = sizeof(k_limits) / sizeof(k_limits[0]);
    const size_t basis_count = sizeof(k_bases) / sizeof(k_bases[0]);
    const size_t position_count = sizeof(k_positions) / sizeof(k_positions[0]);
    const size_t fixed_count = sizeof(k_fixed_boxes) / sizeof(k_fixed_boxes[0]);
    const size_t band_count = sizeof(k_random_bands) / sizeof(k_random_bands[0]);
    for (size_t limit = 0U; limit < limit_count; limit++)
        for (size_t basis = 0U; basis < basis_count; basis++)
            for (size_t position = 0U; position < position_count; position++) {
                sm64_saturn_ztreme_frustum_t frustum;
                memset(&frustum, 0, sizeof(frustum));
                memcpy(frustum.position, k_positions[position],
                       sizeof(frustum.position));
                memcpy(frustum.forward, k_bases[basis].forward,
                       sizeof(frustum.forward));
                memcpy(frustum.right, k_bases[basis].right,
                       sizeof(frustum.right));
                memcpy(frustum.up, k_bases[basis].up, sizeof(frustum.up));
                frustum.near_depth = k_limits[limit].near_depth;
                frustum.far_depth = k_limits[limit].far_depth;
                frustum.half_width = k_limits[limit].half_width;
                frustum.half_height = k_limits[limit].half_height;
                frustum.focal_length = k_limits[limit].focal_length;
                for (size_t box = 0U; box < fixed_count; box++)
                    compare_case(&frustum, &k_fixed_boxes[box][0],
                                 &k_fixed_boxes[box][3]);
                for (size_t band = 0U; band < band_count; band++)
                    for (unsigned sample = 0U; sample < RANDOM_BOXES_PER_BAND;
                         sample++) {
                        const int32_t magnitude = k_random_bands[band];
                        int32_t minimum[3], maximum[3];
                        for (unsigned axis = 0U; axis < 3U; axis++) {
                            const int32_t centre = rng_span(magnitude);
                            const int32_t extent = (int32_t)(rng_next() % 512U);
                            const int64_t low = (int64_t)centre - extent;
                            const int64_t high = (int64_t)centre + extent;
                            minimum[axis] = (int32_t)(low < INT32_MIN
                                                          ? INT32_MIN : low);
                            maximum[axis] = (int32_t)(high > INT32_MAX
                                                          ? INT32_MAX : high);
                        }
                        compare_case(&frustum, minimum, maximum);
                    }
            }
}

/* --------------------------------------------------- admitted-set sweep -- */

#define ORACLE_CLUSTERS 96U
#define ORACLE_EXTRA_REFS 8U
#define ORACLE_SLOTS (ORACLE_CLUSTERS + ORACLE_EXTRA_REFS)

typedef struct oracle_scene {
    sm64_saturn_render_cluster_t clusters[ORACLE_CLUSTERS];
    sm64_saturn_scene_admission_node_t nodes[1];
    uint16_t cluster_refs[ORACLE_SLOTS];
    sm64_saturn_scene_admission_view_t view;
} oracle_scene_t;

static void build_scene(oracle_scene_t *scene, bool duplicate_refs)
{
    int32_t node_min[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t node_max[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    uint16_t ref_count = (uint16_t)ORACLE_CLUSTERS;
    memset(scene, 0, sizeof(*scene));
    for (unsigned index = 0U; index < ORACLE_CLUSTERS; index++) {
        sm64_saturn_render_cluster_t *cluster = &scene->clusters[index];
        const int32_t grid_x = (int32_t)(index % 12U) * 320 - 1920;
        const int32_t grid_z = (int32_t)(index / 12U) * 640 - 2240;
        const int32_t grid_y = (int32_t)(index % 5U) * 96 - 192;
        const int32_t extent = 24 + (int32_t)(index % 7U) * 37;
        cluster->bounds_min_q16[0] = (grid_x - extent) * 65536;
        cluster->bounds_max_q16[0] = (grid_x + extent) * 65536;
        cluster->bounds_min_q16[1] = (grid_y - extent) * 65536;
        cluster->bounds_max_q16[1] = (grid_y + extent) * 65536;
        cluster->bounds_min_q16[2] = (grid_z - extent) * 65536;
        cluster->bounds_max_q16[2] = (grid_z + extent) * 65536;
        cluster->primitive_first = (uint16_t)index;
        cluster->primitive_count = 1U;
        cluster->position_ref_count[0] = 1U;
        cluster->mandatory = (index % 11U) == 0U ? 1U : 0U;
        for (unsigned axis = 0U; axis < 3U; axis++) {
            if (cluster->bounds_min_q16[axis] < node_min[axis])
                node_min[axis] = cluster->bounds_min_q16[axis];
            if (cluster->bounds_max_q16[axis] > node_max[axis])
                node_max[axis] = cluster->bounds_max_q16[axis];
        }
        scene->cluster_refs[index] = (uint16_t)index;
    }
    if (duplicate_refs) {
        /* A package may reference the same cluster from more than one slot;
         * the admission output must still carry it exactly once. */
        for (unsigned extra = 0U; extra < ORACLE_EXTRA_REFS; extra++)
            scene->cluster_refs[ref_count++] = (uint16_t)(extra * 7U);
    }
    memcpy(scene->nodes[0].bounds_min_q16, node_min, sizeof(node_min));
    memcpy(scene->nodes[0].bounds_max_q16, node_max, sizeof(node_max));
    scene->nodes[0].cluster_ref_first = 0U;
    scene->nodes[0].cluster_ref_count = ref_count;
    scene->view.metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION;
    scene->view.metadata_valid = 1U;
    scene->view.clusters = scene->clusters;
    scene->view.cluster_count = (uint16_t)ORACLE_CLUSTERS;
    scene->view.nodes = scene->nodes;
    scene->view.node_count = 1U;
    scene->view.cluster_refs = scene->cluster_refs;
    scene->view.cluster_ref_count = ref_count;
    scene->view.root_node = 0U;
    /* saturn_demo_render.c:134-139, the production BOB frustum. */
    scene->view.frustum.near_depth = 128;
    scene->view.frustum.far_depth = 8192;
    scene->view.frustum.half_width = 160;
    scene->view.frustum.half_height = 112;
    scene->view.frustum.focal_length = 256;
}

static unsigned long g_poses;
static unsigned long g_admitted_total;
static unsigned long g_set_divergences;
/* FNV-1a over every admitted index sequence, in pose order. Printed rather
 * than pinned: a golden constant here would have to be re-blessed whenever the
 * corpus moves, which is how a pinned hash quietly stops meaning anything.
 * Printing it lets any change that claims not to move the admitted set be
 * checked by building this fixture against both revisions and comparing one
 * line of output. */
static uint64_t g_admitted_digest = 0xCBF29CE484222325ULL;

static void digest_u16(uint16_t value)
{
    g_admitted_digest ^= (uint64_t)(value & 0xFFU);
    g_admitted_digest *= 0x100000001B3ULL;
    g_admitted_digest ^= (uint64_t)(value >> 8);
    g_admitted_digest *= 0x100000001B3ULL;
}

static void compare_admitted_set(
    const sm64_saturn_scene_admission_view_t *view,
    const sm64_saturn_render_view_t *camera)
{
    uint16_t shipped_indices[ORACLE_SLOTS];
    uint16_t reference_indices[ORACLE_SLOTS];
    uint16_t shipped_portals[4], reference_portals[4];
    sm64_saturn_scene_admission_output_t shipped_output = {
        shipped_indices, (uint16_t)ORACLE_SLOTS, 0U, shipped_portals, 4U, 0U};
    sm64_saturn_scene_admission_output_t reference_output = {
        reference_indices, (uint16_t)ORACLE_SLOTS, 0U, reference_portals, 4U,
        0U};
    sm64_saturn_scene_admission_stats_t shipped_stats, reference_stats;

    memset(shipped_indices, 0xAA, sizeof(shipped_indices));
    memset(reference_indices, 0x55, sizeof(reference_indices));
    sm64_saturn_ztreme_frustum_force_reference = 0;
    const bool shipped_ok =
        sm64_saturn_scene_admit(view, camera, &shipped_output, &shipped_stats);
    sm64_saturn_ztreme_frustum_force_reference = 1;
    const bool reference_ok = sm64_saturn_scene_admit(
        view, camera, &reference_output, &reference_stats);
    sm64_saturn_ztreme_frustum_force_reference = 0;

    g_poses++;
    g_admitted_total += shipped_output.cluster_count;
    digest_u16(shipped_output.cluster_count);
    for (uint16_t slot = 0U; slot < shipped_output.cluster_count; slot++)
        digest_u16(shipped_indices[slot]);
    if (shipped_ok != reference_ok ||
        shipped_output.cluster_count != reference_output.cluster_count ||
        memcmp(shipped_indices, reference_indices,
               (size_t)shipped_output.cluster_count * sizeof(uint16_t)) != 0 ||
        shipped_stats.clusters_admitted != reference_stats.clusters_admitted ||
        shipped_stats.clusters_rejected_frustum !=
            reference_stats.clusters_rejected_frustum ||
        shipped_stats.mandatory_clusters_admitted !=
            reference_stats.mandatory_clusters_admitted ||
        shipped_stats.duplicate_clusters !=
            reference_stats.duplicate_clusters) {
        g_set_divergences++;
        fprintf(stderr,
                "admitted-set divergence: shipped %u clusters (ok %d), "
                "reference %u clusters (ok %d)\n",
                (unsigned)shipped_output.cluster_count, (int)shipped_ok,
                (unsigned)reference_output.cluster_count, (int)reference_ok);
    }
}

static void sweep_admitted_sets(void)
{
    /* 46341 == round(65536 / sqrt(2)); an eight step yaw ring. */
    static const int32_t k_yaw[8] = {65536, 46341, 0,      -46341,
                                     -65536, -46341, 0,     46341};
    static oracle_scene_t scene;
    for (unsigned variant = 0U; variant < 2U; variant++) {
        build_scene(&scene, variant != 0U);
        for (unsigned pose = 0U; pose < 512U; pose++) {
            sm64_saturn_render_view_t camera;
            const unsigned step = pose % 8U;
            const int32_t cosine = k_yaw[step];
            const int32_t sine = k_yaw[(step + 6U) % 8U];
            memset(&camera, 0, sizeof(camera));
            camera.camera_position_q16[0] = rng_span(3000) * 65536;
            camera.camera_position_q16[1] = rng_span(400) * 65536;
            camera.camera_position_q16[2] = rng_span(3000) * 65536;
            camera.view_forward_q16[0] = sine;
            camera.view_forward_q16[1] = (int32_t)(rng_next() % 20000U) - 10000;
            camera.view_forward_q16[2] = cosine;
            camera.view_projection_q16[0][0] = cosine;
            camera.view_projection_q16[0][2] = -sine;
            camera.view_projection_q16[1][1] = 65536;
            camera.generation = pose + 1U;
            compare_admitted_set(&scene.view, &camera);
        }
    }
}

/* ------------------------------------------------------------------ main - */

int main(void)
{
    sweep_classifier();
    sweep_admitted_sets();

    printf("frustum equivalence: %lu cases, %lu in cross-multiply domain, "
           "%lu outside it, %lu skipped (reference int64 overflow)\n",
           g_cases, g_domain_cases, g_outside_domain_cases, g_skipped_cases);
    printf("frustum verdicts: outside %lu, intersects %lu, inside %lu\n",
           g_verdicts[SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE],
           g_verdicts[SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS],
           g_verdicts[SM64_SATURN_ZTREME_FRUSTUM_INSIDE]);
    printf("frustum divergences: shipped vs reference %lu, "
           "shipped vs model %lu\n",
           g_shipped_vs_reference, g_shipped_vs_model);
    printf("admitted-set equivalence: %lu poses, %lu cluster admissions, "
           "%lu divergences\n",
           g_poses, g_admitted_total, g_set_divergences);
    printf("admitted-set digest: %016llx\n",
           (unsigned long long)g_admitted_digest);

    /* Non-vacuity. A sweep that classified everything the same way, or never
     * entered the cross-multiply domain, or never left it, or admitted
     * nothing, proves nothing and must fail rather than pass quietly. */
    assert(g_cases > 100000UL);
    assert(g_domain_cases > 1000UL);
    assert(g_outside_domain_cases > 1000UL);
    assert(g_verdicts[SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE] > 100UL);
    assert(g_verdicts[SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS] > 100UL);
    assert(g_verdicts[SM64_SATURN_ZTREME_FRUSTUM_INSIDE] > 100UL);
    assert(g_poses == 1024UL);
    assert(g_admitted_total > 1000UL);

    assert(g_shipped_vs_reference == 0UL);
    assert(g_shipped_vs_model == 0UL);
    assert(g_set_divergences == 0UL);

    printf("frustum cross-multiply fixture: PASS\n");
    return 0;
}
