#ifndef SM64_SATURN_ACTOR_MATERIAL_H
#define SM64_SATURN_ACTOR_MATERIAL_H

#include "saturn_actor_bank.h"
#include "saturn_ir_texture.h"

#include <stdbool.h>
#include <stdint.h>

/* Runtime-local mapping published by the master-owned scene activation path.
 * All fields are scalars; no VDP1 pointer or allocator state is serialized or
 * exposed to a worker descriptor. */
typedef struct sm64_saturn_actor_texture_mapping {
    uint32_t bank_id;
    uint32_t texture_base_offset;
    uint16_t clut_base_index;
    uint16_t tile_count;
    uint32_t generation;
} sm64_saturn_actor_texture_mapping_t;

/* Master-only final-emission boundary. The caller supplies a validated S64B-v2
 * view and the current master-owned mapping/generation. Workers must never call
 * this function or receive cmdt/partition/mapping pointers. A false return
 * leaves the complete command table entry byte-exact. */
bool sm64_saturn_actor_material_bind(
    vdp1_cmdt_t *cmdt, const vdp1_vram_partitions_t *partitions,
    const sm64_saturn_actor_bank_view_t *bank, uint16_t primitive_id,
    const sm64_saturn_actor_texture_mapping_t *mapping,
    uint32_t active_generation,
    const int16_vec2_t vertices[4]);

#endif
