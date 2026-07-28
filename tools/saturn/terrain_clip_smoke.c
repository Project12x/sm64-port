#include <assert.h>
#include <stdint.h>

#include "slavedriver_terrain_clip.h"

int main(void)
{
    const sm64_saturn_terrain_clip_vertex_t input[4] = {
        {{-8, -8, 64}, 10U, 0U, 0U, 0},
        {{8, -8, 64}, 20U, 1U, 1U, 0},
        {{8, 8, 256}, 30U, 2U, 2U, 0},
        {{-8, 8, 256}, 40U, 3U, 3U, 0},
    };
    sm64_saturn_terrain_clip_output_t output;
    assert(sm64_saturn_terrain_clip_near_quad(input, 128, &output) == 4);
    assert(output.classification == SM64_SATURN_TERRAIN_CLIP_CROSSES);
    assert(output.vertices[0].view.z >= 128);
    const sm64_saturn_terrain_clip_vertex_t away[4] = {
        {{0, 0, 1}, 0U, 0U, 0U, 0}, {{1, 0, 1}, 0U, 1U, 1U, 0},
        {{1, 1, 1}, 0U, 2U, 2U, 0}, {{0, 1, 1}, 0U, 3U, 3U, 0},
    };
    assert(sm64_saturn_terrain_clip_near_quad(away, 128, &output) == 0);
    assert(output.classification == SM64_SATURN_TERRAIN_CLIP_AWAY);
    return 0;
}
