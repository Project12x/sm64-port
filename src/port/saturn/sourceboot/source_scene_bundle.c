#include "source_scene_bundle.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <yaul.h>

#include "../runtime/saturn_scene_package.h"
#include "../platform/saturn_cart_code.h"

#if !defined(SM64_SATURN_SOURCE_SCENE_BUNDLE_HOST_TEST)
#define SOURCE_SCENE_UNCACHED \
    __attribute__((section(".uncached"), used, aligned(4)))
#define SOURCE_SCENE_CPU_STATE \
    __attribute__((section(".lwram_bss"), used))
#else
#define SOURCE_SCENE_UNCACHED
#define SOURCE_SCENE_CPU_STATE
#endif

/* Materials use only these four actor-owned fields.  Retaining a full global
 * VDP1 partition here would both waste HWRAM and risk a terrain fallback at
 * the generic actor consumer. */
typedef struct source_scene_actor_texture_partitions {
    void *texture_base;
    uint32_t texture_size;
    void *clut_base;
    uint32_t clut_size;
} source_scene_actor_texture_partitions_t;

typedef struct source_scene_bundle_owner {
    sm64_saturn_actor_bundle_view_t bundle;
    sm64_saturn_actor_bundle_publication_t publication;
    sm64_saturn_actor_texture_publication_t textures;
    source_scene_actor_texture_partitions_t texture_partitions;
} source_scene_bundle_owner_t;

typedef union source_scene_bundle_scratch {
    /* Root validation is boot-only. The same fixed LWRAM becomes the two-lane
     * actor workspace after publication; these lifetimes never overlap. */
    sm64_saturn_scene_package_view_t root;
    uint8_t workspace[SM64_SATURN_SOURCE_SCENE_BUNDLE_WORKSPACE_BYTES];
} source_scene_bundle_scratch_t;

_Static_assert(sizeof(sm64_saturn_scene_package_view_t) >=
                   SM64_SATURN_SOURCE_SCENE_BUNDLE_WORKSPACE_BYTES,
               "boot view must fully cover the reused actor workspace");
#if !defined(SM64_SATURN_SOURCE_SCENE_BUNDLE_HOST_TEST)
_Static_assert(sizeof(source_scene_bundle_scratch_t) ==
                   SM64_SATURN_SOURCE_SCENE_BUNDLE_LIFETIME_BYTES,
               "source-scene lifetime workspace size changed");
_Static_assert(sizeof(source_scene_bundle_owner_t) == 2224U,
               "generic actor source owner exceeds its HWRAM budget");
#endif

/* Claims are shared with the slave and therefore live in uncached HWRAM. The
 * large boot/runtime scratch is caller-owned and phase-overlaid with Mario's
 * completed transform context instead of consuming another LWRAM region. */
static source_scene_bundle_owner_t source_scene_owner SOURCE_SCENE_UNCACHED;
/* Bound by sourceboot before the source scene is initialized. These two
 * master-only handles never cross the slave/VDP1 boundary, unlike owner. */
static void *source_scene_workspace SOURCE_SCENE_CPU_STATE;
static uint32_t source_scene_workspace_bytes SOURCE_SCENE_CPU_STATE;
volatile sm64_saturn_source_scene_bundle_probe_t
    g_sm64_saturn_source_scene_bundle_probe SOURCE_SCENE_CPU_STATE;
#if defined(SM64_SATURN_SOURCE_SCENE_BUNDLE_HOST_TEST)
static source_scene_bundle_scratch_t source_scene_host_workspace
    __attribute__((aligned(4)));
static uint8_t source_scene_upload_stage[
    SM64_SATURN_SOURCE_SCENE_BUNDLE_UPLOAD_STAGE_BYTES]
    __attribute__((aligned(32)));
static void *source_scene_upload_stage_get(void)
{
    return source_scene_upload_stage;
}
#else
/* Boot borrows a bounded prefix of the idle command bank. The stage is dead
 * before the first frame build, so generic actors add zero persistent HWRAM. */
extern void *sm64_saturn_source_scene_bundle_upload_stage(void);
static void *source_scene_upload_stage_get(void)
{
    return sm64_saturn_source_scene_bundle_upload_stage();
}
#endif

static source_scene_bundle_scratch_t *source_scene_workspace_get(void)
{
#if defined(SM64_SATURN_SOURCE_SCENE_BUNDLE_HOST_TEST)
    if (source_scene_workspace == NULL) {
        source_scene_workspace = &source_scene_host_workspace;
        source_scene_workspace_bytes = sizeof(source_scene_host_workspace);
    }
#endif
    if (source_scene_workspace == NULL ||
        ((uintptr_t)source_scene_workspace & 3U) != 0U ||
        source_scene_workspace_bytes < sizeof(source_scene_bundle_scratch_t))
        return NULL;
    return (source_scene_bundle_scratch_t *)source_scene_workspace;
}

bool sm64_saturn_source_scene_bundle_bind_workspace(void *workspace,
                                                     uint32_t byte_count)
{
    if (workspace == NULL || ((uintptr_t)workspace & 3U) != 0U ||
        byte_count < sizeof(source_scene_bundle_scratch_t) ||
        g_sm64_saturn_source_scene_bundle_probe.status ==
            SM64_SATURN_SOURCE_SCENE_BUNDLE_READY)
        return false;
    source_scene_workspace = workspace;
    source_scene_workspace_bytes = byte_count;
    memset(workspace, 0, sizeof(source_scene_bundle_scratch_t));
    return true;
}

static const uint8_t source_scene_actor_dependency_id[32] =
    "bob-area1-actors-v3";

enum {
    SOURCE_SCENE_LIFETIME_SCENE = 2U
};

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        uint32_t byte_count)
{
    uint8_t different = 0U;
    uint32_t index;
    for (index = 0U; index < byte_count; index++)
        different |= left[index] ^ right[index];
    return different == 0U;
}

static bool dependency_matches(
    const sm64_saturn_scene_dependency_view_t *dependency,
    const sm64_saturn_actor_bundle_view_t *bundle,
    const void *bundle_bytes, uint32_t byte_count)
{
    uint8_t digest[32];
    return dependency != NULL && bundle != NULL && bundle_bytes != NULL &&
        dependency->payload_kind == SM64_SATURN_SCENE_ACTOR_DEPENDENCIES &&
        dependency->destination_class == SM64_SATURN_SCENE_DESTINATION_CART &&
        dependency->lifetime == SOURCE_SCENE_LIFETIME_SCENE &&
        bytes_equal(dependency->stable_id,
                    source_scene_actor_dependency_id,
                    sizeof(source_scene_actor_dependency_id)) &&
        dependency->byte_count == byte_count && dependency->alignment == 4U &&
        dependency->dependency_mask == 0U &&
        dependency->maximum_scratch == 0U && dependency->generation != 0U &&
        dependency->generation == bundle->package_generation &&
        sm64_saturn_scene_package_sha256(bundle_bytes, byte_count, digest) &&
        bytes_equal(digest, dependency->content_sha256, sizeof(digest));
}

static SM64_SATURN_CART_COLD
bool fail(sm64_saturn_source_scene_bundle_status_t status)
{
    source_scene_bundle_scratch_t *const workspace =
        source_scene_workspace_get();
    void *const upload_stage = source_scene_upload_stage_get();
    memset(&source_scene_owner, 0, sizeof(source_scene_owner));
    if (workspace != NULL)
        memset(workspace, 0, sizeof(*workspace));
    if (upload_stage != NULL)
        memset(upload_stage, 0,
               SM64_SATURN_SOURCE_SCENE_BUNDLE_UPLOAD_STAGE_BYTES);
    memset((void *)&g_sm64_saturn_source_scene_bundle_probe, 0,
           sizeof(g_sm64_saturn_source_scene_bundle_probe));
    g_sm64_saturn_source_scene_bundle_probe.magic =
        SM64_SATURN_SOURCE_SCENE_BUNDLE_PROBE_MAGIC;
    g_sm64_saturn_source_scene_bundle_probe.status = (uint32_t)status;
    return false;
}

SM64_SATURN_CART_COLD
bool sm64_saturn_source_scene_bundle_init_from(
    const void *root, uint32_t root_bytes,
    const void *bundle, uint32_t bundle_bytes,
    uint16_t level_id, uint16_t area_id, uint32_t cart_offset,
    const vdp1_vram_partitions_t *partitions)
{
    const sm64_saturn_scene_dependency_view_t *dependency;
    source_scene_bundle_scratch_t *const workspace =
        source_scene_workspace_get();
    sm64_saturn_scene_package_view_t *const root_view =
        workspace != NULL ? &workspace->root : NULL;
    void *const upload_stage = source_scene_upload_stage_get();
    const uint32_t residency_generation = 1U;
    if (g_sm64_saturn_source_scene_bundle_probe.status ==
        SM64_SATURN_SOURCE_SCENE_BUNDLE_READY)
        return false;
    memset(&source_scene_owner, 0, sizeof(source_scene_owner));
    if (workspace != NULL)
        memset(workspace, 0, sizeof(*workspace));
    if (root == NULL || root_bytes == 0U || bundle == NULL ||
        bundle_bytes == 0U || partitions == NULL || upload_stage == NULL ||
        workspace == NULL ||
        (cart_offset & 3U) != 0U ||
        cart_offset > UINT32_MAX - bundle_bytes ||
        !sm64_saturn_scene_package_validate_target(
            root, root_bytes, root_view) ||
        root_view->level_id != level_id || root_view->area_id != area_id ||
        root_view->dependency_count != 1U)
        return fail(SM64_SATURN_SOURCE_SCENE_BUNDLE_INVALID_ROOT);
    if (!sm64_saturn_actor_bundle_validate(
            bundle, bundle_bytes, &source_scene_owner.bundle))
        return fail(SM64_SATURN_SOURCE_SCENE_BUNDLE_INVALID_BUNDLE);
    dependency = &root_view->dependencies[0];
    if (!dependency_matches(dependency, &source_scene_owner.bundle,
                            bundle, bundle_bytes) ||
        source_scene_owner.bundle.maximum_scratch >
            SM64_SATURN_SOURCE_SCENE_BUNDLE_WORKSPACE_BYTES)
        return fail(SM64_SATURN_SOURCE_SCENE_BUNDLE_INVALID_DEPENDENCY);
    memset(workspace, 0, sizeof(*workspace));
    if (!sm64_saturn_actor_texture_residency_activate(
            &source_scene_owner.textures, &source_scene_owner.bundle,
            partitions, upload_stage,
            SM64_SATURN_SOURCE_SCENE_BUNDLE_UPLOAD_STAGE_BYTES,
            residency_generation,
            true, true))
        return fail(SM64_SATURN_SOURCE_SCENE_BUNDLE_TEXTURE_FAILURE);
    source_scene_owner.texture_partitions.texture_base =
        partitions->texture_base;
    source_scene_owner.texture_partitions.texture_size =
        partitions->texture_size;
    source_scene_owner.texture_partitions.clut_base = partitions->clut_base;
    source_scene_owner.texture_partitions.clut_size = partitions->clut_size;
    if (!sm64_saturn_actor_bundle_runtime_publish(
            &source_scene_owner.publication, &source_scene_owner.bundle,
            residency_generation, cart_offset))
        return fail(SM64_SATURN_SOURCE_SCENE_BUNDLE_PUBLICATION_FAILURE);
    memset((void *)&g_sm64_saturn_source_scene_bundle_probe, 0,
           sizeof(g_sm64_saturn_source_scene_bundle_probe));
    g_sm64_saturn_source_scene_bundle_probe.residency_generation =
        residency_generation;
    g_sm64_saturn_source_scene_bundle_probe.package_generation =
        source_scene_owner.bundle.package_generation;
    g_sm64_saturn_source_scene_bundle_probe.root_bytes = root_bytes;
    g_sm64_saturn_source_scene_bundle_probe.bundle_bytes = bundle_bytes;
    g_sm64_saturn_source_scene_bundle_probe.cart_offset = cart_offset;
    g_sm64_saturn_source_scene_bundle_probe.family_count =
        source_scene_owner.bundle.family_count;
    g_sm64_saturn_source_scene_bundle_probe.variant_count =
        source_scene_owner.bundle.variant_count;
    g_sm64_saturn_source_scene_bundle_probe.magic =
        SM64_SATURN_SOURCE_SCENE_BUNDLE_PROBE_MAGIC;
    g_sm64_saturn_source_scene_bundle_probe.status =
        SM64_SATURN_SOURCE_SCENE_BUNDLE_READY;
    return true;
}

#if !defined(SM64_SATURN_SOURCE_SCENE_BUNDLE_HOST_TEST)
extern const uint8_t __sourceboot_cart_rodata_start[];
extern const uint8_t sm64_saturn_sourceboot_scene_package_root[];
extern const uint32_t sm64_saturn_sourceboot_scene_package_root_size;
extern const uint8_t sm64_saturn_sourceboot_actor_bundle[];
extern const uint32_t sm64_saturn_sourceboot_actor_bundle_size;

SM64_SATURN_CART_COLD
bool sm64_saturn_source_scene_bundle_init(uint16_t level_id,
                                          uint16_t area_id)
{
    sm64_saturn_actor_bundle_view_t bundle_view;
    vdp1_vram_partitions_t partitions, actor_partitions = {0};
    uint32_t texture_bytes, clut_bytes, texture_end;
    uint16_t mapping_count;
    const uintptr_t start = (uintptr_t)__sourceboot_cart_rodata_start;
    const uintptr_t bundle = (uintptr_t)sm64_saturn_sourceboot_actor_bundle;
    if (bundle < start || bundle - start > UINT32_MAX) return false;
    vdp1_vram_partitions_get(&partitions);
    if (!sm64_saturn_actor_bundle_validate(
            sm64_saturn_sourceboot_actor_bundle,
            sm64_saturn_sourceboot_actor_bundle_size, &bundle_view) ||
        !sm64_saturn_actor_texture_residency_requirements(
            &bundle_view, &texture_bytes, &clut_bytes, &mapping_count) ||
        mapping_count == 0U || partitions.remaining_base == NULL ||
        ((uintptr_t)partitions.remaining_base & 31U) != 0U ||
        texture_bytes > UINT32_MAX - 31U)
        return false;
    texture_end = (texture_bytes + 31U) & ~UINT32_C(31);
    if (texture_end > partitions.remaining_size ||
        clut_bytes > partitions.remaining_size - texture_end)
        return false;
    actor_partitions.texture_base = partitions.remaining_base;
    actor_partitions.texture_size = texture_bytes;
    actor_partitions.clut_base = (vdp1_clut_t *)(
        (uint8_t *)partitions.remaining_base + texture_end);
    actor_partitions.clut_size = clut_bytes;
    return sm64_saturn_source_scene_bundle_init_from(
        sm64_saturn_sourceboot_scene_package_root,
        sm64_saturn_sourceboot_scene_package_root_size,
        sm64_saturn_sourceboot_actor_bundle,
        sm64_saturn_sourceboot_actor_bundle_size,
        level_id, area_id, (uint32_t)(bundle - start), &actor_partitions);
}
#else
bool sm64_saturn_source_scene_bundle_init(uint16_t level_id,
                                          uint16_t area_id)
{
    (void)level_id;
    (void)area_id;
    return false;
}
#endif

bool sm64_saturn_source_scene_bundle_step(bool gameplay_suspended)
{
    (void)gameplay_suspended;
    return g_sm64_saturn_source_scene_bundle_probe.status ==
        SM64_SATURN_SOURCE_SCENE_BUNDLE_READY;
}

bool sm64_saturn_source_scene_bundle_resolve(
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_bundle_resolution_t *output)
{
    const uint32_t generation =
        g_sm64_saturn_source_scene_bundle_probe.residency_generation;
    source_scene_bundle_scratch_t *const workspace =
        source_scene_workspace_get();
    if (g_sm64_saturn_source_scene_bundle_probe.status !=
            SM64_SATURN_SOURCE_SCENE_BUNDLE_READY ||
        workspace == NULL ||
        !sm64_saturn_actor_bundle_runtime_claim(
            &source_scene_owner.publication, generation, lane))
        return false;
    if (sm64_saturn_actor_bundle_runtime_resolve(
            &source_scene_owner.publication, &source_scene_owner.bundle,
            workspace->workspace,
            SM64_SATURN_SOURCE_SCENE_BUNDLE_WORKSPACE_BYTES, snapshot,
            lane, records, draw_capacity, output))
        return true;
    (void)sm64_saturn_actor_bundle_runtime_release(
        &source_scene_owner.publication, generation, lane);
    return false;
}

bool sm64_saturn_source_scene_bundle_release(uint8_t lane,
                                             uint32_t generation)
{
    return sm64_saturn_actor_bundle_runtime_release(
        &source_scene_owner.publication, generation, lane);
}

const sm64_saturn_actor_texture_publication_t *
sm64_saturn_source_scene_bundle_textures(uint32_t generation)
{
    if (g_sm64_saturn_source_scene_bundle_probe.status !=
            SM64_SATURN_SOURCE_SCENE_BUNDLE_READY ||
        generation == 0U || source_scene_owner.textures.committed != 1U ||
        source_scene_owner.textures.generation != generation)
        return NULL;
    return &source_scene_owner.textures;
}

bool sm64_saturn_source_scene_bundle_texture_partitions(
    uint32_t generation, vdp1_vram_partitions_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (g_sm64_saturn_source_scene_bundle_probe.status !=
            SM64_SATURN_SOURCE_SCENE_BUNDLE_READY ||
        generation == 0U || source_scene_owner.textures.committed != 1U ||
        source_scene_owner.textures.generation != generation ||
        source_scene_owner.texture_partitions.texture_base == NULL ||
        source_scene_owner.texture_partitions.clut_base == NULL ||
        source_scene_owner.texture_partitions.texture_size <
            source_scene_owner.textures.texture_bytes ||
        source_scene_owner.texture_partitions.clut_size <
            source_scene_owner.textures.clut_bytes)
        return false;
    out->texture_base = source_scene_owner.texture_partitions.texture_base;
    out->texture_size = source_scene_owner.texture_partitions.texture_size;
    out->clut_base = source_scene_owner.texture_partitions.clut_base;
    out->clut_size = source_scene_owner.texture_partitions.clut_size;
    return true;
}

const sm64_saturn_source_scene_bundle_probe_t *
sm64_saturn_source_scene_bundle_probe(void)
{
    return (const sm64_saturn_source_scene_bundle_probe_t *)
        &g_sm64_saturn_source_scene_bundle_probe;
}
