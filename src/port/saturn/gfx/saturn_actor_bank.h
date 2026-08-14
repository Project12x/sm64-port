#ifndef SM64_SATURN_ACTOR_BANK_H
#define SM64_SATURN_ACTOR_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SM64_SATURN_ACTOR_BANK_MAGIC 0x53363442UL
#define SM64_SATURN_ACTOR_BANK_VERSION_V1 1U
#define SM64_SATURN_ACTOR_BANK_VERSION_V2 2U
#define SM64_SATURN_ACTOR_BANK_VERSION SM64_SATURN_ACTOR_BANK_VERSION_V1
#define SM64_SATURN_ACTOR_BANK_HEADER_SIZE 104U
#define SM64_SATURN_ACTOR_BANK_V2_HEADER_SIZE 192U
#define SM64_SATURN_ACTOR_BANK_SOURCE_SHA256_OFFSET 26U
#define SM64_SATURN_ACTOR_BANK_MAXIMUM_SCRATCH_OFFSET 98U
#define SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE 16U
#define SM64_SATURN_ACTOR_RENDER_BINDING_RECORD_SIZE 8U
#define SM64_SATURN_ACTOR_TARGET_MATERIAL_RECORD_SIZE 8U
#define SM64_SATURN_ACTOR_TEXTURE_TILE_RECORD_SIZE 16U
#define SM64_SATURN_ACTOR_BANK_WORK_LANE_COUNT 2U
#define SM64_SATURN_ACTOR_BANK_WORK_ALIGNMENT 4U
#define SM64_SATURN_ACTOR_FAMILY_BANK_MAGIC 0x53363446UL
#define SM64_SATURN_ACTOR_FAMILY_BANK_VERSION 2U
#define SM64_SATURN_ACTOR_FAMILY_BANK_HEADER_SIZE 56U
#define SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE 56U
#define SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED (1U << 0)
#define SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY (1U << 1)
#define SM64_SATURN_ACTOR_CAP_ANIMATED (1U << 0)
#define SM64_SATURN_ACTOR_CAP_SWITCH (1U << 1)
#define SM64_SATURN_ACTOR_CAP_BILLBOARD (1U << 2)
#define SM64_SATURN_ACTOR_CAP_ALPHA (1U << 3)
#define SM64_SATURN_ACTOR_CAP_TRANSLUCENT (1U << 4)
#define SM64_SATURN_ACTOR_CAP_SHADOW (1U << 5)
#define SM64_SATURN_ACTOR_CAP_PARENTED (1U << 6)
#define SM64_SATURN_ACTOR_CAP_HELD (1U << 7)
#define SM64_SATURN_ACTOR_CAP_MODEL_MUTATION (1U << 8)
#define SM64_SATURN_ACTOR_CAP_SURFACE (1U << 9)
#define SM64_SATURN_ACTOR_CAP_LOD (1U << 10)
#define SM64_SATURN_ACTOR_CAP_PARTICLE (1U << 11)
#define SM64_SATURN_ACTOR_CAP_EFFECT (1U << 12)
#define SM64_SATURN_ACTOR_CAP_RIGID (1U << 13)
#define SM64_SATURN_ACTOR_CAP_OPAQUE (1U << 14)
#define SM64_SATURN_ACTOR_CAP_STATIC_TRANSFORM (1U << 15)
#define SM64_SATURN_ACTOR_CAP_PLATFORM (1U << 16)
#define SM64_SATURN_ACTOR_CAP_COLLECTIBLE (1U << 17)
#define SM64_SATURN_ACTOR_CAPABILITY_MASK ((1U << 18) - 1U)
/* Parent/held/LOD admission waits for typed immutable snapshot evidence. */
#define SM64_SATURN_ACTOR_CAPABILITY_ADMISSION_MASK \
    (SM64_SATURN_ACTOR_CAPABILITY_MASK & \
     ~(SM64_SATURN_ACTOR_CAP_PARENTED | SM64_SATURN_ACTOR_CAP_HELD | \
       SM64_SATURN_ACTOR_CAP_LOD))
#define SM64_SATURN_ACTOR_RUNTIME_CAP_TRANSFORM (1U << 0)
#define SM64_SATURN_ACTOR_RUNTIME_CAP_SCALE (1U << 1)
#define SM64_SATURN_ACTOR_RUNTIME_CAP_MATERIAL (1U << 2)
#define SM64_SATURN_ACTOR_RUNTIME_CAP_SURFACE (1U << 3)
#define SM64_SATURN_ACTOR_RUNTIME_CAP_LIFECYCLE (1U << 4)
#define SM64_SATURN_ACTOR_RUNTIME_CAPABILITY_MASK \
    (SM64_SATURN_ACTOR_RUNTIME_CAP_TRANSFORM | \
     SM64_SATURN_ACTOR_RUNTIME_CAP_SCALE | \
     SM64_SATURN_ACTOR_RUNTIME_CAP_MATERIAL | \
     SM64_SATURN_ACTOR_RUNTIME_CAP_SURFACE | \
     SM64_SATURN_ACTOR_RUNTIME_CAP_LIFECYCLE)

typedef struct sm64_saturn_actor_animation_record {
    uint32_t values_offset, indices_offset;
    uint16_t frame_count, joint_count, flags;
    int16_t y_translation_divisor;
} sm64_saturn_actor_animation_record_t;

typedef struct sm64_saturn_actor_bank {
    uint32_t magic;
    uint16_t version, family_id, model_id, joint_count;
    uint16_t animation_count, meshlet_count, primitive_count;
    uint16_t vertex_count, max_instances;
    uint32_t feature_mask, source_hash_words[8];
} sm64_saturn_actor_bank_t;

typedef struct sm64_saturn_actor_bank_view {
    const uint8_t *bytes;
    size_t byte_count;
    sm64_saturn_actor_bank_t bank;
    uint32_t records_offset;
    uint32_t indices_offset, indices_size;
    uint32_t values_offset, values_size;
    uint32_t vertices_offset, vertices_size;
    uint32_t meshlets_offset, meshlets_size;
    uint32_t max_scratch;
    uint16_t material_count, tile_count;
    uint32_t hot_end;
    uint32_t render_bindings_offset, render_bindings_size;
    uint32_t target_materials_offset, target_materials_size;
    uint32_t texture_tiles_offset, texture_tiles_size;
    uint32_t texture_payload_offset, texture_payload_size;
    uint32_t clut_payload_offset, clut_payload_size;
    uint32_t texture_resident_bytes, clut_resident_bytes;
    uint32_t draw_records_per_instance;
    uint32_t texture_commands_per_instance;
    uint32_t gouraud_tables_per_instance;
    uint32_t bake_policy_id;
} sm64_saturn_actor_bank_view_t;

typedef enum sm64_saturn_actor_material_recipe {
    SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD = 1,
    SM64_SATURN_ACTOR_RECIPE_CLUT16_REPLACE = 2,
    SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD = 3,
    SM64_SATURN_ACTOR_RECIPE_RGB1555_REPLACE = 4,
    SM64_SATURN_ACTOR_RECIPE_RGB1555_GOURAUD = 5,
    SM64_SATURN_ACTOR_RECIPE_CLUT16_HALF_TRANSPARENT = 6,
    SM64_SATURN_ACTOR_RECIPE_RGB1555_HALF_TRANSPARENT = 7
} sm64_saturn_actor_material_recipe_t;

typedef enum sm64_saturn_actor_target_layer {
    SM64_SATURN_ACTOR_LAYER_OPAQUE = 0,
    SM64_SATURN_ACTOR_LAYER_CUTOUT = 1,
    SM64_SATURN_ACTOR_LAYER_TRANSLUCENT = 2
} sm64_saturn_actor_target_layer_t;

typedef enum sm64_saturn_actor_alpha_mode {
    SM64_SATURN_ACTOR_ALPHA_OPAQUE = 0,
    SM64_SATURN_ACTOR_ALPHA_BINARY_ZERO_TRANSPARENT = 1,
    SM64_SATURN_ACTOR_ALPHA_HALF_TRANSPARENT = 2
} sm64_saturn_actor_alpha_mode_t;

typedef enum sm64_saturn_actor_tile_format {
    SM64_SATURN_ACTOR_TILE_FORMAT_CLUT16 = 1,
    SM64_SATURN_ACTOR_TILE_FORMAT_RGB1555 = 2
} sm64_saturn_actor_tile_format_t;

typedef struct sm64_saturn_actor_render_binding {
    uint16_t material_id, tile_id, flags, reserved;
} sm64_saturn_actor_render_binding_t;

typedef struct sm64_saturn_actor_target_material {
    uint16_t recipe;
    uint8_t layer, alpha_mode, selector_kind, flags;
    uint16_t reserved;
} sm64_saturn_actor_target_material_t;

typedef struct sm64_saturn_actor_texture_tile {
    uint32_t payload_offset, payload_size;
    uint16_t width, height, clut_id;
    uint8_t format, flags;
} sm64_saturn_actor_texture_tile_t;

typedef struct sm64_saturn_actor_joint {
    int16_t parent_ordinal;
    int16_t translation[3];
    int16_t node_ordinal;
    uint16_t branch_ordinal;
} sm64_saturn_actor_joint_t;

typedef struct sm64_saturn_actor_vertex {
    int16_t local[3];
    uint16_t joint_ordinal;
    uint16_t branch_ordinal;
} sm64_saturn_actor_vertex_t;

typedef struct sm64_saturn_actor_primitive {
    uint16_t material_id;
    uint16_t vertex[4];
} sm64_saturn_actor_primitive_t;

typedef struct sm64_saturn_actor_material_color {
    uint8_t rgb555[3];
} sm64_saturn_actor_material_color_t;

/* Generic immutable family-bank metadata.  Every span is an offset/count
 * relative to the validated S64F payload; no runtime pointer crosses this
 * boundary. */
typedef struct sm64_saturn_actor_family_bank_view {
    const uint8_t *bytes;
    size_t byte_count;
    uint16_t version;
    uint16_t family_count;
    uint32_t records_offset;
    uint32_t records_size;
    uint32_t blob_offset;
    uint32_t blob_size;
} sm64_saturn_actor_family_bank_view_t;

typedef struct sm64_saturn_actor_family_record {
    uint32_t family_id;
    uint32_t capability_mask;
    uint32_t maximum_live_instances;
    uint32_t actor_count;
    uint32_t flags;
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t source_offset;
    uint32_t source_size;
    uint32_t unsupported_offset;
    uint32_t unsupported_size;
    uint32_t metadata_offset;
    uint32_t metadata_size;
    uint32_t runtime_capability_mask;
} sm64_saturn_actor_family_record_t;

bool sm64_saturn_actor_bank_validate(const void *data, size_t byte_count,
                                     sm64_saturn_actor_bank_view_t *view);
/* Returns one aligned lane, both usable lanes, and the worst-case reservation
 * including leading headroom for an arbitrarily aligned raw payload end. */
bool sm64_saturn_actor_bank_workspace_requirements(
    uint16_t vertex_count, uint16_t joint_count, uint32_t *lane_bytes,
    uint32_t *usable_bytes, uint32_t *reserved_bytes);
bool sm64_saturn_actor_bank_validate_expected(
    const void *data, size_t byte_count, const uint32_t expected_source_hash[8],
    sm64_saturn_actor_bank_view_t *view);
/* Opens a bank whose enclosing, generation-owned S64F bundle has already
 * passed sm64_saturn_actor_bundle_validate(). It performs bounded header
 * reconstruction and identity checks only; callers must not expose mutable
 * bytes through the validated bundle lifetime. */
bool sm64_saturn_actor_bank_open_validated(
    const void *data, size_t byte_count, const uint32_t expected_source_hash[8],
    sm64_saturn_actor_bank_view_t *view);
bool sm64_saturn_actor_bank_animation(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    sm64_saturn_actor_animation_record_t *record);
bool sm64_saturn_actor_bank_sample_channel(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    uint16_t frame, uint16_t channel, int16_t *sample);
bool sm64_saturn_actor_bank_joint(
    const sm64_saturn_actor_bank_view_t *view, uint16_t joint,
    sm64_saturn_actor_joint_t *out);
bool sm64_saturn_actor_bank_vertex(
    const sm64_saturn_actor_bank_view_t *view, uint16_t vertex,
    sm64_saturn_actor_vertex_t *out);
bool sm64_saturn_actor_bank_primitive(
    const sm64_saturn_actor_bank_view_t *view, uint16_t primitive,
    sm64_saturn_actor_primitive_t *out);
bool sm64_saturn_actor_bank_material_color(
    const sm64_saturn_actor_bank_view_t *view, uint16_t material,
    sm64_saturn_actor_material_color_t *out);
bool sm64_saturn_actor_bank_render_binding(
    const sm64_saturn_actor_bank_view_t *view, uint16_t primitive,
    sm64_saturn_actor_render_binding_t *out);
bool sm64_saturn_actor_bank_target_material(
    const sm64_saturn_actor_bank_view_t *view, uint16_t material,
    sm64_saturn_actor_target_material_t *out);
bool sm64_saturn_actor_bank_texture_tile(
    const sm64_saturn_actor_bank_view_t *view, uint16_t tile,
    sm64_saturn_actor_texture_tile_t *out);

bool sm64_saturn_actor_family_bank_validate(
    const void *data, size_t byte_count,
    sm64_saturn_actor_family_bank_view_t *view);
bool sm64_saturn_actor_family_bank_validate_expected(
    const void *data, size_t byte_count, const uint32_t expected_hash[8],
    sm64_saturn_actor_family_bank_view_t *view);
bool sm64_saturn_actor_family_bank_record(
    const sm64_saturn_actor_family_bank_view_t *view, uint16_t index,
    sm64_saturn_actor_family_record_t *out);
bool sm64_saturn_actor_family_capability_mask_supported(
    uint32_t required_capability_mask);
bool sm64_saturn_actor_family_capability_supported(
    const sm64_saturn_actor_family_record_t *record,
    uint32_t required_runtime_capability_mask);
int sm64_saturn_actor_family_bank_select(
    const sm64_saturn_actor_family_bank_view_t *view,
    uint32_t required_capability_mask, uint32_t multiplicity);

#endif
