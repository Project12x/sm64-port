#include "saturn_ir_texture.h"
#include "saturn_texture_residency.h"

void sm64_saturn_texture_residency_init(
    sm64_saturn_texture_residency_t *residency,
    const vdp1_vram_partitions_t *partitions)
{
    if (residency == NULL || partitions == NULL) return;
    sm64_saturn_texture_residency_init_region(
        residency, partitions->texture_base, partitions->texture_size);
}

void sm64_saturn_texture_residency_init_region(
    sm64_saturn_texture_residency_t *residency,
    void *base, size_t capacity)
{
    if (residency == NULL) return;
    residency->base = (uint8_t *)base;
    residency->capacity = capacity;
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

static bool vram_span_address(uintptr_t base, size_t offset, size_t bytes,
                              uintptr_t *address)
{
    uintptr_t start;
    if (address == NULL || offset > UINTPTR_MAX - base) return false;
    start = base + (uintptr_t)offset;
    if (bytes != 0U && bytes - 1U > UINTPTR_MAX - start) return false;
    *address = start;
    return true;
}

static bool texture_binding_valid(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint16_t width,
    uint8_t height,
    size_t texture_bytes,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4],
    uintptr_t *texture_address)
{
    uintptr_t texture_base;
    if (cmdt == NULL || partitions == NULL || vertices == NULL ||
        partitions->texture_base == NULL || width < 8U || width > 504U ||
        (width & 7U) != 0U || height == 0U || (texture_offset & 7U) != 0U ||
        texture_offset > partitions->texture_size ||
        texture_bytes > (size_t)partitions->texture_size - texture_offset ||
        (cc_mode != VDP1_CMDT_CC_REPLACE &&
         cc_mode != VDP1_CMDT_CC_GOURAUD &&
         cc_mode != VDP1_CMDT_CC_HALF_TRANSPARENT)) {
        return false;
    }
    texture_base = (uintptr_t)partitions->texture_base;
    if ((texture_base & 7U) != 0U ||
        !vram_span_address(texture_base, texture_offset, texture_bytes,
                           texture_address))
        return false;
    return true;
}

bool sm64_saturn_ir_texture_bind_clut16(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint16_t width,
    uint8_t height,
    uint16_t clut_index,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4])
{
    size_t texture_bytes;
    uintptr_t texture_address, clut_address;
    uint16_t cmd_size;
    if (width < 8U || width > 504U || (width & 7U) != 0U || height == 0U)
        return false;
    texture_bytes = ((size_t)width * (size_t)height) / 2U;
    cmd_size = (uint16_t)(((width / 8U) << 8) | height);
    if (cmd_size > 0x3FFFU ||
        !texture_binding_valid(cmdt, partitions, texture_offset, width,
                               height, texture_bytes, cc_mode, vertices,
                               &texture_address) ||
        partitions->clut_base == NULL ||
        (size_t)clut_index >=
            (size_t)partitions->clut_size / sizeof(vdp1_clut_t)) {
        return false;
    }
    if (((uintptr_t)partitions->clut_base & 7U) != 0U ||
        !vram_span_address((uintptr_t)partitions->clut_base,
                           (size_t)clut_index * sizeof(vdp1_clut_t),
                           sizeof(vdp1_clut_t), &clut_address))
        return false;
    vdp1_cmdt_distorted_sprite_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
        .color_mode = VDP1_CMDT_CM_CLUT_16,
        .cc_mode = cc_mode,
        .end_code_disable = true
    });
    vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)texture_address);
    vdp1_cmdt_char_size_set(cmdt, width, height);
    vdp1_cmdt_color_mode1_set(cmdt, (vdp1_vram_t)clut_address);
    vdp1_cmdt_vtx_set(cmdt, vertices);
    return true;
}

bool sm64_saturn_ir_texture_bind_rgb1555(
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    size_t texture_offset,
    uint16_t width,
    uint8_t height,
    vdp1_cmdt_cc_t cc_mode,
    const int16_vec2_t vertices[4])
{
    size_t pixels, texture_bytes;
    uintptr_t texture_address;
    uint16_t cmd_size;
    if (width < 8U || width > 504U || (width & 7U) != 0U || height == 0U)
        return false;
    pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / 2U) return false;
    texture_bytes = pixels * 2U;
    cmd_size = (uint16_t)(((width / 8U) << 8) | height);
    if (cmd_size > 0x3FFFU ||
        !texture_binding_valid(cmdt, partitions, texture_offset, width,
                               height, texture_bytes, cc_mode, vertices,
                               &texture_address)) {
        return false;
    }
    vdp1_cmdt_distorted_sprite_set(cmdt);
    vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
        .color_mode = VDP1_CMDT_CM_RGB_32768,
        .cc_mode = cc_mode,
        .end_code_disable = true
    });
    vdp1_cmdt_char_base_set(cmdt, (vdp1_vram_t)texture_address);
    vdp1_cmdt_char_size_set(cmdt, width, height);
    vdp1_cmdt_color_set(cmdt, RGB1555(1, 31, 31, 31));
    vdp1_cmdt_vtx_set(cmdt, vertices);
    return true;
}
