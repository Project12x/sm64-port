/* Renderer-neutral actor output and failure ABI shared by queue and meshlets. */
#ifndef SM64_SATURN_ACTOR_OUTPUT_H
#define SM64_SATURN_ACTOR_OUTPUT_H

#include <stdint.h>

typedef enum sm64_saturn_actor_quarantine_reason {
    SM64_SATURN_ACTOR_QUARANTINE_NONE = 0U,
    SM64_SATURN_ACTOR_QUARANTINE_CLAIMANT_FAILURE = 1U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_GENERATION = 2U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_PACKAGE_GENERATION = 3U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_INSTANCE = 4U,
    SM64_SATURN_ACTOR_QUARANTINE_STALE_BANK = 5U,
    SM64_SATURN_ACTOR_QUARANTINE_OUTPUT_OVERFLOW = 6U,
} sm64_saturn_actor_quarantine_reason_t;

typedef struct sm64_saturn_actor_output_record {
    uint16_t meshlet_id;
    uint16_t primitive_id;
    uint32_t sort_key;
} sm64_saturn_actor_output_record_t;

_Static_assert(sizeof(sm64_saturn_actor_output_record_t) == 8U,
               "actor output-record ABI changed");

#endif
