#ifndef SM64_SATURN_TERRAIN_EMIT_POLICY_H
#define SM64_SATURN_TERRAIN_EMIT_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "../gpl/slavedriver_terrain_result.h"

typedef enum sm64_saturn_shade_path {
    SM64_SATURN_SHADE_FLAT_REPLACE,
    SM64_SATURN_SHADE_GOURAUD,
    SM64_SATURN_SHADE_TEXTURED
} sm64_saturn_shade_path_t;

/* Select the least expensive VDP1 shading path without changing a textured
 * material's existing bind-and-REPLACE path. A missing color array is
 * conservatively treated as a gradient, so it never manufactures a color. */
static inline sm64_saturn_shade_path_t sm64_saturn_terrain_shade_path(
    uint16_t flags, const uint16_t colors[4])
{
    if ((flags & SM64_SATURN_TERRAIN_RESULT_TEXTURED) != 0U)
        return SM64_SATURN_SHADE_TEXTURED;
    if (colors == NULL)
        return SM64_SATURN_SHADE_GOURAUD;
    if (colors[0] == colors[1] && colors[0] == colors[2] &&
        colors[0] == colors[3])
        return SM64_SATURN_SHADE_FLAT_REPLACE;
    return SM64_SATURN_SHADE_GOURAUD;
}

#endif
