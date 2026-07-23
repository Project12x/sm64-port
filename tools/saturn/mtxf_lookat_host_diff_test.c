/*
 * Host-vs-target differential test for the gMatStack[1] corruption traced in
 * docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md.
 *
 * This compiles the REAL, unmodified src/engine/math_util.c (mtxf_lookat,
 * mtxf_mul -- the exact functions geo_process_camera calls to populate
 * gMatStack[1], per rendering_graph_node.c:327-328) with the HOST compiler,
 * feeds it the exact real camera pos/focus captured live at the corrupted
 * frame, and sweeps every possible s16 roll value (the one input this
 * session could not resolve a live address for). If any roll value
 * reproduces the captured 0xe200001c bit pattern at composed[2][2], that is
 * strong evidence of a genuine algorithmic edge case in mtxf_lookat/mtxf_mul
 * themselves, independent of platform. If none do (matching this session's
 * earlier Python re-implementation, which used continuous math.sin/cos
 * rather than the real gSineTable/gCosineTable lookup this program links
 * against directly via math_util.c's own "#include trig_tables.inc.c"),
 * that is near-conclusive evidence the corruption is specific to the SH-2
 * cross-compiler's codegen or soft-float library for this exact input
 * pattern, not the algorithm itself.
 *
 * Real captured inputs (docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md,
 * "gLakituState" reads -- the fields update_graph_node_camera syncs into
 * GraphNodeCamera.pos/.focus, which geo_process_camera passes directly to
 * mtxf_lookat; see reports/e2-sourceboot-lakitu-pos-focus-2026-07-22.json):
 *   pos   (gLakituState.pos)   = (-7208.26318359375, 264.13934326171875, 7050.0)
 *   focus (gLakituState.focus) = (-6566.8955078125, 124.66311645507812, 6454.2001953125)
 * roll (GraphNodeCamera.roll) was never resolved to a live address this
 * session (set once at camera creation, never re-synced per frame) -- swept
 * across the full s16 range instead, exactly mirroring the earlier Python
 * check's methodology but now against the REAL C engine code.
 *
 * Exact calling context (rendering_graph_node.c:315-339, geo_process_camera,
 * at gMatStackIndex==0, i.e. the root camera's first push):
 *   mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
 *   mtxf_mul(gMatStack[gMatStackIndex + 1], cameraTransform, gMatStack[gMatStackIndex]);
 * gMatStack[gMatStackIndex] (index 0) is confirmed a clean identity matrix
 * every frame (geo_process_root's mtxf_identity call) -- reproduced here as
 * an explicit mtxf_identity() call, not assumed.
 *
 * Known corrupted value to check against (same doc): gMatStack[1]'s
 * row2[2] (m[2][2] in 0-indexed row/col terms) = -5.902977806835426e+20,
 * raw bits 0xe200001c.
 */
#include <stdio.h>
#include <stdint.h>

#include "types.h"
#include "engine/math_util.h"

/*
 * math_util.c compiles as a single translation unit, and the linker pulls
 * it in at object-file granularity: once ANY symbol from it is needed
 * (mtxf_lookat/mtxf_mul/mtxf_identity, in this test's case), every other
 * function's undefined references become link requirements too -- even
 * functions this test never calls. Three externals used by *other*,
 * unrelated functions in math_util.c (spline/animation helpers this test
 * does not exercise) are stubbed here purely to satisfy the linker. These
 * are never invoked by mtxf_lookat/mtxf_mul/mtxf_identity and have no
 * bearing on this test's result. Real signatures, ground-truthed rather
 * than guessed:
 *   - find_floor: src/engine/surface_collision.h:44
 *   - guMtxF2L:   include/PR/gu.h:19
 *   - gVec3fZero: src/engine/graph_node.h:360 (defined graph_node.c:16)
 */
struct Surface;
f32 find_floor(UNUSED f32 xPos, UNUSED f32 yPos, UNUSED f32 zPos,
               UNUSED struct Surface **pfloor)
{
    return 0.0f;
}
void guMtxF2L(UNUSED float mf[4][4], UNUSED Mtx *m)
{
}
Vec3f gVec3fZero = {0.0f, 0.0f, 0.0f};

#define KNOWN_BAD_BITS 0xe200001cU

typedef union {
    f32 f;
    uint32_t u;
} cell_bits_t;

int main(void)
{
    Vec3f pos = {-7208.26318359375f, 264.13934326171875f, 7050.0f};
    Vec3f focus = {-6566.8955078125f, 124.66311645507812f, 6454.2001953125f};
    Mat4 identity;
    Mat4 cameraTransform;
    Mat4 composed;
    int roll;
    int anomaly_count = 0;
    int huge_magnitude_count = 0;
    cell_bits_t sample;

    mtxf_identity(identity);

    for (roll = 0; roll < 65536; roll++) {
        cell_bits_t cell;

        mtxf_lookat(cameraTransform, pos, focus, (s16) roll);
        mtxf_mul(composed, cameraTransform, identity);

        cell.f = composed[2][2];

        if (cell.u == KNOWN_BAD_BITS) {
            printf("EXACT BIT-FOR-BIT MATCH at roll=%d: composed[2][2] = "
                   "%.9g (bits 0x%08x)\n",
                   roll, (double) cell.f, cell.u);
            anomaly_count++;
        } else if (cell.f < -1e10f || cell.f > 1e10f) {
            printf("Huge-magnitude anomaly at roll=%d: composed[2][2] = "
                   "%.9g (bits 0x%08x)\n",
                   roll, (double) cell.f, cell.u);
            huge_magnitude_count++;
        }
    }

    printf("\nSwept all 65536 roll values.\n");
    printf("Exact 0x%08x bit-for-bit matches: %d\n", KNOWN_BAD_BITS,
           anomaly_count);
    printf("Other huge-magnitude (>1e10) anomalies: %d\n",
           huge_magnitude_count);

    mtxf_lookat(cameraTransform, pos, focus, 0);
    mtxf_mul(composed, cameraTransform, identity);
    sample.f = composed[2][2];
    printf("\nSample result at roll=0: composed[2][2] = %.9g (bits 0x%08x)\n",
           (double) sample.f, sample.u);
    printf("composed row2 = (%.9g, %.9g, %.9g, %.9g)\n",
           (double) composed[2][0], (double) composed[2][1],
           (double) composed[2][2], (double) composed[2][3]);

    if (anomaly_count > 0) {
        printf("\nRESULT: HOST REPRODUCES THE CAPTURED CORRUPTION.\n");
        printf("This points at a genuine algorithmic edge case in "
               "mtxf_lookat/mtxf_mul, independent of platform.\n");
    } else if (huge_magnitude_count > 0) {
        printf("\nRESULT: HOST PRODUCES OTHER HUGE-MAGNITUDE ANOMALIES, "
               "BUT NOT THE EXACT CAPTURED BIT PATTERN.\n");
        printf("Ambiguous -- some numerical instability exists on host too, "
               "but not an exact reproduction. Needs further scrutiny.\n");
    } else {
        printf("\nRESULT: HOST NEVER REPRODUCES THE CORRUPTION ACROSS ALL "
               "65536 ROLL VALUES.\n");
        printf("This is near-conclusive evidence the corruption is specific "
               "to the SH-2 cross-compiler's codegen or soft-float library "
               "for this exact input pattern, not the algorithm itself.\n");
    }

    return anomaly_count > 0 ? 1 : 0;
}
