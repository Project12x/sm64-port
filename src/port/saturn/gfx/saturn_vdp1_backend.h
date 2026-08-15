#ifndef SM64_SATURN_VDP1_BACKEND_H
#define SM64_SATURN_VDP1_BACKEND_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <yaul.h>

#include "saturn_command_arena.h"
#include "saturn_vdp1_frame_bank.h"

/* Shared Yaul VDP1 list lifetime for all Saturn renderer clients.
 *
 * Dependency/API adaptation: libyaul 6012f79f (MIT), specifically
 * vdp1_cmdt_list_alloc() and vdp1_sync_cmdt_list_put(). Jo Engine's pinned
 * persistent-list shape remains pattern-only; no allocator code is copied. */
typedef struct sm64_saturn_vdp1_backend {
    vdp1_cmdt_list_t list;
    sm64_saturn_command_arena_t commands;
} sm64_saturn_vdp1_backend_t;

/* Switch a caller-owned command list after the previous VDP1 cycle has
 * retired.  The setup commands are initialized once per bank by the caller;
 * this operation only resets the arena cursor and list prefix. */
static inline void
sm64_saturn_vdp1_backend_bind_storage(sm64_saturn_vdp1_backend_t *backend,
                                      vdp1_cmdt_t *cmdts,
                                      uint16_t capacity)
{
    backend->list.cmdts = cmdts;
    backend->list.count = 3U;
    sm64_saturn_command_arena_init(&backend->commands, capacity, 2U);
}

static inline void sm64_saturn_vdp1_backend_bind_frame_bank(
    sm64_saturn_vdp1_backend_t *backend,
    const sm64_saturn_vdp1_frame_bank_t *bank)
{
    sm64_saturn_vdp1_backend_bind_storage(
        backend, (vdp1_cmdt_t *)bank->command_storage,
        bank->command_capacity);
}

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
    /* A prior painter-chain finalization turns the local-coordinate command
     * into the head of a linked draw list.  Every new build starts from the
     * ordinary sequential prefix so stale links cannot cross generations. */
    vdp1_cmdt_jump_next(&backend->list.cmdts[
        backend->commands.setup_count - 1U]);
    backend->list.cmdts[backend->commands.setup_count - 1U].cmd_link = 0U;
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

/* Convert the completed sequential draw range into one VDP1 painter chain.
 * Lowerers temporarily put their shared depth-bin tag in `cmd_link`; once all
 * commands have been emitted, this rewrites the same field with libyaul's
 * normal JUMP_ASSIGN links.  No command storage or per-frame side buffer is
 * allocated.  Validation happens before the first write, so an invalid tag
 * leaves the completed bank available for caller quarantine rather than
 * publishing a partial chain.
 *
 * Build the links nearest-to-farthest, scanning each bin in reverse command
 * order.  Prepending each entry then yields a far-to-near execution list with
 * stable original producer ordering for equal bins.  Linked entries are
 * recognized by their non-sequential VDP1 link type, so overwriting `cmd_link`
 * never changes the classification of a later scan. */
static inline bool
sm64_saturn_vdp1_backend_link_depth_bins(
    sm64_saturn_vdp1_backend_t *backend, uint16_t bin_count)
{
    uint16_t first;
    uint16_t end;
    uint16_t next;
    uint16_t bin;
    uint16_t index;

    if (backend == NULL || bin_count == 0U ||
        backend->commands.setup_count == 0U ||
        backend->commands.live_count == 0U ||
        backend->commands.live_count > backend->commands.capacity ||
        backend->list.count != backend->commands.live_count)
        return false;

    first = backend->commands.setup_count;
    end = (uint16_t)(backend->commands.live_count - 1U);
    if (first > end || backend->commands.cursor != end ||
        backend->commands.previous_end != end)
        return false;

    /* Validate every freshly lowered depth-bin tag before mutating even the
     * local-coordinate prefix.  Rebound frame-bank storage may still carry a
     * prior JUMP_ASSIGN type, but vdp1_cmdt_jump_assign() overwrites it when
     * this frame's raw tag selects the command. */
    for (index = first; index < end; index++) {
        const vdp1_cmdt_t *const cmdt = &backend->list.cmdts[index];
        if (cmdt->cmd_link >= bin_count)
            return false;
    }
    /* The raw tags are now known-valid.  Their command records can retain a
     * previous frame's link type because lowerers intentionally overwrite
     * only cmd_link; strip that type before JUMP_ASSIGN establishes this
     * frame's chain.  This is after full validation, so bad tags still leave
     * the completed list unchanged. */
    for (index = first; index < end; index++)
        backend->list.cmdts[index].cmd_ctrl &= 0x8FFFU;

    next = end;
    for (bin = 0U; bin < bin_count; bin++) {
        index = end;
        while (index > first) {
            vdp1_cmdt_t *cmdt;
            index--;
            cmdt = &backend->list.cmdts[index];
            if ((cmdt->cmd_ctrl & 0x7000U) != 0U ||
                cmdt->cmd_link != bin)
                continue;
            vdp1_cmdt_jump_assign(cmdt, next);
            next = index;
        }
    }

    if (next == end) {
        vdp1_cmdt_jump_next(
            &backend->list.cmdts[backend->commands.setup_count - 1U]);
        backend->list.cmdts[backend->commands.setup_count - 1U].cmd_link =
            0U;
    } else {
        vdp1_cmdt_jump_assign(
            &backend->list.cmdts[backend->commands.setup_count - 1U], next);
    }
    return true;
}

#if defined(SM64_SATURN_VDP1_BACKEND_LINK_REFERENCE)
/* Host-only predecessor oracle for the painter relink, kept verbatim so the
 * equivalence test can compare a candidate implementation against the exact
 * bytes this one produces.  It is never compiled into a Saturn image: only
 * tools/saturn/vdp1_painter_chain_test.c defines the selecting macro.  This
 * mirrors the same arrangement saturn_terrain_depth_bins.h already uses for
 * its predecessor merge oracle (SM64_SATURN_TERRAIN_DEPTH_BINS_COMPARE).
 *
 * Cost: one validation pass, one link-type strip pass, then bin_count full
 * rescans of the live command range -- N * (2 + bin_count) record visits. */
static inline bool
sm64_saturn_vdp1_backend_link_depth_bins_reference(
    sm64_saturn_vdp1_backend_t *backend, uint16_t bin_count)
{
    uint16_t first;
    uint16_t end;
    uint16_t next;
    uint16_t bin;
    uint16_t index;

    if (backend == NULL || bin_count == 0U ||
        backend->commands.setup_count == 0U ||
        backend->commands.live_count == 0U ||
        backend->commands.live_count > backend->commands.capacity ||
        backend->list.count != backend->commands.live_count)
        return false;

    first = backend->commands.setup_count;
    end = (uint16_t)(backend->commands.live_count - 1U);
    if (first > end || backend->commands.cursor != end ||
        backend->commands.previous_end != end)
        return false;

    for (index = first; index < end; index++) {
        const vdp1_cmdt_t *const cmdt = &backend->list.cmdts[index];
        if (cmdt->cmd_link >= bin_count)
            return false;
    }
    for (index = first; index < end; index++)
        backend->list.cmdts[index].cmd_ctrl &= 0x8FFFU;

    next = end;
    for (bin = 0U; bin < bin_count; bin++) {
        index = end;
        while (index > first) {
            vdp1_cmdt_t *cmdt;
            index--;
            cmdt = &backend->list.cmdts[index];
            if ((cmdt->cmd_ctrl & 0x7000U) != 0U ||
                cmdt->cmd_link != bin)
                continue;
            vdp1_cmdt_jump_assign(cmdt, next);
            next = index;
        }
    }

    if (next == end) {
        vdp1_cmdt_jump_next(
            &backend->list.cmdts[backend->commands.setup_count - 1U]);
        backend->list.cmdts[backend->commands.setup_count - 1U].cmd_link =
            0U;
    } else {
        vdp1_cmdt_jump_assign(
            &backend->list.cmdts[backend->commands.setup_count - 1U], next);
    }
    return true;
}
#endif

/* Per-frame command-table upload. Two transfer paths, selected at
 * compile time by SM64_SATURN_VDP1_LWRAM_STAGING (defined by sourceboot's
 * CPU-DMAC command path; its current double buffer is HWRAM-resident after
 * the actor-arena reclaim correction):
 *
 * HWRAM (heap) staging -- castleviewer/marioturntable -- keeps libyaul's
 * stock vdp1_sync_cmdt_list_put(): _vdp1_sync_put() flag arming plus an
 * SCU-DMA read of the array (vdp_sync.c). SCU DMA from HWRAM to the
 * B-bus is legal, and it is the mechanism those targets have always
 * shipped with.
 *
 * Why a compile-time switch and not a runtime source-address check: the
 * check itself is cheap, but a runtime branch keeps BOTH paths live in
 * every image. For sourceboot that means linking the (dead) SCU chain
 * -- .text.vdp1_sync_cmdt_put.part.0 (0xD8) + .text.vdp1_sync_cmdt_put
 * (0x18) + vdp1_sync_cmdt_list_put -- on top of the LWRAM path's
 * vdp1_sync_wait (0x2C) and vdp1_sync_force_put (0x84) sections, and
 * the E2 image has only ~0xBC bytes of work-RAM headroom: the
 * runtime-dispatch build measured +272 text bytes and pushed the
 * .uncached section past the physical end of HWRAM (silently -- that
 * section's absolute 0x2xxxxxxx VMA escapes the linker's `ram' region
 * accounting). Compile-time selection lets --gc-sections drop the
 * unused path per target, and the delta fits. The SCU build keeps a
 * runtime assert as the safety net for the misconfiguration this
 * removes the address check for (an LWRAM-staged backend built without
 * the macro), so the failure mode is a loud assert instead of a silent
 * on-hardware lockup.
 *
 * The sourceboot CPU-DMAC path must NOT take the SCU path when a future
 * profile puts command staging back in LWRAM: SCU DMA cannot touch LWRAM.
 * Three independent sources agree:
 *   - libyaul's own scu/dma.h:32: "Reading from or writing to LWRAM
 *     locks up the machine."
 *   - Sega's SGL 3.02j release notes (SGL020A.TXT): PCM data placed in
 *     WORKRAM-L "could not be transferred" by SCU DMA; Sega moved those
 *     transfers to CPU DMA.
 *   - Sega's GFS manual (MANGFS.TXT section 1.7): WORKRAM-L transfer
 *     destinations force a CPU software copy even when SCU DMA is
 *     requested.
 * vdp1_sync_cmdt_put() programs the SCU-DMA read register with the raw
 * array address -- no region check, no fallback (vdp_sync.c:408) -- so
 * on real hardware the first LWRAM-staged frame would hang the machine.
 * (Ymir's SCU DMA is an unrestricted bus read and does not model the
 * restriction; emulator runs cannot catch this.)
 *
 * The LWRAM path below replaces only the transfer mechanism and drives
 * libyaul's vdp_sync flag machine through the same transitions, in the
 * same order, as the SCU path:
 *
 *  1. vdp1_sync_wait(): frame-overrun guard. If the previous cycle's
 *     VBLANK-IN/OUT pair has not yet retired (SYNC_FLAG_VDP1_SYNC still
 *     set), block until _vdp1_mode_auto_vblank_out clears it along with
 *     all VDP1 flags (vdp_sync.c:852-854). This stands in for the wait
 *     at the top of _vdp1_sync_put() (vdp_sync.c:712-718) that the SCU
 *     path performs before touching VRAM; gating on the SYNC flag
 *     releases one half-cycle later (VBLANK-OUT reset instead of the
 *     VBLANK-IN commit), which is strictly more conservative, and in
 *     the steady state -- where the flag is already clear -- costs
 *     nothing.
 *  2. CPU longword copy into the VDP1 VRAM command table. Sega's own
 *     answer for WORKRAM-L bulk moves is a CPU-driven transfer (SGL's
 *     slDMACopy is SH-2 on-chip DMA; the GFS manual's WORKRAM-L path
 *     is a CPU software copy). Between those two flavors, the HWRAM
 *     budget decides: routing this through libyaul's
 *     cpu_dmac_transfer()/cpu_dmac_transfer_wait() drags two
 *     otherwise-gc'd function sections (libyaul builds with
 *     -ffunction-sections; sourceboot links with --gc-sections) into a
 *     work-RAM image that is within ~0.4 KiB of full -- the E2 link
 *     overflows `ram' by ~80 bytes with them -- while this loop
 *     inlines to a few instructions and pulls in nothing. Access
 *     width: VDP1 VRAM accepts CPU word writes (libyaul's own
 *     _vdp1_init() writes the draw-end command with
 *     MEMORY_WRITE(16, VDP1_VRAM(0), 0x8000), vdp_init.c:71), and
 *     32-bit master accesses to the same region are exercised on every
 *     boot by __vdp_init()'s cpu_dmac_memset() clear of all 512 KiB at
 *     4-byte stride (vdp_init.c:55) -- the on-chip DMAC issues its
 *     external bus cycles through the same bus-state controller as CPU
 *     stores, and the SCU splits either master's longword into two
 *     16-bit B-bus cycles, so this loop's longword stores replay the
 *     boot clear's proven bus traffic. Both pointers are volatile:
 *     the destination so the compiler can neither fuse the loop back
 *     into a memcpy() call nor reorder the stores, the source so every
 *     read is a real load (the source lvalue type is not vdp1_cmdt_t,
 *     and volatile plus the integer-laundered pointer below keep the
 *     compiler from drawing any type-based no-alias conclusion against
 *     the emit loop's earlier struct writes). No cache management is
 *     needed in either direction: the SH7604 data cache is
 *     write-through (it has no write-back mode -- which is why
 *     cpu/cache.h offers only purge/invalidate operations, no flush),
 *     so the emit loop's cached LWRAM writes are already in memory,
 *     and a same-CPU read back through the cached alias is trivially
 *     coherent -- the SCU path's CPU_CACHE_THROUGH source alias
 *     (vdp_sync.c:408) exists because the SCU is an external master
 *     that cannot see the CPU cache, a blindness a CPU-driven copy
 *     does not have (cached reads of the just-written array are mostly
 *     hits). The VDP1_VRAM() destination (0x25C00000) lies in the
 *     cache-through partition, so the stores bypass the cache by
 *     address.
 *  3. vdp1_sync_force_put() (vdp_sync.c:388, "Fake a put"): runs
 *     _vdp1_sync_put() -- whose internal wait is a no-op after step 1 --
 *     which atomically (intc mask 15) clears LIST_XFERRED and
 *     LIST_COMMITTED and sets REQUEST_XFER_LIST, then _vdp1_dma_call(),
 *     invoking the current mode's .dma handler exactly as the SCU-DMA
 *     end interrupt would have (vdp_sync.c:1162-1167). In auto (1-cycle)
 *     mode that is _vdp1_mode_auto_dma(): PTMR=AUTO (plot armed for the
 *     next framebuffer change) and LIST_XFERRED set (vdp_sync.c:791-800).
 *     Ordering matches the SCU path: the plot start is armed only after
 *     the complete list is resident in VRAM. Running the .dma handler in
 *     the foreground instead of an ISR is safe because the VBLANK
 *     handlers only touch _state.vdp1.flags while SYNC_FLAG_VDP1_SYNC is
 *     set (vdp_sync.c:1179,1208), and that flag cannot be set until the
 *     caller's subsequent vdp1_sync(). One additional fact this rests
 *     on: the VBLANK-IN handler DOES unconditionally read-modify-write
 *     the separate _state.flags byte every field (read at
 *     vdp_sync.c:1177, write-back at :1196) regardless of the SYNC
 *     gate -- that is benign here only because _state.flags is a
 *     distinct byte from the _state.vdp1.flags bitfield word (no SH-2
 *     storage-unit overlap), and every mainline RMW of _state.flags
 *     runs at intc mask 15 where the ISR cannot interleave. libyaul's
 *     own libmic3d uses force_put the same way after delivering
 *     commands to VRAM without SCU DMA (libmic3d/render.c:538).
 *
 * After this returns, vdp1_sync_render() and vdp1_sync() observe
 * LIST_XFERRED already set and proceed without spinning; the VBLANK-IN
 * commit and VBLANK-OUT reset then advance identically to the SCU path. */
#if defined(SM64_SATURN_VDP1_LWRAM_STAGING)
static inline void
sm64_saturn_vdp1_backend_upload(sm64_saturn_vdp1_backend_t *backend)
{
    vdp1_sync_wait();

    /* INVARIANT (adversarial review of this fix): the foreground
     * force_put below is race-free only while SYNC_FLAG_VDP1_SYNC is
     * clear. vdp1_sync_wait() above just guaranteed that, and only
     * mainline vdp1_sync() can set it, so the precondition provably
     * holds today. What would break it: a sourceboot vblank callback
     * (vdp_sync_vblank_in_set/out_set) that touches VDP1 sync state --
     * sourceboot deliberately registers none -- or calling this upload
     * from inside such a callback. If either is ever introduced,
     * re-verify this whole block's safety argument. (This assert was
     * briefly removed when the image was 16 bytes over the ram region;
     * reinstated once the main-pool shrink freed real headroom. It is
     * live under -DDEBUG, which every Saturn target defines.) */
    assert(!vdp1_sync_busy());

    /* Source address as an integer with the SH-2 partition bits
     * stripped (partition 0x0 is the cached alias -- correct and
     * fastest here, see the coherency note above); the integer
     * round-trip is deliberate pointer laundering -- see the aliasing
     * note in the block comment above. */
    volatile const uint32_t *src = (volatile const uint32_t *)
        ((uintptr_t)backend->list.cmdts & ~CPU_ADDRESS_PARTITION_MASK);
    volatile uint32_t *dst = (volatile uint32_t *)VDP1_VRAM(0);
    uint32_t words = (uint32_t)backend->list.count *
                     (sizeof(vdp1_cmdt_t) / sizeof(uint32_t));
    while (words > 0) {
        *dst++ = *src++;
        words--;
    }

    vdp1_sync_force_put();
}
#else
static inline void
sm64_saturn_vdp1_backend_upload(sm64_saturn_vdp1_backend_t *backend)
{
    /* Safety net for the compile-time dispatch (see the block comment
     * above): an LWRAM-staged backend must never reach the SCU-DMA
     * path -- on real hardware it locks the machine on the first
     * frame. Partition bits stripped so every alias of LWRAM trips
     * the assert. */
    assert(!((((uintptr_t)backend->list.cmdts &
               ~CPU_ADDRESS_PARTITION_MASK) >= LWRAM(0)) &&
             (((uintptr_t)backend->list.cmdts &
               ~CPU_ADDRESS_PARTITION_MASK) < LWRAM(LWRAM_SIZE))));

    vdp1_sync_cmdt_list_put(&backend->list, 0);
}
#endif

#endif
