#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_instance_queue.h"

static sm64_saturn_actor_instance_snapshot_t snapshot(
    uint32_t generation, uint32_t package_generation, uint32_t instance_key,
    uint32_t bank_id, uint16_t family_id, uint16_t model_id)
{
    sm64_saturn_actor_instance_snapshot_t value;
    memset(&value, 0, sizeof(value));
    value.generation = generation;
    value.scene_package_generation = package_generation;
    value.instance_key = instance_key;
    value.actor_bank_id = bank_id;
    value.actor_bank_hash_words[0] = 0x12345678U;
    value.actor_bank_hash_words[7] = 0x87654321U;
    value.family_id = family_id;
    value.model_id = model_id;
    value.active = 1U;
    value.render_active = 1U;
    return value;
}

static sm64_saturn_actor_instance_descriptor_t descriptor(
    const sm64_saturn_actor_instance_snapshot_t *instance,
    uint16_t snapshot_index, uint16_t source_order, uint16_t material_id,
    uint8_t output_class, uint16_t output_offset, uint16_t output_capacity)
{
    sm64_saturn_actor_instance_descriptor_t value;
    assert(sm64_saturn_actor_instance_descriptor_from_snapshot(
        instance, snapshot_index, source_order, material_id, output_class,
        output_offset, output_capacity, &value));
    return value;
}

static void test_zero_instances_publish_as_terminal_generation(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 9U, 4U, 32U, NULL, 0U));
    assert(sm64_saturn_actor_instance_queue_all_terminal(&queue, 9U));
    assert(sm64_saturn_actor_instance_queue_reset_retired(&queue, 9U));
}

static void test_master_and_slave_claim_each_descriptor_exactly_once(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[3] = {
        snapshot(12U, 7U, 0x00010001U, 44U, 10U, 20U),
        snapshot(12U, 7U, 0x00010002U, 44U, 10U, 20U),
        snapshot(12U, 7U, 0x00020003U, 45U, 11U, 21U),
    };
    sm64_saturn_actor_instance_descriptor_t jobs[3] = {
        descriptor(&instances[0], 0U, 0U, 3U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 4U),
        descriptor(&instances[1], 1U, 1U, 3U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 4U, 5U),
        descriptor(&instances[2], 2U, 2U, 4U,
                   SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT, 9U, 6U),
    };
    uint16_t first = UINT16_MAX, second = UINT16_MAX, third = UINT16_MAX;
    uint16_t rejected = UINT16_MAX;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 12U, 3U, 15U, jobs, 3U));
    assert(sm64_saturn_actor_instance_queue_claim_master(
        &queue, 12U, &first));
    assert(sm64_saturn_actor_instance_queue_claim_slave(
        &queue, 12U, &second));
    assert(sm64_saturn_actor_instance_queue_claim_master(
        &queue, 12U, &third));
    assert(first == 0U && second == 1U && third == 2U);
    assert(!sm64_saturn_actor_instance_queue_claim_slave(
        &queue, 12U, &rejected));
    assert(!sm64_saturn_actor_instance_queue_claim_master(
        &queue, 11U, &rejected));

    assert(sm64_saturn_actor_instance_queue_complete(
        &queue, 12U, first, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instances[first], 3U));
    assert(sm64_saturn_actor_instance_queue_complete(
        &queue, 12U, second, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        &instances[second], 5U));
    assert(sm64_saturn_actor_instance_queue_complete(
        &queue, 12U, third, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instances[third], 6U));
    assert(sm64_saturn_actor_instance_queue_all_terminal(&queue, 12U));
    assert(sm64_saturn_actor_instance_queue_result(&queue, 12U, first)->lane ==
           SM64_SATURN_ACTOR_CLAIMED_MASTER);
    assert(sm64_saturn_actor_instance_queue_result(&queue, 12U, second)->lane ==
           SM64_SATURN_ACTOR_CLAIMED_SLAVE);
}

static void test_stale_identity_quarantines_only_its_claim(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[2] = {
        snapshot(22U, 8U, 0x00010005U, 50U, 12U, 30U),
        snapshot(22U, 8U, 0x00010006U, 50U, 12U, 30U),
    };
    sm64_saturn_actor_instance_descriptor_t jobs[2] = {
        descriptor(&instances[0], 0U, 0U, 1U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 4U),
        descriptor(&instances[1], 1U, 1U, 1U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 4U, 4U),
    };
    uint16_t first, second;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 22U, 2U, 8U, jobs, 2U));
    assert(sm64_saturn_actor_instance_queue_claim_master(
        &queue, 22U, &first));
    assert(sm64_saturn_actor_instance_queue_claim_slave(
        &queue, 22U, &second));

    instances[first].instance_key = 0x00020005U;
    assert(!sm64_saturn_actor_instance_queue_complete(
        &queue, 22U, first, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instances[first], 1U));
    assert(sm64_saturn_actor_instance_queue_state(&queue, 22U, first) ==
           SM64_SATURN_ACTOR_QUARANTINED);
    assert(sm64_saturn_actor_instance_queue_result(&queue, 22U, first)->reason ==
           SM64_SATURN_ACTOR_QUARANTINE_STALE_INSTANCE);
    assert(sm64_saturn_actor_instance_queue_state(&queue, 22U, second) ==
           SM64_SATURN_ACTOR_JOB_CLAIMED_SLAVE);
    assert(sm64_saturn_actor_instance_queue_complete(
        &queue, 22U, second, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        &instances[second], 2U));
}

static void test_stale_package_bank_and_generation_are_rejected(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instance =
        snapshot(31U, 13U, 0x00030007U, 61U, 15U, 40U);
    sm64_saturn_actor_instance_descriptor_t job = descriptor(
        &instance, 0U, 0U, 1U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 2U);
    uint16_t index;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 31U, 1U, 2U, &job, 1U));
    assert(!sm64_saturn_actor_instance_queue_claim_master(
        &queue, 30U, &index));
    assert(sm64_saturn_actor_instance_queue_claim_master(
        &queue, 31U, &index));
    instance.scene_package_generation++;
    assert(!sm64_saturn_actor_instance_queue_complete(
        &queue, 31U, index, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instance, 1U));
    assert(sm64_saturn_actor_instance_queue_result(&queue, 31U, index)->reason ==
           SM64_SATURN_ACTOR_QUARANTINE_STALE_PACKAGE_GENERATION);

    sm64_saturn_actor_instance_queue_init(&queue);
    instance.scene_package_generation = 13U;
    job = descriptor(&instance, 0U, 0U, 1U,
                     SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 2U);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 31U, 1U, 2U, &job, 1U));
    assert(sm64_saturn_actor_instance_queue_claim_slave(
        &queue, 31U, &index));
    instance.actor_bank_hash_words[7] ^= 1U;
    assert(!sm64_saturn_actor_instance_queue_complete(
        &queue, 31U, index, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        &instance, 1U));
    assert(sm64_saturn_actor_instance_queue_result(&queue, 31U, index)->reason ==
           SM64_SATURN_ACTOR_QUARANTINE_STALE_BANK);
}

static void test_invalid_spans_capacity_duplicates_and_claimant_failure(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[2] = {
        snapshot(41U, 14U, 0x00010008U, 70U, 17U, 50U),
        snapshot(41U, 14U, 0x00010009U, 70U, 17U, 50U),
    };
    sm64_saturn_actor_instance_descriptor_t jobs[2] = {
        descriptor(&instances[0], 0U, 0U, 1U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 4U),
        descriptor(&instances[1], 1U, 1U, 1U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 3U, 4U),
    };
    uint16_t index;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(!sm64_saturn_actor_instance_queue_publish(
        &queue, 41U, 2U, 8U, jobs, 2U));
    jobs[1].output_offset = 4U;
    assert(!sm64_saturn_actor_instance_queue_publish(
        &queue, 41U, 1U, 8U, jobs, 2U));
    jobs[1].instance_key = jobs[0].instance_key;
    assert(!sm64_saturn_actor_instance_queue_publish(
        &queue, 41U, 2U, 8U, jobs, 2U));
    jobs[1] = descriptor(&instances[1], 1U, 1U, 1U,
                         SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 4U, 4U);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 41U, 2U, 8U, jobs, 2U));
    assert(sm64_saturn_actor_instance_queue_claim_slave(
        &queue, 41U, &index));
    assert(sm64_saturn_actor_instance_queue_fail(
        &queue, 41U, index, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE));
    assert(!sm64_saturn_actor_instance_queue_fail(
        &queue, 41U, index, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE));
    assert(sm64_saturn_actor_instance_queue_state(&queue, 41U, index) ==
           SM64_SATURN_ACTOR_QUARANTINED);
}

static void test_output_count_overflow_quarantines_only_that_instance(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instance =
        snapshot(45U, 15U, 0x0001000aU, 71U, 18U, 51U);
    sm64_saturn_actor_instance_descriptor_t job = descriptor(
        &instance, 0U, 0U, 1U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 2U);
    uint16_t index = UINT16_MAX;

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 45U, 1U, 2U, &job, 1U));
    assert(sm64_saturn_actor_instance_queue_claim_master(
        &queue, 45U, &index));
    assert(!sm64_saturn_actor_instance_queue_complete(
        &queue, 45U, index, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &instance, 3U));
    assert(sm64_saturn_actor_instance_queue_state(&queue, 45U, index) ==
           SM64_SATURN_ACTOR_QUARANTINED);
    assert(sm64_saturn_actor_instance_queue_result(&queue, 45U, index)->reason ==
           SM64_SATURN_ACTOR_QUARANTINE_OUTPUT_OVERFLOW);
}

static void test_full_instance_ceiling_and_p2_safe_count(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    sm64_saturn_actor_instance_descriptor_t jobs[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    uint16_t index;
    uint16_t count = 0U;

    for (index = 0U; index < SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE; index++) {
        instances[index] = snapshot(
            47U, 16U, (uint32_t)index + 1U, 72U, 19U, 52U);
        jobs[index] = descriptor(
            &instances[index], index, index, 1U,
            SM64_SATURN_ACTOR_OUTPUT_OPAQUE, index, 1U);
    }

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 47U, SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE, jobs,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE));
    assert(sm64_saturn_actor_instance_queue_count(&queue, 47U, &count));
    assert(count == SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE);
    assert(!sm64_saturn_actor_instance_queue_count(&queue, 46U, &count));
    assert(count == 0U);

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(!sm64_saturn_actor_instance_queue_publish(
        &queue, 47U, SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE + 1U,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE, jobs,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE));
}

static void test_output_storage_exact_fit_and_one_record_overflow(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instance =
        snapshot(49U, 17U, 0x0001000bU, 73U, 20U, 53U);
    sm64_saturn_actor_instance_descriptor_t exact = descriptor(
        &instance, 0U, 0U, 1U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U,
        SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING);

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 49U, 1U, SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING,
        &exact, 1U));

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(!sm64_saturn_actor_instance_queue_publish(
        &queue, 49U, 1U,
        (uint16_t)(SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING + 1U),
        &exact, 1U));
}

static void test_memory_report_accounts_for_the_complete_actor_arena(void)
{
    sm64_saturn_actor_instance_queue_memory_report_t report;
    sm64_saturn_actor_instance_queue_memory_report(&report);
    assert(report.capacity == SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE);
    assert(report.descriptor_bytes == 4096U);
    assert(report.release_bytes == 768U);
    assert(report.result_bytes == 768U);
    assert(report.queue_bytes == sizeof(sm64_saturn_actor_instance_queue_t));
    assert(report.actor_bank_bytes == 24088U);
    assert(report.observer_bytes == sizeof(sm64_saturn_geo_state_observer_t));
    assert(report.batch_bytes == 1024U);
    assert(report.output_record_bytes == 8U);
    assert(report.output_storage_bytes ==
           SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING *
               sizeof(sm64_saturn_actor_output_record_t));
    assert(report.alignment_padding_bytes == 12U);
    assert(report.runtime_alignment == 16U);
    assert(report.runtime_bytes == 65536U);
    assert(report.lwram_budget_bytes == 65536U);
    assert(report.output_record_ceiling ==
           SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING);
}

typedef struct processor_context {
    uint16_t calls;
    uint32_t fail_key;
} processor_context_t;

static bool process_instance(
    const sm64_saturn_actor_instance_descriptor_t *job,
    const sm64_saturn_actor_instance_snapshot_t *instance, uint8_t lane,
    void *opaque, uint16_t *output_count)
{
    processor_context_t *const context = (processor_context_t *)opaque;
    assert(job != NULL && instance != NULL && output_count != NULL);
    assert(job->instance_key == instance->instance_key);
    assert(lane == SM64_SATURN_ACTOR_CLAIMED_MASTER ||
           lane == SM64_SATURN_ACTOR_CLAIMED_SLAVE);
    context->calls++;
    *output_count = 1U;
    return instance->instance_key != context->fail_key;
}

static void test_drain_rejects_stale_identity_before_local_processing(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_instance_snapshot_t instances[3] = {
        snapshot(61U, 22U, 0x00010011U, 90U, 21U, 60U),
        snapshot(61U, 22U, 0x00010012U, 90U, 21U, 60U),
        snapshot(61U, 22U, 0x00010013U, 90U, 21U, 60U),
    };
    sm64_saturn_actor_instance_descriptor_t jobs[3] = {
        descriptor(&instances[0], 0U, 0U, 7U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 2U),
        descriptor(&instances[1], 1U, 1U, 7U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 2U, 2U),
        descriptor(&instances[2], 2U, 2U, 7U,
                   SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 4U, 2U),
    };
    processor_context_t context = {0U, instances[2].instance_key};
    sm64_saturn_actor_instance_processor_t processor = {
        .snapshots = instances,
        .snapshot_count = 3U,
        .generation = 61U,
        .process = process_instance,
        .context = &context,
    };

    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 61U, 3U, 6U, jobs, 3U));
    instances[1].instance_key = 0x00020012U;
    assert(sm64_saturn_actor_instance_queue_drain_slave(
        &queue, 61U, &processor) == 3U);
    assert(context.calls == 2U);
    assert(sm64_saturn_actor_instance_queue_state(&queue, 61U, 0U) ==
           SM64_SATURN_ACTOR_JOB_DONE);
    assert(sm64_saturn_actor_instance_queue_result(&queue, 61U, 1U)->reason ==
           SM64_SATURN_ACTOR_QUARANTINE_STALE_INSTANCE);
    assert(sm64_saturn_actor_instance_queue_result(&queue, 61U, 2U)->reason ==
           SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE);
}

static void test_in_place_descriptor_staging_publishes_without_copy(void)
{
    sm64_saturn_actor_instance_queue_t queue;
    const sm64_saturn_actor_instance_snapshot_t instance =
        snapshot(71U, 30U, 0x00010001U, 90U, 21U, 60U);
    sm64_saturn_actor_instance_queue_init(&queue);
    queue.descriptors[0] = descriptor(
        &instance, 0U, 0U, 0U, SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 2U);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 71U, 1U, 2U, queue.descriptors, 1U));
    assert(sm64_saturn_actor_instance_queue_descriptor(&queue, 71U, 0U) != NULL);
}

int main(void)
{
    test_zero_instances_publish_as_terminal_generation();
    test_master_and_slave_claim_each_descriptor_exactly_once();
    test_stale_identity_quarantines_only_its_claim();
    test_stale_package_bank_and_generation_are_rejected();
    test_invalid_spans_capacity_duplicates_and_claimant_failure();
    test_output_count_overflow_quarantines_only_that_instance();
    test_full_instance_ceiling_and_p2_safe_count();
    test_output_storage_exact_fit_and_one_record_overflow();
    test_memory_report_accounts_for_the_complete_actor_arena();
    test_drain_rejects_stale_identity_before_local_processing();
    test_in_place_descriptor_staging_publishes_without_copy();
    return 0;
}
