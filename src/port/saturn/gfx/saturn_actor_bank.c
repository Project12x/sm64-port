#include "saturn_actor_bank.h"
#include "../runtime/saturn_sha256.h"

#include <limits.h>
#include <string.h>

#define GEOMETRY_HEADER_SIZE 46U
#define JOINT_RECORD_SIZE 12U
#define PART_RECORD_SIZE 4U
#define MATERIAL_RECORD_SIZE 4U
#define MESHLET_RECORD_SIZE 66U
#define PRIMITIVE_RECORD_SIZE 10U

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int16_t read_be_s16(const uint8_t *data)
{
    return (int16_t)read_be16(data);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool span(uint32_t offset, uint32_t size, size_t byte_count)
{
    return offset <= byte_count && size <= byte_count - offset;
}

static bool add_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (out == NULL || b > UINT32_MAX - a) return false;
    *out = a + b;
    return true;
}

static bool multiply_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (out == NULL || (a != 0U && b > UINT32_MAX / a)) return false;
    *out = a * b;
    return true;
}

static bool align_u32(uint32_t value, uint32_t alignment, uint32_t *out)
{
    uint32_t biased;
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U ||
        !add_u32(value, alignment - 1U, &biased) || out == NULL)
        return false;
    *out = biased & ~(alignment - 1U);
    return true;
}

static bool zero_span(const uint8_t *bytes, uint32_t begin, uint32_t end)
{
    uint32_t index;
    if (bytes == NULL || begin > end) return false;
    for (index = begin; index < end; index++)
        if (bytes[index] != 0U) return false;
    return true;
}

static bool v2_material_valid(uint16_t recipe, uint8_t layer, uint8_t alpha,
                              uint8_t selector, uint8_t flags,
                              uint16_t reserved)
{
    if (selector != 0U || flags != 0U || reserved != 0U) return false;
    switch (recipe) {
    case SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD:
        return layer == SM64_SATURN_ACTOR_LAYER_OPAQUE &&
               alpha == SM64_SATURN_ACTOR_ALPHA_OPAQUE;
    case SM64_SATURN_ACTOR_RECIPE_CLUT16_REPLACE:
    case SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD:
    case SM64_SATURN_ACTOR_RECIPE_RGB1555_REPLACE:
    case SM64_SATURN_ACTOR_RECIPE_RGB1555_GOURAUD:
        return (layer == SM64_SATURN_ACTOR_LAYER_OPAQUE &&
                alpha == SM64_SATURN_ACTOR_ALPHA_OPAQUE) ||
               (layer == SM64_SATURN_ACTOR_LAYER_CUTOUT &&
                alpha == SM64_SATURN_ACTOR_ALPHA_BINARY_ZERO_TRANSPARENT);
    case SM64_SATURN_ACTOR_RECIPE_CLUT16_HALF_TRANSPARENT:
    case SM64_SATURN_ACTOR_RECIPE_RGB1555_HALF_TRANSPARENT:
        return layer == SM64_SATURN_ACTOR_LAYER_TRANSLUCENT &&
               alpha == SM64_SATURN_ACTOR_ALPHA_HALF_TRANSPARENT;
    default:
        return false;
    }
}

static bool workspace_add(uint32_t *cursor, uint32_t size)
{
    if (cursor == NULL || size > UINT32_MAX - *cursor) return false;
    *cursor += size;
    return true;
}

static bool workspace_align(uint32_t *cursor, uint32_t alignment)
{
    if (cursor == NULL || alignment == 0U ||
        *cursor > UINT32_MAX - (alignment - 1U))
        return false;
    *cursor = (*cursor + alignment - 1U) & ~(alignment - 1U);
    return true;
}

bool sm64_saturn_actor_bank_workspace_requirements(
    uint16_t vertex_count, uint16_t joint_count, uint32_t *lane_bytes,
    uint32_t *usable_bytes, uint32_t *reserved_bytes)
{
    uint32_t cursor = 0U;
    if (vertex_count == 0U || joint_count == 0U || lane_bytes == NULL ||
        usable_bytes == NULL || reserved_bytes == NULL ||
        !workspace_align(&cursor, _Alignof(int16_t)) ||
        !workspace_add(&cursor, (uint32_t)vertex_count * 3U * sizeof(int16_t)) ||
        !workspace_add(&cursor, (uint32_t)vertex_count * sizeof(uint8_t)) ||
        !workspace_align(&cursor, _Alignof(int32_t)) ||
        !workspace_add(&cursor,
                       (uint32_t)joint_count * 16U * sizeof(int32_t)) ||
        !workspace_align(&cursor, _Alignof(uint16_t)) ||
        !workspace_add(&cursor, (uint32_t)vertex_count * sizeof(uint16_t)) ||
        !workspace_add(&cursor, (uint32_t)vertex_count * sizeof(uint8_t)) ||
        !workspace_align(&cursor, SM64_SATURN_ACTOR_BANK_WORK_ALIGNMENT) ||
        cursor > UINT32_MAX / SM64_SATURN_ACTOR_BANK_WORK_LANE_COUNT ||
        cursor * SM64_SATURN_ACTOR_BANK_WORK_LANE_COUNT >
            UINT32_MAX - (SM64_SATURN_ACTOR_BANK_WORK_ALIGNMENT - 1U))
        return false;
    *lane_bytes = cursor;
    *usable_bytes = cursor * SM64_SATURN_ACTOR_BANK_WORK_LANE_COUNT;
    *reserved_bytes = *usable_bytes +
        (SM64_SATURN_ACTOR_BANK_WORK_ALIGNMENT - 1U);
    return true;
}

static bool validate_meshlet_tier(const uint8_t *geometry,
                                  uint32_t primitive_offset,
                                  uint32_t primitive_ref_offset,
                                  uint32_t vertex_ref_offset,
                                  uint32_t tier_primitive_offset,
                                  uint32_t tier_primitive_count,
                                  uint32_t tier_vertex_offset,
                                  uint32_t tier_vertex_count,
                                  uint16_t meshlet_material)
{
    uint32_t emitted_vertices = 0U;
    for (uint32_t local_primitive = 0U;
         local_primitive < tier_primitive_count; local_primitive++) {
        uint16_t primitive = read_be16(
            geometry + primitive_ref_offset +
            (tier_primitive_offset + local_primitive) * 2U);
        const uint8_t *primitive_record =
            geometry + primitive_offset + (uint32_t)primitive * PRIMITIVE_RECORD_SIZE;
        if (read_be16(primitive_record) != meshlet_material)
            return false;
        for (uint32_t corner = 0U; corner < 4U; corner++) {
            uint16_t vertex = read_be16(primitive_record + 2U + corner * 2U);
            bool seen = false;
            for (uint32_t prior_primitive = 0U;
                 prior_primitive <= local_primitive && !seen; prior_primitive++) {
                uint16_t prior = read_be16(
                    geometry + primitive_ref_offset +
                    (tier_primitive_offset + prior_primitive) * 2U);
                const uint8_t *prior_record =
                    geometry + primitive_offset +
                    (uint32_t)prior * PRIMITIVE_RECORD_SIZE;
                uint32_t prior_corner_limit =
                    prior_primitive == local_primitive ? corner : 4U;
                for (uint32_t prior_corner = 0U;
                     prior_corner < prior_corner_limit; prior_corner++) {
                    if (read_be16(prior_record + 2U + prior_corner * 2U) == vertex) {
                        seen = true;
                        break;
                    }
                }
            }
            if (!seen) {
                if (emitted_vertices >= tier_vertex_count ||
                    read_be16(geometry + vertex_ref_offset +
                              (tier_vertex_offset + emitted_vertices) * 2U) != vertex)
                    return false;
                emitted_vertices++;
            }
        }
    }
    return emitted_vertices == tier_vertex_count;
}

bool sm64_saturn_actor_bank_animation(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    sm64_saturn_actor_animation_record_t *record)
{
    const uint8_t *source;
    if (view == NULL || record == NULL || animation_id >= view->bank.animation_count ||
        view->records_offset > view->byte_count ||
        (uint32_t)animation_id * SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE >
            view->byte_count - view->records_offset ||
        SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE >
            view->byte_count - view->records_offset -
            (uint32_t)animation_id * SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE)
        return false;
    source = view->bytes + view->records_offset +
             (uint32_t)animation_id * SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE;
    record->values_offset = read_be32(source + 0U);
    record->indices_offset = read_be32(source + 4U);
    record->frame_count = read_be16(source + 8U);
    record->joint_count = read_be16(source + 10U);
    record->flags = read_be16(source + 12U);
    record->y_translation_divisor = read_be_s16(source + 14U);
    return true;
}

bool sm64_saturn_actor_bank_render_binding(
    const sm64_saturn_actor_bank_view_t *view, uint16_t primitive,
    sm64_saturn_actor_render_binding_t *out)
{
    const uint8_t *record;
    uint32_t relative, absolute;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (view == NULL || out == NULL || view->bytes == NULL ||
        view->bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2 ||
        primitive >= view->bank.primitive_count ||
        !multiply_u32(primitive, SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE,
                      &relative) ||
        relative > view->render_bindings_size ||
        SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE >
            view->render_bindings_size - relative ||
        !add_u32(view->render_bindings_offset, relative, &absolute) ||
        !span(absolute,
              SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE, view->byte_count))
        return false;
    record = view->bytes + absolute;
    out->material_id = read_be16(record);
    out->tile_id = read_be16(record + 2U);
    out->flags = read_be16(record + 4U);
    out->reserved = read_be16(record + 6U);
    return true;
}

bool sm64_saturn_actor_bank_target_material(
    const sm64_saturn_actor_bank_view_t *view, uint16_t material,
    sm64_saturn_actor_target_material_t *out)
{
    const uint8_t *record;
    uint32_t relative, absolute;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (view == NULL || out == NULL || view->bytes == NULL ||
        view->bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2 ||
        material >= view->material_count ||
        !multiply_u32(material, SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE,
                      &relative) ||
        relative > view->target_materials_size ||
        SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE >
            view->target_materials_size - relative ||
        !add_u32(view->target_materials_offset, relative, &absolute) ||
        !span(absolute,
              SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE, view->byte_count))
        return false;
    record = view->bytes + absolute;
    out->recipe = read_be16(record);
    out->layer = record[2];
    out->alpha_mode = record[3];
    out->selector_kind = record[4];
    out->flags = record[5];
    out->reserved = read_be16(record + 6U);
    return true;
}

bool sm64_saturn_actor_bank_texture_tile(
    const sm64_saturn_actor_bank_view_t *view, uint16_t tile,
    sm64_saturn_actor_texture_tile_t *out)
{
    const uint8_t *record;
    uint32_t relative, absolute;
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (view == NULL || out == NULL || view->bytes == NULL ||
        view->bank.version != SM64_SATURN_ACTOR_BANK_VERSION_V2 ||
        tile >= view->tile_count ||
        !multiply_u32(tile, SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE,
                      &relative) ||
        relative > view->texture_tiles_size ||
        SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE >
            view->texture_tiles_size - relative ||
        !add_u32(view->texture_tiles_offset, relative, &absolute) ||
        !span(absolute,
              SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE, view->byte_count))
        return false;
    record = view->bytes + absolute;
    out->payload_offset = read_be32(record);
    out->payload_size = read_be32(record + 4U);
    out->width = read_be16(record + 8U);
    out->height = read_be16(record + 10U);
    out->clut_id = read_be16(record + 12U);
    out->format = record[14];
    out->flags = record[15];
    return true;
}

static bool validate_v2_tail(const uint8_t *bytes, uint32_t byte_count,
                             uint32_t geometry_primitive_offset,
                             sm64_saturn_actor_bank_view_t *parsed)
{
    uint32_t binding_end, material_end, tile_end, texture_end, clut_end;
    uint32_t expected, texture_cursor = 0U, texture_used_end;
    uint32_t counted_textures = 0U, counted_gouraud = 0U;
    uint16_t next_clut = 0U, next_tile = 0U;
    uint16_t index;
    if (bytes == NULL || parsed == NULL || byte_count < SM64_SATURN_ACTOR_BANK_V2_HEADER_SIZE)
        return false;
    if (read_be16(bytes + 104U) != SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE ||
        read_be16(bytes + 106U) != SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE ||
        read_be16(bytes + 108U) != SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE ||
        read_be16(bytes + 110U) != 0U || !zero_span(bytes, 180U, 192U))
        return false;
    parsed->render_bindings_offset = read_be32(bytes + 112U);
    parsed->render_bindings_size = read_be32(bytes + 116U);
    parsed->target_materials_offset = read_be32(bytes + 120U);
    parsed->target_materials_size = read_be32(bytes + 124U);
    parsed->texture_tiles_offset = read_be32(bytes + 128U);
    parsed->texture_tiles_size = read_be32(bytes + 132U);
    parsed->texture_payload_offset = read_be32(bytes + 136U);
    parsed->texture_payload_size = read_be32(bytes + 140U);
    parsed->clut_payload_offset = read_be32(bytes + 144U);
    parsed->clut_payload_size = read_be32(bytes + 148U);
    parsed->texture_resident_bytes = read_be32(bytes + 152U);
    parsed->clut_resident_bytes = read_be32(bytes + 156U);
    parsed->draw_records_per_instance = read_be32(bytes + 160U);
    parsed->texture_commands_per_instance = read_be32(bytes + 164U);
    parsed->gouraud_tables_per_instance = read_be32(bytes + 168U);
    if (read_be32(bytes + 172U) != byte_count) return false;
    parsed->bake_policy_id = read_be32(bytes + 176U);
    parsed->hot_end = parsed->texture_payload_offset;
    if (parsed->bake_policy_id == 0U || parsed->bank.meshlet_count == 0U ||
        parsed->bank.primitive_count == 0U ||
        !multiply_u32(parsed->bank.primitive_count,
                      SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE, &expected) ||
        parsed->render_bindings_size != expected ||
        !multiply_u32(parsed->material_count,
                      SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE, &expected) ||
        parsed->target_materials_size != expected ||
        (parsed->texture_tiles_size % SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE) != 0U ||
        parsed->texture_tiles_size / SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE > UINT16_MAX ||
        !add_u32(parsed->render_bindings_offset, parsed->render_bindings_size, &binding_end) ||
        parsed->target_materials_offset != binding_end ||
        !add_u32(parsed->target_materials_offset, parsed->target_materials_size, &material_end) ||
        parsed->texture_tiles_offset != material_end ||
        !add_u32(parsed->texture_tiles_offset, parsed->texture_tiles_size, &tile_end) ||
        !align_u32(tile_end, 8U, &expected) || parsed->texture_payload_offset != expected ||
        (parsed->texture_payload_offset & 7U) != 0U ||
        (parsed->texture_payload_size & 7U) != 0U ||
        !add_u32(parsed->texture_payload_offset, parsed->texture_payload_size, &texture_end) ||
        !align_u32(texture_end, 8U, &expected) || parsed->clut_payload_offset != expected ||
        (parsed->clut_payload_offset & 7U) != 0U ||
        (parsed->clut_payload_size % 32U) != 0U ||
        !add_u32(parsed->clut_payload_offset, parsed->clut_payload_size, &clut_end) ||
        clut_end != byte_count ||
        parsed->texture_resident_bytes != parsed->texture_payload_size ||
        parsed->clut_resident_bytes != parsed->clut_payload_size ||
        parsed->draw_records_per_instance != parsed->bank.primitive_count ||
        !zero_span(bytes, tile_end, parsed->texture_payload_offset))
        return false;
    parsed->tile_count = (uint16_t)(parsed->texture_tiles_size /
                                    SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE);
    for (index = 0U; index < parsed->material_count; index++) {
        sm64_saturn_actor_target_material_t material;
        if (!sm64_saturn_actor_bank_target_material(parsed, index, &material) ||
            !v2_material_valid(material.recipe, material.layer,
                               material.alpha_mode, material.selector_kind,
                               material.flags, material.reserved))
            return false;
    }
    for (index = 0U; index < parsed->tile_count; index++) {
        sm64_saturn_actor_texture_tile_t tile;
        uint32_t relative, pixels, payload_start, payload_end;
        if (!sm64_saturn_actor_bank_texture_tile(parsed, index, &tile) ||
            tile.flags != 0U || tile.width < 8U || tile.width > 504U ||
            (tile.width & 7U) != 0U || tile.height == 0U || tile.height > 255U ||
            !align_u32(texture_cursor, 8U, &relative) ||
            tile.payload_offset != relative ||
            relative > parsed->texture_payload_size ||
            !add_u32(parsed->texture_payload_offset, texture_cursor, &payload_start) ||
            !add_u32(parsed->texture_payload_offset, relative, &payload_end) ||
            !zero_span(bytes, payload_start, payload_end) ||
            !multiply_u32(tile.width, tile.height, &pixels) ||
            !span(tile.payload_offset, tile.payload_size,
                  parsed->texture_payload_size))
            return false;
        if (tile.format == SM64_SATURN_ACTOR_TILE_FORMAT_CLUT16) {
            if (tile.payload_size != pixels / 2U || tile.clut_id == UINT16_MAX ||
                tile.clut_id > next_clut)
                return false;
            if (tile.clut_id == next_clut) next_clut++;
        } else if (tile.format == SM64_SATURN_ACTOR_TILE_FORMAT_RGB1555) {
            uint32_t word_offset;
            if (!multiply_u32(pixels, 2U, &pixels) || tile.payload_size != pixels ||
                tile.clut_id != UINT16_MAX ||
                !add_u32(parsed->texture_payload_offset, tile.payload_offset,
                         &payload_start))
                return false;
            for (word_offset = 0U; word_offset < tile.payload_size; word_offset += 2U) {
                uint16_t word = read_be16(bytes + payload_start + word_offset);
                if (word != 0U && (word & 0x8000U) == 0U) return false;
            }
        } else {
            return false;
        }
        if (!add_u32(tile.payload_offset, tile.payload_size, &texture_cursor))
            return false;
    }
    if (!align_u32(texture_cursor, 8U, &expected) ||
        expected != parsed->texture_payload_size ||
        !add_u32(parsed->texture_payload_offset, texture_cursor, &texture_used_end) ||
        !zero_span(bytes, texture_used_end, texture_end) ||
        next_clut != parsed->clut_payload_size / 32U)
        return false;
    for (index = 0U; index < next_clut; index++) {
        uint32_t entry;
        const uint8_t *palette = bytes + parsed->clut_payload_offset + (uint32_t)index * 32U;
        if (read_be16(palette) != 0U) return false;
        for (entry = 1U; entry < 16U; entry++) {
            uint16_t word = read_be16(palette + entry * 2U);
            if (word != 0U && (word & 0x8000U) == 0U) return false;
        }
    }
    for (index = 0U; index < parsed->bank.primitive_count; index++) {
        sm64_saturn_actor_render_binding_t binding;
        sm64_saturn_actor_target_material_t material;
        sm64_saturn_actor_texture_tile_t tile;
        uint16_t primitive_material = read_be16(
            bytes + parsed->meshlets_offset + geometry_primitive_offset +
            (uint32_t)index * PRIMITIVE_RECORD_SIZE);
        bool wants_clut;
        if (!sm64_saturn_actor_bank_render_binding(parsed, index, &binding) ||
            binding.material_id != primitive_material ||
            binding.material_id >= parsed->material_count ||
            binding.flags != 0U || binding.reserved != 0U ||
            !sm64_saturn_actor_bank_target_material(parsed, binding.material_id,
                                                     &material))
            return false;
        if (material.recipe == SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD) {
            if (binding.tile_id != UINT16_MAX) return false;
        } else {
            if (binding.tile_id >= parsed->tile_count || binding.tile_id > next_tile ||
                !sm64_saturn_actor_bank_texture_tile(parsed, binding.tile_id, &tile))
                return false;
            if (binding.tile_id == next_tile) next_tile++;
            counted_textures++;
            wants_clut = material.recipe == SM64_SATURN_ACTOR_RECIPE_CLUT16_REPLACE ||
                         material.recipe == SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD ||
                         material.recipe == SM64_SATURN_ACTOR_RECIPE_CLUT16_HALF_TRANSPARENT;
            if ((wants_clut && tile.format != SM64_SATURN_ACTOR_TILE_FORMAT_CLUT16) ||
                (!wants_clut && tile.format != SM64_SATURN_ACTOR_TILE_FORMAT_RGB1555))
                return false;
        }
        if (material.recipe == SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD ||
            material.recipe == SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD ||
            material.recipe == SM64_SATURN_ACTOR_RECIPE_RGB1555_GOURAUD)
            counted_gouraud++;
    }
    return next_tile == parsed->tile_count &&
           counted_textures == parsed->texture_commands_per_instance &&
           counted_gouraud == parsed->gouraud_tables_per_instance;
}

bool sm64_saturn_actor_bank_validate_expected(
    const void *data, size_t byte_count, const uint32_t expected_source_hash[8],
    sm64_saturn_actor_bank_view_t *view)
{
    const uint8_t *bytes = data;
    sm64_saturn_actor_bank_view_t parsed;
    uint32_t header_size, expected_header_size, record_size;
    uint32_t records_size;
    uint16_t geometry_part_count, geometry_joint_count;
    uint16_t geometry_material_count, geometry_meshlet_count;
    uint16_t geometry_primitive_count, primitive_ref_count, vertex_ref_count;
    uint32_t geometry_joint_offset, geometry_part_offset, geometry_material_offset;
    uint32_t geometry_meshlet_offset, geometry_primitive_offset;
    uint32_t geometry_primitive_ref_offset, geometry_vertex_ref_offset;
    uint32_t primitive_cursor = 0U, vertex_cursor = 0U;
    uint32_t source_primitive_cursor = 0U;
    uint32_t lane_scratch, usable_scratch, minimum_scratch;
    uint32_t records_end, indices_end, values_end, vertices_end, geometry_end;
    uint32_t aligned;
    int16_t previous_node = -1;
    bool hash_nonzero = false;
    bool is_v2;
    if (bytes == NULL || view == NULL ||
        byte_count < SM64_SATURN_ACTOR_BANK_HEADER_SIZE || byte_count > UINT32_MAX)
        return false;
    memset(&parsed, 0, sizeof(parsed));
    parsed.bytes = bytes;
    parsed.byte_count = byte_count;
    parsed.bank.magic = read_be32(bytes + 0U);
    parsed.bank.version = read_be16(bytes + 4U);
    parsed.bank.family_id = read_be16(bytes + 6U);
    parsed.bank.model_id = read_be16(bytes + 8U);
    parsed.bank.joint_count = read_be16(bytes + 10U);
    parsed.bank.animation_count = read_be16(bytes + 12U);
    parsed.bank.meshlet_count = read_be16(bytes + 14U);
    parsed.bank.primitive_count = read_be16(bytes + 16U);
    parsed.bank.vertex_count = read_be16(bytes + 18U);
    parsed.bank.max_instances = read_be16(bytes + 20U);
    parsed.bank.feature_mask = read_be32(bytes + 22U);
    for (uint16_t word = 0U; word < 8U; word++) {
        parsed.bank.source_hash_words[word] = read_be32(bytes + 26U + word * 4U);
        hash_nonzero |= parsed.bank.source_hash_words[word] != 0U;
        if (expected_source_hash != NULL &&
            parsed.bank.source_hash_words[word] != expected_source_hash[word])
            return false;
    }
    header_size = read_be16(bytes + 58U);
    record_size = read_be16(bytes + 60U);
    parsed.records_offset = read_be32(bytes + 62U);
    parsed.indices_offset = read_be32(bytes + 66U);
    parsed.indices_size = read_be32(bytes + 70U);
    parsed.values_offset = read_be32(bytes + 74U);
    parsed.values_size = read_be32(bytes + 78U);
    parsed.vertices_offset = read_be32(bytes + 82U);
    parsed.vertices_size = read_be32(bytes + 86U);
    parsed.meshlets_offset = read_be32(bytes + 90U);
    parsed.meshlets_size = read_be32(bytes + 94U);
    parsed.max_scratch = read_be32(bytes + 98U);
    is_v2 = false;
    switch (parsed.bank.version) {
    case SM64_SATURN_ACTOR_BANK_VERSION_V1:
        expected_header_size = SM64_SATURN_ACTOR_BANK_HEADER_SIZE;
        break;
    case SM64_SATURN_ACTOR_BANK_VERSION_V2:
        expected_header_size = SM64_SATURN_ACTOR_BANK_V2_HEADER_SIZE;
        is_v2 = true;
        break;
    default:
        return false;
    }
    if (parsed.bank.magic != SM64_SATURN_ACTOR_BANK_MAGIC ||
        header_size != expected_header_size ||
        (is_v2 && byte_count < SM64_SATURN_ACTOR_BANK_V2_HEADER_SIZE) ||
        record_size != SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE || !hash_nonzero ||
        bytes[102] != 0U || bytes[103] != 0U ||
        parsed.bank.family_id == 0U || parsed.bank.model_id == 0U ||
        parsed.bank.joint_count == 0U || parsed.bank.animation_count == 0U ||
        parsed.bank.vertex_count == 0U || parsed.bank.max_instances == 0U ||
        parsed.max_scratch == 0U ||
        (is_v2 && (parsed.bank.meshlet_count == 0U ||
                   parsed.bank.primitive_count == 0U)))
        return false;
    if (!sm64_saturn_actor_bank_workspace_requirements(
            parsed.bank.vertex_count, parsed.bank.joint_count, &lane_scratch,
            &usable_scratch, &minimum_scratch) ||
        (is_v2 ? parsed.max_scratch != minimum_scratch :
                 parsed.max_scratch < minimum_scratch))
        return false;
    if (!multiply_u32(parsed.bank.animation_count, record_size, &records_size) ||
        !add_u32(parsed.records_offset, records_size, &records_end) ||
        !add_u32(parsed.indices_offset, parsed.indices_size, &indices_end) ||
        !add_u32(parsed.values_offset, parsed.values_size, &values_end) ||
        !add_u32(parsed.vertices_offset, parsed.vertices_size, &vertices_end) ||
        !add_u32(parsed.meshlets_offset, parsed.meshlets_size, &geometry_end))
        return false;
    if (!span(parsed.records_offset, records_size, byte_count) ||
        !span(parsed.indices_offset, parsed.indices_size, byte_count) ||
        !span(parsed.values_offset, parsed.values_size, byte_count) ||
        !span(parsed.vertices_offset, parsed.vertices_size, byte_count) ||
        !span(parsed.meshlets_offset, parsed.meshlets_size, byte_count) ||
        parsed.vertices_size != (uint32_t)parsed.bank.vertex_count * 10U ||
        parsed.meshlets_size < GEOMETRY_HEADER_SIZE ||
        read_be32(bytes + parsed.meshlets_offset) != 0x47454F31UL)
        return false;
    if (is_v2) {
        if (parsed.records_offset != SM64_SATURN_ACTOR_BANK_V2_HEADER_SIZE ||
            parsed.indices_offset != records_end ||
            !align_u32(indices_end, 4U, &aligned) || parsed.values_offset != aligned ||
            !zero_span(bytes, indices_end, parsed.values_offset) ||
            !align_u32(values_end, 4U, &aligned) || parsed.vertices_offset != aligned ||
            !zero_span(bytes, values_end, parsed.vertices_offset) ||
            !align_u32(vertices_end, 4U, &aligned) || parsed.meshlets_offset != aligned ||
            !zero_span(bytes, vertices_end, parsed.meshlets_offset) ||
            read_be32(bytes + 112U) != geometry_end)
            return false;
    } else if (parsed.records_offset < header_size ||
               parsed.indices_offset < records_end ||
               parsed.values_offset < indices_end ||
               parsed.vertices_offset < values_end ||
               parsed.meshlets_offset < vertices_end ||
               geometry_end != byte_count) {
        return false;
    }
    geometry_part_count = read_be16(bytes + parsed.meshlets_offset + 4U);
    geometry_joint_count = read_be16(bytes + parsed.meshlets_offset + 6U);
    geometry_material_count = read_be16(bytes + parsed.meshlets_offset + 8U);
    geometry_meshlet_count = read_be16(bytes + parsed.meshlets_offset + 10U);
    geometry_primitive_count = read_be16(bytes + parsed.meshlets_offset + 12U);
    primitive_ref_count = read_be16(bytes + parsed.meshlets_offset + 14U);
    vertex_ref_count = read_be16(bytes + parsed.meshlets_offset + 16U);
    geometry_joint_offset = read_be32(bytes + parsed.meshlets_offset + 18U);
    geometry_part_offset = read_be32(bytes + parsed.meshlets_offset + 22U);
    geometry_material_offset = read_be32(bytes + parsed.meshlets_offset + 26U);
    geometry_meshlet_offset = read_be32(bytes + parsed.meshlets_offset + 30U);
    geometry_primitive_offset = read_be32(bytes + parsed.meshlets_offset + 34U);
    geometry_primitive_ref_offset = read_be32(bytes + parsed.meshlets_offset + 38U);
    geometry_vertex_ref_offset = read_be32(bytes + parsed.meshlets_offset + 42U);
    parsed.material_count = geometry_material_count;
    if (geometry_part_count == 0U || geometry_material_count == 0U ||
        geometry_joint_count != parsed.bank.joint_count ||
        geometry_meshlet_count != parsed.bank.meshlet_count ||
        geometry_primitive_count != parsed.bank.primitive_count ||
        geometry_joint_offset != GEOMETRY_HEADER_SIZE ||
        geometry_part_offset != geometry_joint_offset +
                                (uint32_t)geometry_joint_count * JOINT_RECORD_SIZE ||
        geometry_material_offset != geometry_part_offset +
                                    (uint32_t)geometry_part_count * PART_RECORD_SIZE ||
        geometry_meshlet_offset != geometry_material_offset +
                                   (uint32_t)geometry_material_count * MATERIAL_RECORD_SIZE ||
        geometry_primitive_offset != geometry_meshlet_offset +
                                     (uint32_t)geometry_meshlet_count * MESHLET_RECORD_SIZE ||
        geometry_primitive_ref_offset != geometry_primitive_offset +
                                         (uint32_t)geometry_primitive_count * PRIMITIVE_RECORD_SIZE ||
        geometry_vertex_ref_offset != geometry_primitive_ref_offset +
                                      (uint32_t)primitive_ref_count * 2U ||
        geometry_vertex_ref_offset + (uint32_t)vertex_ref_count * 2U !=
            parsed.meshlets_size)
        return false;
    for (uint16_t joint = 0U; joint < geometry_joint_count; joint++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_joint_offset +
                                (uint32_t)joint * 12U;
        int16_t parent = read_be_s16(record);
        int16_t node = read_be_s16(record + 8U);
        uint16_t branch = read_be16(record + 10U);
        if ((joint == 0U && parent != -1) ||
            (joint != 0U && (parent < 0 || parent >= (int16_t)joint)) ||
            node < 0 || (joint != 0U && node <= previous_node) ||
            (branch != 0xFFFFU && branch != (uint16_t)node))
            return false;
        previous_node = node;
    }
    for (uint16_t part = 0U; part < geometry_part_count; part++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_part_offset +
                                (uint32_t)part * PART_RECORD_SIZE;
        if (read_be16(record) >= geometry_joint_count || read_be16(record + 2U) != part)
            return false;
    }
    for (uint16_t material = 0U; material < geometry_material_count; material++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_material_offset +
                                (uint32_t)material * MATERIAL_RECORD_SIZE;
        if (record[0] > 31U || record[1] > 31U || record[2] > 31U || record[3] != 0U)
            return false;
    }
    for (uint16_t meshlet = 0U; meshlet < geometry_meshlet_count; meshlet++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_meshlet_offset +
                                (uint32_t)meshlet * MESHLET_RECORD_SIZE;
        if (read_be16(record) >= geometry_material_count ||
            read_be16(record + 2U) >= geometry_primitive_count ||
            record[4] > 1U || record[5] != 0U)
            return false;
        for (uint16_t axis = 0U; axis < 3U; axis++)
            if (read_be_s16(record + 6U + axis * 2U) >
                read_be_s16(record + 12U + axis * 2U))
                return false;
        for (uint16_t tier = 0U; tier < 3U; tier++) {
            const uint8_t *fields = record + 18U + (uint32_t)tier * 16U;
            uint32_t primitive_offset = read_be32(fields);
            uint32_t primitive_count = read_be32(fields + 4U);
            uint32_t vertex_offset = read_be32(fields + 8U);
            uint32_t vertex_count = read_be32(fields + 12U);
            if (primitive_offset != primitive_cursor || vertex_offset != vertex_cursor ||
                primitive_count > 32U ||
                primitive_count > (uint32_t)primitive_ref_count - primitive_cursor ||
                vertex_count > (uint32_t)vertex_ref_count - vertex_cursor)
                return false;
            primitive_cursor += primitive_count;
            vertex_cursor += vertex_count;
        }
    }
    if (primitive_cursor != primitive_ref_count || vertex_cursor != vertex_ref_count)
        return false;
    for (uint16_t primitive = 0U; primitive < geometry_primitive_count; primitive++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_primitive_offset +
                                (uint32_t)primitive * PRIMITIVE_RECORD_SIZE;
        uint16_t a = read_be16(record + 2U);
        uint16_t b = read_be16(record + 4U);
        uint16_t c = read_be16(record + 6U);
        uint16_t d = read_be16(record + 8U);
        if (read_be16(record) >= geometry_material_count)
            return false;
        for (uint16_t corner = 0U; corner < 4U; corner++)
            if (read_be16(record + 2U + corner * 2U) >= parsed.bank.vertex_count)
                return false;
        if (a == b || a == c || b == c ||
            (d != c && (a == d || b == d)))
            return false;
    }
    for (uint16_t reference = 0U; reference < primitive_ref_count; reference++)
        if (read_be16(bytes + parsed.meshlets_offset + geometry_primitive_ref_offset +
                      (uint32_t)reference * 2U) >= geometry_primitive_count)
            return false;
    for (uint16_t reference = 0U; reference < vertex_ref_count; reference++)
        if (read_be16(bytes + parsed.meshlets_offset + geometry_vertex_ref_offset +
                      (uint32_t)reference * 2U) >= parsed.bank.vertex_count)
            return false;
    for (uint16_t meshlet = 0U; meshlet < geometry_meshlet_count; meshlet++) {
        const uint8_t *geometry = bytes + parsed.meshlets_offset;
        const uint8_t *record = geometry + geometry_meshlet_offset +
                                (uint32_t)meshlet * MESHLET_RECORD_SIZE;
        uint16_t material = read_be16(record);
        uint16_t source_ordinal = read_be16(record + 2U);
        uint32_t primitive_offsets[3], primitive_counts[3];
        uint32_t vertex_offsets[3], vertex_counts[3];
        for (uint16_t tier = 0U; tier < 3U; tier++) {
            const uint8_t *fields = record + 18U + (uint32_t)tier * 16U;
            primitive_offsets[tier] = read_be32(fields);
            primitive_counts[tier] = read_be32(fields + 4U);
            vertex_offsets[tier] = read_be32(fields + 8U);
            vertex_counts[tier] = read_be32(fields + 12U);
        }
        if (primitive_counts[0] == 0U || primitive_counts[1] == 0U ||
            primitive_counts[0] != primitive_counts[1] ||
            vertex_counts[0] != vertex_counts[1] ||
            (uint32_t)source_ordinal != source_primitive_cursor ||
            read_be16(geometry + geometry_primitive_ref_offset +
                      primitive_offsets[0] * 2U) != source_ordinal)
            return false;
        for (uint32_t local = 0U; local < primitive_counts[0]; local++) {
            uint16_t tier0 = read_be16(
                geometry + geometry_primitive_ref_offset +
                (primitive_offsets[0] + local) * 2U);
            uint16_t tier1 = read_be16(
                geometry + geometry_primitive_ref_offset +
                (primitive_offsets[1] + local) * 2U);
            if ((uint32_t)tier0 != (uint32_t)source_ordinal + local || tier1 != tier0)
                return false;
        }
        for (uint32_t local = 0U; local < vertex_counts[0]; local++) {
            if (read_be16(geometry + geometry_vertex_ref_offset +
                          (vertex_offsets[0] + local) * 2U) !=
                read_be16(geometry + geometry_vertex_ref_offset +
                          (vertex_offsets[1] + local) * 2U))
                return false;
        }
        {
            uint32_t expected_tier2 = 0U;
            for (uint32_t local = 0U; local < primitive_counts[0]; local++) {
                uint16_t primitive = read_be16(
                    geometry + geometry_primitive_ref_offset +
                    (primitive_offsets[0] + local) * 2U);
                if (primitive % 8U == 1U) {
                    if (expected_tier2 >= primitive_counts[2] ||
                        read_be16(geometry + geometry_primitive_ref_offset +
                                  (primitive_offsets[2] + expected_tier2) * 2U) != primitive)
                        return false;
                    expected_tier2++;
                }
            }
            if (expected_tier2 != primitive_counts[2])
                return false;
        }
        for (uint16_t tier = 0U; tier < 3U; tier++) {
            if (!validate_meshlet_tier(
                    geometry, geometry_primitive_offset,
                    geometry_primitive_ref_offset, geometry_vertex_ref_offset,
                    primitive_offsets[tier], primitive_counts[tier],
                    vertex_offsets[tier], vertex_counts[tier], material))
                return false;
        }
        source_primitive_cursor += primitive_counts[0];
    }
    if (source_primitive_cursor != geometry_primitive_count)
        return false;
    for (uint16_t animation = 0U; animation < parsed.bank.animation_count; animation++) {
        sm64_saturn_actor_animation_record_t record;
        uint32_t index_words, value_words, index_bytes, value_bytes;
        if (!sm64_saturn_actor_bank_animation(&parsed, animation, &record) ||
            record.frame_count == 0U || record.joint_count != parsed.bank.joint_count ||
            parsed.indices_size < 4U || parsed.values_size < 4U ||
            record.indices_offset < parsed.indices_offset + 4U ||
            record.values_offset < parsed.values_offset + 4U ||
            record.indices_offset > indices_end ||
            record.values_offset > values_end ||
            (record.indices_offset & 1U) != 0U || (record.values_offset & 1U) != 0U)
            return false;
        index_words = read_be32(bytes + record.indices_offset - 4U);
        value_words = read_be32(bytes + record.values_offset - 4U);
        if (index_words != ((uint32_t)record.joint_count + 1U) * 6U ||
            !multiply_u32(index_words, 2U, &index_bytes) ||
            !multiply_u32(value_words, 2U, &value_bytes) ||
            !span(record.indices_offset, index_bytes, indices_end) ||
            !span(record.values_offset, value_bytes, values_end))
            return false;
        for (uint32_t channel = 0U; channel < index_words; channel += 2U) {
            uint32_t count = read_be16(bytes + record.indices_offset + channel * 2U);
            uint32_t offset = read_be16(bytes + record.indices_offset + (channel + 1U) * 2U);
            if (count == 0U || offset > value_words || count > value_words - offset)
                return false;
        }
    }
    for (uint16_t vertex = 0U; vertex < parsed.bank.vertex_count; vertex++) {
        const uint8_t *record = bytes + parsed.vertices_offset + (uint32_t)vertex * 10U;
        uint16_t joint = read_be16(record + 6U);
        uint16_t branch = read_be16(record + 8U);
        if (joint >= parsed.bank.joint_count || branch >= geometry_part_count ||
            read_be16(bytes + parsed.meshlets_offset + geometry_part_offset +
                      (uint32_t)branch * PART_RECORD_SIZE) != joint)
            return false;
    }
    if (is_v2) {
        if (!validate_v2_tail(bytes, (uint32_t)byte_count,
                              geometry_primitive_offset, &parsed))
            return false;
    } else {
        parsed.hot_end = (uint32_t)byte_count;
    }
    *view = parsed;
    return true;
}

bool sm64_saturn_actor_bank_validate(const void *data, size_t byte_count,
                                     sm64_saturn_actor_bank_view_t *view)
{
    return sm64_saturn_actor_bank_validate_expected(data, byte_count, NULL, view);
}

bool sm64_saturn_actor_bank_sample_channel(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    uint16_t frame, uint16_t channel, int16_t *sample)
{
    sm64_saturn_actor_animation_record_t record;
    uint32_t index_words, value_words, count, offset, selected;
    if (sample == NULL || view == NULL ||
        !sm64_saturn_actor_bank_animation(view, animation_id, &record) ||
        record.indices_offset < 4U || record.values_offset < 4U ||
        record.indices_offset > view->byte_count - 4U ||
        record.values_offset > view->byte_count - 4U ||
        record.indices_offset < view->indices_offset ||
        record.values_offset < view->values_offset ||
        record.indices_offset > view->indices_offset + view->indices_size ||
        record.values_offset > view->values_offset + view->values_size)
        return false;
    index_words = read_be32(view->bytes + record.indices_offset - 4U);
    value_words = read_be32(view->bytes + record.values_offset - 4U);
    if ((uint32_t)channel * 2U + 1U >= index_words ||
        index_words > (view->byte_count - record.indices_offset) / 2U ||
        value_words > (view->byte_count - record.values_offset) / 2U)
        return false;
    count = read_be16(view->bytes + record.indices_offset + (uint32_t)channel * 4U);
    offset = read_be16(view->bytes + record.indices_offset + (uint32_t)channel * 4U + 2U);
    selected = offset + (frame < count ? frame : count - 1U);
    if (count == 0U || selected >= value_words)
        return false;
    *sample = read_be_s16(view->bytes + record.values_offset + selected * 2U);
    return true;
}

bool sm64_saturn_actor_bank_joint(
    const sm64_saturn_actor_bank_view_t *view, uint16_t joint,
    sm64_saturn_actor_joint_t *out)
{
    const uint32_t offset = GEOMETRY_HEADER_SIZE + (uint32_t)joint * JOINT_RECORD_SIZE;
    const uint8_t *record;
    if (view == NULL || out == NULL || joint >= view->bank.joint_count ||
        view->meshlets_size < offset + JOINT_RECORD_SIZE)
        return false;
    record = view->bytes + view->meshlets_offset + offset;
    out->parent_ordinal = read_be_s16(record);
    out->translation[0] = read_be_s16(record + 2U);
    out->translation[1] = read_be_s16(record + 4U);
    out->translation[2] = read_be_s16(record + 6U);
    out->node_ordinal = read_be_s16(record + 8U);
    out->branch_ordinal = read_be16(record + 10U);
    return true;
}

bool sm64_saturn_actor_bank_vertex(
    const sm64_saturn_actor_bank_view_t *view, uint16_t vertex,
    sm64_saturn_actor_vertex_t *out)
{
    const uint32_t offset = view == NULL ? 0U : view->vertices_offset +
        (uint32_t)vertex * 10U;
    if (view == NULL || out == NULL || vertex >= view->bank.vertex_count ||
        offset > view->byte_count || 10U > view->byte_count - offset)
        return false;
    out->local[0] = read_be_s16(view->bytes + offset);
    out->local[1] = read_be_s16(view->bytes + offset + 2U);
    out->local[2] = read_be_s16(view->bytes + offset + 4U);
    out->joint_ordinal = read_be16(view->bytes + offset + 6U);
    out->branch_ordinal = read_be16(view->bytes + offset + 8U);
    return out->joint_ordinal < view->bank.joint_count;
}

static bool family_span(uint32_t offset, uint32_t size, uint32_t blob_size)
{
    return offset <= blob_size && size <= blob_size - offset;
}

static bool family_sha256_payload(const uint8_t *bytes, size_t byte_count,
                                  uint8_t digest[32])
{
    sm64_saturn_sha256_t context;
    static const uint8_t zero_digest[32] = {0};
    if (bytes == NULL || digest == NULL || byte_count < 56U || byte_count > UINT32_MAX)
        return false;
    sm64_saturn_sha256_init(&context);
    return sm64_saturn_sha256_update(&context, bytes, 24U) &&
           sm64_saturn_sha256_update(&context, zero_digest, sizeof(zero_digest)) &&
           sm64_saturn_sha256_update(&context, bytes + 56U,
                                     (uint32_t)byte_count - 56U) &&
           sm64_saturn_sha256_finish(&context, digest);
}

bool sm64_saturn_actor_family_bank_validate_expected(
    const void *data, size_t byte_count,
    const uint32_t expected_hash[8], sm64_saturn_actor_family_bank_view_t *view)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t count;
    uint32_t records_offset, records_size, blob_offset, blob_size;
    uint16_t index;
    uint8_t digest[32];
    bool hash_nonzero = false;
    if (bytes == NULL || view == NULL || byte_count < SM64_SATURN_ACTOR_FAMILY_BANK_HEADER_SIZE ||
        read_be32(bytes) != SM64_SATURN_ACTOR_FAMILY_BANK_MAGIC ||
        read_be16(bytes + 4U) != SM64_SATURN_ACTOR_FAMILY_BANK_VERSION)
        return false;
    count = read_be16(bytes + 6U);
    records_offset = read_be32(bytes + 8U);
    records_size = read_be32(bytes + 12U);
    blob_offset = read_be32(bytes + 16U);
    blob_size = read_be32(bytes + 20U);
    for (index = 0U; index < 8U; index++)
        hash_nonzero |= read_be32(bytes + 24U + index * 4U) != 0U;
    if (!hash_nonzero || count == 0U)
        return false;
    if (!family_sha256_payload(bytes, byte_count, digest))
        return false;
    for (index = 0U; index < 8U; index++) {
        uint32_t actual = ((uint32_t)digest[index * 4U] << 24) |
                          ((uint32_t)digest[index * 4U + 1U] << 16) |
                          ((uint32_t)digest[index * 4U + 2U] << 8) |
                          digest[index * 4U + 3U];
        if (actual != read_be32(bytes + 24U + index * 4U) ||
            (expected_hash != NULL && actual != expected_hash[index]))
            return false;
    }
    if (records_offset != SM64_SATURN_ACTOR_FAMILY_BANK_HEADER_SIZE ||
        records_size != (uint32_t)count * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE ||
        blob_offset != records_offset + records_size ||
        blob_offset > byte_count || blob_size != byte_count - blob_offset)
        return false;
    for (index = 0U; index < count; index++) {
        const uint8_t *record = bytes + records_offset +
            (uint32_t)index * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE;
        uint16_t prior;
        uint32_t family_id = read_be32(record);
        if (family_id == 0U || (read_be32(record + 4U) &
                               ~SM64_SATURN_ACTOR_CAPABILITY_MASK) != 0U ||
            (read_be32(record + 16U) &
                               ~(SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED |
                                 SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY)) != 0U ||
            (read_be32(record + 52U) &
             ~SM64_SATURN_ACTOR_RUNTIME_CAPABILITY_MASK) != 0U)
            return false;
        for (prior = 0U; prior < index; prior++) {
            const uint8_t *other = bytes + records_offset +
                (uint32_t)prior * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE;
            if (read_be32(other) == family_id)
                return false;
        }
        if (!family_span(read_be32(record + 20U), read_be32(record + 24U), blob_size) ||
            !family_span(read_be32(record + 28U), read_be32(record + 32U), blob_size) ||
            !family_span(read_be32(record + 36U), read_be32(record + 40U), blob_size) ||
            !family_span(read_be32(record + 44U), read_be32(record + 48U), blob_size))
            return false;
    }
    view->bytes = bytes;
    view->byte_count = byte_count;
    view->version = SM64_SATURN_ACTOR_FAMILY_BANK_VERSION;
    view->family_count = count;
    view->records_offset = records_offset;
    view->records_size = records_size;
    view->blob_offset = blob_offset;
    view->blob_size = blob_size;
    return true;
}

bool sm64_saturn_actor_family_bank_validate(
    const void *data, size_t byte_count,
    sm64_saturn_actor_family_bank_view_t *view)
{
    return sm64_saturn_actor_family_bank_validate_expected(data, byte_count, NULL, view);
}

bool sm64_saturn_actor_family_bank_record(
    const sm64_saturn_actor_family_bank_view_t *view, uint16_t index,
    sm64_saturn_actor_family_record_t *out)
{
    const uint8_t *record;
    if (view == NULL || out == NULL || index >= view->family_count)
        return false;
    record = view->bytes + view->records_offset +
        (uint32_t)index * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE;
    out->family_id = read_be32(record);
    out->capability_mask = read_be32(record + 4U);
    out->maximum_live_instances = read_be32(record + 8U);
    out->actor_count = read_be32(record + 12U);
    out->flags = read_be32(record + 16U);
    out->name_offset = read_be32(record + 20U);
    out->name_size = read_be32(record + 24U);
    out->source_offset = read_be32(record + 28U);
    out->source_size = read_be32(record + 32U);
    out->unsupported_offset = read_be32(record + 36U);
    out->unsupported_size = read_be32(record + 40U);
    out->metadata_offset = read_be32(record + 44U);
    out->metadata_size = read_be32(record + 48U);
    out->runtime_capability_mask = read_be32(record + 52U);
    return family_span(out->name_offset, out->name_size, view->blob_size) &&
           family_span(out->source_offset, out->source_size, view->blob_size) &&
           family_span(out->unsupported_offset, out->unsupported_size, view->blob_size) &&
           family_span(out->metadata_offset, out->metadata_size, view->blob_size);
}

bool sm64_saturn_actor_family_capability_supported(
    const sm64_saturn_actor_family_record_t *record,
    uint32_t required_runtime_capability_mask)
{
    if (record == NULL ||
        (required_runtime_capability_mask &
         ~SM64_SATURN_ACTOR_RUNTIME_CAPABILITY_MASK) != 0U)
        return false;
    return (record->runtime_capability_mask &
            required_runtime_capability_mask) == required_runtime_capability_mask;
}

bool sm64_saturn_actor_family_capability_mask_supported(
    uint32_t required_capability_mask)
{
    return (required_capability_mask &
            ~SM64_SATURN_ACTOR_CAPABILITY_ADMISSION_MASK) == 0U;
}

int sm64_saturn_actor_family_bank_select(
    const sm64_saturn_actor_family_bank_view_t *view,
    uint32_t required_capability_mask, uint32_t multiplicity)
{
    uint16_t index;
    int selected = -1;
    uint32_t selected_bits = UINT32_MAX, selected_capacity = UINT32_MAX,
             selected_id = UINT32_MAX;
    if (view == NULL || !sm64_saturn_actor_family_capability_mask_supported(
                           required_capability_mask))
        return -1;
    for (index = 0U; index < view->family_count; index++) {
        sm64_saturn_actor_family_record_t record;
        uint32_t bits;
        if (!sm64_saturn_actor_family_bank_record(view, index, &record) ||
            (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED) == 0U ||
            (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) == 0U ||
            (record.capability_mask & required_capability_mask) != required_capability_mask ||
            record.maximum_live_instances < multiplicity)
            continue;
        bits = record.capability_mask;
        bits = bits == 0U ? 0U : (uint32_t)__builtin_popcount(bits);
        if (bits < selected_bits ||
            (bits == selected_bits && record.maximum_live_instances < selected_capacity) ||
            (bits == selected_bits && record.maximum_live_instances == selected_capacity &&
             record.family_id < selected_id)) {
            selected = (int)index;
            selected_bits = bits;
            selected_capacity = record.maximum_live_instances;
            selected_id = record.family_id;
        }
    }
    return selected;
}
