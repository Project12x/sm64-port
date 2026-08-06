/* Pointer-free actor effect admission and master painter-order helpers. */
#ifndef SM64_SATURN_ACTOR_EFFECT_H
#define SM64_SATURN_ACTOR_EFFECT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_actor_bank.h"
#include "saturn_actor_instance.h"

#define SM64_SATURN_EFFECT_DEPTH_BIN_COUNT 64U
#define SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY \
    SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE

typedef enum sm64_saturn_effect_material_class {
    SM64_SATURN_EFFECT_MATERIAL_OPAQUE = 1U,
    SM64_SATURN_EFFECT_MATERIAL_CUTOUT = 2U,
    SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT = 3U,
    SM64_SATURN_EFFECT_MATERIAL_DECAL = 4U,
    SM64_SATURN_EFFECT_MATERIAL_SHADOW = 5U,
} sm64_saturn_effect_material_class_t;

/* These scalar values are translated by the master VDP1 backend to the
 * corresponding Yaul VDP1_CMDT_CC_* mode.  Workers never touch VDP1 state. */
typedef enum sm64_saturn_effect_vdp1_mode {
    SM64_SATURN_EFFECT_VDP1_REPLACE = 1U,
    SM64_SATURN_EFFECT_VDP1_HALF_TRANSPARENT = 2U,
} sm64_saturn_effect_vdp1_mode_t;

typedef enum sm64_saturn_effect_basis_kind {
    SM64_SATURN_EFFECT_BASIS_MODEL = 0U,
    /* The consumer must call sm64_saturn_mtxq_billboard.  This marker does
     * not authorize a second camera-facing implementation. */
    SM64_SATURN_EFFECT_BASIS_EXISTING_MTXQ_BILLBOARD = 1U,
} sm64_saturn_effect_basis_kind_t;

#define SM64_SATURN_EFFECT_FLAG_SOURCE_VISIBLE (1U << 0)
#define SM64_SATURN_EFFECT_FLAG_SOURCE_LIVE (1U << 1)
#define SM64_SATURN_EFFECT_FLAG_RECEIVER_RESOLVED (1U << 2)
#define SM64_SATURN_EFFECT_FLAG_MASK \
    (SM64_SATURN_EFFECT_FLAG_SOURCE_VISIBLE | \
     SM64_SATURN_EFFECT_FLAG_SOURCE_LIVE | \
     SM64_SATURN_EFFECT_FLAG_RECEIVER_RESOLVED)

typedef struct sm64_saturn_actor_effect_descriptor {
    uint32_t generation;
    uint32_t scene_package_generation;
    uint32_t instance_key;
    uint32_t capability_mask;
    int32_t effect_params_q16[4];
    uint32_t effect_lifetime;
    uint16_t source_order;
    uint16_t family_id;
    uint16_t model_id;
    uint16_t opacity;
    uint16_t billboard_state;
    uint16_t shadow_type;
    uint16_t shadow_scale;
    uint16_t shadow_solidity;
    uint16_t effect_kind;
    uint16_t effect_flags;
    uint8_t material_class;
    uint8_t basis_kind;
    uint8_t vdp1_mode;
    uint8_t depth_bin;
    uint8_t reserved[4];
} sm64_saturn_actor_effect_descriptor_t;

typedef struct sm64_saturn_actor_effect_output {
    uint32_t instance_key;
    uint16_t source_order;
    uint16_t opacity;
    uint16_t effect_kind;
    uint16_t effect_flags;
    uint8_t material_class;
    uint8_t basis_kind;
    uint8_t vdp1_mode;
    uint8_t transparent_pixel_enable;
} sm64_saturn_actor_effect_output_t;

typedef struct sm64_saturn_actor_effect_telemetry {
    uint16_t input_count;
    uint16_t emitted_count;
    uint16_t inactive_count;
    uint16_t unresolved_source_count;
    uint16_t stale_count;
    uint16_t unknown_count;
    uint16_t zero_budget_count;
    uint16_t capacity_overflow_count;
} sm64_saturn_actor_effect_telemetry_t;

_Static_assert(sizeof(sm64_saturn_actor_effect_descriptor_t) == 64U,
               "actor effect descriptor ABI changed");
_Static_assert(sizeof(sm64_saturn_actor_effect_output_t) == 16U,
               "actor effect output ABI changed");

bool sm64_saturn_actor_effect_admit(
    const sm64_saturn_actor_instance_snapshot_t *snapshot,
    uint32_t capability_mask, uint8_t material_class,
    uint32_t generation, uint32_t scene_package_generation,
    uint16_t source_order, uint8_t depth_bin,
    sm64_saturn_actor_effect_descriptor_t *descriptor,
    sm64_saturn_actor_effect_telemetry_t *telemetry);

bool sm64_saturn_actor_effect_lower(
    const sm64_saturn_actor_effect_descriptor_t *descriptor,
    sm64_saturn_actor_effect_output_t *output);

/* Master-only. depth_bin is produced by the existing actor meshlet
 * near-plane/depth admission.  Bins are emitted far-to-near and equal-bin
 * items retain source_order.  No worker timer, collision query, global sort,
 * VDP1 write, or VDP2 ownership crosses this boundary. */
bool sm64_saturn_actor_effect_order(
    const sm64_saturn_actor_effect_descriptor_t *descriptors,
    uint16_t descriptor_count, uint32_t generation,
    uint32_t scene_package_generation, uint16_t *ordered_indices,
    uint16_t output_capacity, uint16_t *output_count,
    sm64_saturn_actor_effect_telemetry_t *telemetry);

#endif
