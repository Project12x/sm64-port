/* GPL-3.0-or-later close-port; see slavedriver_terrain_clip.h. */
#include "slavedriver_terrain_clip.h"
#include "../gfx/saturn_render_native_math.h"

#include <stdbool.h>
#include <stddef.h>

static uint16_t interpolate_u16(uint16_t first, uint16_t second,
                                int32_t amount_q16)
{
    const int64_t delta = (int64_t)(int32_t)second - first;
    int64_t value = (int64_t)first + ((delta * amount_q16) >> 16);
    if (value < 0) value = 0;
    if (value > UINT16_MAX) value = UINT16_MAX;
    return (uint16_t)value;
}

static sm64_saturn_terrain_clip_vertex_t interpolate_vertex(
    const sm64_saturn_terrain_clip_vertex_t *first,
    const sm64_saturn_terrain_clip_vertex_t *second,
    int32_t near_depth)
{
    const int32_t denominator = second->view.z - first->view.z;
    int32_t amount = 0;
    if (denominator != 0) {
        (void)sm64_saturn_div_s64_s32(
            (int64_t)(near_depth - first->view.z) << 16,
            denominator, &amount);
    }
    sm64_saturn_terrain_clip_vertex_t result = *first;
    result.view.x = first->view.x + (int32_t)(((int64_t)(second->view.x -
        first->view.x) * amount) >> 16);
    result.view.y = first->view.y + (int32_t)(((int64_t)(second->view.y -
        first->view.y) * amount) >> 16);
    result.view.z = first->view.z + (int32_t)(((int64_t)(second->view.z -
        first->view.z) * amount) >> 16);
    result.view.z = near_depth;
    result.shade = interpolate_u16(first->shade, second->shade, amount);
    result.source_edge = first->source_corner;
    result.source_corner = UINT8_MAX;
    result.edge_t_q16 = amount;
    return result;
}

int sm64_saturn_terrain_clip_near_quad(
    const sm64_saturn_terrain_clip_vertex_t input[4], int32_t near_depth,
    sm64_saturn_terrain_clip_output_t *output)
{
    if (input == NULL || output == NULL || near_depth <= 0) return 0;
    output->count = 0U;
    bool any_front = false;
    bool any_back = false;
    for (uint8_t i = 0U; i < 4U; i++) {
        if (input[i].view.z >= near_depth) any_front = true;
        else any_back = true;
    }
    if (!any_front) {
        output->classification = SM64_SATURN_TERRAIN_CLIP_AWAY;
        return 0;
    }
    if (!any_back) {
        output->classification = SM64_SATURN_TERRAIN_CLIP_FRONT;
        for (uint8_t i = 0U; i < 4U; i++) output->vertices[i] = input[i];
        output->count = 4U;
        return 4;
    }
    output->classification = SM64_SATURN_TERRAIN_CLIP_CROSSES;
    for (uint8_t i = 0U; i < 4U; i++) {
        const sm64_saturn_terrain_clip_vertex_t *current = &input[i];
        const sm64_saturn_terrain_clip_vertex_t *next = &input[(i + 1U) & 3U];
        const bool current_front = current->view.z >= near_depth;
        const bool next_front = next->view.z >= near_depth;
        if (current_front) {
            if (output->count >= SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS)
                goto overflow;
            output->vertices[output->count++] = *current;
        }
        if (current_front != next_front) {
            if (output->count >= SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS)
                goto overflow;
            output->vertices[output->count++] = interpolate_vertex(
                current, next, near_depth);
        }
    }
    return output->count;
overflow:
    output->classification = SM64_SATURN_TERRAIN_CLIP_OVERFLOW;
    output->count = 0U;
    return 0;
}
