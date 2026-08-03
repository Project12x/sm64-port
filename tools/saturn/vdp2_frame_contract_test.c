#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_vdp2_frame.h"

typedef struct vdp2_contract_backend {
    uint32_t sky_updates;
    uint32_t hud_updates;
    uint32_t layer_updates;
    uint32_t commits;
    int32_t sky_x;
    int32_t sky_y;
    uint32_t display_mask;
    uint8_t vdp1_priority;
    char hud[SM64_SATURN_VDP2_FRAME_HUD_TEXT_CAPACITY];
} vdp2_contract_backend_t;

static void sky(int32_t x, int32_t y, void *work)
{
    vdp2_contract_backend_t *const backend = work;
    backend->sky_updates++;
    backend->sky_x = x;
    backend->sky_y = y;
}

static void hud(const char *text, void *work)
{
    vdp2_contract_backend_t *const backend = work;
    backend->hud_updates++;
    (void)strncpy(backend->hud, text, sizeof(backend->hud) - 1U);
    backend->hud[sizeof(backend->hud) - 1U] = '\0';
}

static void layers(uint32_t display_mask, uint8_t vdp1_priority, void *work)
{
    vdp2_contract_backend_t *const backend = work;
    backend->layer_updates++;
    backend->display_mask = display_mask;
    backend->vdp1_priority = vdp1_priority;
}

static void commit(void *work)
{
    ((vdp2_contract_backend_t *)work)->commits++;
}

int main(void)
{
    sm64_saturn_vdp2_frame_t frame;
    sm64_saturn_fast3d_profile_t profile;
    vdp2_contract_backend_t observed;
    const sm64_saturn_vdp2_camera_snapshot_t snapshot = {
        .yaw = 0x4000, .pitch = 0, .valid = 1U,
    };
    const sm64_saturn_vdp2_frame_backend_t backend = {
        .sky_scroll_set = sky, .hud_write = hud, .layers_set = layers,
        .vblank_commit = commit, .work = &observed,
    };

    (void)memset(&profile, 0, sizeof(profile));
    (void)memset(&observed, 0, sizeof(observed));
    profile.demo_bob_results_master = 11U;
    profile.demo_bob_results_slave = 12U;
    profile.vdp1_commands_last = 13U;
    profile.vdp1_bank_late_dma = 14U;
    profile.render_frt_ticks_last = 20U;

    sm64_saturn_vdp2_frame_init(&frame);
    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile, 30U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 1U && observed.sky_updates == 1U);
    assert(observed.hud_updates == 1U && observed.layer_updates == 1U);
    assert(observed.display_mask == SM64_SATURN_VDP2_FRAME_DISPLAY_MASK);
    assert(observed.vdp1_priority == 7U);
    assert(observed.sky_x == 128 && observed.sky_y == 128);
    assert(strstr(observed.hud,
                  "FPS 50 MT 11 ST 12 ORD 13 DMA 14 VDP1 20") != NULL);

    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile, 31U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 2U && observed.sky_updates == 2U);
    assert(observed.layer_updates == 2U && observed.hud_updates == 1U);
    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile, 60U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.hud_updates == 2U);
    return 0;
}
