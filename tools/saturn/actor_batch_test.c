#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_batch.h"

static sm64_saturn_actor_instance_snapshot_t instance(
    uint32_t key, uint16_t family, uint16_t model)
{
    sm64_saturn_actor_instance_snapshot_t value;
    memset(&value, 0, sizeof(value));
    value.generation = 51U;
    value.scene_package_generation = 18U;
    value.instance_key = key;
    value.actor_bank_id = 80U;
    value.actor_bank_hash_words[0] = 1U;
    value.family_id = family;
    value.model_id = model;
    value.active = 1U;
    value.render_active = 1U;
    return value;
}

static sm64_saturn_actor_instance_descriptor_t job(
    const sm64_saturn_actor_instance_snapshot_t *value, uint16_t index,
    uint16_t material, uint8_t output_class)
{
    sm64_saturn_actor_instance_descriptor_t descriptor;
    assert(sm64_saturn_actor_instance_descriptor_from_snapshot(
        value, index, index, material, output_class,
        (uint16_t)(index * 4U), 4U, &descriptor));
    return descriptor;
}

static void complete_all(
    sm64_saturn_actor_instance_queue_t *queue,
    sm64_saturn_actor_instance_snapshot_t *instances, uint16_t count)
{
    uint16_t index;
    for (index = 0U; index < count; index++) {
        uint16_t claimed;
        const uint8_t lane = (index & 1U) == 0U
            ? SM64_SATURN_ACTOR_CLAIMED_MASTER
            : SM64_SATURN_ACTOR_CLAIMED_SLAVE;
        assert((lane == SM64_SATURN_ACTOR_CLAIMED_MASTER
            ? sm64_saturn_actor_instance_queue_claim_master(queue, 51U, &claimed)
            : sm64_saturn_actor_instance_queue_claim_slave(queue, 51U, &claimed)));
        assert(claimed == index);
        assert(sm64_saturn_actor_instance_queue_complete(
            queue, 51U, index, lane, &instances[index],
            (uint16_t)(index + 1U)));
    }
}

static void test_batches_compatible_instances_without_reordering(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[4] = {
        instance(0x00010001U, 10U, 20U),
        instance(0x00010002U, 10U, 20U),
        instance(0x00010003U, 10U, 20U),
        instance(0x00010004U, 10U, 20U),
    };
    sm64_saturn_actor_instance_descriptor_t jobs[4] = {
        job(&instances[0], 0U, 3U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE),
        job(&instances[1], 1U, 3U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE),
        job(&instances[2], 2U, 4U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE),
        job(&instances[3], 3U, 4U, SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT),
    };
    sm64_saturn_actor_batch_t batches[4];
    sm64_saturn_actor_batch_summary_t summary;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 51U, 4U, 16U, jobs, 4U));
    assert(!sm64_saturn_actor_batches_build(
        &queue, 51U, batches, 4U, &summary));
    complete_all(&queue, instances, 4U);
    assert(sm64_saturn_actor_batches_build(
        &queue, 51U, batches, 4U, &summary));
    assert(summary.batch_count == 3U);
    assert(summary.completed_instance_count == 4U);
    assert(summary.quarantined_instance_count == 0U);
    assert(summary.output_record_count == 10U);
    assert(batches[0].first_descriptor == 0U &&
           batches[0].descriptor_count == 2U);
    assert(batches[1].first_descriptor == 2U &&
           batches[1].descriptor_count == 1U);
    assert(batches[2].first_descriptor == 3U &&
           batches[2].descriptor_count == 1U);
    assert(batches[0].material_id == 3U && batches[1].material_id == 4U);
    assert(batches[2].output_class == SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT);
}

static void test_quarantine_removes_only_failed_instance(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[3] = {
        instance(0x00020001U, 12U, 22U),
        instance(0x00020002U, 12U, 22U),
        instance(0x00020003U, 12U, 22U),
    };
    sm64_saturn_actor_instance_descriptor_t jobs[3] = {
        job(&instances[0], 0U, 5U, SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT),
        job(&instances[1], 1U, 5U, SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT),
        job(&instances[2], 2U, 5U, SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT),
    };
    sm64_saturn_actor_batch_t batches[3];
    sm64_saturn_actor_batch_summary_t summary;
    uint16_t claimed;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 51U, 3U, 12U, jobs, 3U));
    assert(sm64_saturn_actor_instance_queue_claim_master(&queue, 51U, &claimed));
    assert(sm64_saturn_actor_instance_queue_complete(
        &queue, 51U, claimed, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instances[claimed], 1U));
    assert(sm64_saturn_actor_instance_queue_claim_slave(&queue, 51U, &claimed));
    assert(sm64_saturn_actor_instance_queue_fail(
        &queue, 51U, claimed, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE));
    assert(sm64_saturn_actor_instance_queue_claim_master(&queue, 51U, &claimed));
    assert(sm64_saturn_actor_instance_queue_complete(
        &queue, 51U, claimed, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instances[claimed], 3U));

    assert(sm64_saturn_actor_batches_build(
        &queue, 51U, batches, 3U, &summary));
    assert(summary.completed_instance_count == 2U);
    assert(summary.quarantined_instance_count == 1U);
    assert(summary.batch_count == 2U);
    assert(batches[0].first_descriptor == 0U);
    assert(batches[1].first_descriptor == 2U);
}

static void test_runtime_storage_exactly_fits_the_actor_lwram_budget(void)
{
    assert(_Alignof(sm64_saturn_actor_runtime_storage_t) == 16U);
    assert(sizeof(sm64_saturn_actor_runtime_storage_t) == 65536U);
    assert(sizeof(((sm64_saturn_actor_runtime_storage_t *)0)->outputs) ==
           22448U);
}

int main(void)
{
    test_batches_compatible_instances_without_reordering();
    test_quarantine_removes_only_failed_instance();
    test_runtime_storage_exactly_fits_the_actor_lwram_budget();
    return 0;
}
