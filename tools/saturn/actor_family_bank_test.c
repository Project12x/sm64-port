#include "saturn_actor_bank.h"

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
    unsigned char *bytes, *copy;
    size_t size;
    uint32_t expected[8];
    sm64_saturn_actor_family_bank_view_t view;
    sm64_saturn_actor_family_record_t record;
    sm64_saturn_actor_family_record_t candidate;
    uint32_t selected_capability_bits;
    int selected;
    unsigned int index;
    if (argc != 2 || !read_file(argv[1], &bytes, &size) ||
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
