#include "libultra_internal.h"
#ifdef GBI_FLOATS
#include <string.h>
#endif

#ifndef GBI_FLOATS
void guMtxF2L(float mf[4][4], Mtx *m) {
    int r, c;
    s32 tmp1;
    s32 tmp2;
    s32 *m1 = &m->m[0][0];
    s32 *m2 = &m->m[2][0];
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 2; c++) {
            tmp1 = mf[r][2 * c] * 65536.0f;
            tmp2 = mf[r][2 * c + 1] * 65536.0f;
            *m1++ = (tmp1 & 0xffff0000) | ((tmp2 >> 0x10) & 0xffff);
            *m2++ = ((tmp1 << 0x10) & 0xffff0000) | (tmp2 & 0xffff);
        }
    }
}

void guMtxL2F(float mf[4][4], Mtx *m) {
    int r, c;
    u32 tmp1;
    u32 tmp2;
    u32 *m1;
    u32 *m2;
    s32 stmp1, stmp2;
    m1 = (u32 *) &m->m[0][0];
    m2 = (u32 *) &m->m[2][0];
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 2; c++) {
            tmp1 = (*m1 & 0xffff0000) | ((*m2 >> 0x10) & 0xffff);
            tmp2 = ((*m1++ << 0x10) & 0xffff0000) | (*m2++ & 0xffff);
            stmp1 = *(s32 *) &tmp1;
            stmp2 = *(s32 *) &tmp2;
            mf[r][c * 2 + 0] = stmp1 / 65536.0f;
            mf[r][c * 2 + 1] = stmp2 / 65536.0f;
        }
    }
}
#else
#ifdef TARGET_SATURN
/* On Saturn, every Mtx reaching the display list must be Q16.16 raw,
 * not float bits -- the port's own Fast3D-to-VDP1 frontend is the only
 * consumer, and it decodes natively under SATURN_MTX_IS_Q16 (a later
 * task), no float ever touched. rendering_graph_node.c's render-graph
 * sites write their wire Mtx directly from already-computed Q16 data
 * (see saturn_mtxq_write_wire there) and never reach this function;
 * this override exists for the OTHER Mtx producers that still build
 * genuinely float matrices via normal float math not implicated by the
 * proven soft-float corruption (guPerspective, skybox/painting/HUD
 * create_dl_* in ingame_menu.c, intro_geo.c) and convert them via
 * mtxf_to_mtx/guMtxF2L exactly as before. Only the WIRE FORMAT changes
 * here -- per-element exact float->Q16.16 conversion instead of a raw
 * float memcpy -- so every Mtx reaching the display list is Q16.16 on
 * this target, regardless of which producer built it. */
#include "port/saturn/gfx/saturn_matrix_kernels.h"
void guMtxF2L(float mf[4][4], Mtx *m) {
    int32_t q[4][4];
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            q[r][c] = sm64_saturn_float_to_q16(mf[r][c]);
        }
    }
    memcpy(m, q, sizeof(q));
}
#else
void guMtxF2L(float mf[4][4], Mtx *m) {
    memcpy(m, mf, sizeof(Mtx));
}
#endif
#endif

void guMtxIdentF(float mf[4][4]) {
    int r, c;
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            if (r == c) {
                mf[r][c] = 1.0f;
            } else {
                mf[r][c] = 0.0f;
            }
        }
    }
}

void guMtxIdent(Mtx *m) {
#ifndef GBI_FLOATS
    float mf[4][4];
    guMtxIdentF(mf);
    guMtxF2L(mf, m);
#else
    guMtxIdentF(m->m);
#endif
}
