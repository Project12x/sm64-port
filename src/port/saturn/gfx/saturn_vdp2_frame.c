#include "saturn_vdp2_frame.h"

#include <stddef.h>
#include <string.h>

#define SM64_SATURN_VDP2_SKY_WIDTH 512
#define SM64_SATURN_VDP2_SKY_HEIGHT 256

static char *vdp2_frame_append_text(char *out, const char *end,
                                    const char *text)
{
    while (*text != '\0' && out < end)
        *out++ = *text++;
    return out;
}

static char *vdp2_frame_append_u32(char *out, const char *end, uint32_t value)
{
    char digits[10];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U && out < end)
        *out++ = digits[--count];
    return out;
}

static void vdp2_frame_hud_prepare(
    sm64_saturn_vdp2_frame_t *frame,
    const sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_vdp2_generation_state_t *generations)
{
    char *out = frame->hud_text;
    char *const end = frame->hud_text + sizeof(frame->hud_text) - 1U;
    /* This intentionally has no printf/sprintf dependency: the sourceboot
     * target is freestanding and the VDP2 HUD must not pull float formatting
     * onto a frame path. The fields are counters, not a promotion threshold.
     * MT/ST are master/slave transform counters, ORD is the final command
     * order count, and DMAW/VDP1W are time spent at their actual fences. */
    out = vdp2_frame_append_text(out, end, "FPS ");
    out = vdp2_frame_append_u32(out, end, frame->total_fps);
    if (generations != NULL) {
        out = vdp2_frame_append_text(out, end, " GEN D ");
        out = vdp2_frame_append_u32(out, end,
                                    generations->displayed_generation);
        out = vdp2_frame_append_text(out, end, " R ");
        out = vdp2_frame_append_u32(out, end,
                                    generations->rendered_generation);
        out = vdp2_frame_append_text(out, end, " S ");
        out = vdp2_frame_append_u32(out, end,
                                    generations->simulation_generation);
    }
    out = vdp2_frame_append_text(out, end, " MT ");
    out = vdp2_frame_append_u32(out, end, profile->master_transform_count);
    out = vdp2_frame_append_text(out, end, " ST ");
    out = vdp2_frame_append_u32(out, end, profile->slave_transform_count);
    out = vdp2_frame_append_text(out, end, " ORD ");
    out = vdp2_frame_append_u32(out, end, profile->ordering_count);
    out = vdp2_frame_append_text(out, end, " DMAW ");
    out = vdp2_frame_append_u32(out, end, profile->dma_wait_ticks_last);
    out = vdp2_frame_append_text(out, end, " VDP1W ");
    out = vdp2_frame_append_u32(out, end, profile->vdp1_wait_ticks_last);
    out = vdp2_frame_append_text(out, end, "\nQM ");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_master_world_admit_claims);
    out = vdp2_frame_append_text(out, end, "/");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_master_world_lower_claims);
    out = vdp2_frame_append_text(out, end, "/");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_master_actor_admit_claims);
    out = vdp2_frame_append_text(out, end, "/");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_master_actor_lower_claims);
    out = vdp2_frame_append_text(out, end, " QS ");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_slave_world_admit_claims);
    out = vdp2_frame_append_text(out, end, "/");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_slave_world_lower_claims);
    out = vdp2_frame_append_text(out, end, "/");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_slave_actor_admit_claims);
    out = vdp2_frame_append_text(out, end, "/");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_slave_actor_lower_claims);
    out = vdp2_frame_append_text(out, end, "\nQN ");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_notified_generation);
    out = vdp2_frame_append_text(out, end, " QR ");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_retired_generation);
    out = vdp2_frame_append_text(out, end, " QW ");
    out = vdp2_frame_append_u32(out, end,
        profile->render_job_master_wait_iterations);
    out = vdp2_frame_append_text(out, end, " QF ");
    out = vdp2_frame_append_u32(out, end, profile->render_job_failures);
    out = vdp2_frame_append_text(out, end, " QQ ");
    out = vdp2_frame_append_u32(out, end, profile->render_job_quarantined);
    out = vdp2_frame_append_text(out, end, "\nCDW ");
    out = vdp2_frame_append_u32(out, end,
        profile->command_cpu_dmac_wait_ticks_last);
    out = vdp2_frame_append_text(out, end, " GDW ");
    out = vdp2_frame_append_u32(out, end,
        profile->gouraud_scu_dma_wait_ticks_last);
    out = vdp2_frame_append_text(out, end, " OW ");
    out = vdp2_frame_append_u32(out, end,
        profile->vdp1_overwrite_wait_ticks_last);
    out = vdp2_frame_append_text(out, end, " BW ");
    out = vdp2_frame_append_u32(out, end,
        profile->vdp1_bank_unavailable_skips);
    out = vdp2_frame_append_text(out, end, " TW ");
    out = vdp2_frame_append_u32(out, end,
        profile->vdp1_terminal_fence_wait_ticks_last);
    out = vdp2_frame_append_text(out, end, " TF ");
    out = vdp2_frame_append_u32(out, end, profile->vdp1_transfer_faults);
    out = vdp2_frame_append_text(out, end, " QNS ");
    out = vdp2_frame_append_u32(out, end,
        profile->vdp1_transfer_queued_not_started);
    *out = '\0';
}

static int vdp2_frame_generation_state_is_coherent(
    const sm64_saturn_vdp2_camera_snapshot_t *snapshot,
    const sm64_saturn_vdp2_generation_state_t *generations)
{
    if (snapshot == NULL || generations == NULL ||
        generations->displayed_generation == 0U ||
        generations->displayed_generation != generations->rendered_generation ||
        snapshot->generation != generations->displayed_generation ||
        generations->simulation_generation == 0U)
        return 0;
    return 1;
}

void sm64_saturn_vdp2_frame_init(sm64_saturn_vdp2_frame_t *frame)
{
    if (frame == NULL)
        return;
    (void)memset(frame, 0, sizeof(*frame));
    frame->display_mask = SM64_SATURN_VDP2_FRAME_DISPLAY_MASK;
    frame->last_hud_source_tick = UINT32_MAX;
    frame->last_hud_displayed_generation = UINT32_MAX;
    frame->last_hud_rendered_generation = UINT32_MAX;
    frame->last_hud_simulation_generation = UINT32_MAX;
    frame->fps_anchor_source_tick = UINT32_MAX;
}

void sm64_saturn_vdp2_frame_begin(
    sm64_saturn_vdp2_frame_t *frame,
    const sm64_saturn_vdp2_camera_snapshot_t *snapshot,
    const sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_vdp2_generation_state_t *generations,
    uint32_t source_tick)
{
    if (frame == NULL || profile == NULL)
        return;

    frame->prepared = 0U;
    if ((snapshot == NULL) != (generations == NULL) ||
        (generations != NULL &&
         !vdp2_frame_generation_state_is_coherent(snapshot, generations)))
        return;

    /* Only the copied camera values enter the scroll calculation. This
     * module deliberately has no geometry, actor, VDP1, or live-game-state
     * dependency. */
    if (snapshot != NULL && snapshot->valid != 0U) {
        frame->sky_scroll_x =
            ((int32_t)(uint16_t)snapshot->yaw * SM64_SATURN_VDP2_SKY_WIDTH) >> 16;
        frame->sky_scroll_y = 128 +
            (((int32_t)snapshot->pitch * SM64_SATURN_VDP2_SKY_HEIGHT) >> 16);
        if (frame->sky_scroll_y < 0)
            frame->sky_scroll_y = 0;
        else if (frame->sky_scroll_y > SM64_SATURN_VDP2_SKY_HEIGHT)
            frame->sky_scroll_y = SM64_SATURN_VDP2_SKY_HEIGHT;
    }

    if (frame->fps_anchor_source_tick == UINT32_MAX ||
        source_tick < frame->fps_anchor_source_tick) {
        frame->fps_anchor_source_tick = source_tick;
        frame->fps_presented_frames = 1U;
        frame->total_fps = 0U;
    } else {
        frame->fps_presented_frames++;
    }

    const int metrics_due = frame->last_hud_source_tick == UINT32_MAX ||
        source_tick < frame->last_hud_source_tick ||
        source_tick - frame->last_hud_source_tick >=
            SM64_SATURN_VDP2_FRAME_HUD_TICK_DIVISOR;
    const int generations_changed = generations != NULL &&
        (frame->last_hud_displayed_generation !=
             generations->displayed_generation ||
         frame->last_hud_rendered_generation !=
             generations->rendered_generation ||
         frame->last_hud_simulation_generation !=
             generations->simulation_generation);
    if (metrics_due || generations_changed) {
        if (metrics_due) {
            const uint32_t elapsed_source_ticks =
                source_tick - frame->fps_anchor_source_tick;
            if (elapsed_source_ticks != 0U) {
                frame->total_fps = (frame->fps_presented_frames *
                    SM64_SATURN_VDP2_FRAME_SOURCE_TICKS_PER_SECOND) /
                    elapsed_source_ticks;
                frame->fps_anchor_source_tick = source_tick;
                frame->fps_presented_frames = 0U;
            }
            frame->last_hud_source_tick = source_tick;
        }
        vdp2_frame_hud_prepare(frame, profile, generations);
        if (generations != NULL) {
            frame->last_hud_displayed_generation =
                generations->displayed_generation;
            frame->last_hud_rendered_generation =
                generations->rendered_generation;
            frame->last_hud_simulation_generation =
                generations->simulation_generation;
        }
        frame->hud_dirty = 1U;
    }
    frame->prepared = 1U;
}

void sm64_saturn_vdp2_frame_commit(
    sm64_saturn_vdp2_frame_t *frame,
    const sm64_saturn_vdp2_frame_backend_t *backend)
{
    if (frame == NULL || backend == NULL || frame->prepared == 0U)
        return;

    if (backend->sky_scroll_set != NULL)
        backend->sky_scroll_set(frame->sky_scroll_x, frame->sky_scroll_y,
                                backend->work);
    if (frame->hud_dirty != 0U && backend->hud_write != NULL)
        backend->hud_write(frame->hud_text, backend->work);
    if (backend->layers_set != NULL)
        backend->layers_set(frame->display_mask, 7U, backend->work);
    /* The Yaul adapter maps this one call to vdp2_sync(), whose VBlank-IN
     * handler commits the prepared shadow state. */
    if (backend->vblank_commit != NULL)
        backend->vblank_commit(backend->work);

    frame->hud_dirty = 0U;
    frame->prepared = 0U;
    frame->commits++;
}
