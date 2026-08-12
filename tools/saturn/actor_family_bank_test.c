#include "saturn_actor_bank.h"
#include "saturn_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, unsigned char **bytes, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0)
        return 0;
    length = ftell(file);
    if (length <= 0L || fseek(file, 0L, SEEK_SET) != 0)
        return 0;
    *bytes = (unsigned char *)malloc((size_t)length);
    if (*bytes == NULL || fread(*bytes, 1U, (size_t)length, file) != (size_t)length)
        return 0;
    fclose(file);
    *size = (size_t)length;
    return 1;
}

static unsigned int read_be32(const unsigned char *bytes)
{
    return ((unsigned int)bytes[0] << 24) | ((unsigned int)bytes[1] << 16) |
           ((unsigned int)bytes[2] << 8) | bytes[3];
}

int main(int argc, char **argv)
{
    /* Task 5's reviewed family compiler was rebuilt twice before this
     * test-only reseal. Both runs produced payload db611af6... and header
     * content 60c329ab...; production parsing and the effect oracle remain
     * unchanged. */
    static const uint8_t task5_v2_payload_sha256[32] = {
        0xdbU,0x61U,0x1aU,0xf6U,0x99U,0x33U,0x7fU,0x38U,
        0xa2U,0x28U,0x4aU,0xbfU,0x58U,0xcbU,0x28U,0x7cU,
        0x70U,0xf9U,0xabU,0x6eU,0x5dU,0xf5U,0xb0U,0x75U,
        0x55U,0xcdU,0xa4U,0x8aU,0xbaU,0x7bU,0xf3U,0x13U,
    };
    unsigned char *bytes, *copy;
    uint8_t payload_digest[32];
    size_t size;
    uint32_t expected[8];
    sm64_saturn_actor_family_bank_view_t view;
    sm64_saturn_actor_family_record_t record;
    sm64_saturn_actor_family_record_t candidate;
    uint32_t selected_capability_bits;
    int selected;
    unsigned int index;
    if (argc != 2 || !read_file(argv[1], &bytes, &size) || size > UINT32_MAX ||
        !sm64_saturn_sha256_digest(bytes, (uint32_t)size, payload_digest) ||
        memcmp(payload_digest, task5_v2_payload_sha256, sizeof(payload_digest)) != 0 ||
        !sm64_saturn_actor_family_bank_validate(bytes, size, &view))
        return 1;
    for (index = 0U; index < 8U; index++)
        expected[index] = read_be32(bytes + 24U + index * 4U);
    if (!sm64_saturn_actor_family_bank_validate_expected(bytes, size, expected, &view) ||
        !sm64_saturn_actor_family_bank_record(&view, 0U, &record) ||
        (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) == 0U)
        return 2;
    selected = sm64_saturn_actor_family_bank_select(&view, 0U, 1U);
    if (selected < 0 || !sm64_saturn_actor_family_bank_record(&view, (uint16_t)selected, &record) ||
        (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED) == 0U ||
        (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) == 0U)
        return 3;
    selected_capability_bits = (uint32_t)__builtin_popcount(record.capability_mask);
    for (index = 0U; index < view.family_count; index++) {
        if (!sm64_saturn_actor_family_bank_record(&view, (uint16_t)index, &candidate) ||
            (candidate.flags & (SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED |
                                SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY)) !=
                (SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED |
                 SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) ||
            candidate.maximum_live_instances < 1U)
            continue;
        if ((uint32_t)__builtin_popcount(candidate.capability_mask) <
            selected_capability_bits)
            return 6;
    }
    copy = (unsigned char *)malloc(size);
    if (copy == NULL)
        return 4;
    memcpy(copy, bytes, size);
    copy[view.blob_offset] ^= 0x01U;
    if (sm64_saturn_actor_family_bank_validate(copy, size, &view))
        return 5;
    free(copy);
    free(bytes);
    puts("actor family bank: PASS");
    return 0;
}
