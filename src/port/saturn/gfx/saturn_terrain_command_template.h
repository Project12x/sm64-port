#ifndef SM64_SATURN_TERRAIN_COMMAND_TEMPLATE_H
#define SM64_SATURN_TERRAIN_COMMAND_TEMPLATE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "saturn_terrain_emit_policy.h"

/* Portable input used while promoting a generated static scene. Target-only
 * VDP1 addresses are deliberately absent: the renderer resolves those once
 * the VRAM partitions and texture residency are available. */
typedef struct sm64_saturn_terrain_primitive {
    uint16_t flags;
    uint16_t colors[4];
    uint16_t texture_slot;
} sm64_saturn_terrain_primitive_t;

/* Patch-mask declarations are shared by generated metadata and resolved VDP1
 * images, so a material cannot acquire a frame-writable word implicitly. */
typedef enum sm64_saturn_terrain_patch_mask {
    SM64_SATURN_TERRAIN_PATCH_END = 1U << 0,
    SM64_SATURN_TERRAIN_PATCH_LINK = 1U << 1,
    SM64_SATURN_TERRAIN_PATCH_TEXTURE_SOURCE = 1U << 4,
    SM64_SATURN_TERRAIN_PATCH_XY = 0x3FC0U,
    SM64_SATURN_TERRAIN_PATCH_GOURAUD = 1U << 14
} sm64_saturn_terrain_patch_mask_t;

typedef struct sm64_saturn_terrain_command_template {
    uint16_t flags;
    uint16_t color;
    uint16_t texture_slot;
    uint16_t shade_path;
    uint16_t patch_mask;
} sm64_saturn_terrain_command_template_t;

/* One bit per VDP1 command word.  A template owns every word, including
 * reserved words, and frame code may change only the explicitly named
 * dynamic fields.  This keeps an accidental material reconstruction from
 * silently changing a draw mode, colour, CLUT, or size word. */
/* Resolved once after VDP1 partitions/texture residency are established.
 * This is deliberately a complete command image, not a five-word material
 * summary: worker lanes publish only coordinate patch data and never build
 * material state. */
typedef struct sm64_saturn_terrain_resolved_command {
    union {
        uint16_t words[16];
        /* Legacy field names remain source-compatible for host fixtures;
         * production templates use the complete `words` image above. */
        struct {
            uint16_t control;
            uint16_t link;
            uint16_t pmod;
            uint16_t colr;
            uint16_t srca;
            uint16_t size;
            uint16_t remaining[10];
        };
    };
    uint16_t patch_mask;
} sm64_saturn_terrain_resolved_command_t;

#define SM64_SATURN_VDP1_COMMAND_BYTES 32U
#define SM64_SATURN_TERRAIN_COMPACT_ENTRY_BYTES \
    ((uint32_t)sizeof(sm64_saturn_terrain_resolved_command_t))

bool sm64_saturn_terrain_template_build(
    sm64_saturn_terrain_command_template_t *out,
    const sm64_saturn_terrain_primitive_t *primitive);

bool sm64_saturn_terrain_template_build_from_bob(
    sm64_saturn_terrain_command_template_t *out, bool textured,
    const uint8_t rgb[3], uint32_t tile_offset);

bool sm64_saturn_terrain_template_matches(
    const sm64_saturn_terrain_command_template_t *template_value,
    uint16_t flags, const uint16_t colors[4], uint16_t texture_slot);

static inline bool sm64_saturn_terrain_template_patch(
    void *out_command, const void *template_command,
    const int16_t vertices[4][2], uint16_t link, bool end_state,
    bool patch_gouraud, uintptr_t gouraud_address)
{
    if (out_command == NULL || template_command == NULL || vertices == NULL)
        return false;

    /* VDP1 command words are a fixed 32-byte hardware format. Copy the
     * resolved command wholesale, then touch only CTRL.END, LINK, XA..YD,
     * and (for a true Gouraud path) GRDA. Byte-offset memcpy keeps this
     * helper portable and avoids aliasing a target vdp1_cmdt_t on the host. */
    uint8_t *out = out_command;
    const uint8_t *source = template_command;
    memcpy(out, source, SM64_SATURN_VDP1_COMMAND_BYTES);

    uint16_t control;
    memcpy(&control, source, sizeof(control));
    control = (uint16_t)((control & 0x7FFFU) |
                         (end_state ? 0x8000U : 0U));
    memcpy(out, &control, sizeof(control));
    memcpy(out + 2U, &link, sizeof(link));
    memcpy(out + 12U, vertices, 8U * sizeof(int16_t));
    if (patch_gouraud) {
        const uint16_t encoded = (uint16_t)((gouraud_address >> 3) & 0xFFFFU);
        memcpy(out + 28U, &encoded, sizeof(encoded));
    }
    return true;
}

static inline bool sm64_saturn_terrain_template_patch_resolved_record_ex(
    void *out_command, const sm64_saturn_terrain_resolved_command_t *resolved,
    const int16_t vertices[4][2], uint16_t link, bool end_state,
    bool patch_gouraud, uintptr_t gouraud_address,
    bool patch_texture_source, uint16_t texture_source);

static inline bool sm64_saturn_terrain_template_patch_resolved(
    void *out_command, uint16_t control, uint16_t pmod, uint16_t colr,
    uint16_t srca, uint16_t size, const int16_t vertices[4][2], uint16_t link,
    bool end_state, bool patch_gouraud, uintptr_t gouraud_address)
{
    if (out_command == NULL || vertices == NULL)
        return false;

    const sm64_saturn_terrain_resolved_command_t resolved = {
        .words = {control, 0U, pmod, colr, srca, size},
        .patch_mask = SM64_SATURN_TERRAIN_PATCH_END |
            SM64_SATURN_TERRAIN_PATCH_LINK |
            SM64_SATURN_TERRAIN_PATCH_XY |
            SM64_SATURN_TERRAIN_PATCH_GOURAUD};
    return sm64_saturn_terrain_template_patch_resolved_record_ex(
        out_command, &resolved, vertices, link, end_state, patch_gouraud,
        gouraud_address, false, 0U);
}

static inline bool sm64_saturn_terrain_template_patch_resolved_record_ex(
    void *out_command, const sm64_saturn_terrain_resolved_command_t *resolved,
    const int16_t vertices[4][2], uint16_t link, bool end_state,
    bool patch_gouraud, uintptr_t gouraud_address,
    bool patch_texture_source, uint16_t texture_source)
{
    if (out_command == NULL || resolved == NULL || vertices == NULL)
        return false;
    const uint16_t required = SM64_SATURN_TERRAIN_PATCH_END |
        SM64_SATURN_TERRAIN_PATCH_LINK | SM64_SATURN_TERRAIN_PATCH_XY |
        (patch_gouraud ? SM64_SATURN_TERRAIN_PATCH_GOURAUD : 0U) |
        (patch_texture_source ? SM64_SATURN_TERRAIN_PATCH_TEXTURE_SOURCE : 0U);
    if ((resolved->patch_mask & required) != required)
        return false;
    uint16_t words[16];
    memcpy(words, resolved->words, sizeof(words));
    words[0] = (uint16_t)((words[0] & 0x7FFFU) |
        (end_state ? 0x8000U : 0U));
    words[1] = link;
    memcpy(&words[6], vertices, 8U * sizeof(int16_t));
    if (patch_gouraud)
        words[14] = (uint16_t)((gouraud_address >> 3) & 0xFFFFU);
    if (patch_texture_source)
        words[4] = texture_source;
    memcpy(out_command, words, sizeof(words));
    return true;
}

/* Compatibility wrapper for statically sourced texture materials. */
static inline bool sm64_saturn_terrain_template_patch_resolved_record(
    void *out_command, const sm64_saturn_terrain_resolved_command_t *resolved,
    const int16_t vertices[4][2], uint16_t link, bool end_state,
    bool patch_gouraud, uintptr_t gouraud_address)
{
    if (resolved == NULL)
        return false;
    sm64_saturn_terrain_resolved_command_t compatible = *resolved;
    if (compatible.patch_mask == 0U)
        compatible.patch_mask = SM64_SATURN_TERRAIN_PATCH_END |
            SM64_SATURN_TERRAIN_PATCH_LINK |
            SM64_SATURN_TERRAIN_PATCH_XY |
            (patch_gouraud ? SM64_SATURN_TERRAIN_PATCH_GOURAUD : 0U);
    return sm64_saturn_terrain_template_patch_resolved_record_ex(
        out_command, &compatible, vertices, link, end_state, patch_gouraud,
        gouraud_address, false, 0U);
}

static inline uint32_t sm64_saturn_terrain_compact_cache_bytes(
    uint32_t primitive_count)
{
    if (primitive_count >
        UINT32_MAX / SM64_SATURN_TERRAIN_COMPACT_ENTRY_BYTES ||
        primitive_count > UINT32_MAX - 7U)
        return UINT32_MAX;
    const uint32_t material_bytes =
        primitive_count * SM64_SATURN_TERRAIN_COMPACT_ENTRY_BYTES;
    const uint32_t validity_bytes = (primitive_count + 7U) / 8U;
    return material_bytes > UINT32_MAX - validity_bytes
        ? UINT32_MAX : material_bytes + validity_bytes;
}

static inline bool sm64_saturn_terrain_compact_cache_fits(
    uint32_t primitive_count, uint32_t byte_budget)
{
    const uint32_t bytes =
        sm64_saturn_terrain_compact_cache_bytes(primitive_count);
    if (bytes == UINT32_MAX)
        return false;
    return bytes <= byte_budget;
}

#endif
