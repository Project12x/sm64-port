#include "saturn_ir_texture.h"
#include "saturn_texture_residency.h"

void sm64_saturn_texture_residency_init(
    sm64_saturn_texture_residency_t *residency,
    const vdp1_vram_partitions_t *partitions)
{
    if (residency == NULL || partitions == NULL) return;
    residency->base = (uint8_t *)partitions->texture_base;
    residency->capacity = partitions->texture_size;
    residency->used = 0;
    residency->peak = 0;
    residency->overflowed = false;
}

bool sm64_saturn_texture_residency_upload(
    sm64_saturn_texture_residency_t *residency, size_t offset,
    const void *source, size_t bytes)
{
    if (residency == NULL || source == NULL || offset > residency->capacity ||
        bytes > residency->capacity - offset) {
        if (residency != NULL) residency->overflowed = true;
        return false;
    }

    scu_dma_transfer(0, residency->base + offset, source, bytes);
    scu_dma_transfer_wait(0);
    const size_t end = offset + bytes;
    if (end > residency->used) residency->used = end;
    if (residency->used > residency->peak) residency->peak = residency->used;
    return true;
}

static bool texture_binding_valid(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint8_t width,
    uint8_t height,
    const int16_vec2_t vertices[4])
{
    if (cmdt == NULL || partitions == NULL || vertices == NULL ||
        width == 0 || height == 0 ||
        texture_offset >= partitions->texture_size) {
        return false;
    }
    return true;
}

bool sm64_saturn_ir_texture_bind_clut16(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint8_t width,
    uint8_t height,
    uint16_t clut_index,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4])
{
    if (!texture_binding_valid(cmdt, partitions, texture_offset, width,
                               height, vertices) ||
        ((size_t)clut_index + 1U) * sizeof(vdp1_clut_t) >
            partitions->clut_size) {
        return false;
    }
    vdp1_cmdt_distorted_sprite_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
        .color_mode = VDP1_CMDT_CM_CLUT_16,
        .cc_mode = cc_mode,
        .end_code_disable = true
    });
    vdp1_cmdt_char_base_set(cmdt,
        (vdp1_vram_t)partitions->texture_base + texture_offset);
    vdp1_cmdt_char_size_set(cmdt, width, height);
    vdp1_cmdt_color_mode1_set(cmdt,
        (vdp1_vram_t)&partitions->clut_base[clut_index]);
    vdp1_cmdt_vtx_set(cmdt, vertices);
    return true;
}

bool sm64_saturn_ir_texture_bind_rgb1555(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint8_t width,
    uint8_t height,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4])
{
    if (!texture_binding_valid(cmdt, partitions, texture_offset, width,
                               height, vertices)) {
        return false;
    }
    vdp1_cmdt_distorted_sprite_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
        .color_mode = VDP1_CMDT_CM_RGB_32768,
        .cc_mode = cc_mode,
        .end_code_disable = true
    });
    vdp1_cmdt_char_base_set(cmdt,
        (vdp1_vram_t)partitions->texture_base + texture_offset);
    vdp1_cmdt_char_size_set(cmdt, width, height);
    vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
    vdp1_cmdt_vtx_set(cmdt, vertices);
    return true;
}
