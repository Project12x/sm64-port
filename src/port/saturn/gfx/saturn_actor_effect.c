#include "saturn_actor_effect.h"

#include <limits.h>
#include <string.h>

static bool material_valid(uint8_t material)
{
    return material >= SM64_SATURN_EFFECT_MATERIAL_OPAQUE &&
           material <= SM64_SATURN_EFFECT_MATERIAL_SHADOW;
}

static bool material_matches(uint32_t capabilities, uint8_t material)
{
    switch (material) {
        case SM64_SATURN_EFFECT_MATERIAL_OPAQUE:
            return (capabilities & (SM64_SATURN_ACTOR_CAP_ALPHA |
                                    SM64_SATURN_ACTOR_CAP_TRANSLUCENT |
                                    SM64_SATURN_ACTOR_CAP_SHADOW)) == 0U;
        case SM64_SATURN_EFFECT_MATERIAL_CUTOUT:
            return (capabilities & SM64_SATURN_ACTOR_CAP_ALPHA) != 0U;
        case SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT:
        case SM64_SATURN_EFFECT_MATERIAL_DECAL:
            return (capabilities & (SM64_SATURN_ACTOR_CAP_TRANSLUCENT |
                                    SM64_SATURN_ACTOR_CAP_EFFECT |
                                    SM64_SATURN_ACTOR_CAP_PARTICLE)) != 0U;
        case SM64_SATURN_EFFECT_MATERIAL_SHADOW:
            return (capabilities & SM64_SATURN_ACTOR_CAP_SHADOW) != 0U;
        default:
            return false;
    }
}

static uint8_t material_mode(uint8_t material)
{
    return (material == SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT ||
            material == SM64_SATURN_EFFECT_MATERIAL_DECAL ||
            material == SM64_SATURN_EFFECT_MATERIAL_SHADOW)
        ? SM64_SATURN_EFFECT_VDP1_HALF_TRANSPARENT
        : SM64_SATURN_EFFECT_VDP1_REPLACE;
}

bool sm64_saturn_actor_effect_admit(
    const sm64_saturn_actor_instance_snapshot_t *snapshot,
    uint32_t capability_mask, uint8_t material_class,
    uint32_t generation, uint32_t scene_package_generation,
    uint16_t source_order, uint8_t depth_bin,
    sm64_saturn_actor_effect_descriptor_t *descriptor,
    sm64_saturn_actor_effect_telemetry_t *telemetry)
{
    if (telemetry != NULL) memset(telemetry, 0, sizeof(*telemetry));
    if (snapshot == NULL || descriptor == NULL || telemetry == NULL ||
        generation == 0U || scene_package_generation == 0U)
        return false;
    telemetry->input_count = 1U;
    if (snapshot->generation != generation ||
        snapshot->scene_package_generation != scene_package_generation) {
        telemetry->stale_count = 1U;
        return false;
    }
    if ((capability_mask & ~SM64_SATURN_ACTOR_CAPABILITY_MASK) != 0U ||
        (snapshot->effect_flags & ~SM64_SATURN_EFFECT_FLAG_MASK) != 0U ||
        !material_valid(material_class) ||
        depth_bin >= SM64_SATURN_EFFECT_DEPTH_BIN_COUNT) {
        telemetry->unknown_count = 1U;
        return false;
    }
    if (snapshot->active == 0U || snapshot->render_active == 0U) {
        telemetry->inactive_count = 1U;
        return false;
    }
    if (snapshot->actor_bank_id == 0U || snapshot->family_id == 0U ||
        snapshot->model_id == SM64_SATURN_ACTOR_INSTANCE_MODEL_NONE ||
        snapshot->opacity == 0U || !material_matches(capability_mask,
                                                     material_class)) {
        telemetry->unresolved_source_count = 1U;
        return false;
    }
    if ((capability_mask & SM64_SATURN_ACTOR_CAP_BILLBOARD) != 0U &&
        snapshot->billboard_state == 0U) {
        telemetry->unresolved_source_count = 1U;
        return false;
    }
    if ((capability_mask & SM64_SATURN_ACTOR_CAP_SHADOW) != 0U &&
        (snapshot->shadow_type == 0U || snapshot->shadow_scale == 0U ||
         snapshot->shadow_solidity == 0U ||
         (snapshot->effect_flags &
          SM64_SATURN_EFFECT_FLAG_RECEIVER_RESOLVED) == 0U)) {
        telemetry->unresolved_source_count = 1U;
        return false;
    }
    if ((capability_mask & (SM64_SATURN_ACTOR_CAP_EFFECT |
                            SM64_SATURN_ACTOR_CAP_PARTICLE)) != 0U &&
        (snapshot->effect_kind == 0U || snapshot->effect_lifetime == 0U ||
         (snapshot->effect_flags & (SM64_SATURN_EFFECT_FLAG_SOURCE_VISIBLE |
                                    SM64_SATURN_EFFECT_FLAG_SOURCE_LIVE)) !=
             (SM64_SATURN_EFFECT_FLAG_SOURCE_VISIBLE |
              SM64_SATURN_EFFECT_FLAG_SOURCE_LIVE))) {
        telemetry->unresolved_source_count = 1U;
        return false;
    }

    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->generation = generation;
    descriptor->scene_package_generation = scene_package_generation;
    descriptor->instance_key = snapshot->instance_key;
    descriptor->capability_mask = capability_mask;
    memcpy(descriptor->effect_params_q16, snapshot->effect_params_q16,
           sizeof(descriptor->effect_params_q16));
    descriptor->effect_lifetime = snapshot->effect_lifetime;
    descriptor->source_order = source_order;
    descriptor->family_id = snapshot->family_id;
    descriptor->model_id = snapshot->model_id;
    descriptor->opacity = snapshot->opacity;
    descriptor->billboard_state = snapshot->billboard_state;
    descriptor->shadow_type = snapshot->shadow_type;
    descriptor->shadow_scale = snapshot->shadow_scale;
    descriptor->shadow_solidity = snapshot->shadow_solidity;
    descriptor->effect_kind = snapshot->effect_kind;
    descriptor->effect_flags = snapshot->effect_flags;
    descriptor->material_class = material_class;
    descriptor->basis_kind = snapshot->billboard_state != 0U
        ? SM64_SATURN_EFFECT_BASIS_EXISTING_MTXQ_BILLBOARD
        : SM64_SATURN_EFFECT_BASIS_MODEL;
    descriptor->vdp1_mode = material_mode(material_class);
    descriptor->depth_bin = depth_bin;
    telemetry->emitted_count = 1U;
    return true;
}

bool sm64_saturn_actor_effect_lower(
    const sm64_saturn_actor_effect_descriptor_t *descriptor,
    sm64_saturn_actor_effect_output_t *output)
{
    if (descriptor == NULL || output == NULL ||
        !material_valid(descriptor->material_class) ||
        descriptor->vdp1_mode != material_mode(descriptor->material_class) ||
        descriptor->basis_kind >
            SM64_SATURN_EFFECT_BASIS_EXISTING_MTXQ_BILLBOARD)
        return false;
    memset(output, 0, sizeof(*output));
    output->instance_key = descriptor->instance_key;
    output->source_order = descriptor->source_order;
    output->opacity = descriptor->opacity;
    output->effect_kind = descriptor->effect_kind;
    output->effect_flags = descriptor->effect_flags;
    output->material_class = descriptor->material_class;
    output->basis_kind = descriptor->basis_kind;
    output->vdp1_mode = descriptor->vdp1_mode;
    output->transparent_pixel_enable =
        descriptor->material_class == SM64_SATURN_EFFECT_MATERIAL_OPAQUE
            ? 0U : 1U;
    return true;
}

bool sm64_saturn_actor_effect_order(
    const sm64_saturn_actor_effect_descriptor_t *descriptors,
    uint16_t descriptor_count, uint32_t generation,
    uint32_t scene_package_generation, uint16_t *ordered_indices,
    uint16_t output_capacity, uint16_t *output_count,
    sm64_saturn_actor_effect_telemetry_t *telemetry)
{
    uint16_t written = 0U;
    int bin;
    if (output_count != NULL) *output_count = 0U;
    if (telemetry != NULL) memset(telemetry, 0, sizeof(*telemetry));
    if (telemetry == NULL || output_count == NULL || generation == 0U ||
        scene_package_generation == 0U ||
        (descriptor_count != 0U &&
         (descriptors == NULL || ordered_indices == NULL)))
        return false;
    telemetry->input_count = descriptor_count;
    if (descriptor_count == 0U) return true;
    if (output_capacity == 0U) {
        telemetry->zero_budget_count = 1U;
        return false;
    }
    if (descriptor_count > output_capacity ||
        descriptor_count > SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY) {
        telemetry->capacity_overflow_count = 1U;
        return false;
    }
    for (uint16_t index = 0U; index < descriptor_count; index++) {
        const sm64_saturn_actor_effect_descriptor_t *item = &descriptors[index];
        if (item->generation != generation ||
            item->scene_package_generation != scene_package_generation) {
            telemetry->stale_count++;
            return false;
        }
        if (!material_valid(item->material_class) ||
            item->depth_bin >= SM64_SATURN_EFFECT_DEPTH_BIN_COUNT ||
            item->vdp1_mode != material_mode(item->material_class)) {
            telemetry->unknown_count++;
            return false;
        }
    }
    for (bin = (int)SM64_SATURN_EFFECT_DEPTH_BIN_COUNT - 1; bin >= 0; bin--) {
        uint16_t emitted_in_bin = 0U;
        for (;;) {
            uint16_t best = UINT16_MAX;
            uint16_t best_source = UINT16_MAX;
            for (uint16_t index = 0U; index < descriptor_count; index++) {
                const sm64_saturn_actor_effect_descriptor_t *item =
                    &descriptors[index];
                bool already = false;
                if ((int)item->depth_bin != bin) continue;
                for (uint16_t out = written - emitted_in_bin; out < written;
                     out++) {
                    if (ordered_indices[out] == index) already = true;
                }
                if (!already && (best == UINT16_MAX ||
                    item->source_order < best_source)) {
                    best = index;
                    best_source = item->source_order;
                }
            }
            if (best == UINT16_MAX) break;
            ordered_indices[written++] = best;
            emitted_in_bin++;
        }
    }
    *output_count = written;
    telemetry->emitted_count = written;
    return written == descriptor_count;
}
