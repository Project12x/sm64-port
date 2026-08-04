#include <assert.h>
#include <stdint.h>

#include "saturn_visible_position_set.h"

int main(void)
{
    /* Two quads share position 2. Primitive 2 is intentionally excluded. */
    static const uint16_t primitive_indices[][4] = {
        {0U, 1U, 2U, 3U},
        {2U, 3U, 4U, 5U},
        {6U, 7U, 8U, 9U},
        {7U, 8U, 9U, 10U}
    };
    uint32_t words[SM64_SATURN_VISIBLE_POSITION_SET_WORDS(10U)];
    sm64_saturn_visible_position_set_t set;

    sm64_saturn_visible_position_set_reset(&set, words,
        SM64_SATURN_VISIBLE_POSITION_SET_WORDS(10U), 10U);
    assert(sm64_saturn_visible_position_set_count(&set) == 0U);

    assert(sm64_saturn_visible_position_set_mark_primitive(
        &set, primitive_indices[0]));
    assert(sm64_saturn_visible_position_set_mark_primitive(
        &set, primitive_indices[1]));
    assert(sm64_saturn_visible_position_set_count(&set) == 6U);
    for (uint16_t position = 0U; position < 6U; position++)
        assert(sm64_saturn_visible_position_set_test(&set, position));
    for (uint16_t position = 6U; position < 10U; position++)
        assert(!sm64_saturn_visible_position_set_test(&set, position));

    /* The excluded third primitive was never admitted, so it marks nothing. */
    assert(!sm64_saturn_visible_position_set_test(&set, 6U));
    assert(!sm64_saturn_visible_position_set_test(&set, 7U));
    assert(!sm64_saturn_visible_position_set_test(&set, 8U));
    assert(!sm64_saturn_visible_position_set_test(&set, 9U));

    /* An out-of-range position index fails closed without partial marks. */
    assert(!sm64_saturn_visible_position_set_mark_primitive(
        &set, primitive_indices[3]));
    assert(sm64_saturn_visible_position_set_count(&set) == 6U);
    assert(!sm64_saturn_visible_position_set_test(&set, 7U));
    assert(!sm64_saturn_visible_position_set_test(&set, 8U));
    assert(!sm64_saturn_visible_position_set_test(&set, 9U));
    assert(!sm64_saturn_visible_position_set_test(&set, 10U));

    /* Admission supplies exact compact references, not whole primitives. */
    static const uint16_t far_refs[] = {1U, 4U, 6U};
    assert(sm64_saturn_visible_position_set_mark_refs(
        &set, far_refs, sizeof(far_refs) / sizeof(far_refs[0])));
    assert(sm64_saturn_visible_position_set_count(&set) == 7U);
    assert(sm64_saturn_visible_position_set_test(&set, 6U));

    static const uint16_t invalid_refs[] = {0U, 10U};
    assert(!sm64_saturn_visible_position_set_mark_refs(
        &set, invalid_refs, sizeof(invalid_refs) / sizeof(invalid_refs[0])));
    assert(sm64_saturn_visible_position_set_count(&set) == 7U);
    return 0;
}
