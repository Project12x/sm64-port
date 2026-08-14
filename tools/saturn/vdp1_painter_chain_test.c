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

int main(void)
{
    test_shared_bins_link_far_to_near_with_stable_ties();
    test_invalid_bin_is_atomic_and_next_begin_restores_prefix();
    test_cold_stage_repair_restores_borrowed_command_prefix();
    puts("vdp1 painter chain: PASS");
    return 0;
}
