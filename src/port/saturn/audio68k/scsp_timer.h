#ifndef SM64_SATURN_AUDIO68K_SCSP_TIMER_H
#define SM64_SATURN_AUDIO68K_SCSP_TIMER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct sm64_saturn_scsp_timer {
    uint32_t input_hz;
    uint32_t service_hz;
    uint32_t remainder;
    uint32_t last_hardware_tick;
    uint32_t last_generation;
    uint32_t service_ticks;
    uint32_t duplicate_or_stale_generations;
    uint16_t stagnant_observations;
    uint16_t stall_limit;
    bool has_hardware_tick;
    bool has_generation;
    bool stalled;
} sm64_saturn_scsp_timer_t;

bool sm64_saturn_scsp_timer_init(sm64_saturn_scsp_timer_t *timer,
                                 uint32_t input_hz, uint32_t service_hz,
                                 uint16_t stall_limit);
bool sm64_saturn_scsp_timer_advance(sm64_saturn_scsp_timer_t *timer,
                                    uint16_t elapsed_input_ticks,
                                    uint16_t *service_ticks_due);
bool sm64_saturn_scsp_timer_observe_hardware_tick(
    sm64_saturn_scsp_timer_t *timer, uint32_t hardware_tick);
bool sm64_saturn_scsp_timer_claim_generation(sm64_saturn_scsp_timer_t *timer,
                                             uint32_t generation);

#endif
