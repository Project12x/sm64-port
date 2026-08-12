/* Pointer-free S64F-v3 generation publication and fixed-lane resolution. */
#ifndef SM64_SATURN_ACTOR_BUNDLE_RUNTIME_H
#define SM64_SATURN_ACTOR_BUNDLE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "saturn_actor_bundle.h"
#include "saturn_actor_meshlets.h"

typedef struct sm64_saturn_actor_bundle_publication {
    uint32_t residency_generation;
    uint32_t package_generation;
    uint32_t cart_offset;
    uint32_t byte_count;
    uint32_t content_hash_words[8];
    uint32_t workspace_lane_stride;
    uint32_t maximum_scratch;
    uint16_t family_count;
    uint16_t variant_count;
    volatile uint8_t lane_claim[2];
    volatile uint8_t committed;
    uint8_t reserved;
} sm64_saturn_actor_bundle_publication_t;

typedef struct sm64_saturn_actor_bundle_resolution {
    sm64_saturn_actor_bundle_variant_t variant;
    sm64_saturn_actor_bank_view_t bank;
    sm64_saturn_actor_meshlet_workspace_t workspace;
    uint32_t residency_generation;
    uint32_t package_generation;
    uint8_t lane;
    uint8_t reserved[3];
} sm64_saturn_actor_bundle_resolution_t;

_Static_assert(sizeof(sm64_saturn_actor_bundle_publication_t) == 64U,
               "actor bundle publication ABI changed");
_Static_assert(offsetof(sm64_saturn_actor_bundle_publication_t, lane_claim) ==
                   60U,
               "actor bundle lane-claim offset changed");
_Static_assert(offsetof(sm64_saturn_actor_bundle_publication_t, committed) ==
                   62U,
               "actor bundle commit offset changed");

bool sm64_saturn_actor_bundle_runtime_publish(
    sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    uint32_t residency_generation, uint32_t cart_offset);
bool sm64_saturn_actor_bundle_runtime_claim(
    sm64_saturn_actor_bundle_publication_t *publication,
    uint32_t residency_generation, uint8_t lane);
bool sm64_saturn_actor_bundle_runtime_resolve(
    const sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    void *workspace, uint32_t workspace_capacity,
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_bundle_resolution_t *output);
bool sm64_saturn_actor_bundle_runtime_release(
    sm64_saturn_actor_bundle_publication_t *publication,
    uint32_t residency_generation, uint8_t lane);

#endif
