#include "saturn_render_lifecycle.h"

#include <stddef.h>

static bool lifecycle_ops_valid(
    const sm64_saturn_render_lifecycle_ops_t *ops)
{
    return ops != NULL && ops->prepare_publish != NULL &&
        ops->notify != NULL && ops->slave_retired != NULL &&
        ops->drain_master != NULL && ops->finalize != NULL &&
        ops->quarantine != NULL;
}

bool sm64_saturn_render_lifecycle_observe(
    sm64_saturn_render_lifecycle_t *lifecycle,
    sm64_saturn_render_lifecycle_observer_t observer, void *context)
{
    if (lifecycle == NULL || lifecycle->active) return false;
    lifecycle->observer = observer;
    lifecycle->observer_context = context;
    return true;
}

bool sm64_saturn_render_lifecycle_start(
    sm64_saturn_render_lifecycle_t *lifecycle,
    const sm64_saturn_render_lifecycle_ops_t *ops, void *context,
    uint32_t generation)
{
    if (lifecycle == NULL || !lifecycle_ops_valid(ops) || generation == 0U ||
        lifecycle->active)
        return false;
    lifecycle->active = true;
    lifecycle->active_generation = generation;
    if (!ops->prepare_publish(context, generation)) {
        ops->quarantine(context, generation);
        lifecycle->active = false;
        lifecycle->active_generation = 0U;
        return false;
    }
#if defined(SM64_SATURN_RENDER_LIFECYCLE_TEST_OBSERVER_BEFORE_NOTIFY)
    if (lifecycle->observer != NULL)
        lifecycle->observer(lifecycle->observer_context,
                            SM64_SATURN_RENDER_LIFECYCLE_NOTIFIED,
                            generation);
#endif
    ops->notify(context);
#if !defined(SM64_SATURN_RENDER_LIFECYCLE_TEST_OBSERVER_BEFORE_NOTIFY)
    if (lifecycle->observer != NULL)
        lifecycle->observer(lifecycle->observer_context,
                            SM64_SATURN_RENDER_LIFECYCLE_NOTIFIED,
                            generation);
#endif
    return true;
}

sm64_saturn_render_lifecycle_status_t sm64_saturn_render_lifecycle_poll(
    sm64_saturn_render_lifecycle_t *lifecycle,
    const sm64_saturn_render_lifecycle_ops_t *ops, void *context,
    uint32_t generation)
{
    if (lifecycle == NULL || !lifecycle_ops_valid(ops) || generation == 0U ||
        !lifecycle->active || lifecycle->active_generation != generation)
        return SM64_SATURN_RENDER_LIFECYCLE_FAILED;
#if !defined(SM64_SATURN_RENDER_LIFECYCLE_TEST_FINALIZE_BEFORE_RETIREMENT)
    if (!ops->slave_retired(context))
        return SM64_SATURN_RENDER_LIFECYCLE_PENDING;
#endif
    if (lifecycle->observer != NULL)
        lifecycle->observer(lifecycle->observer_context,
                            SM64_SATURN_RENDER_LIFECYCLE_RETIRED,
                            generation);
    const uint16_t master_jobs = ops->drain_master(context);
    bool complete = ops->finalize(context, generation, master_jobs);
#if defined(SM64_SATURN_RENDER_LIFECYCLE_TEST_LOWER_TWICE)
    complete = ops->finalize(context, generation, master_jobs) && complete;
#endif
    if (!complete) {
#if defined(SM64_SATURN_RENDER_LIFECYCLE_TEST_REPLAY_ON_FAILURE)
        (void)ops->finalize(context, generation, master_jobs);
#endif
        ops->quarantine(context, generation);
    }
    lifecycle->active = false;
    lifecycle->active_generation = 0U;
    return complete ? SM64_SATURN_RENDER_LIFECYCLE_COMPLETE
                    : SM64_SATURN_RENDER_LIFECYCLE_FAILED;
}
