#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <yaul.h>

/* Minimal behavioral Yaul host double for the persistent command-list helper.
 * The real target compile uses pinned libyaul; this fixture observes the
 * encoded VDP1 link words, including their four-byte link-address units. */
typedef struct int16_vec2 {
    int16_t x;
    int16_t y;
} int16_vec2_t;

typedef uint16_t vdp1_link_t;

typedef struct vdp1_cmdt {
    uint16_t cmd_ctrl;
    uint16_t cmd_link;
    uint16_t words[14];
} vdp1_cmdt_t;

typedef struct vdp1_cmdt_list {
    vdp1_cmdt_t *cmdts;
    uint16_t count;
} vdp1_cmdt_list_t;

#define CPU_ADDRESS_PARTITION_MASK ((uintptr_t)0xE0000000U)
#define LWRAM(address) ((uintptr_t)(address))
#define LWRAM_SIZE 0x00100000U
#define VDP1_VRAM(address) ((uintptr_t)(address))

void *memalign(size_t alignment, size_t size);

static void vdp1_cmdt_list_init(vdp1_cmdt_list_t *list, vdp1_cmdt_t *cmdts)
{
    list->cmdts = cmdts;
    list->count = 0U;
}

static void vdp1_cmdt_system_clip_coord_set(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0xFFF0U) | 0x0009U);
}

static void vdp1_cmdt_vtx_system_clip_coord_set(vdp1_cmdt_t *cmdt,
                                                  int16_vec2_t clip)
{
    (void)cmdt;
    (void)clip;
}

static void vdp1_cmdt_local_coord_set(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0xFFF0U) | 0x000AU);
}

static void vdp1_cmdt_vtx_local_coord_set(vdp1_cmdt_t *cmdt,
                                            int16_vec2_t local)
{
    (void)cmdt;
    (void)local;
}

static void vdp1_cmdt_end_set(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl |= 0x8000U;
}

static void vdp1_cmdt_end_clear(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl &= (uint16_t)~0x8000U;
}

static void vdp1_cmdt_jump_next(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl &= 0x8FFFU;
}

static void vdp1_cmdt_jump_assign(vdp1_cmdt_t *cmdt, vdp1_link_t link)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0x8FFFU) | 0x1000U);
    cmdt->cmd_link = (uint16_t)(link << 2);
}

static void vdp1_sync_wait(void) {}
static bool vdp1_sync_busy(void) { return false; }
static void vdp1_sync_force_put(void) {}

#define SM64_SATURN_VDP1_LWRAM_STAGING 1
#define SM64_SATURN_VDP1_BACKEND_LINK_REFERENCE 1
#include "../../src/port/saturn/gfx/saturn_vdp1_backend.h"

static void expect_assign(const vdp1_cmdt_t *cmdt, uint16_t next)
{
    assert((cmdt->cmd_ctrl & 0x7000U) == 0x1000U);
    assert(cmdt->cmd_link == (uint16_t)(next << 2));
}

static void test_shared_bins_link_far_to_near_with_stable_ties(void)
{
    sm64_saturn_vdp1_backend_t backend;
    vdp1_cmdt_t cmdts[8] = {{0}};
    vdp1_cmdt_t *draws;

    sm64_saturn_vdp1_backend_bind_storage(&backend, cmdts, 8U);
    cmdts[1].cmd_ctrl = 0x000AU;
    sm64_saturn_vdp1_backend_begin(&backend);
    draws = sm64_saturn_vdp1_backend_reserve(&backend, 4U);
    assert(draws == &cmdts[2]);
    cmdts[2].cmd_link = 7U;
    cmdts[3].cmd_link = 2U;
    cmdts[4].cmd_link = 7U;
    cmdts[5].cmd_link = 63U;
    /* Rebinding a frame-bank command list preserves old JUMP_ASSIGN bits;
     * the lowerer has already replaced the raw link word with this frame's
     * depth-bin tag.  The linker must overwrite that type, not reject it. */
    cmdts[2].cmd_ctrl = 0x1002U;
    sm64_saturn_vdp1_backend_finish(&backend);

    assert(sm64_saturn_vdp1_backend_link_depth_bins(&backend, 64U));
    expect_assign(&cmdts[1], 5U);
    expect_assign(&cmdts[5], 2U);
    expect_assign(&cmdts[2], 4U);
    expect_assign(&cmdts[4], 3U);
    expect_assign(&cmdts[3], 6U);
    assert((cmdts[6].cmd_ctrl & 0x8000U) != 0U);
}

static void test_invalid_bin_is_atomic_and_next_begin_restores_prefix(void)
{
    sm64_saturn_vdp1_backend_t backend;
    vdp1_cmdt_t cmdts[8] = {{0}};
    vdp1_cmdt_t before[8];

    sm64_saturn_vdp1_backend_bind_storage(&backend, cmdts, 8U);
    cmdts[1].cmd_ctrl = 0x000AU;
    sm64_saturn_vdp1_backend_begin(&backend);
    assert(sm64_saturn_vdp1_backend_reserve(&backend, 2U) == &cmdts[2]);
    cmdts[2].cmd_link = 1U;
    cmdts[3].cmd_link = 64U;
    sm64_saturn_vdp1_backend_finish(&backend);
    memcpy(before, cmdts, sizeof(cmdts));
    assert(!sm64_saturn_vdp1_backend_link_depth_bins(&backend, 64U));
    assert(memcmp(before, cmdts, sizeof(cmdts)) == 0);

    cmdts[3].cmd_link = 1U;
    assert(sm64_saturn_vdp1_backend_link_depth_bins(&backend, 64U));
    sm64_saturn_vdp1_backend_begin(&backend);
    assert((cmdts[1].cmd_ctrl & 0x7000U) == 0U);
    assert(cmdts[1].cmd_link == 0U);
}

/* Sourceboot borrows exactly this prefix of the otherwise idle first frame
 * bank while it copies the cold actor bundle from CART.  Frame-bank binding
 * intentionally does not create commands, so bootstrap must restore these
 * three commands before the first gameplay build can select bank 0. */
static void test_cold_stage_repair_restores_borrowed_command_prefix(void)
{
    sm64_saturn_vdp1_backend_t backend;
    vdp1_cmdt_t cmdts[80];
    const int16_vec2_t clip = {319, 223};
    const int16_vec2_t local = {0, 0};

    assert(sizeof(cmdts) >= 2560U);
    memset(cmdts, 0xA5, 2560U);
    assert(sm64_saturn_vdp1_backend_init_with_storage(
        &backend, cmdts, 80U, clip, local));
    assert((cmdts[0].cmd_ctrl & 0x000FU) == 0x0009U);
    assert((cmdts[1].cmd_ctrl & 0x000FU) == 0x000AU);
    assert((cmdts[2].cmd_ctrl & 0x8000U) != 0U);

    sm64_saturn_vdp1_backend_bind_storage(&backend, cmdts, 80U);
    assert(backend.list.count == 3U);
    sm64_saturn_vdp1_backend_begin(&backend);
    assert((cmdts[0].cmd_ctrl & 0x000FU) == 0x0009U);
    assert((cmdts[1].cmd_ctrl & 0x000FU) == 0x000AU);
}

/* ------------------------------------------------------------------------
 * Painter-chain equivalence harness (Sprint 2 T2.3).
 *
 * Three independent statements of the same contract are cross-checked on
 * every case:
 *
 *   1. `sm64_saturn_vdp1_backend_link_depth_bins_reference()` -- the
 *      pre-T2.3 per-bin rescan, compiled only here.
 *   2. `sm64_saturn_vdp1_backend_link_depth_bins()` -- the shipped
 *      implementation.
 *   3. `model_expected_chain()` below -- a from-first-principles model that
 *      never looks at either implementation: it stable-sorts the draw
 *      indices by descending bin and writes the links that ordering implies.
 *
 * The assertion is byte identity of the whole command array, so a divergence
 * in link value, link type, END handling, or any untouched field fails.
 * ---------------------------------------------------------------------- */

#define EQUIV_CAPACITY 1664U
#define EQUIV_SETUP 2U
#define EQUIV_MAX_DRAWS (EQUIV_CAPACITY - EQUIV_SETUP - 1U)

static vdp1_cmdt_t g_pristine[EQUIV_CAPACITY];
static vdp1_cmdt_t g_reference[EQUIV_CAPACITY];
static vdp1_cmdt_t g_subject[EQUIV_CAPACITY];
static vdp1_cmdt_t g_expected[EQUIV_CAPACITY];
static uint16_t g_bins[EQUIV_MAX_DRAWS];

static uint32_t g_rng;

static uint16_t rng_next(uint16_t modulus)
{
    g_rng = (g_rng * 1103515245U) + 12345U;
    return (uint16_t)(((g_rng >> 16) & 0x7FFFU) % modulus);
}

/* Deterministic per-index stale control word: varies the command type nibble,
 * the link-type field (so the strip/overwrite path is exercised) and the END
 * bit, none of which the relink may alter beyond the link-type field. */
static uint16_t seed_ctrl(uint16_t draw)
{
    return (uint16_t)(((draw % 7U) << 12) | ((draw & 1U) << 15) |
                      (draw % 11U));
}

/* Populate a bound backend with `draw_count` draw commands whose raw depth
 * tags come from g_bins[]. Mirrors exactly what the demo lowerers leave
 * behind: begin() -> reserve() -> raw cmd_link tags -> finish(). */
static void equiv_build(vdp1_cmdt_t *cmdts,
                        sm64_saturn_vdp1_backend_t *backend,
                        uint16_t draw_count)
{
    uint16_t draw;

    memset(cmdts, 0, sizeof(vdp1_cmdt_t) * EQUIV_CAPACITY);
    sm64_saturn_vdp1_backend_bind_storage(backend, cmdts, EQUIV_CAPACITY);
    cmdts[0].cmd_ctrl = 0x0009U;
    cmdts[1].cmd_ctrl = 0x000AU;
    sm64_saturn_vdp1_backend_begin(backend);
    if (draw_count != 0U)
        assert(sm64_saturn_vdp1_backend_reserve(backend, draw_count) ==
               &cmdts[EQUIV_SETUP]);
    for (draw = 0U; draw < draw_count; draw++) {
        cmdts[EQUIV_SETUP + draw].cmd_ctrl = seed_ctrl(draw);
        cmdts[EQUIV_SETUP + draw].cmd_link = g_bins[draw];
    }
    sm64_saturn_vdp1_backend_finish(backend);
}

/* Independent model. Order = bins descending; within a bin, producer
 * (ascending index) order. Each ordered entry points at its successor;
 * the last points at the END slot; the prefix command points at the head,
 * or reverts to JUMP_NEXT/0 when nothing was drawn. */
static void model_expected_chain(uint16_t draw_count, uint16_t bin_count)
{
    const uint16_t end = (uint16_t)(EQUIV_SETUP + draw_count);
    uint16_t order[EQUIV_MAX_DRAWS];
    uint16_t ordered = 0U;
    uint16_t bin;
    uint16_t draw;
    uint16_t slot;

    memcpy(g_expected, g_pristine, sizeof(g_expected));
    for (bin = bin_count; bin-- > 0U; ) {
        for (draw = 0U; draw < draw_count; draw++) {
            if (g_bins[draw] == bin)
                order[ordered++] = (uint16_t)(EQUIV_SETUP + draw);
        }
    }
    assert(ordered == draw_count);

    for (slot = 0U; slot < ordered; slot++) {
        const uint16_t here = order[slot];
        const uint16_t successor = (slot + 1U < ordered)
            ? order[slot + 1U] : end;
        g_expected[here].cmd_ctrl =
            (uint16_t)((g_expected[here].cmd_ctrl & 0x8FFFU) | 0x1000U);
        g_expected[here].cmd_link = (uint16_t)(successor << 2);
    }
    if (ordered == 0U) {
        g_expected[EQUIV_SETUP - 1U].cmd_ctrl &= 0x8FFFU;
        g_expected[EQUIV_SETUP - 1U].cmd_link = 0U;
    } else {
        g_expected[EQUIV_SETUP - 1U].cmd_ctrl =
            (uint16_t)((g_expected[EQUIV_SETUP - 1U].cmd_ctrl & 0x8FFFU) |
                       0x1000U);
        g_expected[EQUIV_SETUP - 1U].cmd_link = (uint16_t)(order[0] << 2);
    }
}

static void equiv_case(const char *label, uint16_t draw_count,
                       uint16_t bin_count)
{
    sm64_saturn_vdp1_backend_t reference_backend;
    sm64_saturn_vdp1_backend_t subject_backend;

    assert(draw_count <= EQUIV_MAX_DRAWS);

    equiv_build(g_pristine, &reference_backend, draw_count);
    equiv_build(g_reference, &reference_backend, draw_count);
    equiv_build(g_subject, &subject_backend, draw_count);
    assert(memcmp(g_pristine, g_reference, sizeof(g_pristine)) == 0);
    assert(memcmp(g_pristine, g_subject, sizeof(g_pristine)) == 0);

    assert(sm64_saturn_vdp1_backend_link_depth_bins_reference(
        &reference_backend, bin_count));
    assert(sm64_saturn_vdp1_backend_link_depth_bins(
        &subject_backend, bin_count));

    model_expected_chain(draw_count, bin_count);

    if (memcmp(g_reference, g_expected, sizeof(g_expected)) != 0) {
        printf("equivalence case '%s': reference deviates from model\n",
               label);
        assert(0);
    }
    if (memcmp(g_subject, g_expected, sizeof(g_expected)) != 0) {
        printf("equivalence case '%s': subject deviates from model\n", label);
        assert(0);
    }
    if (memcmp(g_subject, g_reference, sizeof(g_reference)) != 0) {
        printf("equivalence case '%s': subject deviates from reference\n",
               label);
        assert(0);
    }
}

static void fill_bins_constant(uint16_t draw_count, uint16_t bin)
{
    uint16_t draw;
    for (draw = 0U; draw < draw_count; draw++)
        g_bins[draw] = bin;
}

static void test_counting_sort_matches_the_rescan_reference(void)
{
    uint16_t draw;

    /* Empty draw range: prefix must revert to JUMP_NEXT with a zero link. */
    equiv_case("empty range", 0U, 64U);

    /* Single command, at both ends of the bin table. */
    fill_bins_constant(1U, 0U);
    equiv_case("single command near bin", 1U, 64U);
    fill_bins_constant(1U, 63U);
    equiv_case("single command far bin", 1U, 64U);

    /* Every command in one bin: 63 empty bins, one full chain. */
    fill_bins_constant(100U, 17U);
    equiv_case("all commands one bin", 100U, 64U);
    fill_bins_constant(100U, 0U);
    equiv_case("all commands nearest bin", 100U, 64U);
    fill_bins_constant(100U, 63U);
    equiv_case("all commands farthest bin", 100U, 64U);

    /* Occupied at both extremes only, interleaved so a stability break
     * inside either bin reorders the chain. */
    for (draw = 0U; draw < 64U; draw++)
        g_bins[draw] = (uint16_t)((draw & 1U) ? 63U : 0U);
    equiv_case("both extremes interleaved", 64U, 64U);

    /* Exactly one command per bin, in both tag orders. */
    for (draw = 0U; draw < 64U; draw++)
        g_bins[draw] = draw;
    equiv_case("one per bin ascending tags", 64U, 64U);
    for (draw = 0U; draw < 64U; draw++)
        g_bins[draw] = (uint16_t)(63U - draw);
    equiv_case("one per bin descending tags", 64U, 64U);

    /* Sparse: three commands sharing a bin plus one far outlier. */
    g_bins[0] = 5U; g_bins[1] = 5U; g_bins[2] = 62U; g_bins[3] = 5U;
    equiv_case("sparse bins with ties", 4U, 64U);

    /* Pseudo-random mixtures at several sizes. */
    g_rng = 0x13572468U;
    for (draw = 0U; draw < 500U; draw++)
        g_bins[draw] = rng_next(64U);
    equiv_case("random 500", 500U, 64U);
    for (draw = 0U; draw < 653U; draw++)
        g_bins[draw] = rng_next(64U);
    equiv_case("random 653 (T2.1 measured peak)", 653U, 64U);

    /* At the arena's 1664-command capacity: 1661 draws + 2 setup + END. */
    for (draw = 0U; draw < EQUIV_MAX_DRAWS; draw++)
        g_bins[draw] = rng_next(64U);
    equiv_case("capacity random", EQUIV_MAX_DRAWS, 64U);
    fill_bins_constant(EQUIV_MAX_DRAWS, 0U);
    equiv_case("capacity all nearest bin", EQUIV_MAX_DRAWS, 64U);
    fill_bins_constant(EQUIV_MAX_DRAWS, 63U);
    equiv_case("capacity all farthest bin", EQUIV_MAX_DRAWS, 64U);
    for (draw = 0U; draw < EQUIV_MAX_DRAWS; draw++)
        g_bins[draw] = (uint16_t)(draw % 64U);
    equiv_case("capacity round-robin bins", EQUIV_MAX_DRAWS, 64U);

    /* Narrower bin tables must behave identically over their own domain. */
    for (draw = 0U; draw < 200U; draw++)
        g_bins[draw] = rng_next(8U);
    equiv_case("eight bins", 200U, 8U);
    fill_bins_constant(200U, 0U);
    equiv_case("single bin table", 200U, 1U);
}

static void test_invalid_tag_leaves_both_implementations_inert(void)
{
    static const uint16_t positions[] = {0U, 1U, 123U, 249U};
    sm64_saturn_vdp1_backend_t reference_backend;
    sm64_saturn_vdp1_backend_t subject_backend;
    size_t which;
    uint16_t draw;

    g_rng = 0x0BADC0DEU;
    for (which = 0U; which < sizeof(positions) / sizeof(positions[0]);
         which++) {
        for (draw = 0U; draw < 250U; draw++)
            g_bins[draw] = rng_next(64U);
        g_bins[positions[which]] = 64U;

        equiv_build(g_pristine, &reference_backend, 250U);
        equiv_build(g_reference, &reference_backend, 250U);
        equiv_build(g_subject, &subject_backend, 250U);

        assert(!sm64_saturn_vdp1_backend_link_depth_bins_reference(
            &reference_backend, 64U));
        assert(!sm64_saturn_vdp1_backend_link_depth_bins(
            &subject_backend, 64U));
        assert(memcmp(g_reference, g_pristine, sizeof(g_pristine)) == 0);
        assert(memcmp(g_subject, g_pristine, sizeof(g_pristine)) == 0);
    }
}

int main(void)
{
    test_shared_bins_link_far_to_near_with_stable_ties();
    test_invalid_bin_is_atomic_and_next_begin_restores_prefix();
    test_cold_stage_repair_restores_borrowed_command_prefix();
    test_counting_sort_matches_the_rescan_reference();
    test_invalid_tag_leaves_both_implementations_inert();
    puts("vdp1 painter chain: PASS");
    return 0;
}
