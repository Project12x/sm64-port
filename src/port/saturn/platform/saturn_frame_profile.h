#ifndef SM64_SATURN_FRAME_PROFILE_H
#define SM64_SATURN_FRAME_PROFILE_H

#include <stdint.h>

/* Hardware-counter samples for one source update and one optional rendered
 * frame. This record belongs to the platform scheduler, not to any scene. */
typedef struct sm64_saturn_frame_profile {
    uint16_t update_ticks;
    uint16_t sort_ticks;
    uint16_t command_ticks;
    uint16_t wait_ticks;
    uint16_t vblank_ticks;
    uint32_t render_ticks;
    uint32_t loop_ticks;
} sm64_saturn_frame_profile_t;

static inline uint32_t
sm64_saturn_frame_profile_render_total(sm64_saturn_frame_profile_t *profile)
{
    profile->render_ticks = (uint32_t)profile->update_ticks +
                            profile->sort_ticks +
                            profile->command_ticks +
                            profile->wait_ticks;
    return profile->render_ticks;
}

static inline uint32_t
sm64_saturn_frame_profile_loop_total(sm64_saturn_frame_profile_t *profile)
{
    profile->loop_ticks = profile->render_ticks + profile->vblank_ticks;
    return profile->loop_ticks;
}

static inline uint32_t
sm64_saturn_frame_profile_rate_x10(uint32_t ticks_per_second_x10,
                                   uint32_t elapsed_ticks)
{
    return elapsed_ticks == 0 ? 0 : ticks_per_second_x10 / elapsed_ticks;
}

#endif
