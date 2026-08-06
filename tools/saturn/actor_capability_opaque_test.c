#include "saturn_actor_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, unsigned char **bytes, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) return 0;
    length = ftell(file);
    if (length <= 0L || fseek(file, 0L, SEEK_SET) != 0) return 0;
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
    unsigned int index;
    unsigned int geometry_count = 0U;

    if (argc != 2 || !read_file(argv[1], &bytes, &size) ||
        !sm64_saturn_actor_family_bank_validate(bytes, size, &view)) return 1;
    for (index = 0U; index < 8U; index++)
        expected[index] = read_be32(bytes + 24U + index * 4U);
    for (index = 0U; index < view.family_count; index++) {
        if (!sm64_saturn_actor_family_bank_record(&view, (uint16_t)index, &record))
            return 2;
        if ((record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) == 0U)
            continue;
        geometry_count++;
        if (!sm64_saturn_actor_family_capability_supported(
                &record, SM64_SATURN_ACTOR_RUNTIME_CAP_TRANSFORM |
                SM64_SATURN_ACTOR_RUNTIME_CAP_SCALE |
                SM64_SATURN_ACTOR_RUNTIME_CAP_MATERIAL |
                SM64_SATURN_ACTOR_RUNTIME_CAP_LIFECYCLE)) return 3;
    }
    if (geometry_count == 0U ||
        sm64_saturn_actor_family_capability_supported(
            &record, SM64_SATURN_ACTOR_RUNTIME_CAPABILITY_MASK | (1U << 31)))
        return 4;
    if (!sm64_saturn_actor_family_bank_validate_expected(bytes, size, expected, &view))
        return 5;
    copy = (unsigned char *)malloc(size);
    if (copy == NULL) return 6;
    memcpy(copy, bytes, size);
    copy[view.records_offset + 52U] |= 0x80U;
    if (sm64_saturn_actor_family_bank_validate(copy, size, &view)) return 7;
    free(copy);
    free(bytes);
    puts("actor capability opaque: PASS");
    return 0;
}
