#include "saturn_actor_bank.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned mutation_count;

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint8_t *load(const char *path, uint32_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    assert(file != NULL && fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && (unsigned long)length <= UINT32_MAX);
    rewind(file);
    bytes = malloc((size_t)length);
    assert(bytes != NULL && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    *size = (uint32_t)length;
    return bytes;
}

static void assert_zero(const void *value, size_t size)
{
    const uint8_t *bytes = value;
    size_t index;
    for (index = 0U; index < size; index++) assert(bytes[index] == 0U);
}

static void reject(const uint8_t *valid, uint32_t size, uint32_t offset,
                   uint32_t width, uint32_t replacement)
{
    uint8_t *copy = malloc(size);
    sm64_saturn_actor_bank_view_t view;
    assert(copy != NULL && offset <= size && width <= size - offset);
    memcpy(copy, valid, size);
    if (width == 1U) copy[offset] = (uint8_t)replacement;
    else if (width == 2U) put16(copy + offset, (uint16_t)replacement);
    else { assert(width == 4U); put32(copy + offset, replacement); }
    memset(&view, 0xA5, sizeof(view));
    if (sm64_saturn_actor_bank_validate(copy, size, &view)) {
        fprintf(stderr, "accepted mutation offset=%lu width=%lu replacement=%lu\n",
                (unsigned long)offset, (unsigned long)width,
                (unsigned long)replacement);
        assert(false);
    }
    mutation_count++;
    free(copy);
}

static void check_v1(const uint8_t *bytes, uint32_t size)
{
    sm64_saturn_actor_bank_view_t view;
    sm64_saturn_actor_render_binding_t binding;
    assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    assert(view.bank.version == SM64_SATURN_ACTOR_BANK_VERSION_V1);
    assert(view.hot_end == size && view.material_count == 1U);
    assert(view.render_bindings_offset == 0U && view.render_bindings_size == 0U);
    assert(view.target_materials_offset == 0U && view.target_materials_size == 0U);
    assert(view.texture_tiles_offset == 0U && view.texture_tiles_size == 0U);
    assert(view.texture_payload_offset == 0U && view.texture_payload_size == 0U);
    assert(view.clut_payload_offset == 0U && view.clut_payload_size == 0U);
    assert(view.texture_resident_bytes == 0U && view.clut_resident_bytes == 0U);
    assert(view.draw_records_per_instance == 0U);
    assert(view.texture_commands_per_instance == 0U);
    assert(view.gouraud_tables_per_instance == 0U && view.bake_policy_id == 0U);
    memset(&binding, 0xA5, sizeof(binding));
    assert(!sm64_saturn_actor_bank_render_binding(&view, 0U, &binding));
    assert_zero(&binding, sizeof(binding));
}

static void check_untextured(const uint8_t *bytes, uint32_t size)
{
    sm64_saturn_actor_bank_view_t view;
    sm64_saturn_actor_render_binding_t binding;
    sm64_saturn_actor_target_material_t material;
    assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    assert(view.bank.version == SM64_SATURN_ACTOR_BANK_VERSION_V2);
    assert(view.bank.family_id == 2U && view.bank.model_id == 3U);
    assert(view.bank.primitive_count == 3U && view.material_count == 1U);
    assert(view.render_bindings_offset == be32(bytes + 112U));
    assert(view.render_bindings_size == 24U);
    assert(view.target_materials_offset == be32(bytes + 120U));
    assert(view.target_materials_size == 8U);
    assert(view.texture_tiles_size == 0U && view.texture_payload_size == 0U);
    assert(view.clut_payload_size == 0U && view.hot_end == be32(bytes + 136U));
    assert(view.draw_records_per_instance == 3U);
    assert(view.texture_commands_per_instance == 0U);
    assert(view.gouraud_tables_per_instance == 3U);
    assert(view.bake_policy_id == 0x12345678UL);
    assert(sm64_saturn_actor_bank_render_binding(&view, 2U, &binding));
    assert(binding.material_id == 0U && binding.tile_id == UINT16_MAX);
    assert(sm64_saturn_actor_bank_target_material(&view, 0U, &material));
    assert(material.recipe == SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD);
    assert(material.layer == SM64_SATURN_ACTOR_LAYER_OPAQUE);
    assert(material.alpha_mode == SM64_SATURN_ACTOR_ALPHA_OPAQUE);
}

static void check_textured(const uint8_t *bytes, uint32_t size)
{
    sm64_saturn_actor_bank_view_t view;
    sm64_saturn_actor_render_binding_t binding;
    sm64_saturn_actor_target_material_t material;
    sm64_saturn_actor_texture_tile_t tile;
    sm64_saturn_actor_primitive_t primitive;
    sm64_saturn_actor_material_color_t color;
    uint32_t extension_fields[] = {
        112U, 116U, 120U, 124U, 128U, 132U, 136U, 140U, 144U, 148U,
        152U, 156U, 160U, 164U, 168U, 172U, 176U
    };
    uint32_t index;
    assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    assert(view.bytes == bytes && view.byte_count == size);
    assert(view.bank.magic == SM64_SATURN_ACTOR_BANK_MAGIC);
    assert(view.bank.version == SM64_SATURN_ACTOR_BANK_VERSION_V2);
    assert(view.bank.family_id == be16(bytes + 6U));
    assert(view.bank.model_id == be16(bytes + 8U));
    assert(view.bank.joint_count == be16(bytes + 10U));
    assert(view.bank.animation_count == be16(bytes + 12U));
    assert(view.bank.meshlet_count == be16(bytes + 14U));
    assert(view.bank.primitive_count == be16(bytes + 16U));
    assert(view.bank.vertex_count == be16(bytes + 18U));
    assert(view.bank.max_instances == be16(bytes + 20U));
    assert(view.bank.feature_mask == be32(bytes + 22U));
    for (index = 0U; index < 8U; index++)
        assert(view.bank.source_hash_words[index] == be32(bytes + 26U + index * 4U));
    assert(view.records_offset == be32(bytes + 62U));
    assert(view.indices_offset == be32(bytes + 66U));
    assert(view.indices_size == be32(bytes + 70U));
    assert(view.values_offset == be32(bytes + 74U));
    assert(view.values_size == be32(bytes + 78U));
    assert(view.vertices_offset == be32(bytes + 82U));
    assert(view.vertices_size == be32(bytes + 86U));
    assert(view.meshlets_offset == be32(bytes + 90U));
    assert(view.meshlets_size == be32(bytes + 94U));
    assert(view.max_scratch == be32(bytes + 98U));
    assert(view.texture_tiles_size == 32U && view.tile_count == 2U);
    assert(view.render_bindings_offset == be32(bytes + 112U));
    assert(view.render_bindings_size == be32(bytes + 116U));
    assert(view.target_materials_offset == be32(bytes + 120U));
    assert(view.target_materials_size == be32(bytes + 124U));
    assert(view.texture_tiles_offset == be32(bytes + 128U));
    assert(view.texture_tiles_size == be32(bytes + 132U));
    assert(view.texture_payload_offset == be32(bytes + 136U));
    assert(view.texture_payload_size == 16U && view.texture_resident_bytes == 16U);
    assert(view.texture_payload_size == be32(bytes + 140U));
    assert(view.clut_payload_offset == be32(bytes + 144U));
    assert(view.clut_payload_size == 32U && view.clut_resident_bytes == 32U);
    assert(view.clut_payload_size == be32(bytes + 148U));
    assert(view.texture_resident_bytes == be32(bytes + 152U));
    assert(view.clut_resident_bytes == be32(bytes + 156U));
    assert(view.draw_records_per_instance == be32(bytes + 160U));
    assert(view.texture_commands_per_instance == be32(bytes + 164U));
    assert(view.gouraud_tables_per_instance == be32(bytes + 168U));
    assert(view.hot_end == view.texture_payload_offset);
    assert(view.texture_commands_per_instance == 3U);
    assert(view.gouraud_tables_per_instance == 0U && view.bake_policy_id == 7U);
    assert(sm64_saturn_actor_bank_render_binding(&view, 0U, &binding));
    assert(binding.material_id == 0U && binding.tile_id == 0U);
    assert(sm64_saturn_actor_bank_render_binding(&view, 2U, &binding));
    assert(binding.tile_id == 1U);
    assert(sm64_saturn_actor_bank_target_material(&view, 0U, &material));
    assert(material.recipe == SM64_SATURN_ACTOR_RECIPE_CLUT16_REPLACE);
    assert(material.layer == SM64_SATURN_ACTOR_LAYER_CUTOUT);
    assert(material.alpha_mode == SM64_SATURN_ACTOR_ALPHA_BINARY_ZERO_TRANSPARENT);
    assert(sm64_saturn_actor_bank_texture_tile(&view, 1U, &tile));
    assert(tile.payload_offset == 8U && tile.payload_size == 8U);
    assert(tile.width == 8U && tile.height == 2U && tile.clut_id == 0U);
    assert(tile.format == SM64_SATURN_ACTOR_TILE_FORMAT_CLUT16);
    assert(sm64_saturn_actor_bank_primitive(&view, 0U, &primitive));
    assert(primitive.material_id == be16(
        bytes + view.meshlets_offset + be32(bytes + view.meshlets_offset + 34U)));
    assert(sm64_saturn_actor_bank_material_color(
        &view, primitive.material_id, &color));
    assert(color.rgb555[0] <= 31U && color.rgb555[1] <= 31U &&
           color.rgb555[2] <= 31U);
    assert(!sm64_saturn_actor_bank_primitive(
        &view, view.bank.primitive_count, &primitive));
    assert(!sm64_saturn_actor_bank_material_color(
        &view, view.material_count, &color));
    {
        uint32_t expected_source[8];
        uint8_t *copy = malloc(size);
        assert(copy != NULL);
        for (index = 0U; index < 8U; index++)
            expected_source[index] = be32(bytes + 26U + index * 4U);
        memcpy(copy, bytes, size);
        copy[26] ^= 1U;
        memset(&view, 0xA5, sizeof(view));
        assert(!sm64_saturn_actor_bank_validate_expected(
            copy, size, expected_source, &view));
        free(copy);
        mutation_count++;
        assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    }
    {
        uint8_t *copy = malloc(size + 1U);
        assert(copy != NULL);
        memcpy(copy, bytes, size);
        memset(copy + 26U, 0, 32U);
        assert(!sm64_saturn_actor_bank_validate(copy, size, &view));
        mutation_count++;
        memcpy(copy, bytes, size);
        copy[size] = 0U;
        assert(!sm64_saturn_actor_bank_validate(copy, size + 1U, &view));
        mutation_count++;
        free(copy);
        assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    }

    reject(bytes, size, 0U, 4U, 0U);
    reject(bytes, size, 6U, 2U, 0U);
    reject(bytes, size, 8U, 2U, 0U);
    reject(bytes, size, 10U, 2U, 0U);
    reject(bytes, size, 12U, 2U, 0U);
    reject(bytes, size, 14U, 2U, 0U);
    reject(bytes, size, 16U, 2U, 0U);
    reject(bytes, size, 18U, 2U, 0U);
    reject(bytes, size, 20U, 2U, 0U);
    reject(bytes, size, 62U, 4U, 0U);
    reject(bytes, size, 66U, 4U, be32(bytes + 66U) + 1U);
    reject(bytes, size, 70U, 4U, 0U);
    reject(bytes, size, 74U, 4U, be32(bytes + 74U) + 1U);
    reject(bytes, size, 78U, 4U, 0U);
    reject(bytes, size, 82U, 4U, be32(bytes + 82U) + 1U);
    reject(bytes, size, 86U, 4U, 0U);
    reject(bytes, size, 90U, 4U, be32(bytes + 90U) + 1U);
    reject(bytes, size, 94U, 4U, 0U);
    reject(bytes, size, 98U, 4U, be32(bytes + 98U) + 1U);
    reject(bytes, size, be32(bytes + 62U) + 8U, 2U, 0U);
    reject(bytes, size, be32(bytes + be32(bytes + 62U)) - 4U,
           4U, 0x80000000UL);
    reject(bytes, size, be32(bytes + 90U), 4U, 0U);

    reject(bytes, size, 4U, 2U, 3U);
    reject(bytes, size, 58U, 2U, 104U);
    reject(bytes, size, 60U, 2U, 15U);
    reject(bytes, size, 102U, 1U, 1U);
    reject(bytes, size, 104U, 2U, 7U);
    reject(bytes, size, 106U, 2U, 7U);
    reject(bytes, size, 108U, 2U, 15U);
    reject(bytes, size, 110U, 2U, 1U);
    for (index = 0U; index < sizeof(extension_fields) / sizeof(extension_fields[0]); index++) {
        uint32_t offset = extension_fields[index];
        uint32_t replacement = 0U;
        if (offset <= 148U || offset == 172U) replacement = be32(bytes + offset) + 1U;
        else if (offset == 168U) replacement = 1U;
        reject(bytes, size, offset, 4U, replacement);
    }
    for (index = 180U; index < 192U; index++) reject(bytes, size, index, 1U, 1U);
    reject(bytes, size, view.render_bindings_offset + 4U, 2U, 1U);
    reject(bytes, size, view.render_bindings_offset + 6U, 2U, 1U);
    reject(bytes, size, view.render_bindings_offset, 2U, 1U);
    reject(bytes, size, view.render_bindings_offset + 2U, 2U, 1U);
    reject(bytes, size, view.target_materials_offset, 2U, 99U);
    reject(bytes, size, view.target_materials_offset + 2U, 1U, 2U);
    reject(bytes, size, view.target_materials_offset + 3U, 1U, 2U);
    reject(bytes, size, view.target_materials_offset + 4U, 1U, 1U);
    reject(bytes, size, view.target_materials_offset + 5U, 1U, 1U);
    reject(bytes, size, view.target_materials_offset + 6U, 2U, 1U);
    reject(bytes, size, view.texture_tiles_offset, 4U, 1U);
    reject(bytes, size, view.texture_tiles_offset + 4U, 4U, 7U);
    reject(bytes, size, view.texture_tiles_offset + 8U, 2U, 7U);
    reject(bytes, size, view.texture_tiles_offset + 10U, 2U, 0U);
    reject(bytes, size, view.texture_tiles_offset + 12U, 2U, UINT16_MAX);
    reject(bytes, size, view.texture_tiles_offset + 14U, 1U, 3U);
    reject(bytes, size, view.texture_tiles_offset + 15U, 1U, 1U);
    reject(bytes, size, view.texture_tiles_offset + 16U, 4U, 0U);
    reject(bytes, size, view.texture_tiles_offset + 28U, 2U, 1U);
    reject(bytes, size, view.texture_tiles_offset + view.texture_tiles_size,
           1U, 1U);
    reject(bytes, size, view.clut_payload_offset, 2U, 0x8000U);
    reject(bytes, size, view.clut_payload_offset + 2U, 2U, 1U);
}

static void check_padded(const uint8_t *bytes, uint32_t size)
{
    sm64_saturn_actor_bank_view_t view;
    assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    assert(view.texture_payload_size == 8U);
    reject(bytes, size, view.texture_payload_offset + 4U, 1U, 1U);
}

static void check_rgb1555(const uint8_t *bytes, uint32_t size)
{
    sm64_saturn_actor_bank_view_t view;
    sm64_saturn_actor_texture_tile_t tile;
    assert(sm64_saturn_actor_bank_validate(bytes, size, &view));
    assert(view.tile_count == 1U && view.clut_payload_size == 0U);
    assert(sm64_saturn_actor_bank_texture_tile(&view, 0U, &tile));
    assert(tile.format == SM64_SATURN_ACTOR_TILE_FORMAT_RGB1555);
    assert(tile.clut_id == UINT16_MAX && tile.payload_size == 16U);
    reject(bytes, size, view.texture_payload_offset + 2U, 2U, 1U);
}

int main(int argc, char **argv)
{
    uint32_t v1_size, untextured_size, textured_size, rgb_size, padded_size;
    uint8_t *v1, *untextured, *textured, *rgb, *padded;
    assert(argc == 6);
    assert(sizeof(sm64_saturn_actor_render_binding_t) ==
           SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE);
    assert(sizeof(sm64_saturn_actor_target_material_t) ==
           SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE);
    assert(sizeof(sm64_saturn_actor_texture_tile_t) ==
           SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE);
    v1 = load(argv[1], &v1_size);
    untextured = load(argv[2], &untextured_size);
    textured = load(argv[3], &textured_size);
    rgb = load(argv[4], &rgb_size);
    padded = load(argv[5], &padded_size);
    check_v1(v1, v1_size);
    check_untextured(untextured, untextured_size);
    check_textured(textured, textured_size);
    check_rgb1555(rgb, rgb_size);
    check_padded(padded, padded_size);
    free(padded); free(rgb); free(textured); free(untextured); free(v1);
    printf("actor bank v2: PASS (%u mutations)\n", mutation_count);
    return 0;
}
