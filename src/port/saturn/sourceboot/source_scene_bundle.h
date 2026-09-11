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
    /* MUST be >= the bake's SM64_SATURN_ACTOR_BUNDLE_WORKSPACE_BYTES in the
     * generated actor_bundle_capacity.h. That header is not on this include
     * path, so this constant is maintained by hand and
     * compile_actor_family_bundle.py enforces the relationship at bake time.
     *
     * It is enforced because getting it wrong is silent: when BOB's actor
     * roster grew from 18 to 27 variants the bake needed 3,328 B while this
     * still said 3,072, and the result was a clean build that booted, matched
     * its sealed identity, and then presented zero frames forever -- a black
     * screen with no fault reported by either CPU. A build error is a much
     * cheaper way to learn that.
     *
     * King Bob-omb needs 5,891 B, rounded to 6,144. The LWRAM phase union
     * holds the larger of this workspace, the boot view and Mario context;
     * their sequential ownership is unchanged. No boot-view fields grow. */
    SM64_SATURN_SOURCE_SCENE_BUNDLE_WORKSPACE_BYTES = 6144U,
    SM64_SATURN_SOURCE_SCENE_BUNDLE_LIFETIME_BYTES = 6144U,
    /* Boot staging for ONE actor bank's texture or CLUT payload, borrowed
     * from the idle command bank (53,248 B, so this is free space, not a
     * budget). preflight_span() in saturn_actor_texture_residency.c rejects
     * any single bank whose resident bytes exceed this, and a rejected bundle
     * takes the same silent path as the workspace overflow above: init fails,
     * sourceboot spins at main.c's `for (;;)`, and the console shows a black
     * screen with no fault from either CPU.
     *
     * 2560 -> 4096 (2026-09-03): BOB's eight new objects and enemies put
     * MODEL_GOOMBA's texture bank at 3,584 B, over the old value. This is a
     * PER-BANK limit, not a total -- growing the roster with small actors
     * never trips it, and one large actor trips it alone.
     * compile_actor_family_bundle.py enforces the relationship at bake time.
     *
     * 4096 -> 6144 (2026-09-10): MODEL_BOB_SEESAW_PLATFORM's texture bank is
     * 4,608 B. Rounded up to 6,144 to match the workspace above rather than
     * tracking each actor by 512 B; the space is still borrowed from the idle
     * command bank, so the only cost is that a bank between 4,608 and 6,144 B
     * no longer fails the bake. That enforcement is what makes this safe to
     * round: the 2026-09-03 note above was written after two of these limits
     * silently black-screened the console, and the bake now names the exact
     * constant and byte count instead. */
    SM64_SATURN_SOURCE_SCENE_BUNDLE_UPLOAD_STAGE_BYTES = 6144U,
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
