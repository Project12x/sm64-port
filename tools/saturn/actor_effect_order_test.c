#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_effect.h"

static sm64_saturn_actor_effect_descriptor_t item(
    uint16_t source_order, uint8_t depth_bin)
{
    sm64_saturn_actor_effect_descriptor_t value;
    memset(&value, 0, sizeof(value));
    value.generation = 41U;
    value.scene_package_generation = 6U;
    value.instance_key = 0x10000U + source_order;
    value.source_order = source_order;
    value.depth_bin = depth_bin;
    value.material_class = SM64_SATURN_EFFECT_MATERIAL_TRANSLUCENT;
    value.vdp1_mode = SM64_SATURN_EFFECT_VDP1_HALF_TRANSPARENT;
    return value;
}

static void test_far_to_near_bins_preserve_equal_depth_source_order(void)
{
    sm64_saturn_actor_effect_descriptor_t descriptors[5] = {
        item(4U, 8U), item(1U, 30U), item(3U, 8U), item(0U, 63U),
        item(2U, 30U),
    };
    uint16_t order[5], count = 0U;
    sm64_saturn_actor_effect_telemetry_t telemetry;
    assert(sm64_saturn_actor_effect_order(
        descriptors, 5U, 41U, 6U, order, 5U, &count, &telemetry));
    assert(count == 5U);
    assert(order[0] == 3U);
    assert(order[1] == 1U && order[2] == 4U);
    assert(order[3] == 2U && order[4] == 0U);
}

static void test_budget_overflow_is_atomic_and_counted(void)
{
    sm64_saturn_actor_effect_descriptor_t descriptors[2] = {
        item(0U, 1U), item(1U, 2U),
    };
    uint16_t order[2] = {0xaaaaU, 0xbbbbU}, count = 9U;
    sm64_saturn_actor_effect_telemetry_t telemetry;
    assert(!sm64_saturn_actor_effect_order(
        descriptors, 2U, 41U, 6U, order, 0U, &count, &telemetry));
    assert(count == 0U && telemetry.zero_budget_count == 1U);
    assert(order[0] == 0xaaaaU && order[1] == 0xbbbbU);
    assert(!sm64_saturn_actor_effect_order(
        descriptors, 2U, 41U, 6U, order, 1U, &count, &telemetry));
    assert(count == 0U && telemetry.capacity_overflow_count == 1U);
    assert(order[0] == 0xaaaaU && order[1] == 0xbbbbU);
}

static void test_stale_and_unknown_descriptor_reject_the_whole_order(void)
{
    sm64_saturn_actor_effect_descriptor_t descriptors[2] = {
        item(0U, 1U), item(1U, 2U),
    };
    uint16_t order[2], count;
    sm64_saturn_actor_effect_telemetry_t telemetry;
    descriptors[1].generation = 40U;
    assert(!sm64_saturn_actor_effect_order(
        descriptors, 2U, 41U, 6U, order, 2U, &count, &telemetry));
    assert(count == 0U && telemetry.stale_count == 1U);
    descriptors[1] = item(1U, 2U);
    descriptors[1].material_class = 255U;
    assert(!sm64_saturn_actor_effect_order(
        descriptors, 2U, 41U, 6U, order, 2U, &count, &telemetry));
    assert(count == 0U && telemetry.unknown_count == 1U);
}

static void test_fixed_descriptor_capacity_rejects_atomically(void)
{
    sm64_saturn_actor_effect_descriptor_t
        descriptors[SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY + 1U];
    uint16_t ordered[SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY + 1U];
    uint16_t count = 9U;
    sm64_saturn_actor_effect_telemetry_t telemetry;
    for (uint16_t index = 0U;
         index < SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY + 1U; index++) {
        descriptors[index] = item(index, (uint8_t)(index % 64U));
        ordered[index] = 0xaaaaU;
    }
    assert(!sm64_saturn_actor_effect_order(
        descriptors, SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY + 1U, 41U, 6U,
        ordered, SM64_SATURN_EFFECT_DESCRIPTOR_CAPACITY + 1U, &count,
        &telemetry));
    assert(count == 0U && telemetry.capacity_overflow_count == 1U);
    assert(ordered[0] == 0xaaaaU);
}

int main(void)
{
    test_far_to_near_bins_preserve_equal_depth_source_order();
    test_budget_overflow_is_atomic_and_counted();
    test_stale_and_unknown_descriptor_reject_the_whole_order();
    test_fixed_descriptor_capacity_rejects_atomically();
    return 0;
}
