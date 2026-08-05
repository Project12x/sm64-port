#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "saturn_render_lifecycle.h"

typedef struct fake_runtime {
    uint32_t generation;
    bool retired;
    bool descriptor_ok;
    uint32_t graph_publish;
    uint32_t notify;
    uint32_t drain;
    uint32_t validate;
    uint32_t merge;
    uint32_t queue_reset;
    uint32_t gouraud_begin;
    uint32_t vdp1_begin;
    uint32_t lower;
    uint32_t vdp1_finish;
    uint32_t quarantine;
} fake_runtime_t;

static bool fake_prepare_publish(void *opaque, uint32_t generation)
{
    fake_runtime_t *runtime = opaque;
    if (generation == 0U || runtime->graph_publish != 0U ||
        runtime->notify != 0U || runtime->drain != 0U ||
        runtime->queue_reset != 0U || runtime->vdp1_begin != 0U ||
        runtime->gouraud_begin != 0U || runtime->lower != 0U)
        return false;
    runtime->generation = generation;
    runtime->graph_publish++;
    return true;
}

static void fake_notify(void *opaque)
{
    fake_runtime_t *runtime = opaque;
    if (runtime->graph_publish == 1U) runtime->notify++;
}

static bool fake_slave_retired(void *opaque)
{
    return ((fake_runtime_t *)opaque)->retired;
}

static uint16_t fake_drain(void *opaque)
{
    fake_runtime_t *runtime = opaque;
    if (!runtime->retired) return UINT16_MAX;
    runtime->drain++;
    return 1U;
}

static bool fake_finalize(void *opaque, uint32_t generation,
                          uint16_t master_jobs)
{
    fake_runtime_t *runtime = opaque;
    if (!runtime->retired || runtime->generation != generation ||
        master_jobs != 1U)
        return false;
    runtime->validate++;
    runtime->merge++;
    runtime->queue_reset++;
    if (!runtime->descriptor_ok) return false;
    runtime->gouraud_begin++;
    runtime->vdp1_begin++;
    runtime->lower++;
    runtime->vdp1_finish++;
    return true;
}

static void fake_quarantine(void *opaque, uint32_t generation)
{
    fake_runtime_t *runtime = opaque;
    if (generation == runtime->generation) runtime->quarantine++;
}

static const sm64_saturn_render_lifecycle_ops_t fake_ops = {
    .prepare_publish = fake_prepare_publish,
    .notify = fake_notify,
    .slave_retired = fake_slave_retired,
    .drain_master = fake_drain,
    .finalize = fake_finalize,
    .quarantine = fake_quarantine,
};

static int expect_zero_finalization(const fake_runtime_t *runtime)
{
    return runtime->drain == 0U && runtime->validate == 0U &&
        runtime->merge == 0U && runtime->queue_reset == 0U &&
        runtime->gouraud_begin == 0U && runtime->vdp1_begin == 0U &&
        runtime->lower == 0U && runtime->vdp1_finish == 0U;
}

static int test_pending_then_exactly_once_complete(void)
{
    sm64_saturn_render_lifecycle_t lifecycle = {0};
    fake_runtime_t runtime = {.descriptor_ok = true};
    const uint32_t generation = 17U;

    if (!sm64_saturn_render_lifecycle_start(
            &lifecycle, &fake_ops, &runtime, generation))
        return 1;
    if (runtime.graph_publish != 1U || runtime.notify != 1U ||
        !expect_zero_finalization(&runtime))
        return 2;
    if (sm64_saturn_render_lifecycle_start(
            &lifecycle, &fake_ops, &runtime, generation + 1U))
        return 3;
    if (sm64_saturn_render_lifecycle_poll(
            &lifecycle, &fake_ops, &runtime, generation + 1U) !=
        SM64_SATURN_RENDER_LIFECYCLE_FAILED)
        return 4;
    if (sm64_saturn_render_lifecycle_poll(
            &lifecycle, &fake_ops, &runtime, generation) !=
        SM64_SATURN_RENDER_LIFECYCLE_PENDING)
        return 5;
    if (!expect_zero_finalization(&runtime)) return 6;

    runtime.retired = true;
    if (sm64_saturn_render_lifecycle_poll(
            &lifecycle, &fake_ops, &runtime, generation) !=
        SM64_SATURN_RENDER_LIFECYCLE_COMPLETE)
        return 7;
    if (runtime.drain != 1U || runtime.validate != 1U ||
        runtime.merge != 1U || runtime.queue_reset != 1U ||
        runtime.gouraud_begin != 1U || runtime.vdp1_begin != 1U ||
        runtime.lower != 1U || runtime.vdp1_finish != 1U ||
        runtime.quarantine != 0U)
        return 8;
    if (sm64_saturn_render_lifecycle_poll(
            &lifecycle, &fake_ops, &runtime, generation) !=
        SM64_SATURN_RENDER_LIFECYCLE_FAILED)
        return 9;
    if (runtime.drain != 1U || runtime.vdp1_begin != 1U ||
        runtime.lower != 1U || runtime.vdp1_finish != 1U)
        return 10;
    return 0;
}

static int test_failed_descriptor_quarantines_without_replay(void)
{
    sm64_saturn_render_lifecycle_t lifecycle = {0};
    fake_runtime_t runtime = {.descriptor_ok = false};
    const uint32_t generation = 31U;

    if (!sm64_saturn_render_lifecycle_start(
            &lifecycle, &fake_ops, &runtime, generation))
        return 20;
    runtime.retired = true;
    if (sm64_saturn_render_lifecycle_poll(
            &lifecycle, &fake_ops, &runtime, generation) !=
        SM64_SATURN_RENDER_LIFECYCLE_FAILED)
        return 21;
    if (runtime.drain != 1U || runtime.validate != 1U ||
        runtime.merge != 1U || runtime.queue_reset != 1U ||
        runtime.quarantine != 1U || runtime.vdp1_begin != 0U ||
        runtime.lower != 0U || runtime.vdp1_finish != 0U)
        return 22;

    /* A late/repeated retirement observation cannot reopen or replay N. */
    if (sm64_saturn_render_lifecycle_poll(
            &lifecycle, &fake_ops, &runtime, generation) !=
        SM64_SATURN_RENDER_LIFECYCLE_FAILED)
        return 23;
    if (runtime.drain != 1U || runtime.quarantine != 1U ||
        runtime.lower != 0U)
        return 24;
    return 0;
}

static int test_reserved_zero_never_activates(void)
{
    sm64_saturn_render_lifecycle_t lifecycle = {0};
    fake_runtime_t runtime = {.descriptor_ok = true};
    if (sm64_saturn_render_lifecycle_start(
            &lifecycle, &fake_ops, &runtime, 0U))
        return 30;
    return runtime.graph_publish == 0U && runtime.notify == 0U ? 0 : 31;
}

int main(void)
{
    int failure = test_pending_then_exactly_once_complete();
    if (failure == 0) failure = test_failed_descriptor_quarantines_without_replay();
    if (failure == 0) failure = test_reserved_zero_never_activates();
    if (failure != 0) return failure;
    puts("demo render overlap lifecycle: PASS");
    return 0;
}
