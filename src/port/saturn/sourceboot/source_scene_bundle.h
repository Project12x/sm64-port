#ifndef SM64_SATURN_SOURCEBOOT_SOURCE_SCENE_BUNDLE_H
#define SM64_SATURN_SOURCEBOOT_SOURCE_SCENE_BUNDLE_H

#include <stdbool.h>
#include <stdint.h>

#include "../gfx/saturn_actor_bundle_runtime.h"
#include "../gfx/saturn_actor_texture_residency.h"

typedef enum sm64_saturn_source_scene_bundle_status {
    SM64_SATURN_SOURCE_SCENE_BUNDLE_EMPTY = 0,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_INVALID_ROOT,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_INVALID_DEPENDENCY,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_INVALID_BUNDLE,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_TEXTURE_FAILURE,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_PUBLICATION_FAILURE,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_READY,
} sm64_saturn_source_scene_bundle_status_t;

typedef struct sm64_saturn_source_scene_bundle_probe {
    uint32_t magic;
    uint32_t status;
    uint32_t residency_generation;
    uint32_t package_generation;
    uint32_t root_bytes;
    uint32_t bundle_bytes;
    uint32_t cart_offset;
    uint16_t family_count;
    uint16_t variant_count;
} sm64_saturn_source_scene_bundle_probe_t;

enum {
    SM64_SATURN_SOURCE_SCENE_BUNDLE_PROBE_MAGIC = 0x5342554EU,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_BOB_LEVEL_ID = 9U,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_BOB_AREA_ID = 1U,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_WORKSPACE_BYTES = 3072U,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_LIFETIME_BYTES = 3348U,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_UPLOAD_STAGE_BYTES = 2560U,
};

bool sm64_saturn_source_scene_bundle_bind_workspace(void *workspace,
                                                     uint32_t byte_count);
bool sm64_saturn_source_scene_bundle_init(uint16_t level_id,
                                          uint16_t area_id);
bool sm64_saturn_source_scene_bundle_init_from(
    const void *root, uint32_t root_bytes,
    const void *bundle, uint32_t bundle_bytes,
    uint16_t level_id, uint16_t area_id, uint32_t cart_offset,
    const vdp1_vram_partitions_t *partitions);
bool sm64_saturn_source_scene_bundle_step(bool gameplay_suspended);
bool sm64_saturn_source_scene_bundle_resolve(
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_bundle_resolution_t *output);
bool sm64_saturn_source_scene_bundle_release(uint8_t lane,
                                             uint32_t generation);
const sm64_saturn_actor_texture_publication_t *
sm64_saturn_source_scene_bundle_textures(uint32_t generation);
/* Generic material offsets are relative to this actor-only partition, never
 * to the terrain/Mario VDP1 partition. */
bool sm64_saturn_source_scene_bundle_texture_partitions(
    uint32_t generation, vdp1_vram_partitions_t *out);
const sm64_saturn_source_scene_bundle_probe_t *
sm64_saturn_source_scene_bundle_probe(void);

#endif
