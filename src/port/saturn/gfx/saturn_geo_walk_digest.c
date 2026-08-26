#include "saturn_geo_walk_digest.h"

#include <stddef.h>

/* FNV-1a 64. Constants are the published ones; they are not tunable. */
#define GEO_WALK_DIGEST_OFFSET_BASIS 0xCBF29CE484222325ULL
#define GEO_WALK_DIGEST_PRIME        0x00000100000001B3ULL

static sm64_saturn_geo_walk_digest_t *s_bound_digest;

static void fold_byte(sm64_saturn_geo_walk_digest_t *digest, uint8_t value)
{
    digest->state ^= (uint64_t)value;
    digest->state *= GEO_WALK_DIGEST_PRIME;
}

static void fold_u32_raw(sm64_saturn_geo_walk_digest_t *digest, uint32_t value)
{
    /* Explicit big-endian byte order rather than memcpy of the native word, so
     * a host fixture and the SH-2 target agree byte for byte. The digests are
     * compared across those two machines; letting endianness leak in would
     * make every host-side check meaningless. */
    fold_byte(digest, (uint8_t)((value >> 24) & 0xFFU));
    fold_byte(digest, (uint8_t)((value >> 16) & 0xFFU));
    fold_byte(digest, (uint8_t)((value >> 8) & 0xFFU));
    fold_byte(digest, (uint8_t)(value & 0xFFU));
}

void sm64_saturn_geo_walk_digest_begin(sm64_saturn_geo_walk_digest_t *digest,
                                       uint32_t generation)
{
    if (digest == NULL) return;
    digest->state = GEO_WALK_DIGEST_OFFSET_BASIS;
    digest->event_count = 0U;
    digest->generation = generation;
    digest->active = 1U;
    digest->reserved[0] = 0U;
    digest->reserved[1] = 0U;
    digest->reserved[2] = 0U;
    fold_u32_raw(digest, generation);
}

void sm64_saturn_geo_walk_digest_fold(sm64_saturn_geo_walk_digest_t *digest,
                                      uint32_t tag, const void *bytes,
                                      uint32_t size)
{
    if (digest == NULL || !digest->active) return;
    if (bytes == NULL && size != 0U) return;
    fold_u32_raw(digest, tag);
    /* The length is folded as well as the payload: without it, two adjacent
     * events could be split differently and still hash the same. */
    fold_u32_raw(digest, size);
    const uint8_t *cursor = (const uint8_t *)bytes;
    for (uint32_t index = 0U; index < size; index++)
        fold_byte(digest, cursor[index]);
    digest->event_count++;
}

void sm64_saturn_geo_walk_digest_fold_u32(
    sm64_saturn_geo_walk_digest_t *digest, uint32_t tag, uint32_t value)
{
    if (digest == NULL || !digest->active) return;
    fold_u32_raw(digest, tag);
    fold_u32_raw(digest, 4U);
    fold_u32_raw(digest, value);
    digest->event_count++;
}

void sm64_saturn_geo_walk_digest_fold_matrix_q16(
    sm64_saturn_geo_walk_digest_t *digest, uint32_t tag,
    const int32_t matrix[4][4])
{
    if (digest == NULL || !digest->active || matrix == NULL) return;
    fold_u32_raw(digest, tag);
    fold_u32_raw(digest, 64U);
    for (uint32_t row = 0U; row < 4U; row++)
        for (uint32_t column = 0U; column < 4U; column++)
            fold_u32_raw(digest, (uint32_t)matrix[row][column]);
    digest->event_count++;
}

uint64_t sm64_saturn_geo_walk_digest_finish(
    sm64_saturn_geo_walk_digest_t *digest)
{
    if (digest == NULL) return 0ULL;
    fold_u32_raw(digest, digest->event_count);
    digest->active = 0U;
    return digest->state;
}

sm64_saturn_geo_walk_digest_t *sm64_saturn_geo_walk_digest_bound(void)
{
    return s_bound_digest;
}

void sm64_saturn_geo_walk_digest_bind(sm64_saturn_geo_walk_digest_t *digest)
{
    s_bound_digest = digest;
}
