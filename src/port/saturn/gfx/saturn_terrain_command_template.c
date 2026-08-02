#include "saturn_terrain_command_template.h"

bool sm64_saturn_terrain_template_build(
    sm64_saturn_terrain_command_template_t *out,
    const sm64_saturn_terrain_primitive_t *primitive)
{
    if (out == NULL || primitive == NULL)
        return false;

    const uint16_t material_path = primitive->flags &
        (SM64_SATURN_TERRAIN_RESULT_TEXTURED |
         SM64_SATURN_TERRAIN_RESULT_GOURAUD);
    if (material_path == (SM64_SATURN_TERRAIN_RESULT_TEXTURED |
                          SM64_SATURN_TERRAIN_RESULT_GOURAUD))
        return false;

    out->flags = primitive->flags;
    out->color = primitive->colors[0];
    out->texture_slot = primitive->texture_slot;
    out->shade_path = sm64_saturn_terrain_shade_path(
        primitive->flags, primitive->colors);
    return true;
}

bool sm64_saturn_terrain_template_build_from_bob(
    sm64_saturn_terrain_command_template_t *out, bool textured,
    const uint8_t rgb[3], uint32_t tile_offset)
{
    if (rgb == NULL)
        return false;
    const uint16_t shade = (uint16_t)(((uint16_t)rgb[0] << 10) |
                                      ((uint16_t)rgb[1] << 5) | rgb[2]);
    const sm64_saturn_terrain_primitive_t primitive = {
        .flags = SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            (textured ? SM64_SATURN_TERRAIN_RESULT_TEXTURED
                      : SM64_SATURN_TERRAIN_RESULT_GOURAUD),
        .colors = {shade, shade, shade, shade},
        .texture_slot = tile_offset > UINT16_MAX
            ? UINT16_MAX : (uint16_t)tile_offset,
    };
    return sm64_saturn_terrain_template_build(out, &primitive);
}

bool sm64_saturn_terrain_template_matches(
    const sm64_saturn_terrain_command_template_t *template_value,
    uint16_t flags, const uint16_t colors[4], uint16_t texture_slot)
{
    if (template_value == NULL || colors == NULL ||
        template_value->texture_slot != texture_slot)
        return false;

    const uint16_t material_mask =
        SM64_SATURN_TERRAIN_RESULT_OPAQUE |
        SM64_SATURN_TERRAIN_RESULT_TEXTURED |
        SM64_SATURN_TERRAIN_RESULT_GOURAUD;
    if ((template_value->flags & material_mask) != (flags & material_mask))
        return false;

    const sm64_saturn_shade_path_t path =
        sm64_saturn_terrain_shade_path(flags, colors);
    if (template_value->shade_path != (uint16_t)path)
        return false;
    return path != SM64_SATURN_SHADE_FLAT_REPLACE ||
           template_value->color == colors[0];
}
