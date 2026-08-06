#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_effect.h"

static sm64_saturn_actor_instance_snapshot_t snapshot(void)
{
    sm64_saturn_actor_instance_snapshot_t value;
    memset(&value, 0, sizeof(value));
    value.generation = 71U;
    value.scene_package_generation = 19U;
    value.instance_key = 0x00040002U;
    value.actor_bank_id = 9U;
    value.actor_bank_hash_words[0] = 1U;
    value.family_id = 7U;
    value.model_id = 11U;
    value.active = 1U;
    value.render_active = 1U;
    value.opacity = 255U;
    return value;
}

static void test_cutout_and_translucent_modes_are_distinct(void)
{
    sm64_saturn_actor_instance_snapshot_t value = snapshot();
    sm64_saturn_actor_effect_descriptor_t descriptor;
    sm64_saturn_actor_effect_output_t output;
    sm64_saturn_actor_effect_telemetry_t telemetry;

    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA,
        SM64_SATURN_EFFECT_MATERIAL_CUTOUT, 71U, 19U, 3U, 27U, &descriptor,
        &telemetry));
    assert(descriptor.depth_bin == 27U);
    assert(sm64_saturn_actor_effect_lower(&descriptor, &output));
    assert(output.vdp1_mode == SM64_SATURN_EFFECT_VDP1_REPLACE);
    assert(output.transparent_pixel_enable == 1U);

    value.opacity = 128U;
    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_TRANSLUCENT,
        SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT, 71U, 19U, 4U, 28U, &descriptor,
        &telemetry));
    assert(sm64_saturn_actor_effect_lower(&descriptor, &output));
    assert(output.vdp1_mode == SM64_SATURN_EFFECT_VDP1_HALF_TRANSPARENT);
    assert(output.transparent_pixel_enable == 1U);
}

static void test_billboard_uses_existing_fixed_point_basis_contract(void)
{
    sm64_saturn_actor_instance_snapshot_t value = snapshot();
    sm64_saturn_actor_effect_descriptor_t descriptor;
    sm64_saturn_actor_effect_telemetry_t telemetry;

    value.billboard_state = 1U;
    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_BILLBOARD | SM64_SATURN_ACTOR_CAP_ALPHA,
        SM64_SATURN_EFFECT_MATERIAL_CUTOUT, 71U, 19U, 8U, 12U, &descriptor,
        &telemetry));
    assert(descriptor.basis_kind ==
           SM64_SATURN_EFFECT_BASIS_EXISTING_MTXQ_BILLBOARD);

    value.billboard_state = 0U;
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_BILLBOARD,
        SM64_SATURN_EFFECT_MATERIAL_OPAQUE, 71U, 19U, 8U, 12U, &descriptor,
        &telemetry));
    assert(telemetry.unresolved_source_count == 1U);
}

static void test_shadow_requires_master_resolved_receiver_state(void)
{
    sm64_saturn_actor_instance_snapshot_t value = snapshot();
    sm64_saturn_actor_effect_descriptor_t descriptor;
    sm64_saturn_actor_effect_telemetry_t telemetry;

    value.shadow_type = 1U;
    value.shadow_scale = 100U;
    value.shadow_solidity = 180U;
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_SHADOW,
        SM64_SATURN_EFFECT_MATERIAL_SHADOW, 71U, 19U, 2U, 9U, &descriptor,
        &telemetry));
    value.effect_flags = SM64_SATURN_EFFECT_FLAG_RECEIVER_RESOLVED;
    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_SHADOW,
        SM64_SATURN_EFFECT_MATERIAL_SHADOW, 71U, 19U, 2U, 9U, &descriptor,
        &telemetry));
}

static void test_effect_lifetime_is_consumed_but_never_advanced(void)
{
    sm64_saturn_actor_instance_snapshot_t value = snapshot();
    sm64_saturn_actor_effect_descriptor_t descriptor;
    sm64_saturn_actor_effect_telemetry_t telemetry;

    value.effect_kind = 4U;
    value.effect_flags = SM64_SATURN_EFFECT_FLAG_SOURCE_VISIBLE |
                         SM64_SATURN_EFFECT_FLAG_SOURCE_LIVE;
    value.effect_lifetime = 0U;
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_EFFECT,
        SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT, 71U, 19U, 1U, 6U, &descriptor,
        &telemetry));
    value.effect_lifetime = 13U;
    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_EFFECT,
        SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT, 71U, 19U, 1U, 6U, &descriptor,
        &telemetry));
    assert(descriptor.effect_lifetime == 13U);
    assert(value.effect_lifetime == 13U);
}

static void test_stale_unknown_and_zero_source_fields_fail_closed(void)
{
    sm64_saturn_actor_instance_snapshot_t value = snapshot();
    sm64_saturn_actor_effect_descriptor_t descriptor;
    sm64_saturn_actor_effect_telemetry_t telemetry;

    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA, SM64_SATURN_EFFECT_MATERIAL_CUTOUT,
        71U, 19U, 1U, 64U, &descriptor, &telemetry));
    assert(telemetry.unknown_count == 1U);
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA | (1U << 31),
        SM64_SATURN_EFFECT_MATERIAL_CUTOUT, 71U, 19U, 1U, 3U, &descriptor, &telemetry));
    assert(telemetry.unknown_count == 1U);
    value.effect_flags = (uint16_t)(SM64_SATURN_EFFECT_FLAG_MASK + 1U);
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA, SM64_SATURN_EFFECT_MATERIAL_CUTOUT,
        71U, 19U, 1U, 3U, &descriptor, &telemetry));
    assert(telemetry.unknown_count == 1U);
    value.effect_flags = 0U;
    value.scene_package_generation++;
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA, SM64_SATURN_EFFECT_MATERIAL_CUTOUT,
        71U, 19U, 1U, 3U, &descriptor, &telemetry));
    assert(telemetry.stale_count == 1U);
    value.scene_package_generation--;
    value.opacity = 0U;
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA, SM64_SATURN_EFFECT_MATERIAL_CUTOUT,
        71U, 19U, 1U, 3U, &descriptor, &telemetry));
    assert(telemetry.unresolved_source_count == 1U);
}

int main(void)
{
    test_cutout_and_translucent_modes_are_distinct();
    test_billboard_uses_existing_fixed_point_basis_contract();
    test_shadow_requires_master_resolved_receiver_state();
    test_effect_lifetime_is_consumed_but_never_advanced();
    test_stale_unknown_and_zero_source_fields_fail_closed();
    return 0;
}
