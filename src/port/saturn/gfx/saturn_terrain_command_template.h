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

#define SM64_SATURN_VDP1_COMMAND_BYTES 32U

bool sm64_saturn_terrain_template_build(
    sm64_saturn_terrain_command_template_t *out,
    const sm64_saturn_terrain_primitive_t *primitive);

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

#endif
