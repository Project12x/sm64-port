#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_terrain_command_template.h"

static sm64_saturn_terrain_primitive_t primitive(
    uint16_t flags, uint16_t a, uint16_t b, uint16_t c, uint16_t d,
    uint16_t texture_slot)
{
    const sm64_saturn_terrain_primitive_t value = {
        .flags = flags,
        .colors = {a, b, c, d},
        .texture_slot = texture_slot,
    };
    return value;
}

static void test_builds_each_shade_path(void)
{
    sm64_saturn_terrain_command_template_t output;
    const sm64_saturn_terrain_primitive_t flat = primitive(
        SM64_SATURN_TERRAIN_RESULT_OPAQUE,
        0x1234U, 0x1234U, 0x1234U, 0x1234U, 7U);
    assert(sm64_saturn_terrain_template_build(&output, &flat));
    assert(output.flags == SM64_SATURN_TERRAIN_RESULT_OPAQUE);
    assert(output.color == 0x1234U);
    assert(output.texture_slot == 7U);
    assert(output.shade_path == SM64_SATURN_SHADE_FLAT_REPLACE);

    const sm64_saturn_terrain_primitive_t textured = primitive(
        SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            SM64_SATURN_TERRAIN_RESULT_TEXTURED,
        1U, 2U, 3U, 4U, 19U);
    assert(sm64_saturn_terrain_template_build(&output, &textured));
    assert(output.flags == (SM64_SATURN_TERRAIN_RESULT_OPAQUE |
                            SM64_SATURN_TERRAIN_RESULT_TEXTURED));
    assert(output.color == 1U);
    assert(output.texture_slot == 19U);
    assert(output.shade_path == SM64_SATURN_SHADE_TEXTURED);

    const sm64_saturn_terrain_primitive_t gouraud = primitive(
        SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            SM64_SATURN_TERRAIN_RESULT_GOURAUD,
        0x0100U, 0x0200U, 0x0300U, 0x0400U, 0U);
    assert(sm64_saturn_terrain_template_build(&output, &gouraud));
    assert(output.color == 0x0100U);
    assert(output.shade_path == SM64_SATURN_SHADE_GOURAUD);
}

static void test_patch_changes_only_runtime_words(void)
{
    const uint16_t command_template[16] = {
        0x0004U, 0x0000U, 0x04C0U, 0x8000U,
        0x0123U, 0x0410U, 0x0000U, 0x0000U,
        0x0000U, 0x0000U, 0x0000U, 0x0000U,
        0x0000U, 0x0000U, 0x0000U, 0x55AAU};
    const int16_t vertices_a[4][2] = {
        {-20, -10}, {20, -10}, {20, 10}, {-20, 10}};
    const int16_t vertices_b[4][2] = {
        {100, 40}, {130, 42}, {125, 90}, {95, 87}};
    uint16_t patch_a[16];
    uint16_t patch_b[16];
    assert(sm64_saturn_terrain_template_patch(
        patch_a, command_template, vertices_a, 2U, false, true,
        0x00123458U));
    assert(sm64_saturn_terrain_template_patch(
        patch_b, command_template, vertices_b, 9U, true, true,
        0x0012ABC0U));

    /* PMOD/COLR/SRCA/SIZE and the reserved word are the immutable VDP1
     * material words. The command bits of CTRL are immutable; only END may
     * differ between patches. */
    const uint8_t immutable_words[] = {2U, 3U, 4U, 5U, 15U};
    for (uint8_t i = 0U; i < sizeof(immutable_words); i++) {
        const uint8_t word = immutable_words[i];
        assert(patch_a[word] == command_template[word]);
        assert(patch_b[word] == command_template[word]);
    }
    assert((patch_a[0] & 0x7FFFU) == command_template[0]);
    assert((patch_b[0] & 0x7FFFU) == command_template[0]);
    assert(patch_a[1] == 2U);
    assert(patch_b[1] == 9U);
    assert(memcmp(&patch_a[6], &patch_b[6], 8U * sizeof(uint16_t)) != 0);
    assert(patch_a[14] == (uint16_t)(0x00123458U >> 3));
    assert(patch_b[14] == (uint16_t)(0x0012ABC0U >> 3));
    assert((patch_a[0] & 0x8000U) == 0U);
    assert((patch_b[0] & 0x8000U) != 0U);

    uint16_t no_gouraud_patch[16];
    assert(sm64_saturn_terrain_template_patch(
        no_gouraud_patch, command_template, vertices_a, 0U, false, false,
        UINTPTR_MAX));
    assert(no_gouraud_patch[14] == command_template[14]);
}

static void test_compact_resolved_state_matches_full_templates(void)
{
    const int16_t vertices[4][2] = {
        {-20, -10}, {20, -10}, {20, 10}, {-20, 10}};
    const uint16_t full_templates[][16] = {
        /* Flat REPLACE. */
        {0x0004U, 0U, 0x00C0U, 0x9234U, 0U, 0U,
         0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
        /* Textured REPLACE is a distorted sprite, not a polygon. */
        {0x0002U, 0U, 0x04C0U, 0x0040U, 0x0123U, 0x0410U,
         0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
        /* Gouraud; GRDA is deliberately dynamic. */
        {0x0004U, 0U, 0x00C4U, 0x8000U, 0U, 0U,
         0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    };
    const bool patches_gouraud[] = {false, false, true};

    for (uint8_t path = 0U; path < 3U; path++) {
        uint16_t old_path[16];
        uint16_t compact_path[16];
        uint16_t compact_record_path[16];
        const uint16_t *full = full_templates[path];
        const sm64_saturn_terrain_resolved_command_t resolved = {
            .control = full[0], .pmod = full[2], .colr = full[3],
            .srca = full[4], .size = full[5]};
        assert(sm64_saturn_terrain_template_patch(
            old_path, full, vertices, 7U, path == 1U,
            patches_gouraud[path], 0x00123458U));
        assert(sm64_saturn_terrain_template_patch_resolved(
            compact_path, full[0], full[2], full[3], full[4], full[5],
            vertices, 7U, path == 1U, patches_gouraud[path], 0x00123458U));
        assert(sm64_saturn_terrain_template_patch_resolved_record(
            compact_record_path, &resolved, vertices, 7U, path == 1U,
            patches_gouraud[path], 0x00123458U));
        assert(memcmp(old_path, compact_path, sizeof(old_path)) == 0);
        assert(memcmp(old_path, compact_record_path, sizeof(old_path)) == 0);
    }
}

static void test_resolved_record_derives_bob_static_inputs_within_budget(void)
{
    const uint8_t rgb[3] = {12U, 17U, 3U};
    sm64_saturn_terrain_command_template_t metadata;
    assert(sm64_saturn_terrain_template_build_from_bob(
        &metadata, true, rgb, 0x1234U));
    assert(metadata.flags == (SM64_SATURN_TERRAIN_RESULT_OPAQUE |
                              SM64_SATURN_TERRAIN_RESULT_TEXTURED));
    assert(metadata.texture_slot == 0x1234U);
    assert(metadata.shade_path == SM64_SATURN_SHADE_TEXTURED);

    const sm64_saturn_terrain_resolved_command_t resolved = {
        .control = 0x0002U, .pmod = 0x04C0U, .colr = 0x0040U,
        .srca = 0x0123U, .size = 0x0410U};
    const int16_t vertices[4][2] = {
        {-20, -10}, {20, -10}, {20, 10}, {-20, 10}};
    uint16_t compact[16];
    assert(sm64_saturn_terrain_template_patch_resolved_record(
        compact, &resolved, vertices, 7U, false, false, 0U));
    assert(compact[0] == resolved.control);
    assert(compact[2] == resolved.pmod);
    assert(compact[3] == resolved.colr);
    assert(compact[4] == resolved.srca);
    assert(compact[5] == resolved.size);
    assert(sizeof(resolved) == 10U);
    assert(sm64_saturn_terrain_compact_cache_bytes(867U) == 0x224BU);
    assert(0x3710U - sm64_saturn_terrain_compact_cache_bytes(867U) >=
           0x14C5U);
}

static void test_compact_cache_rejects_fragment_profile_above_budget(void)
{
    assert(sm64_saturn_terrain_compact_cache_fits(867U, 0x3710U));
    assert(!sm64_saturn_terrain_compact_cache_fits(2108U, 0x3710U));
}

static void test_runtime_material_changes_use_fallback(void)
{
    sm64_saturn_terrain_command_template_t flat_template;
    const sm64_saturn_terrain_primitive_t flat = primitive(
        SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            SM64_SATURN_TERRAIN_RESULT_GOURAUD,
        0x1234U, 0x1234U, 0x1234U, 0x1234U, 7U);
    assert(sm64_saturn_terrain_template_build(&flat_template, &flat));
    assert(sm64_saturn_terrain_template_matches(
        &flat_template, flat.flags, flat.colors, 7U));

    const uint16_t changed_flat[4] = {
        0x4321U, 0x4321U, 0x4321U, 0x4321U};
    assert(!sm64_saturn_terrain_template_matches(
        &flat_template, flat.flags, changed_flat, 7U));
    assert(!sm64_saturn_terrain_template_matches(
        &flat_template, flat.flags, flat.colors, 8U));

    sm64_saturn_terrain_command_template_t texture_template;
    const sm64_saturn_terrain_primitive_t textured = primitive(
        SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            SM64_SATURN_TERRAIN_RESULT_TEXTURED,
        1U, 2U, 3U, 4U, 19U);
    assert(sm64_saturn_terrain_template_build(&texture_template, &textured));
    assert(sm64_saturn_terrain_template_matches(
        &texture_template, textured.flags, textured.colors, 19U));

    const uint16_t suppressed_colors[4] = {1U, 1U, 1U, 1U};
    assert(!sm64_saturn_terrain_template_matches(
        &texture_template,
        SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            SM64_SATURN_TERRAIN_RESULT_GOURAUD,
        suppressed_colors, 19U));
}

static void test_invalid_inputs_fail_closed(void)
{
    sm64_saturn_terrain_command_template_t output;
    uint16_t patch[16];
    uint16_t command_template[16] = {0};
    const int16_t vertices[4][2] = {{0}};
    const sm64_saturn_terrain_primitive_t conflicting = primitive(
        SM64_SATURN_TERRAIN_RESULT_TEXTURED |
            SM64_SATURN_TERRAIN_RESULT_GOURAUD,
        1U, 1U, 1U, 1U, 0U);

    assert(!sm64_saturn_terrain_template_build(NULL, &conflicting));
    assert(!sm64_saturn_terrain_template_build(&output, NULL));
    assert(!sm64_saturn_terrain_template_build(&output, &conflicting));
    assert(!sm64_saturn_terrain_template_patch(
        NULL, command_template, vertices, 0U, false, false, 0U));
    assert(!sm64_saturn_terrain_template_patch(
        patch, NULL, vertices, 0U, false, false, 0U));
    assert(!sm64_saturn_terrain_template_patch(
        patch, command_template, NULL, 0U, false, false, 0U));
}

int main(void)
{
    test_builds_each_shade_path();
    test_patch_changes_only_runtime_words();
    test_compact_resolved_state_matches_full_templates();
    test_resolved_record_derives_bob_static_inputs_within_budget();
    test_compact_cache_rejects_fragment_profile_above_budget();
    test_runtime_material_changes_use_fallback();
    test_invalid_inputs_fail_closed();
    return 0;
}
