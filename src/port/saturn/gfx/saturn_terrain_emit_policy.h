#ifndef SM64_SATURN_TERRAIN_EMIT_POLICY_H
#define SM64_SATURN_TERRAIN_EMIT_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "../gpl/slavedriver_terrain_result.h"
#include "saturn_gouraud_bank.h"

typedef enum sm64_saturn_shade_path {
    SM64_SATURN_SHADE_FLAT_REPLACE,
    SM64_SATURN_SHADE_GOURAUD,
    SM64_SATURN_SHADE_TEXTURED_REPLACE,
    /* Kept for source compatibility with Task 6 templates. */
    SM64_SATURN_SHADE_TEXTURED = SM64_SATURN_SHADE_TEXTURED_REPLACE
} sm64_saturn_shade_path_t;

/* `clip_class` already carries a two-bit clip classification plus material
 * recovery bits.  Bits 2..3 are otherwise unused, so compact worker records
 * can carry their pre-reservation shade decision without growing the
 * cross-SH2 descriptor.  This is project-owned metadata; Yaul's VDP1 API is
 * used only later by the master when it lowers the selected template.
 *
 * Reference examined: libyaul@6012f79f237773378c8014e70d8998ad95a38d98,
 * MIT, libyaul/scu/bus/b/vdp/vdp1_cmdt.c and vdp1/cmdt.h. Reuse mode:
 * pattern-only; no upstream code copied. */
#define SM64_SATURN_TERRAIN_COMPACT_SHADE_SHIFT 2U
#define SM64_SATURN_TERRAIN_COMPACT_SHADE_MASK 0x0CU

/* Select the least expensive VDP1 shading path without changing a textured
 * material's existing bind-and-REPLACE path. A missing color array is
 * conservatively treated as a gradient, so it never manufactures a color. */
static inline sm64_saturn_shade_path_t sm64_saturn_terrain_shade_path(
    uint16_t flags, const uint16_t colors[4])
{
    /* Recovery owns a Gouraud resolved-template variant. It overrides a
     * material downgrade so PMOD and GRDA remain coherent. */
    if ((flags & SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL) != 0U)
        return SM64_SATURN_SHADE_GOURAUD;
    /* Tier degradation has already accepted the flat-material tradeoff. It
     * must win before the distinct source colors reach the Gouraud allocator. */
    if ((flags & SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED) != 0U)
        return SM64_SATURN_SHADE_FLAT_REPLACE;
    if ((flags & SM64_SATURN_TERRAIN_RESULT_TEXTURED) != 0U)
        return SM64_SATURN_SHADE_TEXTURED_REPLACE;
    if (colors == NULL)
        return SM64_SATURN_SHADE_GOURAUD;
    if (colors[0] == colors[1] && colors[0] == colors[2] &&
        colors[0] == colors[3])
        return SM64_SATURN_SHADE_FLAT_REPLACE;
    return SM64_SATURN_SHADE_GOURAUD;
}

/* Master-only lowering seam. This is deliberately Yaul-free so host tests
 * exercise the same reserve/fill path as demo_emit_terrain_result(). */
typedef struct sm64_saturn_terrain_gouraud_lowering {
    sm64_saturn_gouraud_table_t *table;
    uintptr_t address;
    bool patch;
} sm64_saturn_terrain_gouraud_lowering_t;

static inline bool sm64_saturn_terrain_lower_gouraud(
    sm64_saturn_gouraud_bank_t *bank, sm64_saturn_shade_path_t path,
    const uint16_t colors[4], sm64_saturn_terrain_gouraud_lowering_t *out)
{
    if (out == NULL)
        return false;
    out->table = NULL;
    out->address = 0U;
    out->patch = false;
    if (path != SM64_SATURN_SHADE_GOURAUD)
        return true;
    if (colors == NULL)
        return false;
    out->table = sm64_saturn_gouraud_bank_alloc(bank, &out->address);
    if (out->table == NULL)
        return true;
    for (uint8_t corner = 0U; corner < 4U; corner++)
        out->table->colors[corner] = (uint16_t)(colors[corner] | 0x8000U);
    out->patch = true;
    return true;
}

static inline uint8_t sm64_saturn_terrain_shade_path_compact_flags(
    sm64_saturn_shade_path_t path)
{
    const uint8_t encoded = (uint8_t)path;
    return encoded <= (uint8_t)SM64_SATURN_SHADE_TEXTURED_REPLACE
        ? (uint8_t)(encoded << SM64_SATURN_TERRAIN_COMPACT_SHADE_SHIFT)
        : (uint8_t)(SM64_SATURN_SHADE_GOURAUD <<
                    SM64_SATURN_TERRAIN_COMPACT_SHADE_SHIFT);
}

static inline sm64_saturn_shade_path_t
sm64_saturn_terrain_shade_path_from_compact_flags(uint8_t compact_flags)
{
    const uint8_t encoded = (uint8_t)((compact_flags &
        SM64_SATURN_TERRAIN_COMPACT_SHADE_MASK) >>
        SM64_SATURN_TERRAIN_COMPACT_SHADE_SHIFT);
    return encoded <= (uint8_t)SM64_SATURN_SHADE_TEXTURED_REPLACE
        ? (sm64_saturn_shade_path_t)encoded
        : SM64_SATURN_SHADE_GOURAUD;
}

#endif
