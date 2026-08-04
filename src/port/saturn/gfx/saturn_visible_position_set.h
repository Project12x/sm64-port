#ifndef SM64_SATURN_VISIBLE_POSITION_SET_H
#define SM64_SATURN_VISIBLE_POSITION_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Caller-owned, fixed storage. The master prepares it before transform jobs
 * launch; workers consume it as immutable frame input. */
#define SM64_SATURN_VISIBLE_POSITION_SET_WORDS(position_count) \
    (((uint32_t)(position_count) + 31U) / 32U)

typedef struct sm64_saturn_visible_position_set {
    uint32_t *words;
    uint16_t word_count;
    uint16_t position_count;
    uint16_t marked_count;
} sm64_saturn_visible_position_set_t;

static inline void sm64_saturn_visible_position_set_reset(
    sm64_saturn_visible_position_set_t *set, uint32_t *words,
    uint16_t word_count, uint16_t position_count)
{
    if (set == NULL)
        return;
    *set = (sm64_saturn_visible_position_set_t){0};
    if (words == NULL ||
        word_count < SM64_SATURN_VISIBLE_POSITION_SET_WORDS(position_count))
        return;
    set->words = words;
    set->word_count = word_count;
    set->position_count = position_count;
    memset(words, 0, (size_t)word_count * sizeof(*words));
}

static inline bool sm64_saturn_visible_position_set_test(
    const sm64_saturn_visible_position_set_t *set, uint16_t position)
{
    if (set == NULL || set->words == NULL || position >= set->position_count)
        return false;
    return (set->words[position >> 5U] &
            ((uint32_t)1U << (position & 31U))) != 0U;
}

/* Validate every corner before marking. A malformed generated primitive can
 * never leave a partially transformed position set behind. */
static inline bool sm64_saturn_visible_position_set_mark_primitive(
    sm64_saturn_visible_position_set_t *set, const uint16_t indices[4])
{
    if (set == NULL || set->words == NULL || indices == NULL)
        return false;
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        if (indices[corner] >= set->position_count)
            return false;
    }
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        const uint16_t position = indices[corner];
        uint32_t *const word = &set->words[position >> 5U];
        const uint32_t mask = (uint32_t)1U << (position & 31U);
        if ((*word & mask) == 0U) {
            *word |= mask;
            set->marked_count++;
        }
    }
    return true;
}

/* Compact admission spans name exactly the positions the selected tier uses.
 * Validate the entire immutable span first so malformed generated metadata
 * cannot partially alter the frame's transform workload. */
static inline bool sm64_saturn_visible_position_set_mark_refs(
    sm64_saturn_visible_position_set_t *set, const uint16_t *refs,
    uint16_t ref_count)
{
    if (set == NULL || set->words == NULL || (refs == NULL && ref_count != 0U))
        return false;
    for (uint16_t offset = 0U; offset < ref_count; offset++) {
        if (refs[offset] >= set->position_count)
            return false;
    }
    for (uint16_t offset = 0U; offset < ref_count; offset++) {
        const uint16_t position = refs[offset];
        uint32_t *const word = &set->words[position >> 5U];
        const uint32_t mask = (uint32_t)1U << (position & 31U);
        if ((*word & mask) == 0U) {
            *word |= mask;
            set->marked_count++;
        }
    }
    return true;
}

static inline uint16_t sm64_saturn_visible_position_set_count(
    const sm64_saturn_visible_position_set_t *set)
{
    return set == NULL ? 0U : set->marked_count;
}

#endif
