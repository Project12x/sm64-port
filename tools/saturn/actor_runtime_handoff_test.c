#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_actor_runtime_handoff.h"

enum { generation = 71U, package_generation = 19U };

static sm64_saturn_actor_instance_snapshot_t snapshot(uint32_t key)
{
    sm64_saturn_actor_instance_snapshot_t value;
    memset(&value, 0, sizeof(value));
    value.generation = generation;
    value.scene_package_generation = package_generation;
    value.instance_key = key;
    value.actor_bank_id = 91U;
    value.actor_bank_hash_words[0] = 0x1234U;
    value.family_id = 7U;
    value.model_id = 8U;
    value.active = 1U;
    value.render_active = 1U;
    return value;
}

static void publish_bank(sm64_saturn_actor_instance_bank_t *bank,
                         uint16_t count, uint8_t *bank_index)
{
    uint16_t index;
    sm64_saturn_actor_instance_bank_init(bank);
    assert(sm64_saturn_actor_instance_bank_begin_write(bank, generation,
                                                        bank_index));
    for (index = 0U; index < count; index++)
        bank->snapshots[*bank_index][index] = snapshot(0x100U + index);
    assert(sm64_saturn_actor_instance_bank_publish(
        bank, *bank_index, count, generation));
}

static void descriptors_from_bank(
    const sm64_saturn_actor_instance_bank_t *bank, uint8_t bank_index,
    sm64_saturn_actor_instance_descriptor_t *descriptors, uint16_t count)
{
    uint16_t index;
    for (index = 0U; index < count; index++)
        assert(sm64_saturn_actor_instance_descriptor_from_snapshot(
            &bank->snapshots[bank_index][index], index, index,
            (uint16_t)(3U + index), SM64_SATURN_ACTOR_OUTPUT_OPAQUE,
            (uint16_t)(index * 2U), 2U, &descriptors[index]));
}

static bool begin_two(sm64_saturn_actor_runtime_handoff_t *handoff,
                      sm64_saturn_actor_instance_bank_t *bank,
                      sm64_saturn_actor_instance_queue_t *queue,
                      uint8_t bank_index,
                      sm64_saturn_actor_instance_descriptor_t *descriptors,
                      sm64_saturn_actor_batch_t *batches)
{
    return sm64_saturn_actor_runtime_handoff_begin(
        handoff, bank, queue, bank_index, generation, descriptors, 2U, 2U,
        4U, batches, 2U);
}

static void make_terminal(sm64_saturn_actor_instance_queue_t *queue,
                          sm64_saturn_actor_instance_bank_t *bank,
                          uint8_t bank_index)
{
    uint16_t claimed;
    assert(sm64_saturn_actor_instance_queue_claim_master(queue, generation,
                                                          &claimed));
    assert(sm64_saturn_actor_instance_queue_complete(
        queue, generation, claimed, SM64_SATURN_ACTOR_CLAIMED_MASTER,
        &bank->snapshots[bank_index][claimed], 1U));
    assert(sm64_saturn_actor_instance_queue_claim_slave(queue, generation,
                                                         &claimed));
    assert(sm64_saturn_actor_instance_queue_fail(
        queue, generation, claimed, SM64_SATURN_ACTOR_CLAIMED_SLAVE,
        SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE));
}

static void test_zero_count_lifecycle(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    uint8_t bank_index;
    publish_bank(&bank, 0U, &bank_index);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, NULL, 0U, 1U, 0U,
        NULL, 0U));
    assert(handoff.state == SM64_SATURN_ACTOR_HANDOFF_QUEUED);
    assert(queue.generation == generation);
    assert(sm64_saturn_actor_runtime_handoff_finalize(&handoff));
    assert(handoff.summary.batch_count == 0U);
    assert(sm64_saturn_actor_runtime_handoff_acknowledge_consumed(&handoff));
    assert(sm64_saturn_actor_runtime_handoff_retire(&handoff));
    assert(queue.generation == 0U);
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_FREE);
}

static void test_zero_count_rejects_live_queue(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    uint8_t bank_index;
    publish_bank(&bank, 0U, &bank_index);
    sm64_saturn_actor_instance_queue_init(&queue);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 99U, 1U, 0U, NULL, 0U));
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, NULL, 0U, 1U, 0U,
        NULL, 0U));
    assert(queue.generation == 99U);
    assert(bank.state[bank_index] ==
           SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED);
}

static void test_exact_lifecycle_and_consumer_ack(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[2];
    sm64_saturn_actor_batch_t batches[2];
    uint8_t bank_index;
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(begin_two(&handoff, &bank, &queue, bank_index, descriptors, batches));
    assert(!sm64_saturn_actor_runtime_handoff_finalize(&handoff));
    assert(!sm64_saturn_actor_runtime_handoff_retire(&handoff));
    make_terminal(&queue, &bank, bank_index);
    assert(sm64_saturn_actor_runtime_handoff_finalize(&handoff));
    assert(handoff.summary.completed_instance_count == 1U);
    assert(handoff.summary.quarantined_instance_count == 1U);
    assert(handoff.summary.batch_count == 1U);
    assert(!sm64_saturn_actor_runtime_handoff_finalize(&handoff));
    assert(!sm64_saturn_actor_runtime_handoff_retire(&handoff));
    assert(sm64_saturn_actor_runtime_handoff_acknowledge_consumed(&handoff));
    assert(!sm64_saturn_actor_runtime_handoff_acknowledge_consumed(&handoff));
    assert(sm64_saturn_actor_runtime_handoff_retire(&handoff));
    assert(!sm64_saturn_actor_runtime_handoff_retire(&handoff));
    assert(queue.generation == 0U);
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_FREE);
}

static void test_identity_mutations_quarantine_after_acquire(void)
{
    enum { mutations = 9U };
    uint16_t mutation;
    for (mutation = 0U; mutation < mutations; mutation++) {
        sm64_saturn_actor_instance_bank_t bank;
        sm64_saturn_actor_instance_queue_t queue;
        sm64_saturn_actor_runtime_handoff_t handoff;
        sm64_saturn_actor_instance_descriptor_t descriptors[2];
        sm64_saturn_actor_batch_t batches[2];
        uint8_t bank_index;
        publish_bank(&bank, 2U, &bank_index);
        descriptors_from_bank(&bank, bank_index, descriptors, 2U);
        switch (mutation) {
        case 0U: descriptors[1].snapshot_index = 0U; break;
        case 1U: descriptors[1].generation++; break;
        case 2U: descriptors[1].scene_package_generation++; break;
        case 3U: descriptors[1].instance_key++; break;
        case 4U: descriptors[1].actor_bank_id++; break;
        case 5U: descriptors[1].actor_bank_hash_words[0]++; break;
        case 6U: descriptors[1].family_id++; break;
        case 7U: descriptors[1].model_id++; break;
        default: descriptors[1].reserved = 1U; break;
        }
        sm64_saturn_actor_instance_queue_init(&queue);
        sm64_saturn_actor_runtime_handoff_init(&handoff);
        assert(!begin_two(&handoff, &bank, &queue, bank_index, descriptors,
                          batches));
        assert(bank.state[bank_index] ==
               SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED);
        assert(queue.generation == 0U);
    }
}

static void test_snapshot_swap_is_rejected(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[2];
    sm64_saturn_actor_batch_t batches[2];
    uint8_t bank_index;
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    descriptors[0].snapshot_index = 1U;
    descriptors[1].snapshot_index = 0U;
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!begin_two(&handoff, &bank, &queue, bank_index, descriptors,
                      batches));
    assert(bank.state[bank_index] ==
           SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED);
}

static void test_caller_output_fields_are_preserved(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[2];
    sm64_saturn_actor_batch_t batches[2];
    const sm64_saturn_actor_instance_descriptor_t *published;
    uint8_t bank_index;
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    descriptors[1].material_id = 29U;
    descriptors[1].output_offset = 3U;
    descriptors[1].output_capacity = 3U;
    descriptors[1].output_class = SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT;
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 2U,
        2U, 6U, batches, 2U));
    published = sm64_saturn_actor_instance_queue_descriptor(&queue, generation,
                                                            1U);
    assert(published != NULL);
    assert(published->material_id == 29U && published->output_offset == 3U &&
           published->output_capacity == 3U &&
           published->output_class == SM64_SATURN_ACTOR_OUTPUT_TRANSLUCENT);
}

static void test_count_and_output_capacity_fail_after_acquire(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[2];
    sm64_saturn_actor_batch_t batches[2];
    uint8_t bank_index;
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 1U,
        2U, 4U, batches, 2U));
    assert(bank.state[bank_index] ==
           SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED);
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 2U,
        2U, 3U, batches, 2U));
    assert(bank.state[bank_index] ==
           SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED);
}

static void test_generation_wrap_refusal(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptor;
    sm64_saturn_actor_batch_t batch;
    uint8_t bank_index;
    sm64_saturn_actor_instance_bank_init(&bank);
    assert(sm64_saturn_actor_instance_bank_begin_write(
        &bank, UINT32_MAX, &bank_index));
    bank.snapshots[bank_index][0] = snapshot(0x200U);
    bank.snapshots[bank_index][0].generation = UINT32_MAX;
    assert(sm64_saturn_actor_instance_bank_publish(
        &bank, bank_index, 1U, UINT32_MAX));
    assert(sm64_saturn_actor_instance_descriptor_from_snapshot(
        &bank.snapshots[bank_index][0], 0U, 0U, 3U,
        SM64_SATURN_ACTOR_OUTPUT_OPAQUE, 0U, 1U, &descriptor));
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, UINT32_MAX, &descriptor, 1U,
        1U, 1U, &batch, 1U));
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, 1U, &descriptor, 1U, 1U, 1U,
        &batch, 1U));
}

static void test_exact_capacity_boundaries(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    sm64_saturn_actor_batch_t batches[SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    uint16_t index;
    uint8_t bank_index;
    publish_bank(&bank, SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE, &bank_index);
    for (index = 0U; index < SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE; index++) {
        const uint16_t capacity = index + 1U ==
            SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE
            ? (uint16_t)(SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING - index)
            : 1U;
        assert(sm64_saturn_actor_instance_descriptor_from_snapshot(
            &bank.snapshots[bank_index][index], index, index, index + 1U,
            SM64_SATURN_ACTOR_OUTPUT_OPAQUE, index, capacity,
            &descriptors[index]));
    }
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE,
        SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING, batches,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE));
    publish_bank(&bank, 1U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 1U);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE + 1U,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE + 1U, 1U, batches,
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 1U,
        1U, SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING + 1U, batches, 1U));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 1U,
        1U, 1U, NULL, 0U));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
}

static void test_retire_retry_after_bank_failure(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    uint8_t bank_index;
    publish_bank(&bank, 0U, &bank_index);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, NULL, 0U, 1U, 0U,
        NULL, 0U));
    assert(sm64_saturn_actor_runtime_handoff_finalize(&handoff));
    assert(sm64_saturn_actor_runtime_handoff_acknowledge_consumed(&handoff));
    bank.state[bank_index] = SM64_SATURN_ACTOR_INSTANCE_BANK_READY;
    assert(!sm64_saturn_actor_runtime_handoff_retire(&handoff));
    assert(handoff.queue_reset_completed != 0U && queue.generation == 0U);
    bank.state[bank_index] = SM64_SATURN_ACTOR_INSTANCE_BANK_COMPLETE;
    assert(sm64_saturn_actor_runtime_handoff_retire(&handoff));
}

static void test_finalize_retry_after_bank_complete_failure(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    uint8_t bank_index;
    publish_bank(&bank, 0U, &bank_index);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, NULL, 0U, 1U, 0U,
        NULL, 0U));
    bank.state[bank_index] = SM64_SATURN_ACTOR_INSTANCE_BANK_READY;
    assert(!sm64_saturn_actor_runtime_handoff_finalize(&handoff));
    assert(handoff.state == SM64_SATURN_ACTOR_HANDOFF_TERMINAL &&
           queue.generation == generation);
    bank.state[bank_index] = SM64_SATURN_ACTOR_INSTANCE_BANK_RENDERING;
    assert(sm64_saturn_actor_runtime_handoff_finalize(&handoff));
}

static void test_preacquire_refusal_is_immutable(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[2];
    sm64_saturn_actor_batch_t batches[2];
    uint8_t bank_index;
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, 0U, descriptors, 2U, 2U, 4U,
        batches, 2U));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
    assert(queue.generation == 0U);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, 2U, generation, descriptors, 2U, 2U, 4U,
        batches, 2U));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
}

static void test_stale_bank_live_queue_and_capacity_refusals(void)
{
    sm64_saturn_actor_instance_bank_t bank;
    sm64_saturn_actor_instance_queue_t queue;
    sm64_saturn_actor_runtime_handoff_t handoff;
    sm64_saturn_actor_instance_descriptor_t descriptors[2];
    sm64_saturn_actor_batch_t batches[2];
    uint8_t bank_index;
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    sm64_saturn_actor_instance_queue_init(&queue);
    sm64_saturn_actor_runtime_handoff_init(&handoff);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation + 1U, descriptors,
        2U, 2U, 4U, batches, 2U));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
    assert(sm64_saturn_actor_instance_queue_publish(
        &queue, 99U, 1U, 2U, descriptors, 0U));
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 2U,
        2U, 4U, batches, 2U));
    assert(bank.state[bank_index] ==
           SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED);
    publish_bank(&bank, 2U, &bank_index);
    descriptors_from_bank(&bank, bank_index, descriptors, 2U);
    sm64_saturn_actor_instance_queue_init(&queue);
    assert(!sm64_saturn_actor_runtime_handoff_begin(
        &handoff, &bank, &queue, bank_index, generation, descriptors, 2U,
        1U, 4U, batches, 2U));
    assert(bank.state[bank_index] == SM64_SATURN_ACTOR_INSTANCE_BANK_READY);
}

int main(void)
{
    test_zero_count_lifecycle();
    test_zero_count_rejects_live_queue();
    test_exact_lifecycle_and_consumer_ack();
    test_identity_mutations_quarantine_after_acquire();
    test_snapshot_swap_is_rejected();
    test_caller_output_fields_are_preserved();
    test_count_and_output_capacity_fail_after_acquire();
    test_preacquire_refusal_is_immutable();
    test_stale_bank_live_queue_and_capacity_refusals();
    test_generation_wrap_refusal();
    test_exact_capacity_boundaries();
    test_retire_retry_after_bank_failure();
    test_finalize_retry_after_bank_complete_failure();
    return 0;
}
