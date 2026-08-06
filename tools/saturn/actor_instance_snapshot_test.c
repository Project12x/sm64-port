#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_instance.h"

static sm64_saturn_actor_source_observation_t observation(
    uint32_t generation, uint16_t slot)
{
    sm64_saturn_actor_source_observation_t value;
    uint16_t i;
    memset(&value, 0, sizeof(value));
    value.source_generation = generation;
    value.scene_package_generation = 91U;
    value.actor_bank_id = 17U;
    for (i = 0U; i < 8U; i++) value.actor_bank_hash_words[i] = 0x1100U + i;
    value.pool_slot = slot;
    value.family_id = 3U;
    value.model_id = SM64_SATURN_ACTOR_INSTANCE_MODEL_NONE;
    value.parent_index = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
    value.parent_node_ordinal = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
    value.position_q16[0] = 0x10000;
    value.position_q16[1] = 0x20000;
    value.position_q16[2] = 0x30000;
    value.scale_q16[0] = value.scale_q16[1] = value.scale_q16[2] = 0x10000;
    value.held_offset_q16[0] = 0x4000;
    value.held_offset_q16[1] = 0x5000;
    value.held_offset_q16[2] = 0x6000;
    value.draw_distance_q16 = 0x70000;
    value.render_range_min_q16 = 0x1000;
    value.render_range_max_q16 = 0x80000;
    value.angle[0] = 1;
    value.angle[1] = 2;
    value.angle[2] = 3;
    value.animation_id = 27;
    value.animation_frame = 8;
    value.animation_accel = 0x12000;
    value.anim_state = 4;
    value.effect_params_q16[0] = 0x9000;
    value.effect_params_q16[1] = 0xa000;
    value.effect_params_q16[2] = 0xb000;
    value.effect_params_q16[3] = 0xc000;
    value.effect_lifetime = 13U;
    value.opacity = 201U;
    value.render_range_state = 2U;
    value.billboard_state = 1U;
    value.shadow_type = 4U;
    value.shadow_scale = 55U;
    value.shadow_solidity = 77U;
    value.effect_kind = 9U;
    value.effect_flags = 0x33U;
    value.area_index = 1;
    value.active = 1U;
    value.render_active = 1U;
    value.switch_count = 2U;
    value.switch_state[0] = 0x1234U;
    value.switch_state[1] = 0x4321U;
    return value;
}

static void test_all_typed_fields_and_model_none(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = observation(5U, 2U);
    sm64_saturn_actor_instance_snapshot_t destination;
    sm64_saturn_actor_capture_telemetry_t telemetry;
    uint16_t count = 0U;

    sm64_saturn_geo_state_observer_init(&observer, 8U);
    sm64_saturn_actor_instances_set_observer(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 5U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_record_switch(&observer, 3U, 0x7777U));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    assert(sm64_saturn_actor_instances_capture(&destination, 1U, 5U, &count,
                                               &telemetry));
    assert(count == 1U && telemetry.published_count == 1U);
    assert(destination.model_id == SM64_SATURN_ACTOR_INSTANCE_MODEL_NONE);
    assert(destination.family_id == source.family_id);
    assert(destination.position_q16[1] == source.position_q16[1]);
    assert(destination.held_offset_q16[2] == source.held_offset_q16[2]);
    assert(destination.render_range_min_q16 == source.render_range_min_q16);
    assert(destination.render_range_max_q16 == source.render_range_max_q16);
    assert(destination.draw_distance_q16 == source.draw_distance_q16);
    assert(destination.opacity == source.opacity);
    assert(destination.billboard_state == source.billboard_state);
    assert(destination.shadow_solidity == source.shadow_solidity);
    assert(destination.effect_kind == source.effect_kind);
    assert(destination.effect_lifetime == source.effect_lifetime);
    assert(destination.switch_count == 4U);
    assert(destination.switch_state[3] == 0x7777U);
}

static void test_mutation_is_observed_and_parent_is_pointer_free(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = observation(7U, 1U);
    sm64_saturn_actor_instance_snapshot_t first, second;
    sm64_saturn_actor_capture_telemetry_t telemetry;
    uint16_t count;

    source.parent_index = 4U;
    source.parent_node_ordinal = 12U;
    sm64_saturn_geo_state_observer_init(&observer, 8U);
    sm64_saturn_actor_instances_set_observer(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 7U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    assert(sm64_saturn_actor_instances_capture(&first, 1U, 7U, &count, &telemetry));
    source.position_q16[0] += 0x10000;
    source.effect_params_q16[2] += 0x1000;
    sm64_saturn_geo_state_observer_end_frame(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 8U);
    source.source_generation = 8U;
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    assert(sm64_saturn_actor_instances_capture(&second, 1U, 8U, &count, &telemetry));
    assert(second.position_q16[0] != first.position_q16[0]);
    assert(second.effect_params_q16[2] != first.effect_params_q16[2]);
    assert(second.parent_index == 4U && second.parent_node_ordinal == 12U);
    assert(offsetof(sm64_saturn_actor_instance_snapshot_t, instance_key) == 8U);
}

static void test_identity_reuse_and_despawn(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = observation(10U, 6U);
    sm64_saturn_actor_instance_snapshot_t first, second;
    sm64_saturn_actor_capture_telemetry_t telemetry;
    uint16_t count;

    sm64_saturn_geo_state_observer_init(&observer, 8U);
    sm64_saturn_actor_instances_set_observer(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 10U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    assert(sm64_saturn_actor_instances_capture(&first, 1U, 10U, &count, &telemetry));
    sm64_saturn_geo_state_observer_end_frame(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 11U);
    source.source_generation = 11U;
    assert(sm64_saturn_actor_instances_capture(&second, 1U, 11U, &count, &telemetry));
    assert(count == 0U);
    sm64_saturn_geo_state_observer_end_frame(&observer);
    assert(sm64_saturn_actor_instances_capture(&second, 1U, 11U, &count,
                                               &telemetry));
    assert(count == 0U);
    assert(telemetry.despawned_count == 1U);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 12U);
    source.source_generation = 12U;
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    assert(sm64_saturn_actor_instances_capture(&second, 1U, 12U, &count, &telemetry));
    assert(count == 1U && second.instance_key != first.instance_key);
    assert(telemetry.pool_reuse_count == 1U);
}

static void test_fail_closed_cases(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = observation(15U, 0U);
    sm64_saturn_actor_instance_snapshot_t output[2];
    sm64_saturn_actor_capture_telemetry_t telemetry;
    uint16_t count;

    sm64_saturn_geo_state_observer_init(&observer, 8U);
    sm64_saturn_actor_instances_set_observer(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 15U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    source.family_id = 0U;
    observer.observations[0].family_id = 0U;
    assert(sm64_saturn_actor_instances_capture(output, 2U, 15U, &count, &telemetry));
    assert(count == 0U && telemetry.unknown_family_count == 1U);
    observer.observations[0] = observation(14U, 0U);
    assert(sm64_saturn_actor_instances_capture(output, 2U, 15U, &count, &telemetry));
    assert(count == 0U && telemetry.stale_generation_count == 1U);
    observer.observations[0] = observation(15U, 0U);
    observer.observations[0].render_range_min_q16 = 4;
    observer.observations[0].render_range_max_q16 = 3;
    assert(sm64_saturn_actor_instances_capture(output, 2U, 15U, &count, &telemetry));
    assert(count == 0U && telemetry.malformed_count == 1U);
    observer.observations[0] = observation(15U, 0U);
    observer.observations[0].scale_q16[0] = 0;
    assert(sm64_saturn_actor_instances_capture(output, 2U, 15U, &count, &telemetry));
    assert(count == 0U && telemetry.malformed_count == 1U);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 15U);
    source = observation(15U, 0U);
    source.active = 0U;
    assert(!sm64_saturn_geo_state_observer_begin_object(&observer, &source));
}

static void test_capacity_and_two_bank_lifecycle(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = observation(20U, 0U);
    sm64_saturn_actor_instance_snapshot_t output[1];
    sm64_saturn_actor_capture_telemetry_t telemetry;
    sm64_saturn_actor_instance_bank_t bank;
    uint16_t count;
    uint8_t index0, index1, first_index;

    assert(sizeof(bank) + sizeof(observer) <= 0x00100000U);

    sm64_saturn_geo_state_observer_init(&observer, 2U);
    sm64_saturn_actor_instances_set_observer(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 20U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    source.pool_slot = 1U;
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(!sm64_saturn_actor_instances_capture(output, 1U, 20U, &count, &telemetry));
    assert(count == 0U && telemetry.capacity_overflow_count == 1U);
    sm64_saturn_actor_instance_bank_init(&bank);
    assert(sm64_saturn_actor_instance_bank_begin_write(&bank, 1U, &index0));
    first_index = index0;
    assert(sm64_saturn_actor_instance_bank_begin_write(&bank, 2U, &index1));
    assert(index0 != index1);
    assert(!sm64_saturn_actor_instance_bank_begin_write(&bank, 3U, &index0));
    memcpy(bank.snapshots[first_index], output, sizeof(output));
    assert(sm64_saturn_actor_instance_bank_publish(&bank, first_index, 1U, 1U));
    assert(sm64_saturn_actor_instance_bank_acquire(&bank, first_index, 1U, &count) != NULL);
    assert(count == 1U);
    assert(sm64_saturn_actor_instance_bank_complete(&bank, first_index));
    assert(sm64_saturn_actor_instance_bank_retire(&bank, first_index));
    assert(sm64_saturn_actor_instance_bank_quarantine(&bank, 2U));
    assert(!sm64_saturn_actor_instance_bank_acquire(&bank, index1, 2U, &count));

    sm64_saturn_actor_instance_bank_init(&bank);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 21U);
    source = observation(21U, 0U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    sm64_saturn_actor_instances_set_observer(&observer);
    assert(sm64_saturn_actor_instance_bank_capture(
        &bank, 21U, 2U, &index0, &count, &telemetry));
    assert(count == 1U && bank.state[index0] ==
           SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
}

static void test_observer_overflow_latches_and_bounds_fail_closed(void)
{
    sm64_saturn_geo_state_observer_t observer;
    sm64_saturn_actor_source_observation_t source = observation(30U, 0U);
    sm64_saturn_actor_instance_snapshot_t output;
    sm64_saturn_actor_capture_telemetry_t telemetry;
    uint16_t count;

    sm64_saturn_geo_state_observer_init(&observer, 1U);
    sm64_saturn_actor_instances_set_observer(&observer);
    sm64_saturn_geo_state_observer_begin_frame(&observer, 30U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(sm64_saturn_geo_state_observer_end_object(&observer));
    assert(!sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(observer.overflow_latched != 0U);
    assert(!sm64_saturn_actor_instances_capture(
        &output, 1U, 30U, &count, &telemetry));
    assert(count == 0U && telemetry.capacity_overflow_count == 1U);

    sm64_saturn_geo_state_observer_begin_frame(&observer, 31U);
    source = observation(31U, 0U);
    assert(sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    observer.observations[0].pool_slot = observer.capacity;
    assert(sm64_saturn_actor_instances_capture(
        &output, 1U, 31U, &count, &telemetry));
    assert(count == 0U && telemetry.malformed_count == 1U);

    sm64_saturn_geo_state_observer_begin_frame(&observer, 32U);
    source = observation(32U, observer.capacity);
    assert(!sm64_saturn_geo_state_observer_begin_object(&observer, &source));
    assert(observer.pool_slot_overflow_count == 1U);
    assert(sm64_saturn_actor_instances_capture(
        &output, 1U, 32U, &count, &telemetry));
    assert(count == 0U && telemetry.pool_slot_overflow_count == 1U);
}

static void test_bank_rejects_stale_and_duplicate_generations(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    uint8_t first, second;

    sm64_saturn_actor_instance_bank_init(&bank);
    assert(sm64_saturn_actor_instance_bank_begin_write(&bank, 40U, &first));
    assert(!sm64_saturn_actor_instance_bank_begin_write(&bank, 40U, &second));
    assert(sm64_saturn_actor_instance_bank_publish(&bank, first, 0U, 40U));
    assert(!sm64_saturn_actor_instance_bank_begin_write(&bank, 40U, &second));
    assert(sm64_saturn_actor_instance_bank_begin_write(&bank, 41U, &second));
    assert(!sm64_saturn_actor_instance_bank_publish(&bank, second, 0U, 40U));
}

int main(void)
{
    test_all_typed_fields_and_model_none();
    test_mutation_is_observed_and_parent_is_pointer_free();
    test_identity_reuse_and_despawn();
    test_fail_closed_cases();
    test_capacity_and_two_bank_lifecycle();
    test_observer_overflow_latches_and_bounds_fail_closed();
    test_bank_rejects_stale_and_duplicate_generations();
    return 0;
}
