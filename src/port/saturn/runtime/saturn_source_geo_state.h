#ifndef SM64_SATURN_SOURCE_GEO_STATE_H
#define SM64_SATURN_SOURCE_GEO_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_source_geo_digest {
    uint32_t generation;
    uint32_t object_count;
    uint32_t animation_digest;
    uint32_t geo_state_digest;
    uint32_t visibility_digest;
} sm64_saturn_source_geo_digest_t;

/*
 * Reserved evidence boundary for a future state-only source-geo pass.
 *
 * The current source graph interleaves state mutations with matrix and
 * generated-display-list construction. Until an exact differential proves a
 * narrower boundary, this entry point deliberately fails closed and leaves
 * the caller's digest untouched. The normal geo_process_root() walk remains
 * the sole authoritative source-state update.
 */
bool sm64_saturn_source_geo_update_state(
    uint32_t generation, sm64_saturn_source_geo_digest_t *digest);

#endif
