#include <assert.h>
#include <stdint.h>

#include "scsp_timer.h"

static void test_rational_cadence_has_no_long_term_drift(void)
{
    sm64_saturn_scsp_timer_t timer;
    uint32_t total = 0U;
    uint16_t second;
    assert(sm64_saturn_scsp_timer_init(&timer, 44100U, 240U, 3U));
    for (second = 0U; second < 10U; ++second) {
        uint16_t due = 0U;
        assert(sm64_saturn_scsp_timer_advance(&timer, 44100U, &due));
        assert(due == 240U);
        total += due;
    }
    assert(total == 2400U);
    assert(timer.remainder == 0U);
}

static void test_fractional_chunks_match_one_full_interval(void)
{
    sm64_saturn_scsp_timer_t whole, chunks;
    uint16_t whole_due = 0U, chunk_due = 0U, due;
    uint16_t i;
    assert(sm64_saturn_scsp_timer_init(&whole, 44100U, 240U, 2U));
    assert(sm64_saturn_scsp_timer_init(&chunks, 44100U, 240U, 2U));
    assert(sm64_saturn_scsp_timer_advance(&whole, 44100U, &whole_due));
    for (i = 0U; i < 63U; ++i) {
        assert(sm64_saturn_scsp_timer_advance(&chunks, 700U, &due));
        chunk_due = (uint16_t)(chunk_due + due);
    }
    assert(whole_due == chunk_due);
    assert(whole.remainder == chunks.remainder);
}

static void test_stall_and_duplicate_generation_fail_closed(void)
{
    sm64_saturn_scsp_timer_t timer;
    assert(sm64_saturn_scsp_timer_init(&timer, 44100U, 240U, 3U));
    assert(sm64_saturn_scsp_timer_observe_hardware_tick(&timer, 10U));
    assert(sm64_saturn_scsp_timer_observe_hardware_tick(&timer, 10U));
    assert(sm64_saturn_scsp_timer_observe_hardware_tick(&timer, 10U));
    assert(!sm64_saturn_scsp_timer_observe_hardware_tick(&timer, 10U));
    assert(timer.stalled);

    assert(sm64_saturn_scsp_timer_claim_generation(&timer, 0xfffffffeU));
    assert(!sm64_saturn_scsp_timer_claim_generation(&timer, 0xfffffffeU));
    assert(sm64_saturn_scsp_timer_claim_generation(&timer, 1U));
    assert(!sm64_saturn_scsp_timer_claim_generation(&timer, 0xffffffffU));
    assert(timer.duplicate_or_stale_generations == 2U);
}

static void test_invalid_and_oversized_steps_are_rejected(void)
{
    sm64_saturn_scsp_timer_t timer;
    uint16_t due = 7U;
    assert(!sm64_saturn_scsp_timer_init(&timer, 0U, 240U, 3U));
    assert(!sm64_saturn_scsp_timer_init(&timer, 100U, 101U, 3U));
    assert(sm64_saturn_scsp_timer_init(&timer, 44100U, 240U, 3U));
    assert(!sm64_saturn_scsp_timer_advance(&timer, 44101U, &due));
    assert(due == 0U);
    assert(!sm64_saturn_scsp_timer_advance(&timer, 1U, 0));
}

int main(void)
{
    test_rational_cadence_has_no_long_term_drift();
    test_fractional_chunks_match_one_full_interval();
    test_stall_and_duplicate_generation_fail_closed();
    test_invalid_and_oversized_steps_are_rejected();
    return 0;
}
