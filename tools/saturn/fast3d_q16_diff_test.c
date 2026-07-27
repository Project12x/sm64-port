/*
 * Task 2 host differential gate for the Q16 Fast3D vertex path.
 *
 * The seed cases use positions copied from Bob-omb Battlefield's canonical
 * source batches (levels/bob/areas/1/1/model.inc.c).  The matrices below are
 * deliberately small, representable Q16 camera/projection probes; they are
 * not presented as a live capture.  A bounded sourceboot trace will extend
 * this corpus with route-captured matrix/viewport/light state before the
 * Q16 path replaces the float frontend.
 *
 * This is original test code.  It exercises the project-owned Q16 kernel
 * (whose Jo Engine attribution is carried by saturn_q16_sh2.h); no upstream
 * renderer implementation is copied here.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "saturn_matrix.h"

typedef struct corpus_vertex {
    int32_t x;
    int32_t y;
    int32_t z;
} corpus_vertex_t;

typedef struct corpus_case {
    const char *name;
    sm64_saturn_mtx_t matrix;
    int16_t viewport_x;
    int16_t viewport_y;
    int16_t viewport_width;
    int16_t viewport_height;
} corpus_case_t;

typedef struct q16_clip_vertex {
    int32_t x;
    int32_t y;
    int32_t w;
} q16_clip_vertex_t;

/* Representative positions from bob_seg7_vertex_07002818 and
 * bob_seg7_vertex_07002908.  Keep this corpus in source-world units: the
 * frontend receives float GBI vertices today, whereas the Q16 candidate must
 * prove its explicit conversion boundary. */
static const corpus_vertex_t k_bob_vertices[] = {
    { 4864, 1024, 4096 }, { 7680, 768, 0 }, { 3840, 768, 2304 },
    { 2304, 768, 4352 }, { 3584, 656, -767 }, { 5888, 1024, 4096 },
    { -1535, 256, 5888 }, { -3071, 0, 7168 }, { -921, 256, 5888 },
    { -4991, 1024, -4479 }, { -4132, 513, -6035 }, { -7167, 1024, -7167 },
};

static int32_t q16_from_int(int32_t value)
{
    return value << 16;
}

static int32_t q16_div(int32_t numerator, int32_t denominator)
{
    assert(denominator > 0);
    return (int32_t)(((int64_t)numerator << 16) / denominator);
}

static q16_clip_vertex_t q16_transform(const sm64_saturn_mtx_t *m,
                                       corpus_vertex_t vertex)
{
    const int32_t x = q16_from_int(vertex.x);
    const int32_t y = q16_from_int(vertex.y);
    const int32_t z = q16_from_int(vertex.z);

    return (q16_clip_vertex_t){
        sm64_saturn_q16_mul(x, m->m[0][0]) +
            sm64_saturn_q16_mul(y, m->m[1][0]) +
            sm64_saturn_q16_mul(z, m->m[2][0]) + m->m[3][0],
        sm64_saturn_q16_mul(x, m->m[0][1]) +
            sm64_saturn_q16_mul(y, m->m[1][1]) +
            sm64_saturn_q16_mul(z, m->m[2][1]) + m->m[3][1],
        sm64_saturn_q16_mul(x, m->m[0][3]) +
            sm64_saturn_q16_mul(y, m->m[1][3]) +
            sm64_saturn_q16_mul(z, m->m[2][3]) + m->m[3][3]
    };
}

static void float_project(const corpus_case_t *test, corpus_vertex_t vertex,
                          int16_t *screen_x, int16_t *screen_y)
{
    const sm64_saturn_mtx_t *m = &test->matrix;
    const float x = (float)vertex.x * ((float)m->m[0][0] / 65536.0f) +
                    (float)vertex.y * ((float)m->m[1][0] / 65536.0f) +
                    (float)vertex.z * ((float)m->m[2][0] / 65536.0f) +
                    ((float)m->m[3][0] / 65536.0f);
    const float y = (float)vertex.x * ((float)m->m[0][1] / 65536.0f) +
                    (float)vertex.y * ((float)m->m[1][1] / 65536.0f) +
                    (float)vertex.z * ((float)m->m[2][1] / 65536.0f) +
                    ((float)m->m[3][1] / 65536.0f);
    const float w = (float)vertex.x * ((float)m->m[0][3] / 65536.0f) +
                    (float)vertex.y * ((float)m->m[1][3] / 65536.0f) +
                    (float)vertex.z * ((float)m->m[2][3] / 65536.0f) +
                    ((float)m->m[3][3] / 65536.0f);

    assert(w > 0.0f);
    *screen_x = (int16_t)((float)test->viewport_x +
        (x / w * 0.5f + 0.5f) * (float)test->viewport_width);
    *screen_y = (int16_t)((float)test->viewport_y +
        (1.0f - (y / w * 0.5f + 0.5f)) * (float)test->viewport_height);
}

static void q16_project(const corpus_case_t *test, corpus_vertex_t vertex,
                        int16_t *screen_x, int16_t *screen_y)
{
    const q16_clip_vertex_t clip = q16_transform(&test->matrix, vertex);
    const int32_t ndc_x = q16_div(clip.x, clip.w);
    const int32_t ndc_y = q16_div(clip.y, clip.w);
    const int32_t half_plus_x = (ndc_x >> 1) + (1 << 15);
    const int32_t half_plus_y = (ndc_y >> 1) + (1 << 15);

    *screen_x = (int16_t)(test->viewport_x +
        (int32_t)(((int64_t)half_plus_x * test->viewport_width) >> 16));
    *screen_y = (int16_t)(test->viewport_y + test->viewport_height -
        (int32_t)(((int64_t)half_plus_y * test->viewport_height) >> 16));
}

static int32_t abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

int main(void)
{
    corpus_case_t cases[2];

    for (uint32_t i = 0; i < 2U; i++) {
        sm64_saturn_matrix_identity(&cases[i].matrix);
        cases[i].viewport_x = 0;
        cases[i].viewport_y = 0;
        cases[i].viewport_width = 320;
        cases[i].viewport_height = 224;
        /* q16 0.05 exactly as represented by the production matrix wire
         * format; w is source z + a positive camera distance. */
        cases[i].matrix.m[0][0] = 3276;
        cases[i].matrix.m[1][1] = 3276;
        cases[i].matrix.m[2][3] = 1 << 16;
        cases[i].matrix.m[3][3] = 10000 << 16;
    }
    cases[0].name = "bob-source-batch-forward";
    cases[1].name = "bob-source-batch-offset";
    cases[1].matrix.m[3][0] = -(300 << 16);
    cases[1].matrix.m[3][1] = 175 << 16;

    for (uint32_t c = 0; c < 2U; c++) {
        for (uint32_t v = 0; v < sizeof(k_bob_vertices) / sizeof(k_bob_vertices[0]); v++) {
            int16_t float_x, float_y, q16_x, q16_y;
            float_project(&cases[c], k_bob_vertices[v], &float_x, &float_y);
            q16_project(&cases[c], k_bob_vertices[v], &q16_x, &q16_y);
            if (abs_i32((int32_t)float_x - q16_x) > 1 ||
                abs_i32((int32_t)float_y - q16_y) > 1) {
                (void)fprintf(stderr, "%s vertex %u: float=(%d,%d) q16=(%d,%d)\\n",
                              cases[c].name, v, float_x, float_y, q16_x, q16_y);
                return 1;
            }
        }
    }

    return 0;
}
