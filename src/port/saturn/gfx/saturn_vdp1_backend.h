#ifndef SM64_SATURN_VDP1_BACKEND_H
#define SM64_SATURN_VDP1_BACKEND_H

#include <stdlib.h>
#include <string.h>
#include <yaul.h>

#include "saturn_command_arena.h"

/* Shared Yaul VDP1 list lifetime for all Saturn renderer clients.
 *
 * Dependency/API adaptation: libyaul 6012f79f (MIT), specifically
 * vdp1_cmdt_list_alloc() and vdp1_sync_cmdt_list_put(). Jo Engine's pinned
 * persistent-list shape remains pattern-only; no allocator code is copied. */
typedef struct sm64_saturn_vdp1_backend {
    vdp1_cmdt_list_t list;
    sm64_saturn_command_arena_t commands;
} sm64_saturn_vdp1_backend_t;

static inline bool
sm64_saturn_vdp1_backend_init(sm64_saturn_vdp1_backend_t *backend,
                              uint16_t capacity, int16_vec2_t clip,
                              int16_vec2_t local)
{
    if (capacity <= 2U)
        return false;

    /* memalign, not malloc: vdp1_cmdt_t is __aligned(32)
     * (yaul/vdp1/cmdt.h), and this project's malloc is TLSF-backed
     * (.yaul.env: YAUL_OPTION_MALLOC_IMPL=tlsf) -- on this 32-bit target
     * TLSF only guarantees 4-byte alignment, far short of the required
     * 32. This preserves the exact allocation behavior of the
     * vdp1_cmdt_list_alloc() call this replaces (which itself calls
     * memalign(sizeof(vdp1_cmdt_t), ...) internally); only the storage
     * of the 8-byte list header changed shape, from a second heap
     * allocation to this struct's embedded `list` field. */
    vdp1_cmdt_t *cmdts = memalign(sizeof(vdp1_cmdt_t),
                                  sizeof(vdp1_cmdt_t) * capacity);
    if (cmdts == NULL)
        return false;
    vdp1_cmdt_list_init(&backend->list, cmdts);

    (void)memset(backend->list.cmdts, 0, sizeof(vdp1_cmdt_t) * capacity);
    sm64_saturn_command_arena_init(&backend->commands, capacity, 2);
    vdp1_cmdt_system_clip_coord_set(&backend->list.cmdts[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&backend->list.cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&backend->list.cmdts[1]);
    vdp1_cmdt_vtx_local_coord_set(&backend->list.cmdts[1], local);
    vdp1_cmdt_end_set(&backend->list.cmdts[2]);
    backend->list.count = 3;
    return true;
}

/* Caller-supplied (e.g. LWRAM) storage variant of
 * sm64_saturn_vdp1_backend_init. `cmdts` must point to at least
 * `capacity` vdp1_cmdt_t entries, must be aligned to sizeof(vdp1_cmdt_t)
 * (32 bytes -- the same alignment the heap path below gets from
 * memalign()), and must outlive the backend. Unlike the heap path
 * above, this performs no allocation, so satisfying the alignment
 * requirement is the caller's responsibility.
 *
 * Zero-init note: neither vdp1_cmdt_list_init() nor any linker-driven
 * zero-init covers caller-supplied storage outside .bss (a new LWRAM
 * output section is not .bss and crt0's _bss_clear() never visits it --
 * see the design spec's "VDP1 backend entry point" finding). This
 * function must therefore memset explicitly, exactly mirroring the
 * heap path above. */
static inline bool
sm64_saturn_vdp1_backend_init_with_storage(sm64_saturn_vdp1_backend_t *backend,
                                           vdp1_cmdt_t *cmdts,
                                           uint16_t capacity,
                                           int16_vec2_t clip,
                                           int16_vec2_t local)
{
    if (capacity <= 2U || cmdts == NULL)
        return false;

    vdp1_cmdt_list_init(&backend->list, cmdts);
    (void)memset(backend->list.cmdts, 0, sizeof(vdp1_cmdt_t) * capacity);
    sm64_saturn_command_arena_init(&backend->commands, capacity, 2);
    vdp1_cmdt_system_clip_coord_set(&backend->list.cmdts[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&backend->list.cmdts[0], clip);
    vdp1_cmdt_local_coord_set(&backend->list.cmdts[1]);
    vdp1_cmdt_vtx_local_coord_set(&backend->list.cmdts[1], local);
    vdp1_cmdt_end_set(&backend->list.cmdts[2]);
    backend->list.count = 3;
    return true;
}

static inline void
sm64_saturn_vdp1_backend_begin(sm64_saturn_vdp1_backend_t *backend)
{
    vdp1_cmdt_end_clear(&backend->list.cmdts[
        sm64_saturn_command_arena_begin(&backend->commands)]);
}

static inline vdp1_cmdt_t *
sm64_saturn_vdp1_backend_reserve(sm64_saturn_vdp1_backend_t *backend,
                                 uint16_t count)
{
    uint16_t first;
    if (!sm64_saturn_command_arena_reserve(&backend->commands, count, &first))
        return NULL;
    return &backend->list.cmdts[first];
}

static inline void
sm64_saturn_vdp1_backend_finish(sm64_saturn_vdp1_backend_t *backend)
{
    vdp1_cmdt_end_set(&backend->list.cmdts[
        sm64_saturn_command_arena_finish(&backend->commands)]);
    backend->list.count = backend->commands.live_count;
}

static inline void
sm64_saturn_vdp1_backend_upload(sm64_saturn_vdp1_backend_t *backend)
{
    vdp1_sync_cmdt_list_put(&backend->list, 0);
}

#endif
