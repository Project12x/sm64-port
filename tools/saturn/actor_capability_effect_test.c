#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_effect.h"

static uint32_t bank_token(const sm64_saturn_actor_instance_snapshot_t *value)
{
    return sm64_saturn_actor_effect_bank_token(
        value->actor_bank_id, value->actor_bank_hash_words);
}

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
    assert(sm64_saturn_actor_effect_lower(
        &descriptor, value.actor_bank_id, bank_token(&value), &output));
    assert(output.actor_bank_id == value.actor_bank_id);
    assert(output.actor_bank_token == bank_token(&value));
    assert(output.vdp1_mode == SM64_SATURN_EFFECT_VDP1_REPLACE);
    assert(output.transparent_pixel_enable == 1U);

    value.opacity = 128U;
    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_TRANSLUCENT,
        SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT, 71U, 19U, 4U, 28U, &descriptor,
        &telemetry));
    assert(sm64_saturn_actor_effect_lower(
        &descriptor, value.actor_bank_id, bank_token(&value), &output));
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
    value.actor_bank_hash_words[0] = 0U;
    assert(!sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA, SM64_SATURN_EFFECT_MATERIAL_CUTOUT,
        71U, 19U, 1U, 3U, &descriptor, &telemetry));
    assert(telemetry.unresolved_source_count == 1U);
    value.actor_bank_hash_words[0] = 1U;
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

static void test_lower_rejects_stale_bank_and_direct_descriptor_mutations(void)
{
    sm64_saturn_actor_instance_snapshot_t value = snapshot();
    sm64_saturn_actor_effect_descriptor_t descriptor;
    sm64_saturn_actor_effect_output_t output;
    sm64_saturn_actor_effect_telemetry_t telemetry;
    uint32_t token = bank_token(&value);
    assert(sm64_saturn_actor_effect_admit(
        &value, SM64_SATURN_ACTOR_CAP_ALPHA,
        SM64_SATURN_EFFECT_MATERIAL_CUTOUT, 71U, 19U, 1U, 3U, &descriptor,
        &telemetry));
    assert(!sm64_saturn_actor_effect_lower(
        &descriptor, value.actor_bank_id, token ^ 1U, &output));
    descriptor.capability_mask |= 1U << 31;
    assert(!sm64_saturn_actor_effect_lower(
        &descriptor, value.actor_bank_id, token, &output));
    descriptor.capability_mask &= ~(1U << 31);
    descriptor.effect_flags |= (uint16_t)(SM64_SATURN_EFFECT_FLAG_MASK + 1U);
    assert(!sm64_saturn_actor_effect_lower(
        &descriptor, value.actor_bank_id, token, &output));
    descriptor.effect_flags = 0U;
    descriptor.opacity = 0U;
    assert(!sm64_saturn_actor_effect_lower(
        &descriptor, value.actor_bank_id, token, &output));
}

int main(void)
{
    test_cutout_and_translucent_modes_are_distinct();
    test_billboard_uses_existing_fixed_point_basis_contract();
    test_shadow_requires_master_resolved_receiver_state();
    test_effect_lifetime_is_consumed_but_never_advanced();
    test_stale_unknown_and_zero_source_fields_fail_closed();
    test_lower_rejects_stale_bank_and_direct_descriptor_mutations();
    return 0;
}
