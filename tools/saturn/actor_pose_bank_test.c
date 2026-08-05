#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saturn_actor_bank.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    long length;
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    data = malloc((size_t)length);
    if (data == NULL || fread(data, 1U, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

int main(int argc, char **argv)
{
    size_t size;
    uint8_t *bytes;
    sm64_saturn_actor_bank_view_t view;
    sm64_saturn_actor_animation_record_t first, shared_a, shared_b;
    int16_t sample;
    if (argc != 2 || (bytes = read_file(argv[1], &size)) == NULL) {
        fprintf(stderr, "usage: actor-pose-bank-test actor-bank.s64b\n");
        return 1;
    }
    if (!sm64_saturn_actor_bank_validate(bytes, size, &view) ||
        view.bank.animation_count != 209U || view.bank.joint_count != 20U ||
        view.bank.vertex_count != 424U || view.bank.primitive_count != 644U ||
        view.bank.meshlet_count != 31U || view.max_scratch < 3928U) {
        fprintf(stderr, "complete actor bank did not validate\n");
        free(bytes);
        return 1;
    }
    if (!sm64_saturn_actor_bank_animation(&view, 0U, &first) ||
        !sm64_saturn_actor_bank_animation(&view, 1U, &shared_a) ||
        !sm64_saturn_actor_bank_animation(&view, 2U, &shared_b) ||
        shared_a.indices_offset != shared_b.indices_offset ||
        shared_a.values_offset != shared_b.values_offset ||
        !sm64_saturn_actor_bank_sample_channel(&view, 0U, 0U, 0U, &sample) ||
        sample != 7) {
        fprintf(stderr, "actor animation lookup or source clamping changed\n");
        free(bytes);
        return 1;
    }
    {
        uint8_t saved[4];
        memcpy(saved, bytes + first.values_offset - 4U, sizeof(saved));
        memset(bytes + first.values_offset - 4U, 0xFF, sizeof(saved));
        if (sm64_saturn_actor_bank_validate(bytes, size, &view)) {
            fprintf(stderr, "overflowing value-stream word count was accepted\n");
            free(bytes);
            return 1;
        }
        memcpy(bytes + first.values_offset - 4U, saved, sizeof(saved));
    }
    {
        uint8_t *owner = bytes + view.vertices_offset + 6U;
        uint8_t saved[2] = {owner[0], owner[1]};
        owner[0] = 0xFFU;
        owner[1] = 0xFFU;
        if (sm64_saturn_actor_bank_validate(bytes, size, &view)) {
            fprintf(stderr, "invalid vertex joint ordinal was accepted\n");
            free(bytes);
            return 1;
        }
        owner[0] = saved[0];
        owner[1] = saved[1];
    }
    {
        uint8_t *joint_count = bytes + view.meshlets_offset + 6U;
        uint8_t saved[2] = {joint_count[0], joint_count[1]};
        joint_count[0] = joint_count[1] = 0U;
        if (sm64_saturn_actor_bank_validate(bytes, size, &view)) {
            fprintf(stderr, "incomplete skeleton table was accepted\n");
            free(bytes);
            return 1;
        }
        joint_count[0] = saved[0];
        joint_count[1] = saved[1];
    }
    bytes[26U] = bytes[27U] = bytes[28U] = bytes[29U] = 0U;
    memset(bytes + 30U, 0, 28U);
    if (sm64_saturn_actor_bank_validate(bytes, size, &view)) {
        fprintf(stderr, "zero source identity was accepted\n");
        free(bytes);
        return 1;
    }
    free(bytes);
    puts("actor pose bank fixture: PASS");
    return 0;
}
