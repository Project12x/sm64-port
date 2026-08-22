#ifndef MATH_UTIL_H
#define MATH_UTIL_H

#include <PR/ultratypes.h>

#include "types.h"

/*
 * The sine and cosine tables overlap, but "#define gCosineTable (gSineTable +
 * 0x400)" doesn't give expected codegen; gSineTable and gCosineTable need to
 * be different symbols for code to match. Most likely the tables were placed
 * adjacent to each other, and gSineTable cut short, such that reads overflow
 * into gCosineTable.
 *
 * These kinds of out of bounds reads are undefined behavior, and break on
 * e.g. GCC (which doesn't place the tables next to each other, and probably
 * exploits array sizes for range analysis-based optimizations as well).
 * Thus, for non-IDO compilers we use the standard-compliant version.
 */
#ifdef TARGET_SATURN
/* Saturn stores only the first quadrant: 1025 entries (4,100 B) instead of
 * 5120 (20,480 B), returning 16,380 B of HWRAM -- the table was the largest
 * tenant there after the object pool and the VDP1 command tables.
 *
 * The literals in trig_tables_quarter.inc.c are copied verbatim from the full
 * table, and quarter-wave symmetry was verified bit-exact against the shipped
 * values for all 4,096 sine indices. There is exactly one difference in the
 * whole table: at index 2048 (sin 180 degrees) the full table holds +0.0 and
 * the fold yields -0.0. They compare equal and behave identically under every
 * arithmetic operation this codebase performs on them.
 *
 * The index decomposes into two flag bits and an offset, so no division,
 * no modulo and no variable shift is needed -- SH-2 has no barrel shifter,
 * so only the compile-time-constant shifts used here are cheap:
 *   bit 10 -> mirror the offset about 90 degrees
 *   bit 11 -> negate
 * The table is exactly periodic over 4,096 indices (verified), which is why
 * coss() is just sins() advanced by a quarter turn.
 */
extern f32 gSineQuarterTable[1025];

static inline f32 saturn_sine_index(u16 index) {
    const u16 offset = index & 0x3FF;
    const f32 value = gSineQuarterTable[(index & 0x400) ? (1024 - offset) : offset];
    return (index & 0x800) ? -value : value;
}

#define sins(x) saturn_sine_index((u16) ((u16) (x) >> 4) & 0xFFF)
#define coss(x) saturn_sine_index((u16) ((((u16) (x) >> 4) + 0x400) & 0xFFF))
#else
extern f32 gSineTable[];
#ifdef AVOID_UB
#define gCosineTable (gSineTable + 0x400)
#else
extern f32 gCosineTable[];
#endif

#define sins(x) gSineTable[(u16) (x) >> 4]
#define coss(x) gCosineTable[(u16) (x) >> 4]
#endif

#define min(a, b) ((a) <= (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define sqr(x) ((x) * (x))

void *vec3f_copy(Vec3f dest, Vec3f src);
void *vec3f_set(Vec3f dest, f32 x, f32 y, f32 z);
void *vec3f_add(Vec3f dest, Vec3f a);
void *vec3f_sum(Vec3f dest, Vec3f a, Vec3f b);
void *vec3s_copy(Vec3s dest, Vec3s src);
void *vec3s_set(Vec3s dest, s16 x, s16 y, s16 z);
void *vec3s_add(Vec3s dest, Vec3s a);
void *vec3s_sum(Vec3s dest, Vec3s a, Vec3s b);
void *vec3s_sub(Vec3s dest, Vec3s a);
void *vec3s_to_vec3f(Vec3f dest, Vec3s a);
void *vec3f_to_vec3s(Vec3s dest, Vec3f a);
void *find_vector_perpendicular_to_plane(Vec3f dest, Vec3f a, Vec3f b, Vec3f c);
void *vec3f_cross(Vec3f dest, Vec3f a, Vec3f b);
void *vec3f_normalize(Vec3f dest);
void mtxf_copy(Mat4 dest, Mat4 src);
void mtxf_identity(Mat4 mtx);
void mtxf_translate(Mat4 dest, Vec3f b);
void mtxf_lookat(Mat4 mtx, Vec3f from, Vec3f to, s16 roll);
void mtxf_rotate_zxy_and_translate(Mat4 dest, Vec3f translate, Vec3s rotate);
void mtxf_rotate_xyz_and_translate(Mat4 dest, Vec3f b, Vec3s c);
void mtxf_billboard(Mat4 dest, Mat4 mtx, Vec3f position, s16 angle);
void mtxf_align_terrain_normal(Mat4 dest, Vec3f upDir, Vec3f pos, s16 yaw);
void mtxf_align_terrain_triangle(Mat4 mtx, Vec3f pos, s16 yaw, f32 radius);
void mtxf_mul(Mat4 dest, Mat4 a, Mat4 b);
void mtxf_scale_vec3f(Mat4 dest, Mat4 mtx, Vec3f s);
void mtxf_mul_vec3s(Mat4 mtx, Vec3s b);
void mtxf_to_mtx(Mtx *dest, Mat4 src);
void mtxf_rotate_xy(Mtx *mtx, s16 angle);
void get_pos_from_transform_mtx(Vec3f dest, Mat4 objMtx, Mat4 camMtx);
void vec3f_get_dist_and_angle(Vec3f from, Vec3f to, f32 *dist, s16 *pitch, s16 *yaw);
void vec3f_set_dist_and_angle(Vec3f from, Vec3f to, f32  dist, s16  pitch, s16  yaw);
s32 approach_s32(s32 current, s32 target, s32 inc, s32 dec);
f32 approach_f32(f32 current, f32 target, f32 inc, f32 dec);
s16 atan2s(f32 y, f32 x);
f32 atan2f(f32 a, f32 b);
void spline_get_weights(Vec4f result, f32 t, UNUSED s32 c);
void anim_spline_init(Vec4s *keyFrames);
s32 anim_spline_poll(Vec3f result);

#endif // MATH_UTIL_H
