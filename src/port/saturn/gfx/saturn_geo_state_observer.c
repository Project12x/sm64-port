#include "saturn_geo_state_observer.h"
#include "port/saturn/platform/saturn_cart_code.h"

#include <string.h>

/* Mutation hooks for verify-actor-pose-owner-mutation.  Each must make the
 * owner-key / leak fixture fail; see Makefile.saturn.mk. */
#ifndef SM64_SATURN_GEO_MUTATE_NO_DESPAWN
#define SM64_SATURN_GEO_MUTATE_NO_DESPAWN 0
#endif
#ifndef SM64_SATURN_GEO_MUTATE_STICKY_INCARNATION
#define SM64_SATURN_GEO_MUTATE_STICKY_INCARNATION 0
#endif
#ifndef SM64_SATURN_GEO_MUTATE_LATE_DESPAWN
#define SM64_SATURN_GEO_MUTATE_LATE_DESPAWN 0
#endif

static sm64_saturn_geo_state_observer_t *s_observer;
static sm64_saturn_actor_source_observation_t *s_current;
static sm64_saturn_geo_despawn_fn s_despawn_sink;
static void *s_despawn_context;

void sm64_saturn_geo_state_observer_set_despawn_sink(
    sm64_saturn_geo_despawn_fn sink, void *context)
{
    s_despawn_sink = sink;
    s_despawn_context = context;
}

SM64_SATURN_CART_COLD
void sm64_saturn_geo_state_observer_init(
    sm64_saturn_geo_state_observer_t *observer, uint16_t capacity)
{
    if (observer == NULL) return;
    memset(observer, 0, sizeof(*observer));
    observer->capacity = capacity > SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE
        ? SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE : capacity;
}

void sm64_saturn_geo_state_observer_begin_frame(
    sm64_saturn_geo_state_observer_t *observer, uint32_t generation)
{
    if (observer == NULL) return;
    memset(observer->seen, 0, sizeof(observer->seen));
    observer->count = 0U;
    observer->source_generation = generation;
    observer->geo_evaluation_count = 0U;
    observer->geo_rendered_count = 0U;
    observer->geo_rejected_count = 0U;
    observer->despawned_count = 0U;
    observer->pool_reuse_count = 0U;
    observer->pool_slot_overflow_count = 0U;
    observer->overflow_latched = 0U;
    s_current = NULL;
    s_observer = observer;
}

uint32_t sm64_saturn_geo_state_observer_generation(
    const sm64_saturn_geo_state_observer_t *observer)
{
    return observer != NULL ? observer->source_generation : 0U;
}

sm64_saturn_geo_state_observer_t *sm64_saturn_geo_state_observer_bound(void)
{
    return s_observer;
}

bool sm64_saturn_geo_state_observer_begin_object(
    sm64_saturn_geo_state_observer_t *observer,
    const sm64_saturn_actor_source_observation_t *observation)
{
    uint16_t slot, incarnation;
    if (observer == NULL || observation == NULL ||
        observer != s_observer || observer->capacity == 0U ||
        observation->active == 0U)
        return false;
    if (observation->pool_slot >= SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY) {
        if (observer->pool_slot_overflow_count != UINT16_MAX)
            observer->pool_slot_overflow_count++;
        return false;
    }
    if (observer->count >= observer->capacity) {
        observer->overflow_latched = 1U;
        return false;
    }
    if (observer->seen[observation->pool_slot] != 0U)
        return false;
    slot = observation->pool_slot;
    incarnation = observer->incarnation[slot];
    if (!observer->live[slot]) {
        if (incarnation != 0U) observer->pool_reuse_count++;
#if !SM64_SATURN_GEO_MUTATE_STICKY_INCARNATION
        incarnation++;
#endif
        if (incarnation == 0U) incarnation = 1U;
        observer->incarnation[slot] = incarnation;
    }
    observer->observations[observer->count] = *observation;
    observer->observations[observer->count].pool_slot = slot;
    s_current = &observer->observations[observer->count];
    observer->seen[slot] = 1U;
    observer->live[slot] = 1U;
    observer->count++;
    return true;
}

bool sm64_saturn_geo_state_observer_record_switch(
    sm64_saturn_geo_state_observer_t *observer, uint16_t ordinal,
    uint16_t state)
{
    if (observer == NULL || observer != s_observer || s_current == NULL ||
        ordinal >= SM64_SATURN_ACTOR_MAX_SWITCHES)
        return false;
    s_current->switch_state[ordinal] = state;
    if (s_current->switch_count <= ordinal)
        s_current->switch_count = (uint8_t)(ordinal + 1U);
    return true;
}

bool sm64_saturn_geo_state_observer_record_selected_switch(
    sm64_saturn_geo_state_observer_t *observer, uint16_t state)
{
    if (observer == NULL || observer != s_observer || s_current == NULL ||
        s_current->switch_count >= SM64_SATURN_ACTOR_MAX_SWITCHES)
        return false;
    return sm64_saturn_geo_state_observer_record_switch(
        observer, s_current->switch_count, state);
}

bool sm64_saturn_geo_state_observer_record_render_range(
    sm64_saturn_geo_state_observer_t *observer, int32_t min_q16,
    int32_t max_q16, bool selected)
{
    if (observer == NULL || observer != s_observer || s_current == NULL ||
        min_q16 > max_q16)
        return false;
    if (selected) {
        s_current->render_range_min_q16 = min_q16;
        s_current->render_range_max_q16 = max_q16;
        s_current->render_range_state = 1U;
    }
    return true;
}

bool sm64_saturn_geo_state_observer_record_opacity(
    sm64_saturn_geo_state_observer_t *observer, uint16_t opacity)
{
    if (observer == NULL || observer != s_observer || s_current == NULL ||
        opacity > 255U)
        return false;
    s_current->opacity = opacity;
    return true;
}

bool sm64_saturn_geo_state_observer_end_object(
    sm64_saturn_geo_state_observer_t *observer)
{
    if (observer == NULL || observer != s_observer || s_current == NULL)
        return false;
    s_current = NULL;
    return true;
}

void sm64_saturn_geo_state_observer_end_frame(
    sm64_saturn_geo_state_observer_t *observer)
{
    uint16_t slot;
    if (observer == NULL) return;
    for (slot = 0U; slot < SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY; slot++) {
        if (!observer->seen[slot]) {
            if (observer->live[slot]) {
                observer->despawned_count++;
                /* The despawn edge.  `incarnation[slot]` still names the
                 * actor that just left -- the next `begin_object` on this
                 * slot is what bumps it -- so a sink keyed on
                 * (slot, incarnation) releases the departing actor's
                 * resources and can never reach its successor's. */
#if !SM64_SATURN_GEO_MUTATE_NO_DESPAWN
                if (s_despawn_sink != NULL) {
#if SM64_SATURN_GEO_MUTATE_LATE_DESPAWN
                    s_despawn_sink(slot,
                                   (uint16_t)(observer->incarnation[slot] + 1U),
                                   s_despawn_context);
#else
                    s_despawn_sink(slot, observer->incarnation[slot],
                                   s_despawn_context);
#endif
                }
#endif
            }
            observer->live[slot] = 0U;
        }
    }
    s_current = NULL;
}

void sm64_saturn_geo_state_observer_record_geo_decision(
    sm64_saturn_geo_state_observer_t *observer, bool rendered)
{
    if (observer == NULL) return;
    observer->geo_evaluation_count++;
    if (rendered) observer->geo_rendered_count++;
    else observer->geo_rejected_count++;
}

void sm64_saturn_geo_state_observer_record_authoritative_geo_decision(
    bool rendered)
{
    sm64_saturn_geo_state_observer_record_geo_decision(s_observer, rendered);
    if (s_current != NULL) s_current->render_active = rendered ? 1U : 0U;
}
