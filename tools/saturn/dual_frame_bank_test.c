#include <assert.h>
#include <stdint.h>

#include "saturn_dual_frame_bank.h"

static void test_publish_releases_count_and_sequence(void)
{
    sm64_saturn_dual_frame_bank_t bank = {0};
    uint16_t count = UINT16_MAX;
    sm64_saturn_dual_frame_reset(&bank);
    assert(!sm64_saturn_dual_frame_peer_ready(&bank, 0U, 7U, &count));
    assert(count == 0U);

    sm64_saturn_dual_frame_publish(&bank, 1U, 7U, 31U);
    assert(bank.lane[1].sequence == 7U);
    assert(bank.lane[1].count == 31U);
    assert(bank.lane[1].ready == 1U);
    assert(sm64_saturn_dual_frame_peer_ready(&bank, 0U, 7U, &count));
    assert(count == 31U);
    assert(!sm64_saturn_dual_frame_peer_ready(&bank, 0U, 8U, &count));
}

static void test_owner_and_peer_pointer_selection(void)
{
    uint32_t values[2] = {11U, 17U};
    assert(sm64_saturn_dual_frame_read_range(0U, 0U, values) == values);
    /* Host aliases collapse by design; target static verification separately
     * proves that the SH-2 branch uses LWRAM_UNCACHED. */
    assert(sm64_saturn_dual_frame_read_range(0U, 1U, values) == values);
}

int main(void)
{
    test_publish_releases_count_and_sequence();
    test_owner_and_peer_pointer_selection();
    return 0;
}
