/* Contract test for the S1.10 geo-walk equivalence digest.
 *
 * This tests the properties the oracle must have to be worth trusting, not a
 * snapshot of what the current implementation happens to emit. A digest that
 * failed any of these could pass a replacement walk that is actually wrong:
 *
 *   determinism      -- otherwise no comparison means anything
 *   order sensitivity-- the walk's publication ORDER is part of its contract;
 *                       a replacement that emits the right records in the
 *                       wrong sequence changes animation and cull outcomes
 *   value sensitivity-- any single byte of any published record
 *   tag sensitivity  -- right values published through the wrong event
 *   length framing   -- the same bytes split into different events
 *   truncation       -- a replacement that stops early
 *   generation       -- a replacement that FREEZES would otherwise match a
 *                       stationary reference tick for tick
 *   byte order       -- host fixture and SH-2 target must agree exactly, or
 *                       every host-side comparison is vacuous
 */
#include "saturn_geo_walk_digest.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t run_events(uint32_t generation, const uint32_t *tags,
                           const uint32_t *values, uint32_t count)
{
    sm64_saturn_geo_walk_digest_t digest;
    sm64_saturn_geo_walk_digest_begin(&digest, generation);
    for (uint32_t index = 0U; index < count; index++)
        sm64_saturn_geo_walk_digest_fold_u32(&digest, tags[index],
                                             values[index]);
    return sm64_saturn_geo_walk_digest_finish(&digest);
}

int main(void)
{
    const uint32_t tags[4] = {
        SM64_SATURN_GEO_WALK_EVENT_OBJECT_OBSERVED,
        SM64_SATURN_GEO_WALK_EVENT_CULL_VERDICT,
        SM64_SATURN_GEO_WALK_EVENT_ANIMATION_GLOBALS,
        SM64_SATURN_GEO_WALK_EVENT_OBSERVER_TOTALS};
    const uint32_t values[4] = {0x11111111U, 0x22222222U, 0x33333333U,
                                0x44444444U};

    /* determinism */
    const uint64_t base = run_events(7U, tags, values, 4U);
    assert(base == run_events(7U, tags, values, 4U));

    /* order sensitivity: same multiset of events, different sequence */
    const uint32_t swapped_tags[4] = {tags[1], tags[0], tags[2], tags[3]};
    const uint32_t swapped_values[4] = {values[1], values[0], values[2],
                                        values[3]};
    assert(run_events(7U, swapped_tags, swapped_values, 4U) != base);

    /* value sensitivity: every single bit position of every payload */
    for (uint32_t index = 0U; index < 4U; index++) {
        for (uint32_t bit = 0U; bit < 32U; bit++) {
            uint32_t mutated[4];
            memcpy(mutated, values, sizeof(values));
            mutated[index] ^= (uint32_t)1U << bit;
            assert(run_events(7U, tags, mutated, 4U) != base);
        }
    }

    /* tag sensitivity: identical payloads, one event re-labelled */
    uint32_t retagged[4];
    memcpy(retagged, tags, sizeof(tags));
    retagged[2] = SM64_SATURN_GEO_WALK_EVENT_THROW_MATRIX;
    assert(run_events(7U, retagged, values, 4U) != base);

    /* generation sensitivity: a frozen replacement must not match a
     * stationary reference */
    assert(run_events(8U, tags, values, 4U) != base);

    /* truncation: a prefix must never collide with the full stream */
    assert(run_events(7U, tags, values, 3U) != base);
    assert(run_events(7U, tags, values, 0U) != base);

    /* event framing: eight bytes as one event vs two four-byte events.
     *
     * Note what actually enforces this. Mutation testing showed that removing
     * the LENGTH fold does not change any digest, because every event folds a
     * tag first, and the tag positions already pin the payload boundaries: two
     * streams with identical tags and identical concatenated bytes cannot have
     * different splits. The length fold is therefore an equivalent mutant --
     * kept as cheap defence in depth for any future event that does not carry
     * a tag, but it is the TAG that makes this assertion bite. Claiming a
     * separately-testable "length" property here would be hollow. */
    sm64_saturn_geo_walk_digest_t one, two;
    const uint8_t payload[8] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    sm64_saturn_geo_walk_digest_begin(&one, 7U);
    sm64_saturn_geo_walk_digest_fold(
        &one, SM64_SATURN_GEO_WALK_EVENT_THROW_MATRIX, payload, 8U);
    sm64_saturn_geo_walk_digest_begin(&two, 7U);
    sm64_saturn_geo_walk_digest_fold(
        &two, SM64_SATURN_GEO_WALK_EVENT_THROW_MATRIX, payload, 4U);
    sm64_saturn_geo_walk_digest_fold(
        &two, SM64_SATURN_GEO_WALK_EVENT_THROW_MATRIX, payload + 4, 4U);
    assert(sm64_saturn_geo_walk_digest_finish(&one) !=
           sm64_saturn_geo_walk_digest_finish(&two));

    /* tag sensitivity for the BYTE-PAYLOAD path. The scalar case above only
     * exercises fold_u32's tag fold; mutation testing found that removing the
     * tag fold in sm64_saturn_geo_walk_digest_fold() survived the whole suite,
     * because nothing published the same bytes under two different tags
     * through that entry point. This is that case. */
    sm64_saturn_geo_walk_digest_t tag_a, tag_b;
    sm64_saturn_geo_walk_digest_begin(&tag_a, 7U);
    sm64_saturn_geo_walk_digest_fold(
        &tag_a, SM64_SATURN_GEO_WALK_EVENT_THROW_MATRIX, payload, 8U);
    sm64_saturn_geo_walk_digest_begin(&tag_b, 7U);
    sm64_saturn_geo_walk_digest_fold(
        &tag_b, SM64_SATURN_GEO_WALK_EVENT_GENERATED_LIST, payload, 8U);
    assert(sm64_saturn_geo_walk_digest_finish(&tag_a) !=
           sm64_saturn_geo_walk_digest_finish(&tag_b));

    /* matrix folding is layout-fixed and value sensitive */
    int32_t matrix[4][4];
    for (uint32_t row = 0U; row < 4U; row++)
        for (uint32_t column = 0U; column < 4U; column++)
            matrix[row][column] = (int32_t)(row * 4U + column);
    sm64_saturn_geo_walk_digest_t m_a, m_b;
    sm64_saturn_geo_walk_digest_begin(&m_a, 7U);
    sm64_saturn_geo_walk_digest_fold_matrix_q16(
        &m_a, SM64_SATURN_GEO_WALK_EVENT_CAMERA_MATRIX, matrix);
    const uint64_t matrix_digest = sm64_saturn_geo_walk_digest_finish(&m_a);
    matrix[2][3] ^= 1;
    sm64_saturn_geo_walk_digest_begin(&m_b, 7U);
    sm64_saturn_geo_walk_digest_fold_matrix_q16(
        &m_b, SM64_SATURN_GEO_WALK_EVENT_CAMERA_MATRIX, matrix);
    assert(sm64_saturn_geo_walk_digest_finish(&m_b) != matrix_digest);

    /* Byte order is pinned to one known value. This is the one golden constant
     * in this file and it is deliberate: the digest is compared between a host
     * fixture and a big-endian SH-2, so an implementation that folded native
     * words would pass every property above and still make every cross-machine
     * comparison vacuous. Recomputed by hand from FNV-1a 64 over the
     * big-endian byte stream: generation, then tag, length, payload, then the
     * event count. */
    sm64_saturn_geo_walk_digest_t pinned;
    sm64_saturn_geo_walk_digest_begin(&pinned, 0U);
    sm64_saturn_geo_walk_digest_fold_u32(&pinned, 1U, 0U);
    const uint64_t pinned_digest = sm64_saturn_geo_walk_digest_finish(&pinned);
    uint64_t expected = 0xCBF29CE484222325ULL;
    const uint8_t stream[17] = {
        0U, 0U, 0U, 0U,          /* generation 0 */
        0U, 0U, 0U, 1U,          /* tag 1 */
        0U, 0U, 0U, 4U,          /* length 4 */
        0U, 0U, 0U, 0U,          /* value 0 */
        1U                       /* event count 1, folded as 4 bytes below */
    };
    for (uint32_t index = 0U; index < 16U; index++) {
        expected ^= (uint64_t)stream[index];
        expected *= 0x00000100000001B3ULL;
    }
    const uint8_t count_bytes[4] = {0U, 0U, 0U, 1U};
    for (uint32_t index = 0U; index < 4U; index++) {
        expected ^= (uint64_t)count_bytes[index];
        expected *= 0x00000100000001B3ULL;
    }
    assert(pinned_digest == expected);

    /* fail-closed inputs must not corrupt or crash */
    sm64_saturn_geo_walk_digest_fold(NULL, 1U, payload, 4U);
    sm64_saturn_geo_walk_digest_fold_u32(NULL, 1U, 0U);
    assert(sm64_saturn_geo_walk_digest_finish(NULL) == 0ULL);
    sm64_saturn_geo_walk_digest_t guarded;
    sm64_saturn_geo_walk_digest_begin(&guarded, 3U);
    const uint32_t before = guarded.event_count;
    sm64_saturn_geo_walk_digest_fold(&guarded, 1U, NULL, 4U); /* refused */
    assert(guarded.event_count == before);
    sm64_saturn_geo_walk_digest_fold(&guarded, 1U, NULL, 0U); /* allowed */
    assert(guarded.event_count == before + 1U);

    /* folding after finish must not silently continue accumulating */
    (void)sm64_saturn_geo_walk_digest_finish(&guarded);
    const uint64_t settled = guarded.state;
    sm64_saturn_geo_walk_digest_fold_u32(&guarded, 1U, 0xDEADBEEFU);
    assert(guarded.state == settled);

    /* binding is observable and clears */
    assert(sm64_saturn_geo_walk_digest_bound() == NULL);
    sm64_saturn_geo_walk_digest_bind(&guarded);
    assert(sm64_saturn_geo_walk_digest_bound() == &guarded);
    sm64_saturn_geo_walk_digest_bind(NULL);
    assert(sm64_saturn_geo_walk_digest_bound() == NULL);

    printf("geo walk digest contract: PASS\n");
    return 0;
}
