#ifndef SM64_SATURN_GOURAUD_BANK_H
#define SM64_SATURN_GOURAUD_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame-local VDP1 Gouraud table staging bank -- pure bookkeeping,
 * Yaul-free so the accounting is host-testable. One 8-byte table per
 * emitted primitive per frame (design spec 2026-07-24; per-frame
 * rebuild by user decision, caching is fidelity-ladder item 1).
 *
 * The Yaul-side owner (sourceboot) provides the staging array (which
 * MUST live in HWRAM, never .lwram_bss -- the SCU-DMA-from-LWRAM
 * lockup class documented in the VDP1 backend applies to this upload
 * path too) and the device base address (partitions.gouraud_base). A7's
 * frame-bank manager binds this complete object to the matching command
 * source bank; callers no longer select the two arrays independently and
 * perform the actual used-prefix upload after emission. Layout
 * matches Yaul's vdp1_gouraud_table_t (4 x RGB1555, 8 bytes,
 * libyaul .../vdp1/vram.h:32-34) -- static-asserted at the emit TU,
 * which sees both types.
 *
 * __attribute__((aligned(8))) mirrors vdp1_gouraud_table_t's own
 * alignment exactly (Task 6 code-review finding, 2026-07-24): without
 * it, nothing source-level guarantees this array's alignment when
 * SCU-DMA'd out of HWRAM -- the first sourceboot build happened to
 * link it 4-byte aligned, which is all libyaul's scu_dma_transfer()
 * actually requires (dnad=0x101, 4-byte address stepping), so there
 * was no live bug, but that was incidental compiler layout, not a
 * guarantee -- the exact "works today, invisible on Ymir, breaks
 * later" class this project's VDP1 backend LWRAM-DMA notes already
 * warn about elsewhere. Plain GCC attribute syntax, not Yaul's
 * __aligned() macro (libyaul/libc/sys/cdefs.h) -- this header must
 * stay Yaul-free to remain host-testable, and the host build (plain
 * MinGW GCC) never sees that macro's definition. */
typedef struct sm64_saturn_gouraud_table {
    uint16_t colors[4];
} __attribute__((aligned(8))) sm64_saturn_gouraud_table_t;

typedef struct sm64_saturn_gouraud_bank {
    sm64_saturn_gouraud_table_t *staging;
    uintptr_t vram_base;
    uint16_t capacity;
    uint16_t used;
    /* Per-frame diagnostics. These are evidence only: no scheduling or
     * promotion decision may depend on how many flat entries were avoided. */
    uint32_t saved_tables;
    uint32_t saved_bytes;
} sm64_saturn_gouraud_bank_t;

/* Returns false (and configures an always-NULL bank) for capacity 0 --
 * the caller's graceful all-flat fallback when the VRAM partition is
 * missing or overlaps the command region. */
static inline bool
sm64_saturn_gouraud_bank_init(sm64_saturn_gouraud_bank_t *bank,
                              sm64_saturn_gouraud_table_t *staging,
                              uint16_t capacity, uintptr_t vram_base)
{
    bank->staging = staging;
    bank->vram_base = vram_base;
    bank->capacity = capacity;
    bank->used = 0;
    bank->saved_tables = 0U;
    bank->saved_bytes = 0U;
    return capacity > 0;
}

static inline void
sm64_saturn_gouraud_bank_begin(sm64_saturn_gouraud_bank_t *bank)
{
    bank->used = 0;
    bank->saved_tables = 0U;
    bank->saved_bytes = 0U;
}

/* Called only after classification has chosen FLAT_REPLACE.  Keeping this
 * accounting beside the bounded allocator makes its 8-byte unit explicit and
 * lets host fixtures prove that flat input did not consume a staging slot. */
static inline void sm64_saturn_gouraud_bank_note_saved(
    sm64_saturn_gouraud_bank_t *bank)
{
    if (bank == NULL)
        return;
    if (bank->saved_tables != UINT32_MAX)
        bank->saved_tables++;
    const uint32_t table_bytes = (uint32_t)sizeof(sm64_saturn_gouraud_table_t);
    bank->saved_bytes = bank->saved_bytes > UINT32_MAX - table_bytes
        ? UINT32_MAX : bank->saved_bytes + table_bytes;
}

/* Returns the staging slot to fill and writes the table's device
 * address (for CMDGRDA) to *vram_addr; NULL when exhausted. */
static inline sm64_saturn_gouraud_table_t *
sm64_saturn_gouraud_bank_alloc(sm64_saturn_gouraud_bank_t *bank,
                               uintptr_t *vram_addr)
{
    if (bank->used >= bank->capacity) {
        return NULL;
    }
    *vram_addr = bank->vram_base +
        (uintptr_t)bank->used * sizeof(sm64_saturn_gouraud_table_t);
    return &bank->staging[bank->used++];
}

static inline size_t
sm64_saturn_gouraud_bank_used_bytes(const sm64_saturn_gouraud_bank_t *bank)
{
    return (size_t)bank->used * sizeof(sm64_saturn_gouraud_table_t);
}

#endif
