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
        .yaw = 0x4000, .pitch = 0, .valid = 1U, .generation = 27U,
    };
    const sm64_saturn_vdp2_generation_state_t generations = {
        .displayed_generation = 27U,
        .rendered_generation = 27U,
        .simulation_generation = 28U,
    };
    const sm64_saturn_vdp2_camera_snapshot_t next_snapshot = {
        .yaw = (int16_t)0x8000, .pitch = 0, .valid = 1U, .generation = 28U,
    };
    const sm64_saturn_vdp2_generation_state_t next_generations = {
        .displayed_generation = 28U,
        .rendered_generation = 28U,
        .simulation_generation = 29U,
    };
    const sm64_saturn_vdp2_frame_backend_t backend = {
        .sky_scroll_set = sky, .hud_write = hud, .layers_set = layers,
        .vblank_commit = commit, .work = &observed,
    };

    (void)memset(&profile, 0, sizeof(profile));
    (void)memset(&observed, 0, sizeof(observed));
    profile.master_transform_count = 11U;
    profile.slave_transform_count = 12U;
    profile.ordering_count = 13U;
    profile.dma_wait_ticks_last = 14U;
    profile.vdp1_wait_ticks_last = 20U;
    profile.render_job_master_world_admit_claims = 1U;
    profile.render_job_master_world_lower_claims = 2U;
    profile.render_job_master_actor_admit_claims = 3U;
    profile.render_job_master_actor_lower_claims = 4U;
    profile.render_job_slave_world_admit_claims = 5U;
    profile.render_job_slave_world_lower_claims = 6U;
    profile.render_job_slave_actor_admit_claims = 7U;
    profile.render_job_slave_actor_lower_claims = 8U;
    profile.render_job_notified_generation = 9U;
    profile.render_job_retired_generation = 9U;
    profile.render_job_master_wait_iterations = 10U;
    profile.render_job_failures = 11U;
    profile.render_job_quarantined = 12U;

    sm64_saturn_vdp2_frame_init(&frame);
    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile, &generations,
                                 0U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 1U && observed.sky_updates == 1U);
    assert(observed.hud_updates == 1U && observed.layer_updates == 1U);
    assert(observed.display_mask == SM64_SATURN_VDP2_FRAME_DISPLAY_MASK);
    assert(observed.vdp1_priority == 7U);
    assert(observed.sky_x == 128 && observed.sky_y == 128);
    assert(strstr(observed.hud,
                  "FPS 0 GEN D 27 R 27 S 28 MT 11 ST 12 ORD 13 DMAW 14 VDP1W 20") != NULL);
    assert(strstr(observed.hud, "QM 1/2/3/4 QS 5/6/7/8") != NULL);
    assert(strstr(observed.hud, "QN 9 QR 9 QW 10 QF 11 QQ 12") != NULL);

    /* New composition metadata cannot wait for the rate-limited performance
     * counters: the sky and HUD must switch generations together. */
    sm64_saturn_vdp2_frame_begin(&frame, &next_snapshot, &profile,
                                 &next_generations, 15U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 2U && observed.sky_updates == 2U);
    assert(observed.sky_x == 256 && observed.sky_y == 128);
    assert(observed.layer_updates == 2U && observed.hud_updates == 2U);
    assert(strstr(observed.hud, "GEN D 28 R 28 S 29") != NULL);
    sm64_saturn_vdp2_frame_begin(&frame, &next_snapshot, &profile,
                                 &next_generations,
                                 30U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.hud_updates == 3U);
    assert(strstr(observed.hud,
                  "FPS 3 GEN D 28 R 28 S 29 MT 11 ST 12 ORD 13 DMAW 14 VDP1W 20") != NULL);

    /* A VDP2 commit with a stale render generation must be rejected before
     * either the sky or HUD can be mixed with the displayed VDP1 bank. */
    sm64_saturn_vdp2_generation_state_t incoherent = generations;
    incoherent.rendered_generation = 26U;
    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile, &incoherent,
                                 31U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 3U && observed.sky_updates == 3U);
    assert(observed.hud_updates == 3U && observed.layer_updates == 3U);

    sm64_saturn_vdp2_generation_state_t zero_displayed = generations;
    zero_displayed.displayed_generation = 0U;
    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile,
                                 &zero_displayed, 32U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 3U && observed.sky_updates == 3U);
    assert(observed.hud_updates == 3U && observed.layer_updates == 3U);

    sm64_saturn_vdp2_generation_state_t zero_simulation = generations;
    zero_simulation.simulation_generation = 0U;
    sm64_saturn_vdp2_frame_begin(&frame, &snapshot, &profile,
                                 &zero_simulation, 33U);
    sm64_saturn_vdp2_frame_commit(&frame, &backend);
    assert(observed.commits == 3U && observed.sky_updates == 3U);
    assert(observed.hud_updates == 3U && observed.layer_updates == 3U);
    return 0;
}
