/* Host test for sm64_saturn_terrain_clip_side_planes().
 *
 * These assertions check the geometry, not just the return code: a clip that
 * returned the right corner COUNT while putting the corners in the wrong place
 * would still warp textures, which is the bug this function exists to fix. */
#include <assert.h>
#include <stdio.h>

#include "slavedriver_terrain_clip.h"

#define FOCAL 256
#define CENTER_X 160
#define CENTER_Y 112
#define COORD_MIN (-1024)
#define COORD_MAX 1023

static sm64_saturn_terrain_clip_vertex_t vertex(int32_t x, int32_t y,
                                                int32_t z, uint8_t corner)
{
    sm64_saturn_terrain_clip_vertex_t v = {0};
    v.view.x = x;
    v.view.y = y;
    v.view.z = z;
    v.shade = 0x1234U;
    v.source_corner = corner;
    v.source_edge = UINT8_MAX;
    return v;
}

static int32_t screen_x(const sm64_saturn_terrain_clip_vertex_t *v)
{
    return CENTER_X + (int32_t)(((int64_t)FOCAL * v->view.x) / v->view.z);
}

static int32_t screen_y(const sm64_saturn_terrain_clip_vertex_t *v)
{
    return CENTER_Y - (int32_t)(((int64_t)FOCAL * v->view.y) / v->view.z);
}

static int run(const sm64_saturn_terrain_clip_vertex_t *in, uint8_t count,
               sm64_saturn_terrain_clip_output_t *out)
{
    return sm64_saturn_terrain_clip_side_planes(
        in, count, FOCAL, CENTER_X, CENTER_Y, COORD_MIN, COORD_MAX, out);
}

int main(void)
{
    sm64_saturn_terrain_clip_output_t output;

    /* A quad entirely on screen must pass through untouched: same count, same
     * positions, and classified FRONT rather than CROSSES. A clip that always
     * ran would be a silent per-frame cost on every primitive. */
    {
        const sm64_saturn_terrain_clip_vertex_t in[4] = {
            vertex(-100, 100, 1000, 0), vertex(100, 100, 1000, 1),
            vertex(100, -100, 1000, 2), vertex(-100, -100, 1000, 3)};
        assert(run(in, 4U, &output) == 4);
        assert(output.classification == SM64_SATURN_TERRAIN_CLIP_FRONT);
        for (uint8_t i = 0U; i < 4U; i++) {
            assert(output.vertices[i].view.x == in[i].view.x);
            assert(output.vertices[i].view.y == in[i].view.y);
            assert(output.vertices[i].view.z == in[i].view.z);
            assert(output.vertices[i].source_corner == in[i].source_corner);
        }
    }

    /* THE CASE THAT CAUSED THE WARP: ground running off the bottom of the
     * screen. Two ADJACENT corners are below the bottom plane, so the result
     * must still be exactly four corners -- emittable as one VDP1 quad with no
     * fallback and no extra command. */
    {
        const sm64_saturn_terrain_clip_vertex_t in[4] = {
            vertex(-100, -50, 1000, 0), vertex(100, -50, 1000, 1),
            vertex(100, -9000, 1000, 2), vertex(-100, -9000, 1000, 3)};
        assert(screen_y(&in[2]) > COORD_MAX);
        assert(run(in, 4U, &output) == 4);
        assert(output.classification == SM64_SATURN_TERRAIN_CLIP_CROSSES);
        for (uint8_t i = 0U; i < output.count; i++) {
            const int32_t sy = screen_y(&output.vertices[i]);
            const int32_t sx = screen_x(&output.vertices[i]);
            assert(sy >= COORD_MIN && sy <= COORD_MAX);
            assert(sx >= COORD_MIN && sx <= COORD_MAX);
        }
        /* The two corners that were already inside must NOT have moved. That
         * is the whole point: clamping moved them, clipping does not. */
        assert(output.vertices[0].view.y == -50);
        assert(output.vertices[1].view.y == -50);
    }

    /* A ring entirely beyond one plane is rejected outright rather than
     * collapsed onto the boundary. */
    {
        const sm64_saturn_terrain_clip_vertex_t in[4] = {
            vertex(9000, 100, 100, 0), vertex(9100, 100, 100, 1),
            vertex(9100, -100, 100, 2), vertex(9000, -100, 100, 3)};
        assert(run(in, 4U, &output) == 0);
        assert(output.classification == SM64_SATURN_TERRAIN_CLIP_AWAY);
    }

    /* One corner outside a single plane becomes a pentagon. The function must
     * report it honestly so a four-corner caller can fall back; silently
     * dropping a corner here would punch a hole in the terrain. */
    {
        const sm64_saturn_terrain_clip_vertex_t in[4] = {
            vertex(-100, 100, 1000, 0), vertex(9000, 100, 1000, 1),
            vertex(100, -100, 1000, 2), vertex(-100, -100, 1000, 3)};
        const int produced = run(in, 4U, &output);
        assert(produced == 5 || output.classification ==
               SM64_SATURN_TERRAIN_CLIP_OVERFLOW);
    }

    /* Degenerate guards must fail closed, not read past the ring. */
    {
        const sm64_saturn_terrain_clip_vertex_t in[4] = {
            vertex(-100, 100, 1000, 0), vertex(100, 100, 1000, 1),
            vertex(100, -100, 1000, 2), vertex(-100, -100, 1000, 3)};
        assert(sm64_saturn_terrain_clip_side_planes(
                   in, 4U, 0, CENTER_X, CENTER_Y, COORD_MIN, COORD_MAX,
                   &output) == 0);
        assert(output.classification == SM64_SATURN_TERRAIN_CLIP_OVERFLOW);
        assert(run(NULL, 4U, &output) == 0);
        assert(run(in, 0U, &output) == 0);
    }

    printf("terrain clip side-plane test passed\n");
    return 0;
}
