#ifndef SM64_SATURN_ACTOR_INSTANCE_H
#define SM64_SATURN_ACTOR_INSTANCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__sh__)
#include <yaul/scu/map.h>
#endif

/* The generated scene package supplies the live bound.  This ceiling is only
 * a compile-time safety rail for malformed or not-yet-generated packages. */
#define SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE 64U
/* Source-attested from src/game/object_list_processor.h: OBJECT_POOL_CAPACITY.
 * This identity domain deliberately outlives the compact drawable snapshot
 * domain above: source slots 0..239 can be observed without enlarging queue
 * or snapshot payloads beyond 64 entries. */
#define SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY 240U
#define SM64_SATURN_ACTOR_MAX_SWITCHES 8U
#define SM64_SATURN_ACTOR_INSTANCE_MODEL_NONE 0U
#define SM64_SATURN_ACTOR_INSTANCE_NO_PARENT 0xffffU

typedef struct sm64_saturn_actor_instance_snapshot {
    uint32_t generation, scene_package_generation, instance_key;
    uint32_t actor_bank_id, actor_bank_hash_words[8];
    uint16_t family_id, model_id, parent_index, parent_node_ordinal;
    uint32_t feature_state;
    int32_t position_q16[3], scale_q16[3];
    int32_t held_offset_q16[3], draw_distance_q16;
    int32_t render_range_min_q16, render_range_max_q16;
    int16_t angle[3], animation_id, animation_frame;
    int32_t animation_accel, anim_state;
    int32_t effect_params_q16[4];
    uint32_t effect_lifetime;
    uint16_t opacity, render_range_state, billboard_state;
    uint16_t shadow_type, shadow_scale, shadow_solidity;
    uint16_t effect_kind, effect_flags;
    int8_t area_index;
    uint8_t active, render_active, switch_count, reserved;
    uint16_t switch_state[SM64_SATURN_ACTOR_MAX_SWITCHES];
} sm64_saturn_actor_instance_snapshot_t;

/* This is the source-owned, pointer-free observation assembled at the geo
 * seam.  It deliberately contains no Object, GraphNode, behavior, or bank
 * pointers.  A zero family or zero bank identity is unknown and fails closed. */
typedef struct sm64_saturn_actor_source_observation {
    uint32_t source_generation, scene_package_generation;
    uint32_t actor_bank_id, actor_bank_hash_words[8];
    uint16_t pool_slot, family_id, model_id, parent_index;
    uint16_t parent_node_ordinal;
    uint32_t feature_state;
    int32_t position_q16[3], scale_q16[3];
    int32_t held_offset_q16[3], draw_distance_q16;
    int32_t render_range_min_q16, render_range_max_q16;
    int16_t angle[3], animation_id, animation_frame;
    int32_t animation_accel, anim_state;
    int32_t effect_params_q16[4];
    uint32_t effect_lifetime;
    uint16_t opacity, render_range_state, billboard_state;
    uint16_t shadow_type, shadow_scale, shadow_solidity;
    uint16_t effect_kind, effect_flags;
    int8_t area_index;
    uint8_t active, render_active, switch_count, reserved;
    uint16_t switch_state[SM64_SATURN_ACTOR_MAX_SWITCHES];
} sm64_saturn_actor_source_observation_t;

typedef struct sm64_saturn_actor_capture_telemetry {
    uint16_t observed_count, published_count, rejected_count;
    uint16_t stale_generation_count, unknown_family_count;
    uint16_t malformed_count, capacity_overflow_count;
    uint16_t pool_slot_overflow_count;
    uint16_t despawned_count, pool_reuse_count;
    uint32_t identity_hash, generation_hash;
} sm64_saturn_actor_capture_telemetry_t;

typedef struct sm64_saturn_geo_state_observer {
    sm64_saturn_actor_source_observation_t observations[
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    uint8_t seen[SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY];
    uint8_t live[SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY];
    uint16_t incarnation[SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY];
    uint16_t capacity, count;
    uint32_t source_generation;
    uint32_t geo_evaluation_count, geo_rendered_count, geo_rejected_count;
    uint16_t despawned_count, pool_reuse_count, pool_slot_overflow_count;
    uint8_t overflow_latched;
    uint8_t reserved[3];
} sm64_saturn_geo_state_observer_t;

typedef struct sm64_saturn_actor_instance_bank {
    sm64_saturn_actor_instance_snapshot_t snapshots[2][
        SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE];
    uint16_t count[2];
    uint32_t generation[2];
    uint8_t state[2];
    uint8_t active_index;
    uint8_t reserved[3];
    uint32_t last_published_generation;
} sm64_saturn_actor_instance_bank_t;

enum {
    SM64_SATURN_ACTOR_INSTANCE_BANK_FREE = 0U,
    SM64_SATURN_ACTOR_INSTANCE_BANK_WRITING = 1U,
    SM64_SATURN_ACTOR_INSTANCE_BANK_READY = 2U,
    SM64_SATURN_ACTOR_INSTANCE_BANK_RENDERING = 3U,
    SM64_SATURN_ACTOR_INSTANCE_BANK_COMPLETE = 4U,
    SM64_SATURN_ACTOR_INSTANCE_BANK_QUARANTINED = 5U,
};

void sm64_saturn_geo_state_observer_init(
    sm64_saturn_geo_state_observer_t *observer, uint16_t capacity);
void sm64_saturn_geo_state_observer_begin_frame(
    sm64_saturn_geo_state_observer_t *observer, uint32_t generation);
uint32_t sm64_saturn_geo_state_observer_generation(
    const sm64_saturn_geo_state_observer_t *observer);
sm64_saturn_geo_state_observer_t *sm64_saturn_geo_state_observer_bound(void);
bool sm64_saturn_geo_state_observer_begin_object(
    sm64_saturn_geo_state_observer_t *observer,
    const sm64_saturn_actor_source_observation_t *observation);
bool sm64_saturn_geo_state_observer_record_switch(
    sm64_saturn_geo_state_observer_t *observer, uint16_t ordinal,
    uint16_t state);
bool sm64_saturn_geo_state_observer_end_object(
    sm64_saturn_geo_state_observer_t *observer);
void sm64_saturn_geo_state_observer_end_frame(
    sm64_saturn_geo_state_observer_t *observer);
void sm64_saturn_geo_state_observer_record_geo_decision(
    sm64_saturn_geo_state_observer_t *observer, bool rendered);
void sm64_saturn_geo_state_observer_record_authoritative_geo_decision(
    bool rendered);

/* Bind the authoritative observer used by the simple capture entry point. */
void sm64_saturn_actor_instances_set_observer(
    sm64_saturn_geo_state_observer_t *observer);

bool sm64_saturn_actor_instances_capture(
    sm64_saturn_actor_instance_snapshot_t *out, uint16_t capacity,
    uint32_t generation, uint16_t *count,
    sm64_saturn_actor_capture_telemetry_t *stats);

void sm64_saturn_actor_instance_bank_init(
    sm64_saturn_actor_instance_bank_t *bank);
bool sm64_saturn_actor_instance_bank_begin_write(
    sm64_saturn_actor_instance_bank_t *bank, uint32_t generation,
    uint8_t *index);
bool sm64_saturn_actor_instance_bank_publish(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index, uint16_t count,
    uint32_t generation);
bool sm64_saturn_actor_instance_bank_capture(
    sm64_saturn_actor_instance_bank_t *bank, uint32_t generation,
    uint16_t capacity, uint8_t *index, uint16_t *count,
    sm64_saturn_actor_capture_telemetry_t *stats);
const sm64_saturn_actor_instance_snapshot_t *
sm64_saturn_actor_instance_bank_acquire(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index,
    uint32_t generation, uint16_t *count);
bool sm64_saturn_actor_instance_bank_complete(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index);
bool sm64_saturn_actor_instance_bank_retire(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index);
/* Producer-owned only: READY may be recycled only before the exact
 * (index,generation) has been exposed to/acquired by a consumer. */
bool sm64_saturn_actor_instance_bank_recycle_pre_acquire(
    sm64_saturn_actor_instance_bank_t *bank, uint8_t index,
    uint32_t generation, uint8_t expected_state);
bool sm64_saturn_actor_instance_bank_quarantine(
    sm64_saturn_actor_instance_bank_t *bank, uint32_t generation);

/* Two complete banks plus the observer must stay below the platform/package
 * LWRAM bound.  The target assertion consumes Yaul's declared physical
 * LWRAM_SIZE; linker-side sourceboot margins remain a separate gate. */
_Static_assert(sizeof(sm64_saturn_actor_instance_snapshot_t) == 188U,
               "actor instance ABI size changed");
_Static_assert(_Alignof(sm64_saturn_actor_instance_snapshot_t) == 4U,
               "actor instance ABI alignment changed");
#if defined(__sh__)
_Static_assert(sizeof(sm64_saturn_actor_instance_bank_t) +
                   sizeof(sm64_saturn_geo_state_observer_t) <= LWRAM_SIZE,
               "actor snapshot banks exceed declared LWRAM bound");
#endif

#endif
