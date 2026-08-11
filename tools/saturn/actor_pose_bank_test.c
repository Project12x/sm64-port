#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "saturn_actor_pose.h"
#include "saturn_actor_bank.h"

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static int reject_u16_mutation(const uint8_t *source, size_t size,
                               uint32_t offset, uint16_t value,
                               const char *label)
{
    uint8_t *copy = malloc(size);
    sm64_saturn_actor_bank_view_t view;
    if (copy == NULL) return 1;
    memcpy(copy, source, size);
    write_be16(copy + offset, value);
    if (sm64_saturn_actor_bank_validate(copy, size, &view)) {
        fprintf(stderr, "%s was accepted\n", label);
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
}

static int reject_u32_mutation(const uint8_t *source, size_t size,
                               uint32_t offset, uint32_t value,
                               const char *label)
{
    uint8_t *copy = malloc(size);
    sm64_saturn_actor_bank_view_t view;
    if (copy == NULL) return 1;
    memcpy(copy, source, size);
    write_be32(copy + offset, value);
    if (sm64_saturn_actor_bank_validate(copy, size, &view)) {
        fprintf(stderr, "%s was accepted\n", label);
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
}

static int reject_u8_mutation(const uint8_t *source, size_t size,
                              uint32_t offset, uint8_t value,
                              const char *label)
{
    uint8_t *copy = malloc(size);
    sm64_saturn_actor_bank_view_t view;
    if (copy == NULL) return 1;
    memcpy(copy, source, size);
    copy[offset] = value;
    if (sm64_saturn_actor_bank_validate(copy, size, &view)) {
        fprintf(stderr, "%s was accepted\n", label);
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
}

static int reject_duplicate_meshlet_partition(const uint8_t *source, size_t size,
                                              uint32_t meshlet_offset,
                                              uint32_t primitive_ref_offset,
                                              uint32_t vertex_ref_offset)
{
    uint8_t *copy = malloc(size);
    sm64_saturn_actor_bank_view_t view;
    const uint8_t *source_meshlet = source + meshlet_offset;
    uint8_t *duplicate_meshlet;
    if (copy == NULL) return 1;
    memcpy(copy, source, size);
    duplicate_meshlet = copy + meshlet_offset + 4U * 66U;
    write_be16(duplicate_meshlet + 2U, read_be16(source_meshlet + 2U));
    for (uint32_t tier = 0U; tier < 3U; tier++) {
        const uint8_t *source_fields = source_meshlet + 18U + tier * 16U;
        const uint8_t *duplicate_fields =
            source + meshlet_offset + 4U * 66U + 18U + tier * 16U;
        uint32_t source_primitive_start = read_be32(source_fields);
        uint32_t primitive_count = read_be32(source_fields + 4U);
        uint32_t source_vertex_start = read_be32(source_fields + 8U);
        uint32_t vertex_count = read_be32(source_fields + 12U);
        uint32_t duplicate_primitive_start = read_be32(duplicate_fields);
        uint32_t duplicate_vertex_start = read_be32(duplicate_fields + 8U);
        if (primitive_count != read_be32(duplicate_fields + 4U) ||
            vertex_count != read_be32(duplicate_fields + 12U)) {
            fprintf(stderr, "duplicate-partition fixture counts changed\n");
            free(copy);
            return 1;
        }
        memcpy(copy + primitive_ref_offset + duplicate_primitive_start * 2U,
               source + primitive_ref_offset + source_primitive_start * 2U,
               primitive_count * 2U);
        memcpy(copy + vertex_ref_offset + duplicate_vertex_start * 2U,
               source + vertex_ref_offset + source_vertex_start * 2U,
               vertex_count * 2U);
    }
    if (sm64_saturn_actor_bank_validate(copy, size, &view)) {
        fprintf(stderr, "coordinated duplicate/gap meshlet partition was accepted\n");
        free(copy);
        return 1;
    }
    free(copy);
    return 0;
}

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
    uint32_t geometry, joint_offset, part_offset, material_offset, meshlet_offset;
    uint32_t primitive_offset, primitive_ref_offset, vertex_ref_offset;
    uint32_t tier1_primitive_ref, tier1_vertex_ref, tier2_primitive_ref;
    uint16_t material_count, primitive_count, vertex_count;
    uint16_t first_material, first_source, first_primitive, first_vertex;
    uint32_t expected_source_hash[8];
    uint32_t lane_bytes, total_bytes;
    if (!sm64_saturn_actor_bank_workspace_requirements(
            1U, 1U, &lane_bytes, &total_bytes) || lane_bytes != 76U ||
        total_bytes != 152U ||
        !sm64_saturn_actor_bank_workspace_requirements(
            2U, 1U, &lane_bytes, &total_bytes) || lane_bytes != 88U ||
        total_bytes != 176U ||
        sm64_saturn_actor_bank_workspace_requirements(
            0U, 1U, &lane_bytes, &total_bytes)) {
        fprintf(stderr, "package-derived actor workspace accounting failed\n");
        return 1;
    }
    if (argc != 2 || (bytes = read_file(argv[1], &size)) == NULL) {
        fprintf(stderr, "usage: actor-pose-bank-test actor-bank.s64b\n");
        return 1;
    }
    if (!sm64_saturn_actor_bank_validate(bytes, size, &view) ||
        view.bank.animation_count != 209U || view.bank.joint_count != 20U ||
        view.bank.vertex_count != 424U || view.bank.primitive_count != 644U ||
        view.bank.meshlet_count != 31U || view.max_scratch != 11040U) {
        fprintf(stderr, "complete actor bank did not validate\n");
        free(bytes);
        return 1;
    }

    {
        int16_t vertices[424][3];
        uint8_t lights[424];
        int32_t joints[20 * 16];
        sm64_saturn_actor_pose_work_t work = {
            .vertices = vertices, .light_intensity = lights,
            .joint_matrices_q16 = joints, .vertex_capacity = 424U,
            .joint_capacity = 20U, .light_capacity = 424U,
        };
        sm64_saturn_actor_pose_view_t pose;
        uint8_t seen[209] = {0};
        for (int16_t animation = 0; animation < 209; animation++) {
            if (!sm64_saturn_actor_pose_evaluate(
                    &view, animation, -3, &work, &pose) ||
                pose.animation_id != (uint16_t)animation || pose.frame != 0U ||
                pose.vertex_count != 424U || pose.joint_count != 20U ||
                pose.vertices != (const int16_t (*)[3])vertices ||
                lights[0] != 255U) {
                fprintf(stderr,
                        "complete evaluator rejected animation %d frame=%u "
                        "verts=%u joints=%u lights=%u\n",
                        animation, pose.frame, pose.vertex_count,
                        pose.joint_count, lights[0]);
                free(bytes);
                return 1;
            }
            seen[animation] = 1U;
        }
        for (uint16_t animation = 0U; animation < 209U; animation++)
            if (seen[animation] == 0U) {
                fprintf(stderr, "animation ID %u was not evaluated\n", animation);
                free(bytes);
                return 1;
            }
        if (!sm64_saturn_actor_pose_evaluate(&view, 208, INT16_MAX,
                                             &work, &pose) ||
            pose.frame != pose.frame_count - 1U ||
            sm64_saturn_actor_pose_evaluate(&view, -1, 0, &work, &pose) ||
            sm64_saturn_actor_pose_evaluate(&view, 209, 0, &work, &pose) ||
            sm64_saturn_actor_pose_evaluate(&view, 0, 0, &work, NULL)) {
            fprintf(stderr, "evaluator bounds/overflow contract failed\n");
            free(bytes);
            return 1;
        }
        {
            int16_t sample;
            if (!sm64_saturn_actor_pose_evaluate(&view, 0, 0, &work, &pose) ||
                !sm64_saturn_actor_bank_sample_channel(&view, 0U, 0U, 0U,
                                                       &sample) ||
                pose.root_translation[0] != sample) {
                fprintf(stderr, "root translation was not published\n");
                free(bytes);
                return 1;
            }
        }
    }
    {
        uint8_t *extreme = malloc(size);
        sm64_saturn_actor_bank_view_t extreme_view;
        int16_t extreme_vertices[424][3];
        uint8_t extreme_lights[424];
        int32_t extreme_joints[20 * 16];
        sm64_saturn_actor_pose_work_t extreme_work = {
            .vertices = extreme_vertices, .light_intensity = extreme_lights,
            .joint_matrices_q16 = extreme_joints, .vertex_capacity = 424U,
            .joint_capacity = 20U, .light_capacity = 424U,
        };
        sm64_saturn_actor_pose_view_t extreme_pose;
        const uint32_t joint_table = view.meshlets_offset +
            read_be32(bytes + view.meshlets_offset + 18U);
        if (extreme == NULL) {
            free(bytes);
            return 1;
        }
        memcpy(extreme, bytes, size);
        /* A root translation at INT16_MAX plus the source root channel is a
         * valid-width field but not representable in Q16.16 int32. The
         * evaluator must reject it without signed-wrap UB. */
        extreme[joint_table + 2U] = 0x7FU;
        extreme[joint_table + 3U] = 0xFFU;
        if (!sm64_saturn_actor_bank_validate(extreme, size, &extreme_view) ||
            sm64_saturn_actor_pose_evaluate(
                &extreme_view, 0, 0, &extreme_work, &extreme_pose)) {
            fprintf(stderr, "extreme Q16 translation was not rejected\n");
            free(extreme);
            free(bytes);
            return 1;
        }
        free(extreme);
    }
    memcpy(expected_source_hash, view.bank.source_hash_words,
           sizeof(expected_source_hash));
    if (!sm64_saturn_actor_bank_validate_expected(
            bytes, size, expected_source_hash, &view)) {
        fprintf(stderr, "expected source identity rejected valid actor bank\n");
        free(bytes);
        return 1;
    }
    {
        uint8_t *copy = malloc(size);
        if (copy == NULL) {
            free(bytes);
            return 1;
        }
        memcpy(copy, bytes, size);
        copy[26U] ^= 1U;
        if (sm64_saturn_actor_bank_validate_expected(
                copy, size, expected_source_hash, &view)) {
            fprintf(stderr, "mismatched source identity was accepted\n");
            free(copy);
            free(bytes);
            return 1;
        }
        free(copy);
    }
    geometry = view.meshlets_offset;
    joint_offset = geometry + read_be32(bytes + geometry + 18U);
    part_offset = geometry + read_be32(bytes + geometry + 22U);
    material_offset = geometry + read_be32(bytes + geometry + 26U);
    meshlet_offset = geometry + read_be32(bytes + geometry + 30U);
    primitive_offset = geometry + read_be32(bytes + geometry + 34U);
    primitive_ref_offset = geometry + read_be32(bytes + geometry + 38U);
    vertex_ref_offset = geometry + read_be32(bytes + geometry + 42U);
    material_count = read_be16(bytes + geometry + 8U);
    primitive_count = read_be16(bytes + geometry + 12U);
    vertex_count = view.bank.vertex_count;
    first_material = read_be16(bytes + meshlet_offset);
    first_source = read_be16(bytes + meshlet_offset + 2U);
    first_primitive = read_be16(bytes + primitive_ref_offset);
    first_vertex = read_be16(bytes + vertex_ref_offset);
    tier1_primitive_ref = primitive_ref_offset +
                          read_be32(bytes + meshlet_offset + 34U) * 2U;
    tier1_vertex_ref = vertex_ref_offset +
                       read_be32(bytes + meshlet_offset + 42U) * 2U;
    tier2_primitive_ref = primitive_ref_offset +
                          read_be32(bytes + meshlet_offset + 50U) * 2U;
    if (reject_u32_mutation(bytes, size, geometry + 26U,
                            read_be32(bytes + geometry + 26U) + 1U,
                            "noncanonical material span") ||
        reject_u32_mutation(bytes, size, geometry + 30U,
                            read_be32(bytes + geometry + 30U) + 1U,
                            "noncanonical meshlet span") ||
        reject_u32_mutation(bytes, size, geometry + 34U,
                            read_be32(bytes + geometry + 34U) + 1U,
                            "noncanonical primitive span") ||
        reject_u32_mutation(bytes, size, geometry + 38U,
                            read_be32(bytes + geometry + 38U) + 1U,
                            "noncanonical primitive-reference span") ||
        reject_u32_mutation(bytes, size, geometry + 42U,
                            read_be32(bytes + geometry + 42U) + 1U,
                            "noncanonical vertex-reference span") ||
        reject_u16_mutation(bytes, size, geometry + 10U,
                            (uint16_t)(view.bank.meshlet_count + 1U),
                            "meshlet count mismatch") ||
        reject_u16_mutation(bytes, size, geometry + 12U,
                            (uint16_t)(view.bank.primitive_count + 1U),
                            "primitive count mismatch") ||
        reject_u16_mutation(bytes, size, geometry + 14U,
                            (uint16_t)(read_be16(bytes + geometry + 14U) + 1U),
                            "primitive-reference count mismatch") ||
        reject_u16_mutation(bytes, size, geometry + 16U,
                            (uint16_t)(read_be16(bytes + geometry + 16U) + 1U),
                            "vertex-reference count mismatch") ||
        reject_u16_mutation(bytes, size, part_offset, view.bank.joint_count,
                            "part joint ownership overflow") ||
        reject_u16_mutation(bytes, size, part_offset + 2U, 0xFFFFU,
                            "part branch ownership overflow") ||
        reject_u8_mutation(bytes, size, material_offset, 32U,
                           "material RGB555 lane overflow") ||
        reject_u16_mutation(bytes, size, meshlet_offset,
                            read_be16(bytes + geometry + 8U),
                            "meshlet material overflow") ||
        reject_u8_mutation(bytes, size, meshlet_offset + 5U, 1U,
                           "meshlet reserved metadata") ||
        reject_u16_mutation(bytes, size, meshlet_offset + 6U, 0x7FFFU,
                            "inverted meshlet bounds") ||
        reject_u32_mutation(bytes, size, meshlet_offset + 18U, 1U,
                            "nonmonotonic meshlet tier primitive span") ||
        reject_u16_mutation(bytes, size, primitive_offset,
                            read_be16(bytes + geometry + 8U),
                            "primitive material overflow") ||
        reject_u16_mutation(bytes, size, primitive_offset + 2U,
                            view.bank.vertex_count,
                            "primitive vertex overflow") ||
        reject_u16_mutation(bytes, size, primitive_ref_offset,
                            view.bank.primitive_count,
                            "meshlet primitive reference overflow") ||
        reject_u16_mutation(bytes, size, vertex_ref_offset,
                            view.bank.vertex_count,
                            "meshlet vertex reference overflow") ||
        reject_u16_mutation(bytes, size, meshlet_offset,
                            (uint16_t)((first_material + 1U) % material_count),
                            "in-range meshlet material mismatch") ||
        reject_u16_mutation(bytes, size, meshlet_offset + 2U,
                            (uint16_t)((first_source + 1U) % primitive_count),
                            "in-range meshlet source ownership mismatch") ||
        reject_u16_mutation(bytes, size, primitive_ref_offset,
                            (uint16_t)((first_primitive + 1U) % primitive_count),
                            "in-range tier primitive-list mismatch") ||
        reject_u16_mutation(bytes, size, vertex_ref_offset,
                            (uint16_t)((first_vertex + 1U) % vertex_count),
                            "in-range tier vertex-list mismatch") ||
        reject_u16_mutation(bytes, size, tier1_primitive_ref,
                            (uint16_t)((read_be16(bytes + tier1_primitive_ref) + 1U) %
                                       primitive_count),
                            "in-range tier-one primitive-list mismatch") ||
        reject_u16_mutation(bytes, size, tier1_vertex_ref,
                            (uint16_t)((read_be16(bytes + tier1_vertex_ref) + 1U) %
                                       vertex_count),
                            "in-range tier-one vertex-list mismatch") ||
        reject_u16_mutation(bytes, size, tier2_primitive_ref,
                            (uint16_t)((read_be16(bytes + tier2_primitive_ref) + 1U) %
                                       primitive_count),
                            "in-range tier-two primitive-subset mismatch") ||
        reject_u16_mutation(bytes, size,
                            primitive_offset + (uint32_t)first_primitive * 10U,
                            (uint16_t)((first_material + 1U) % material_count),
                            "in-range primitive material mismatch") ||
        reject_u16_mutation(bytes, size,
                            primitive_offset + (uint32_t)first_primitive * 10U + 2U,
                            (uint16_t)((read_be16(bytes + primitive_offset +
                                                (uint32_t)first_primitive * 10U + 2U) + 1U) %
                                       vertex_count),
                            "in-range primitive vertex mismatch") ||
        reject_u16_mutation(bytes, size, joint_offset + 12U + 8U,
                            read_be16(bytes + joint_offset + 8U),
                            "in-range duplicate joint node ordinal") ||
        reject_u16_mutation(bytes, size, joint_offset + 7U * 12U + 10U, 0U,
                            "in-range joint branch metadata mismatch") ||
        reject_u16_mutation(bytes, size, part_offset,
                            (uint16_t)((read_be16(bytes + part_offset) + 1U) %
                                       view.bank.joint_count),
                            "in-range part joint ownership mismatch") ||
        reject_duplicate_meshlet_partition(bytes, size, meshlet_offset,
                                           primitive_ref_offset,
                                           vertex_ref_offset) ||
        reject_u32_mutation(bytes, size, 98U, view.max_scratch - 1U,
                            "undersized actor scratch claim")) {
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
