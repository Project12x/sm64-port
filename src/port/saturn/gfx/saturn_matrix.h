#ifndef SM64_SATURN_MATRIX_H
#define SM64_SATURN_MATRIX_H

#include <stdbool.h>
#include <stdint.h>

/* Q16.16 fixed-point 4x4 matrix stack for the Fast3D-to-VDP1 lowering
 * front end. No Yaul/Saturn dependency by design: this header is
 * compiled directly by tools/saturn/runtime_contract_test.c under the
 * host's native C compiler (see Makefile.saturn.mk's
 * verify-runtime-contracts target), independent of any emulator or
 * cross-compiler.
 *
 * Q16.16 was chosen as the fixed-point TARGET format because SH-2 has no
 * FPU. The SOURCE wire format is NOT the classic N64 split-int s15.16 GBI
 * encoding: this build defines F3DEX_GBI_2E=1 for every source file
 * (src/port/saturn/sourceboot/Makefile:74), which makes
 * include/PR/gbi.h's GBI_FLOATS active unconditionally (gbi.h:90-94).
 * Under GBI_FLOATS, `Mtx` is `struct { float m[4][4]; }` (gbi.h:1192-1194).
 * Every real G_MTX command's w1 therefore points at 16 consecutive
 * row-major floats. This decode converts those floats to Q16.16 by
 * truncating multiply. Reference: src/pc/gfx/gfx_pc.c's gfx_matrix_mul
 * (~L544-555) for the row-major multiply convention this stack must
 * match, and gfx_sp_vertex (~L616-619) for the row-vector transform
 * convention. This is a distinct fixed-point implementation (SH-2 has no
 * FPU), not a copy. */

typedef struct sm64_saturn_mtx {
    int32_t m[4][4];
} sm64_saturn_mtx_t;

/* Decode one N64 Fast3D matrix from its real on-target GBI_FLOATS
 * encoding (16 consecutive row-major floats) into Q16.16. */
static inline void
sm64_saturn_matrix_decode(const float *gbi_floats, sm64_saturn_mtx_t *out)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out->m[i][j] = (int32_t)(gbi_floats[i * 4 + j] * 65536.0f);
        }
    }
}

static inline void
sm64_saturn_matrix_identity(sm64_saturn_mtx_t *out)
{
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out->m[i][j] = (i == j) ? (1 << 16) : 0;
        }
    }
}

#endif
