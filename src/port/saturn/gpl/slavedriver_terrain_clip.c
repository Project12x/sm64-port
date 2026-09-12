/* GPL-3.0-or-later close-port; see slavedriver_terrain_clip.h. */
#include "slavedriver_terrain_clip.h"
#include "../gfx/saturn_render_native_math.h"

#include <stdbool.h>
#include <stddef.h>

static bool same_position(const sm64_saturn_terrain_clip_vertex_t *a,
                          const sm64_saturn_terrain_clip_vertex_t *b)
{
    return a->view.x == b->view.x && a->view.y == b->view.y &&
           a->view.z == b->view.z;
}

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
    /* SM64 triangles repeat their last corner in the four-corner bank.
     * Intersections at a plane endpoint can also repeat a ring position.
     * Count geometric corners before the consumer chooses one quad versus
     * a pentagon split; otherwise one triangle becomes two full-tile draws.
     * Preserve the first occurrence and its source-edge provenance. */
    uint8_t unique_count = 0U;
    for (uint8_t i = 0U; i < output->count; i++) {
        if (unique_count != 0U && same_position(
                &output->vertices[unique_count - 1U], &output->vertices[i]))
            continue;
        output->vertices[unique_count++] = output->vertices[i];
    }
    if (unique_count > 1U && same_position(
            &output->vertices[0], &output->vertices[unique_count - 1U]))
        unique_count--;
    output->count = unique_count;
    return output->count;
overflow:
    output->classification = SM64_SATURN_TERRAIN_CLIP_OVERFLOW;
    output->count = 0U;
    return 0;
}

/* Interpolate at an explicit Q16 ratio rather than to a fixed depth. Same
 * provenance rules as interpolate_vertex(): a generated corner records which
 * source corner its edge started from and is itself not a source corner. */
static sm64_saturn_terrain_clip_vertex_t interpolate_vertex_at(
    const sm64_saturn_terrain_clip_vertex_t *first,
    const sm64_saturn_terrain_clip_vertex_t *second, int32_t amount_q16)
{
    sm64_saturn_terrain_clip_vertex_t result = *first;
    result.view.x = first->view.x + (int32_t)(((int64_t)(second->view.x -
        first->view.x) * amount_q16) >> 16);
    result.view.y = first->view.y + (int32_t)(((int64_t)(second->view.y -
        first->view.y) * amount_q16) >> 16);
    result.view.z = first->view.z + (int32_t)(((int64_t)(second->view.z -
        first->view.z) * amount_q16) >> 16);
    result.shade = interpolate_u16(first->shade, second->shade, amount_q16);
    result.source_edge = first->source_corner;
    result.source_corner = UINT8_MAX;
    result.edge_t_q16 = amount_q16;
    return result;
}

/* Signed distance to one side plane. Every side plane passes through the
 * camera origin, so it has no constant term and the test stays exact in
 * integers: inside is f <= 0. int64 is required -- z * (coord - centre)
 * overflows int32 for distant geometry. */
static int64_t side_plane_eval(const sm64_saturn_terrain_clip_vertex_t *vertex,
                               int32_t a, int32_t b, int32_t c)
{
    return (int64_t)a * vertex->view.x + (int64_t)b * vertex->view.y +
           (int64_t)c * vertex->view.z;
}

/* Clip a convex view-space ring against the four screen-edge planes.
 *
 * This is the exact fix for the long-standing terrain warp. Projected
 * coordinates are 11-bit signed, and a vertex outside that box used to be
 * pulled back to it -- first per-axis (which changed the vertex's direction
 * entirely), now along its view ray (which keeps the direction but still moves
 * the vertex relative to its neighbours). Either way the polygon is deformed.
 * Cutting the ring at the boundary instead puts the new corner exactly where
 * the EDGE crosses, so no surviving vertex moves at all.
 *
 * Derivation, with F = focal_length, and screen_x = CX + F*x/z,
 * screen_y = CY - F*y/z, valid because z > 0 after the near clip:
 *   right   screen_x <= MAX  ->  F*x - z*(MAX - CX) <= 0
 *   left    screen_x >= MIN  ->  z*(MIN - CX) - F*x <= 0
 *   bottom  screen_y <= MAX  ->  -F*y - z*(MAX - CY) <= 0
 *   top     screen_y >= MIN  ->  F*y + z*(MIN - CY) <= 0
 *
 * Returns at most SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS corners and reports
 * CLIP_OVERFLOW if the ring grows past the buffer, so a caller that can only
 * emit four-corner commands can fall back rather than truncate. The dominant
 * case -- ground leaving the bottom of the screen, two ADJACENT corners
 * outside one plane -- yields exactly four corners and needs no fallback. */
int sm64_saturn_terrain_clip_side_planes(
    const sm64_saturn_terrain_clip_vertex_t *input, uint8_t input_count,
    int32_t focal_length, int16_t center_x, int16_t center_y,
    int16_t coord_min, int16_t coord_max,
    sm64_saturn_terrain_clip_output_t *output)
{
    if (input == NULL || output == NULL || focal_length <= 0 ||
        coord_min > coord_max || input_count == 0U ||
        input_count > SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS) {
        if (output != NULL) {
            output->classification = SM64_SATURN_TERRAIN_CLIP_OVERFLOW;
            output->count = 0U;
        }
        return 0;
    }
    const int32_t planes[4][3] = {
        {focal_length, 0, -((int32_t)coord_max - center_x)},
        {-focal_length, 0, (int32_t)coord_min - center_x},
        {0, -focal_length, -((int32_t)coord_max - center_y)},
        {0, focal_length, (int32_t)coord_min - center_y},
    };
    sm64_saturn_terrain_clip_vertex_t ring[2][
        SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS];
    uint8_t count = input_count;
    uint8_t source = 0U;
    bool clipped_any = false;
    for (uint8_t i = 0U; i < input_count; i++) ring[0][i] = input[i];

    for (uint8_t plane = 0U; plane < 4U; plane++) {
        const sm64_saturn_terrain_clip_vertex_t *const in = ring[source];
        sm64_saturn_terrain_clip_vertex_t *const out = ring[source ^ 1U];
        uint8_t produced = 0U;
        for (uint8_t i = 0U; i < count; i++) {
            const sm64_saturn_terrain_clip_vertex_t *const current = &in[i];
            const sm64_saturn_terrain_clip_vertex_t *const next =
                &in[(uint8_t)((i + 1U) % count)];
            const int64_t distance_current = side_plane_eval(
                current, planes[plane][0], planes[plane][1], planes[plane][2]);
            const int64_t distance_next = side_plane_eval(
                next, planes[plane][0], planes[plane][1], planes[plane][2]);
            if (distance_current <= 0) {
                if (produced >= SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS)
                    goto side_overflow;
                out[produced++] = *current;
            }
            if ((distance_current > 0) != (distance_next > 0)) {
                int64_t numerator = distance_current;
                int64_t denominator = distance_current - distance_next;
                /* Narrow to what the s64/s32 divide accepts; halving both
                 * preserves the ratio and the residual is far below one
                 * screen coordinate. */
                while (denominator > INT32_MAX || denominator < INT32_MIN) {
                    numerator >>= 1;
                    denominator >>= 1;
                }
                int32_t amount = 0;
                if (denominator != 0)
                    (void)sm64_saturn_div_s64_s32(numerator << 16,
                                                  (int32_t)denominator,
                                                  &amount);
                if (amount < 0) amount = 0;
                if (amount > (1 << 16)) amount = 1 << 16;
                if (produced >= SM64_SATURN_TERRAIN_CLIP_OUTPUT_CORNERS)
                    goto side_overflow;
                out[produced++] = interpolate_vertex_at(current, next, amount);
                clipped_any = true;
            }
        }
        count = produced;
        source ^= 1U;
        if (count == 0U) {
            output->classification = SM64_SATURN_TERRAIN_CLIP_AWAY;
            output->count = 0U;
            return 0;
        }
    }

    /* Same duplicate-corner collapse the near clip performs: an intersection
     * landing on an existing corner must not become a second draw. */
    uint8_t unique_count = 0U;
    for (uint8_t i = 0U; i < count; i++) {
        if (unique_count != 0U && same_position(
                &ring[source][unique_count - 1U], &ring[source][i]))
            continue;
        ring[source][unique_count++] = ring[source][i];
    }
    if (unique_count > 1U && same_position(&ring[source][0],
                                           &ring[source][unique_count - 1U]))
        unique_count--;
    for (uint8_t i = 0U; i < unique_count; i++)
        output->vertices[i] = ring[source][i];
    output->count = unique_count;
    output->classification = clipped_any
        ? SM64_SATURN_TERRAIN_CLIP_CROSSES : SM64_SATURN_TERRAIN_CLIP_FRONT;
    return output->count;
side_overflow:
    output->classification = SM64_SATURN_TERRAIN_CLIP_OVERFLOW;
    output->count = 0U;
    return 0;
}
