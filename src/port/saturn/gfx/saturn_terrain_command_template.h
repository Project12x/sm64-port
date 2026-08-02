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

typedef struct sm64_saturn_terrain_command_template {
    uint16_t flags;
    uint16_t color;
    uint16_t texture_slot;
    uint16_t shade_path;
} sm64_saturn_terrain_command_template_t;

/* The resolved VDP1 material state is deliberately compact: LINK,
 * vertices, END, and GRDA remain dynamic. CTRL must be retained too: a
 * textured primitive is a distorted sprite, while flat/Gouraud paths are
 * polygons. Keeping only these five immutable words cuts each cached image
 * from 32 to 10 bytes without changing the command submitted to VDP1. */
typedef struct sm64_saturn_terrain_resolved_command {
    uint16_t control;
    uint16_t pmod;
    uint16_t colr;
    uint16_t srca;
    uint16_t size;
} sm64_saturn_terrain_resolved_command_t;

#define SM64_SATURN_VDP1_COMMAND_BYTES 32U
#define SM64_SATURN_TERRAIN_COMPACT_ENTRY_BYTES 10U

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

static inline bool sm64_saturn_terrain_template_patch_resolved(
    void *out_command, uint16_t control, uint16_t pmod, uint16_t colr,
    uint16_t srca, uint16_t size, const int16_t vertices[4][2], uint16_t link,
    bool end_state, bool patch_gouraud, uintptr_t gouraud_address)
{
    if (out_command == NULL || vertices == NULL)
        return false;

    uint8_t *out = out_command;
    memset(out, 0, SM64_SATURN_VDP1_COMMAND_BYTES);
    control = (uint16_t)((control & 0x7FFFU) |
        (end_state ? 0x8000U : 0U));
    memcpy(out, &control, sizeof(control));
    memcpy(out + 2U, &link, sizeof(link));
    memcpy(out + 4U, &pmod, sizeof(pmod));
    memcpy(out + 6U, &colr, sizeof(colr));
    memcpy(out + 8U, &srca, sizeof(srca));
    memcpy(out + 10U, &size, sizeof(size));
    memcpy(out + 12U, vertices, 8U * sizeof(int16_t));
    if (patch_gouraud) {
        const uint16_t encoded = (uint16_t)((gouraud_address >> 3) & 0xFFFFU);
        memcpy(out + 28U, &encoded, sizeof(encoded));
    }
    return true;
}

static inline bool sm64_saturn_terrain_template_patch_resolved_record(
    void *out_command, const sm64_saturn_terrain_resolved_command_t *resolved,
    const int16_t vertices[4][2], uint16_t link, bool end_state,
    bool patch_gouraud, uintptr_t gouraud_address)
{
    if (resolved == NULL)
        return false;
    return sm64_saturn_terrain_template_patch_resolved(
        out_command, resolved->control, resolved->pmod, resolved->colr,
        resolved->srca, resolved->size, vertices, link, end_state,
        patch_gouraud, gouraud_address);
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
