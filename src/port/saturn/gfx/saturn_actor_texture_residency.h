#ifndef SM64_SATURN_ACTOR_TEXTURE_RESIDENCY_H
#define SM64_SATURN_ACTOR_TEXTURE_RESIDENCY_H

#include "saturn_actor_bundle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep the public publication header hardware-header-free. The pinned Yaul
 * VDP1 header uses this tag; only the master-owned implementation needs the
 * complete partition definition. */
typedef struct vdp1_vram_partitions vdp1_vram_partitions_t;

#define SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY 128U

/* Runtime-local scalar mapping published by master-owned scene activation.
 * No VDP1 pointer, allocator state, queue ticket, or mutable residency object
 * enters this record or a worker descriptor. */
typedef struct sm64_saturn_actor_texture_mapping {
    uint32_t bank_id;
    uint32_t texture_base_offset;
    uint16_t clut_base_index;
    uint16_t tile_count;
    uint32_t generation;
} sm64_saturn_actor_texture_mapping_t;

typedef struct sm64_saturn_actor_texture_publication {
    sm64_saturn_actor_texture_mapping_t
        mappings[SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY];
    uint32_t texture_bytes;
    uint32_t clut_bytes;
    uint32_t generation;
    uint16_t mapping_count;
    volatile uint8_t committed;
    uint8_t reserved;
} sm64_saturn_actor_texture_publication_t;

_Static_assert(SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY ==
                   SM64_SATURN_ACTOR_BUNDLE_MAX_VARIANTS,
               "every valid S64F variant must fit the publication");
_Static_assert(sizeof(sm64_saturn_actor_texture_mapping_t) == 16U &&
                   _Alignof(sm64_saturn_actor_texture_mapping_t) == 4U,
               "actor texture mapping ABI must remain scalar and SH-2 aligned");
_Static_assert(offsetof(sm64_saturn_actor_texture_mapping_t, bank_id) == 0U &&
                   offsetof(sm64_saturn_actor_texture_mapping_t,
                            texture_base_offset) == 4U &&
                   offsetof(sm64_saturn_actor_texture_mapping_t,
                            clut_base_index) == 8U &&
                   offsetof(sm64_saturn_actor_texture_mapping_t,
                            tile_count) == 10U &&
                   offsetof(sm64_saturn_actor_texture_mapping_t,
                            generation) == 12U,
               "actor texture mapping field layout changed");
_Static_assert(sizeof(sm64_saturn_actor_texture_publication_t) == 2064U &&
                   _Alignof(sm64_saturn_actor_texture_publication_t) == 4U &&
                   offsetof(sm64_saturn_actor_texture_publication_t,
                            generation) == 2056U &&
                   offsetof(sm64_saturn_actor_texture_publication_t,
                            committed) == 2062U,
               "actor publication must remain a fixed 2064-byte HWRAM owner");

/* Complete scalar consistency check. This has no bundle dependency and must
 * gate both replacement prestate and every lookup. */
bool sm64_saturn_actor_texture_publication_validate(
    const sm64_saturn_actor_texture_publication_t *publication);

/* Exact resident byte requirements for a fully validated bundle. Callers use
 * this to carve a dedicated actor partition from Yaul's remaining VDP1 VRAM;
 * terrain/Mario partitions are never aliased or overwritten. */
bool sm64_saturn_actor_texture_residency_requirements(
    const sm64_saturn_actor_bundle_view_t *bundle,
    uint32_t *texture_bytes, uint32_t *clut_bytes,
    uint16_t *mapping_count);

/* Master-only, all-resident activation. The checked DMA queue must already be
 * initialized and exclusively owned by the scene transition. Staging is a
 * caller-owned, 4-byte-aligned HWRAM span; it must not overlap the bundle,
 * publication, or either VDP1 destination region. The complete bundle, memory
 * ownership, and transfer plan are validated before the first transfer; a
 * later failure may leave dirty VRAM bytes, but committed remains zero so they
 * are unreachable. */
bool sm64_saturn_actor_texture_residency_activate(
    sm64_saturn_actor_texture_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    const vdp1_vram_partitions_t *partitions,
    void *staging, uint32_t staging_capacity,
    uint32_t generation, bool gameplay_suspended, bool vdp1_idle);

/* Copies one scalar mapping only when publication and mapping both match the
 * exact current nonzero generation. Output is cleared on every failure. */
bool sm64_saturn_actor_texture_residency_lookup(
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t generation, uint32_t bank_id,
    sm64_saturn_actor_texture_mapping_t *out);

#endif
