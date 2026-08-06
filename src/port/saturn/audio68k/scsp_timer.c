/* Original project rational timer model. It deliberately does not inherit the
 * VBlank-driven scheduler in libyaul-examples@66b648eb, whose
 * scsp-ponesound-pcm8/ponesound.c:63-92,118-121 was inspected pattern-only. */
#include "scsp_timer.h"

static bool generation_newer(uint32_t candidate, uint32_t previous)
{
    uint32_t delta = candidate - previous;
    return delta != 0U && delta < 0x80000000U;
}

bool sm64_saturn_scsp_timer_init(sm64_saturn_scsp_timer_t *timer,
                                 uint32_t input_hz, uint32_t service_hz,
                                 uint16_t stall_limit)
{
    if (timer == 0 || input_hz == 0U || service_hz == 0U ||
        service_hz > input_hz || stall_limit == 0U)
        return false;
    *timer = (sm64_saturn_scsp_timer_t){0};
    timer->input_hz = input_hz;
    timer->service_hz = service_hz;
    timer->stall_limit = stall_limit;
    return true;
}

bool sm64_saturn_scsp_timer_advance(sm64_saturn_scsp_timer_t *timer,
                                    uint16_t elapsed_input_ticks,
                                    uint16_t *service_ticks_due)
{
    uint16_t input_tick;
    uint16_t due = 0U;
    if (service_ticks_due != 0) *service_ticks_due = 0U;
    if (timer == 0 || service_ticks_due == 0 || elapsed_input_ticks == 0U ||
        elapsed_input_ticks > timer->input_hz || timer->stalled)
        return false;
    for (input_tick = 0U; input_tick < elapsed_input_ticks; ++input_tick) {
        timer->remainder += timer->service_hz;
        if (timer->remainder >= timer->input_hz) {
            timer->remainder -= timer->input_hz;
            due++;
        }
    }
    timer->service_ticks += due;
    *service_ticks_due = due;
    return true;
}

bool sm64_saturn_scsp_timer_observe_hardware_tick(
    sm64_saturn_scsp_timer_t *timer, uint32_t hardware_tick)
{
    if (timer == 0 || timer->stalled) return false;
    if (!timer->has_hardware_tick || hardware_tick != timer->last_hardware_tick) {
        timer->has_hardware_tick = true;
        timer->last_hardware_tick = hardware_tick;
        timer->stagnant_observations = 0U;
        return true;
    }
    timer->stagnant_observations++;
    if (timer->stagnant_observations >= timer->stall_limit) {
        timer->stalled = true;
        return false;
    }
    return true;
}

bool sm64_saturn_scsp_timer_claim_generation(sm64_saturn_scsp_timer_t *timer,
                                             uint32_t generation)
{
    if (timer == 0 || generation == 0U ||
        (timer->has_generation &&
         !generation_newer(generation, timer->last_generation))) {
        if (timer != 0) timer->duplicate_or_stale_generations++;
        return false;
    }
    timer->has_generation = true;
    timer->last_generation = generation;
    return true;
}
