#ifndef SM64_SATURN_IR_TEXTURE_H
#define SM64_SATURN_IR_TEXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yaul.h>

/* Shared IR material lowering. The caller owns command allocation and the
 * residency policy; this module owns the exact VDP1 texture binding contract.
 * All offsets are relative to partitions->texture_base and are bounds-checked
 * by the residency uploader before a command is emitted. */
bool sm64_saturn_ir_texture_bind_clut16(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint16_t width,
    uint8_t height,
    uint16_t clut_index,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4]);

bool sm64_saturn_ir_texture_bind_rgb1555(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint16_t width,
    uint8_t height,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4]);

#endif
