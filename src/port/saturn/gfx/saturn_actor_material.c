#include "saturn_actor_material.h"

#include <limits.h>
#include <stddef.h>

typedef enum sm64_saturn_actor_binding_kind {
    SM64_SATURN_ACTOR_BINDING_FLAT,
    SM64_SATURN_ACTOR_BINDING_CLUT16,
    SM64_SATURN_ACTOR_BINDING_RGB1555
} sm64_saturn_actor_binding_kind_t;

static bool actor_add_u32(uint32_t left, uint32_t right, uint32_t *out)
{
    if (out == NULL || left > UINT32_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool actor_multiply_u32(uint32_t left, uint32_t right, uint32_t *out)
{
    if (out == NULL || (left != 0U && right > UINT32_MAX / left)) return false;
    *out = left * right;
    return true;
}

static bool actor_material_translate(
    const sm64_saturn_actor_target_material_t *material,
    sm64_saturn_actor_binding_kind_t *kind, vdp1_cmdt_cc_t *cc_mode)
{
    bool opaque_or_cutout;
    if (material == NULL || kind == NULL || cc_mode == NULL ||
        material->selector_kind != 0U || material->flags != 0U ||
        material->reserved != 0U)
        return false;
    opaque_or_cutout =
        (material->layer == SM64_SATURN_ACTOR_LAYER_OPAQUE &&
         material->alpha_mode == SM64_SATURN_ACTOR_ALPHA_OPAQUE) ||
        (material->layer == SM64_SATURN_ACTOR_LAYER_CUTOUT &&
         material->alpha_mode ==
             SM64_SATURN_ACTOR_ALPHA_BINARY_ZERO_TRANSPARENT);
    switch (material->recipe) {
    case SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD:
        if (material->layer != SM64_SATURN_ACTOR_LAYER_OPAQUE ||
            material->alpha_mode != SM64_SATURN_ACTOR_ALPHA_OPAQUE)
            return false;
        *kind = SM64_SATURN_ACTOR_BINDING_FLAT;
        *cc_mode = VDP1_CMDT_CC_GOURAUD;
        return true;
    case SM64_SATURN_ACTOR_RECIPE_CLUT16_REPLACE:
        if (!opaque_or_cutout) return false;
        *kind = SM64_SATURN_ACTOR_BINDING_CLUT16;
        *cc_mode = VDP1_CMDT_CC_REPLACE;
        return true;
    case SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD:
        if (!opaque_or_cutout) return false;
        *kind = SM64_SATURN_ACTOR_BINDING_CLUT16;
        *cc_mode = VDP1_CMDT_CC_GOURAUD;
        return true;
    case SM64_SATURN_ACTOR_RECIPE_RGB1555_REPLACE:
        if (!opaque_or_cutout) return false;
        *kind = SM64_SATURN_ACTOR_BINDING_RGB1555;
        *cc_mode = VDP1_CMDT_CC_REPLACE;
        return true;
    case SM64_SATURN_ACTOR_RECIPE_RGB1555_GOURAUD:
        if (!opaque_or_cutout) return false;
        *kind = SM64_SATURN_ACTOR_BINDING_RGB1555;
        *cc_mode = VDP1_CMDT_CC_GOURAUD;
        return true;
    case SM64_SATURN_ACTOR_RECIPE_CLUT16_HALF_TRANSPARENT:
        if (material->layer != SM64_SATURN_ACTOR_LAYER_TRANSLUCENT ||
            material->alpha_mode != SM64_SATURN_ACTOR_ALPHA_HALF_TRANSPARENT)
            return false;
        *kind = SM64_SATURN_ACTOR_BINDING_CLUT16;
        *cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT;
        return true;
    case SM64_SATURN_ACTOR_RECIPE_RGB1555_HALF_TRANSPARENT:
        if (material->layer != SM64_SATURN_ACTOR_LAYER_TRANSLUCENT ||
            material->alpha_mode != SM64_SATURN_ACTOR_ALPHA_HALF_TRANSPARENT)
            return false;
        *kind = SM64_SATURN_ACTOR_BINDING_RGB1555;
        *cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT;
        return true;
    default:
        return false;
    }
}

static bool actor_mapping_valid(
    const vdp1_vram_partitions_t *partitions,
    const sm64_saturn_actor_bank_view_t *bank,
    const sm64_saturn_actor_texture_mapping_t *mapping,
    uint32_t active_generation)
{
    uint32_t texture_end, clut_start, clut_end;
    uintptr_t texture_base, clut_base;
    if (partitions == NULL || bank == NULL || mapping == NULL ||
        bank->bytes == NULL ||
        bank->bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2 ||
        bank->bank.source_hash_words[0] == 0U || mapping->bank_id == 0U ||
        mapping->bank_id != bank->bank.source_hash_words[0] ||
        active_generation == 0U || mapping->generation != active_generation ||
        mapping->tile_count != bank->tile_count ||
        bank->texture_resident_bytes != bank->texture_payload_size ||
        bank->clut_resident_bytes != bank->clut_payload_size ||
        (mapping->texture_base_offset & 7U) != 0U ||
        (bank->clut_payload_size % sizeof(vdp1_clut_t)) != 0U ||
        !actor_add_u32(mapping->texture_base_offset,
                       bank->texture_resident_bytes, &texture_end) ||
        texture_end > partitions->texture_size ||
        !actor_multiply_u32(mapping->clut_base_index,
                            (uint32_t)sizeof(vdp1_clut_t), &clut_start) ||
        !actor_add_u32(clut_start, bank->clut_resident_bytes, &clut_end) ||
        clut_end > partitions->clut_size)
        return false;
    if ((bank->texture_resident_bytes != 0U &&
         partitions->texture_base == NULL) ||
        (bank->clut_resident_bytes != 0U && partitions->clut_base == NULL))
        return false;
    texture_base = (uintptr_t)partitions->texture_base;
    clut_base = (uintptr_t)partitions->clut_base;
    if ((bank->texture_resident_bytes != 0U &&
         ((texture_base & 7U) != 0U ||
          texture_base > UINTPTR_MAX - mapping->texture_base_offset)) ||
        (bank->clut_resident_bytes != 0U &&
         ((clut_base & 7U) != 0U || clut_base > UINTPTR_MAX - clut_start)))
        return false;
    return true;
}

static bool actor_tile_valid(
    const sm64_saturn_actor_bank_view_t *bank,
    const sm64_saturn_actor_texture_tile_t *tile,
    sm64_saturn_actor_binding_kind_t kind)
{
    uint32_t pixels, expected_size, tile_end;
    if (bank == NULL || tile == NULL || tile->flags != 0U ||
        tile->width < 8U || tile->width > 504U ||
        (tile->width & 7U) != 0U || tile->height == 0U ||
        tile->height > UINT8_MAX ||
        !actor_multiply_u32(tile->width, tile->height, &pixels))
        return false;
    if (kind == SM64_SATURN_ACTOR_BINDING_CLUT16) {
        if (tile->format != SM64_SATURN_ACTOR_TILE_FORMAT_CLUT16 ||
            tile->clut_id == UINT16_MAX)
            return false;
        expected_size = pixels / 2U;
    } else if (kind == SM64_SATURN_ACTOR_BINDING_RGB1555) {
        if (tile->format != SM64_SATURN_ACTOR_TILE_FORMAT_RGB1555 ||
            tile->clut_id != UINT16_MAX ||
            !actor_multiply_u32(pixels, 2U, &expected_size))
            return false;
    } else {
        return false;
    }
    return tile->payload_size == expected_size &&
           actor_add_u32(tile->payload_offset, tile->payload_size, &tile_end) &&
           tile_end <= bank->texture_payload_size;
}

bool sm64_saturn_actor_material_bind(
    vdp1_cmdt_t *cmdt, const vdp1_vram_partitions_t *partitions,
    const sm64_saturn_actor_bank_view_t *bank, uint16_t primitive_id,
    const sm64_saturn_actor_texture_mapping_t *mapping,
    uint32_t active_generation,
    const int16_vec2_t vertices[4])
{
    sm64_saturn_actor_render_binding_t binding;
    sm64_saturn_actor_target_material_t material;
    sm64_saturn_actor_texture_tile_t tile;
    sm64_saturn_actor_binding_kind_t kind;
    vdp1_cmdt_cc_t cc_mode;
    uint32_t texture_offset, clut_index;
    if (cmdt == NULL || vertices == NULL ||
        !actor_mapping_valid(partitions, bank, mapping, active_generation) ||
        primitive_id >= bank->bank.primitive_count ||
        !sm64_saturn_actor_bank_render_binding(bank, primitive_id, &binding) ||
        binding.flags != 0U || binding.reserved != 0U ||
        binding.material_id >= bank->material_count ||
        !sm64_saturn_actor_bank_target_material(
            bank, binding.material_id, &material) ||
        !actor_material_translate(&material, &kind, &cc_mode))
        return false;

    if (kind == SM64_SATURN_ACTOR_BINDING_FLAT) {
        if (binding.tile_id != UINT16_MAX) return false;
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_GOURAUD
        });
        vdp1_cmdt_vtx_set(cmdt, vertices);
        return true;
    }

    if (binding.tile_id >= mapping->tile_count ||
        !sm64_saturn_actor_bank_texture_tile(bank, binding.tile_id, &tile) ||
        !actor_tile_valid(bank, &tile, kind) ||
        !actor_add_u32(mapping->texture_base_offset, tile.payload_offset,
                       &texture_offset))
        return false;
    if (kind == SM64_SATURN_ACTOR_BINDING_CLUT16) {
        if (!actor_add_u32(mapping->clut_base_index, tile.clut_id, &clut_index) ||
            clut_index > UINT16_MAX)
            return false;
        return sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, texture_offset, tile.width, (uint8_t)tile.height,
            (uint16_t)clut_index, cc_mode, vertices);
    }
    return sm64_saturn_ir_texture_bind_rgb1555(
        cmdt, partitions, texture_offset, tile.width, (uint8_t)tile.height,
        cc_mode, vertices);
}
