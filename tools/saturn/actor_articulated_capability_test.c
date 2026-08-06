#include "saturn_actor_bank.h"

#include <stdio.h>
#include <stdlib.h>

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

static int require_selection(const sm64_saturn_actor_family_bank_view_t *view,
                             uint32_t capability)
{
    int selected = sm64_saturn_actor_family_bank_select(view, capability, 1U);
    sm64_saturn_actor_family_record_t record;
    if (selected < 0 ||
        !sm64_saturn_actor_family_bank_record(view, (uint16_t)selected, &record) ||
        (record.flags & (SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED |
                         SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY)) !=
            (SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED |
             SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) ||
        (record.capability_mask & capability) != capability)
        return 0;
    return 1;
}

int main(int argc, char **argv)
{
    unsigned char *bytes;
    size_t size;
    sm64_saturn_actor_family_bank_view_t view;
    const uint32_t articulated_mask =
        SM64_SATURN_ACTOR_CAP_ANIMATED |
        SM64_SATURN_ACTOR_CAP_SWITCH |
        SM64_SATURN_ACTOR_CAP_PARENTED |
        SM64_SATURN_ACTOR_CAP_MODEL_MUTATION;
    if (argc != 2 || !read_file(argv[1], &bytes, &size) ||
        !sm64_saturn_actor_family_bank_validate(bytes, size, &view))
        return 1;
    if (!sm64_saturn_actor_family_capability_mask_supported(articulated_mask) ||
        sm64_saturn_actor_family_capability_mask_supported(1U << 31) ||
        sm64_saturn_actor_family_bank_select(&view, 1U << 31, 1U) >= 0)
        return 2;
    if (!require_selection(&view, SM64_SATURN_ACTOR_CAP_ANIMATED) ||
        !require_selection(&view, SM64_SATURN_ACTOR_CAP_SWITCH) ||
        !require_selection(&view, SM64_SATURN_ACTOR_CAP_PARENTED) ||
        !require_selection(&view, SM64_SATURN_ACTOR_CAP_MODEL_MUTATION))
        return 3;
    free(bytes);
    puts("actor articulated capability: PASS");
    return 0;
}
