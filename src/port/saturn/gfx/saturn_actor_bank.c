#include "saturn_actor_bank.h"

#include <limits.h>
#include <string.h>

#define GEOMETRY_HEADER_SIZE 46U
#define JOINT_RECORD_SIZE 12U
#define PART_RECORD_SIZE 4U
#define MATERIAL_RECORD_SIZE 4U
#define MESHLET_RECORD_SIZE 66U
#define PRIMITIVE_RECORD_SIZE 10U

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int16_t read_be_s16(const uint8_t *data)
{
    return (int16_t)read_be16(data);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool span(uint32_t offset, uint32_t size, size_t byte_count)
{
    return offset <= byte_count && size <= byte_count - offset;
}

static bool validate_meshlet_tier(const uint8_t *geometry,
                                  uint32_t primitive_offset,
                                  uint32_t primitive_ref_offset,
                                  uint32_t vertex_ref_offset,
                                  uint32_t tier_primitive_offset,
                                  uint32_t tier_primitive_count,
                                  uint32_t tier_vertex_offset,
                                  uint32_t tier_vertex_count,
                                  uint16_t meshlet_material)
{
    uint32_t emitted_vertices = 0U;
    for (uint32_t local_primitive = 0U;
         local_primitive < tier_primitive_count; local_primitive++) {
        uint16_t primitive = read_be16(
            geometry + primitive_ref_offset +
            (tier_primitive_offset + local_primitive) * 2U);
        const uint8_t *primitive_record =
            geometry + primitive_offset + (uint32_t)primitive * PRIMITIVE_RECORD_SIZE;
        if (read_be16(primitive_record) != meshlet_material)
            return false;
        for (uint32_t corner = 0U; corner < 4U; corner++) {
            uint16_t vertex = read_be16(primitive_record + 2U + corner * 2U);
            bool seen = false;
            for (uint32_t prior_primitive = 0U;
                 prior_primitive <= local_primitive && !seen; prior_primitive++) {
                uint16_t prior = read_be16(
                    geometry + primitive_ref_offset +
                    (tier_primitive_offset + prior_primitive) * 2U);
                const uint8_t *prior_record =
                    geometry + primitive_offset +
                    (uint32_t)prior * PRIMITIVE_RECORD_SIZE;
                uint32_t prior_corner_limit =
                    prior_primitive == local_primitive ? corner : 4U;
                for (uint32_t prior_corner = 0U;
                     prior_corner < prior_corner_limit; prior_corner++) {
                    if (read_be16(prior_record + 2U + prior_corner * 2U) == vertex) {
                        seen = true;
                        break;
                    }
                }
            }
            if (!seen) {
                if (emitted_vertices >= tier_vertex_count ||
                    read_be16(geometry + vertex_ref_offset +
                              (tier_vertex_offset + emitted_vertices) * 2U) != vertex)
                    return false;
                emitted_vertices++;
            }
        }
    }
    return emitted_vertices == tier_vertex_count;
}

bool sm64_saturn_actor_bank_animation(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    sm64_saturn_actor_animation_record_t *record)
{
    const uint8_t *source;
    if (view == NULL || record == NULL || animation_id >= view->bank.animation_count ||
        view->records_offset > view->byte_count ||
        (uint32_t)animation_id * SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE >
            view->byte_count - view->records_offset ||
        SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE >
            view->byte_count - view->records_offset -
            (uint32_t)animation_id * SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE)
        return false;
    source = view->bytes + view->records_offset +
             (uint32_t)animation_id * SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE;
    record->values_offset = read_be32(source + 0U);
    record->indices_offset = read_be32(source + 4U);
    record->frame_count = read_be16(source + 8U);
    record->joint_count = read_be16(source + 10U);
    record->flags = read_be16(source + 12U);
    record->y_translation_divisor = read_be_s16(source + 14U);
    return true;
}

bool sm64_saturn_actor_bank_validate_expected(
    const void *data, size_t byte_count, const uint32_t expected_source_hash[8],
    sm64_saturn_actor_bank_view_t *view)
{
    const uint8_t *bytes = data;
    sm64_saturn_actor_bank_view_t parsed;
    uint32_t header_size, record_size;
    uint32_t records_size;
    uint16_t geometry_part_count, geometry_joint_count;
    uint16_t geometry_material_count, geometry_meshlet_count;
    uint16_t geometry_primitive_count, primitive_ref_count, vertex_ref_count;
    uint32_t geometry_joint_offset, geometry_part_offset, geometry_material_offset;
    uint32_t geometry_meshlet_offset, geometry_primitive_offset;
    uint32_t geometry_primitive_ref_offset, geometry_vertex_ref_offset;
    uint32_t primitive_cursor = 0U, vertex_cursor = 0U;
    uint32_t source_primitive_cursor = 0U;
    uint32_t minimum_scratch;
    int16_t previous_node = -1;
    bool hash_nonzero = false;
    if (bytes == NULL || view == NULL || byte_count < SM64_SATURN_ACTOR_BANK_HEADER_SIZE)
        return false;
    memset(&parsed, 0, sizeof(parsed));
    parsed.bytes = bytes;
    parsed.byte_count = byte_count;
    parsed.bank.magic = read_be32(bytes + 0U);
    parsed.bank.version = read_be16(bytes + 4U);
    parsed.bank.family_id = read_be16(bytes + 6U);
    parsed.bank.model_id = read_be16(bytes + 8U);
    parsed.bank.joint_count = read_be16(bytes + 10U);
    parsed.bank.animation_count = read_be16(bytes + 12U);
    parsed.bank.meshlet_count = read_be16(bytes + 14U);
    parsed.bank.primitive_count = read_be16(bytes + 16U);
    parsed.bank.vertex_count = read_be16(bytes + 18U);
    parsed.bank.max_instances = read_be16(bytes + 20U);
    parsed.bank.feature_mask = read_be32(bytes + 22U);
    for (uint16_t word = 0U; word < 8U; word++) {
        parsed.bank.source_hash_words[word] = read_be32(bytes + 26U + word * 4U);
        hash_nonzero |= parsed.bank.source_hash_words[word] != 0U;
        if (expected_source_hash != NULL &&
            parsed.bank.source_hash_words[word] != expected_source_hash[word])
            return false;
    }
    header_size = read_be16(bytes + 58U);
    record_size = read_be16(bytes + 60U);
    parsed.records_offset = read_be32(bytes + 62U);
    parsed.indices_offset = read_be32(bytes + 66U);
    parsed.indices_size = read_be32(bytes + 70U);
    parsed.values_offset = read_be32(bytes + 74U);
    parsed.values_size = read_be32(bytes + 78U);
    parsed.vertices_offset = read_be32(bytes + 82U);
    parsed.vertices_size = read_be32(bytes + 86U);
    parsed.meshlets_offset = read_be32(bytes + 90U);
    parsed.meshlets_size = read_be32(bytes + 94U);
    parsed.max_scratch = read_be32(bytes + 98U);
    if (parsed.bank.magic != SM64_SATURN_ACTOR_BANK_MAGIC ||
        parsed.bank.version != SM64_SATURN_ACTOR_BANK_VERSION ||
        header_size != SM64_SATURN_ACTOR_BANK_HEADER_SIZE ||
        record_size != SM64_SATURN_ACTOR_ANIMATION_RECORD_SIZE || !hash_nonzero ||
        parsed.bank.joint_count == 0U || parsed.bank.animation_count == 0U ||
        parsed.bank.vertex_count == 0U || parsed.bank.max_instances == 0U ||
        parsed.max_scratch == 0U)
        return false;
    minimum_scratch = (uint32_t)parsed.bank.vertex_count * 7U +
                      (uint32_t)parsed.bank.joint_count * 48U;
    if (parsed.max_scratch < minimum_scratch)
        return false;
    records_size = (uint32_t)parsed.bank.animation_count * record_size;
    if (!span(parsed.records_offset, records_size, byte_count) ||
        !span(parsed.indices_offset, parsed.indices_size, byte_count) ||
        !span(parsed.values_offset, parsed.values_size, byte_count) ||
        !span(parsed.vertices_offset, parsed.vertices_size, byte_count) ||
        !span(parsed.meshlets_offset, parsed.meshlets_size, byte_count) ||
        parsed.vertices_size != (uint32_t)parsed.bank.vertex_count * 10U ||
        parsed.records_offset < header_size ||
        parsed.indices_offset < parsed.records_offset + records_size ||
        parsed.values_offset < parsed.indices_offset + parsed.indices_size ||
        parsed.vertices_offset < parsed.values_offset + parsed.values_size ||
        parsed.meshlets_offset < parsed.vertices_offset + parsed.vertices_size ||
        parsed.meshlets_offset + parsed.meshlets_size != byte_count ||
        parsed.meshlets_size < GEOMETRY_HEADER_SIZE ||
        read_be32(bytes + parsed.meshlets_offset) != 0x47454F31UL)
        return false;
    geometry_part_count = read_be16(bytes + parsed.meshlets_offset + 4U);
    geometry_joint_count = read_be16(bytes + parsed.meshlets_offset + 6U);
    geometry_material_count = read_be16(bytes + parsed.meshlets_offset + 8U);
    geometry_meshlet_count = read_be16(bytes + parsed.meshlets_offset + 10U);
    geometry_primitive_count = read_be16(bytes + parsed.meshlets_offset + 12U);
    primitive_ref_count = read_be16(bytes + parsed.meshlets_offset + 14U);
    vertex_ref_count = read_be16(bytes + parsed.meshlets_offset + 16U);
    geometry_joint_offset = read_be32(bytes + parsed.meshlets_offset + 18U);
    geometry_part_offset = read_be32(bytes + parsed.meshlets_offset + 22U);
    geometry_material_offset = read_be32(bytes + parsed.meshlets_offset + 26U);
    geometry_meshlet_offset = read_be32(bytes + parsed.meshlets_offset + 30U);
    geometry_primitive_offset = read_be32(bytes + parsed.meshlets_offset + 34U);
    geometry_primitive_ref_offset = read_be32(bytes + parsed.meshlets_offset + 38U);
    geometry_vertex_ref_offset = read_be32(bytes + parsed.meshlets_offset + 42U);
    if (geometry_part_count == 0U || geometry_material_count == 0U ||
        geometry_joint_count != parsed.bank.joint_count ||
        geometry_meshlet_count != parsed.bank.meshlet_count ||
        geometry_primitive_count != parsed.bank.primitive_count ||
        geometry_joint_offset != GEOMETRY_HEADER_SIZE ||
        geometry_part_offset != geometry_joint_offset +
                                (uint32_t)geometry_joint_count * JOINT_RECORD_SIZE ||
        geometry_material_offset != geometry_part_offset +
                                    (uint32_t)geometry_part_count * PART_RECORD_SIZE ||
        geometry_meshlet_offset != geometry_material_offset +
                                   (uint32_t)geometry_material_count * MATERIAL_RECORD_SIZE ||
        geometry_primitive_offset != geometry_meshlet_offset +
                                     (uint32_t)geometry_meshlet_count * MESHLET_RECORD_SIZE ||
        geometry_primitive_ref_offset != geometry_primitive_offset +
                                         (uint32_t)geometry_primitive_count * PRIMITIVE_RECORD_SIZE ||
        geometry_vertex_ref_offset != geometry_primitive_ref_offset +
                                      (uint32_t)primitive_ref_count * 2U ||
        geometry_vertex_ref_offset + (uint32_t)vertex_ref_count * 2U !=
            parsed.meshlets_size)
        return false;
    for (uint16_t joint = 0U; joint < geometry_joint_count; joint++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_joint_offset +
                                (uint32_t)joint * 12U;
        int16_t parent = read_be_s16(record);
        int16_t node = read_be_s16(record + 8U);
        uint16_t branch = read_be16(record + 10U);
        if ((joint == 0U && parent != -1) ||
            (joint != 0U && (parent < 0 || parent >= (int16_t)joint)) ||
            node < 0 || (joint != 0U && node <= previous_node) ||
            (branch != 0xFFFFU && branch != (uint16_t)node))
            return false;
        previous_node = node;
    }
    for (uint16_t part = 0U; part < geometry_part_count; part++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_part_offset +
                                (uint32_t)part * PART_RECORD_SIZE;
        if (read_be16(record) >= geometry_joint_count || read_be16(record + 2U) != part)
            return false;
    }
    for (uint16_t material = 0U; material < geometry_material_count; material++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_material_offset +
                                (uint32_t)material * MATERIAL_RECORD_SIZE;
        if (record[0] > 31U || record[1] > 31U || record[2] > 31U || record[3] != 0U)
            return false;
    }
    for (uint16_t meshlet = 0U; meshlet < geometry_meshlet_count; meshlet++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_meshlet_offset +
                                (uint32_t)meshlet * MESHLET_RECORD_SIZE;
        if (read_be16(record) >= geometry_material_count ||
            read_be16(record + 2U) >= geometry_primitive_count ||
            record[4] > 1U || record[5] != 0U)
            return false;
        for (uint16_t axis = 0U; axis < 3U; axis++)
            if (read_be_s16(record + 6U + axis * 2U) >
                read_be_s16(record + 12U + axis * 2U))
                return false;
        for (uint16_t tier = 0U; tier < 3U; tier++) {
            const uint8_t *fields = record + 18U + (uint32_t)tier * 16U;
            uint32_t primitive_offset = read_be32(fields);
            uint32_t primitive_count = read_be32(fields + 4U);
            uint32_t vertex_offset = read_be32(fields + 8U);
            uint32_t vertex_count = read_be32(fields + 12U);
            if (primitive_offset != primitive_cursor || vertex_offset != vertex_cursor ||
                primitive_count > 32U ||
                primitive_count > (uint32_t)primitive_ref_count - primitive_cursor ||
                vertex_count > (uint32_t)vertex_ref_count - vertex_cursor)
                return false;
            primitive_cursor += primitive_count;
            vertex_cursor += vertex_count;
        }
    }
    if (primitive_cursor != primitive_ref_count || vertex_cursor != vertex_ref_count)
        return false;
    for (uint16_t primitive = 0U; primitive < geometry_primitive_count; primitive++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_primitive_offset +
                                (uint32_t)primitive * PRIMITIVE_RECORD_SIZE;
        uint16_t a = read_be16(record + 2U);
        uint16_t b = read_be16(record + 4U);
        uint16_t c = read_be16(record + 6U);
        uint16_t d = read_be16(record + 8U);
        if (read_be16(record) >= geometry_material_count)
            return false;
        for (uint16_t corner = 0U; corner < 4U; corner++)
            if (read_be16(record + 2U + corner * 2U) >= parsed.bank.vertex_count)
                return false;
        if (a == b || a == c || b == c ||
            (d != c && (a == d || b == d)))
            return false;
    }
    for (uint16_t reference = 0U; reference < primitive_ref_count; reference++)
        if (read_be16(bytes + parsed.meshlets_offset + geometry_primitive_ref_offset +
                      (uint32_t)reference * 2U) >= geometry_primitive_count)
            return false;
    for (uint16_t reference = 0U; reference < vertex_ref_count; reference++)
        if (read_be16(bytes + parsed.meshlets_offset + geometry_vertex_ref_offset +
                      (uint32_t)reference * 2U) >= parsed.bank.vertex_count)
            return false;
    for (uint16_t meshlet = 0U; meshlet < geometry_meshlet_count; meshlet++) {
        const uint8_t *geometry = bytes + parsed.meshlets_offset;
        const uint8_t *record = geometry + geometry_meshlet_offset +
                                (uint32_t)meshlet * MESHLET_RECORD_SIZE;
        uint16_t material = read_be16(record);
        uint16_t source_ordinal = read_be16(record + 2U);
        uint32_t primitive_offsets[3], primitive_counts[3];
        uint32_t vertex_offsets[3], vertex_counts[3];
        for (uint16_t tier = 0U; tier < 3U; tier++) {
            const uint8_t *fields = record + 18U + (uint32_t)tier * 16U;
            primitive_offsets[tier] = read_be32(fields);
            primitive_counts[tier] = read_be32(fields + 4U);
            vertex_offsets[tier] = read_be32(fields + 8U);
            vertex_counts[tier] = read_be32(fields + 12U);
        }
        if (primitive_counts[0] == 0U || primitive_counts[1] == 0U ||
            primitive_counts[0] != primitive_counts[1] ||
            vertex_counts[0] != vertex_counts[1] ||
            (uint32_t)source_ordinal != source_primitive_cursor ||
            read_be16(geometry + geometry_primitive_ref_offset +
                      primitive_offsets[0] * 2U) != source_ordinal)
            return false;
        for (uint32_t local = 0U; local < primitive_counts[0]; local++) {
            uint16_t tier0 = read_be16(
                geometry + geometry_primitive_ref_offset +
                (primitive_offsets[0] + local) * 2U);
            uint16_t tier1 = read_be16(
                geometry + geometry_primitive_ref_offset +
                (primitive_offsets[1] + local) * 2U);
            if ((uint32_t)tier0 != (uint32_t)source_ordinal + local || tier1 != tier0)
                return false;
        }
        for (uint32_t local = 0U; local < vertex_counts[0]; local++) {
            if (read_be16(geometry + geometry_vertex_ref_offset +
                          (vertex_offsets[0] + local) * 2U) !=
                read_be16(geometry + geometry_vertex_ref_offset +
                          (vertex_offsets[1] + local) * 2U))
                return false;
        }
        {
            uint32_t expected_tier2 = 0U;
            for (uint32_t local = 0U; local < primitive_counts[0]; local++) {
                uint16_t primitive = read_be16(
                    geometry + geometry_primitive_ref_offset +
                    (primitive_offsets[0] + local) * 2U);
                if (primitive % 8U == 1U) {
                    if (expected_tier2 >= primitive_counts[2] ||
                        read_be16(geometry + geometry_primitive_ref_offset +
                                  (primitive_offsets[2] + expected_tier2) * 2U) != primitive)
                        return false;
                    expected_tier2++;
                }
            }
            if (expected_tier2 != primitive_counts[2])
                return false;
        }
        for (uint16_t tier = 0U; tier < 3U; tier++) {
            if (!validate_meshlet_tier(
                    geometry, geometry_primitive_offset,
                    geometry_primitive_ref_offset, geometry_vertex_ref_offset,
                    primitive_offsets[tier], primitive_counts[tier],
                    vertex_offsets[tier], vertex_counts[tier], material))
                return false;
        }
        source_primitive_cursor += primitive_counts[0];
    }
    if (source_primitive_cursor != geometry_primitive_count)
        return false;
    for (uint16_t animation = 0U; animation < parsed.bank.animation_count; animation++) {
        sm64_saturn_actor_animation_record_t record;
        uint32_t index_words, value_words;
        if (!sm64_saturn_actor_bank_animation(&parsed, animation, &record) ||
            record.frame_count == 0U || record.joint_count != parsed.bank.joint_count ||
            record.indices_offset < parsed.indices_offset + 4U ||
            record.values_offset < parsed.values_offset + 4U ||
            record.indices_offset > parsed.indices_offset + parsed.indices_size ||
            record.values_offset > parsed.values_offset + parsed.values_size ||
            (record.indices_offset & 1U) != 0U || (record.values_offset & 1U) != 0U)
            return false;
        index_words = read_be32(bytes + record.indices_offset - 4U);
        value_words = read_be32(bytes + record.values_offset - 4U);
        if (index_words != ((uint32_t)record.joint_count + 1U) * 6U ||
            !span(record.indices_offset, index_words * 2U,
                  parsed.indices_offset + parsed.indices_size) ||
            !span(record.values_offset, value_words * 2U,
                  parsed.values_offset + parsed.values_size))
            return false;
        for (uint32_t channel = 0U; channel < index_words; channel += 2U) {
            uint32_t count = read_be16(bytes + record.indices_offset + channel * 2U);
            uint32_t offset = read_be16(bytes + record.indices_offset + (channel + 1U) * 2U);
            if (count == 0U || offset > value_words || count > value_words - offset)
                return false;
        }
    }
    for (uint16_t vertex = 0U; vertex < parsed.bank.vertex_count; vertex++) {
        const uint8_t *record = bytes + parsed.vertices_offset + (uint32_t)vertex * 10U;
        uint16_t joint = read_be16(record + 6U);
        uint16_t branch = read_be16(record + 8U);
        if (joint >= parsed.bank.joint_count || branch >= geometry_part_count ||
            read_be16(bytes + parsed.meshlets_offset + geometry_part_offset +
                      (uint32_t)branch * PART_RECORD_SIZE) != joint)
            return false;
    }
    *view = parsed;
    return true;
}

bool sm64_saturn_actor_bank_validate(const void *data, size_t byte_count,
                                     sm64_saturn_actor_bank_view_t *view)
{
    return sm64_saturn_actor_bank_validate_expected(data, byte_count, NULL, view);
}

bool sm64_saturn_actor_bank_sample_channel(
    const sm64_saturn_actor_bank_view_t *view, uint16_t animation_id,
    uint16_t frame, uint16_t channel, int16_t *sample)
{
    sm64_saturn_actor_animation_record_t record;
    uint32_t index_words, value_words, count, offset, selected;
    if (sample == NULL || view == NULL ||
        !sm64_saturn_actor_bank_animation(view, animation_id, &record) ||
        record.indices_offset < 4U || record.values_offset < 4U ||
        record.indices_offset > view->byte_count - 4U ||
        record.values_offset > view->byte_count - 4U ||
        record.indices_offset < view->indices_offset ||
        record.values_offset < view->values_offset ||
        record.indices_offset > view->indices_offset + view->indices_size ||
        record.values_offset > view->values_offset + view->values_size)
        return false;
    index_words = read_be32(view->bytes + record.indices_offset - 4U);
    value_words = read_be32(view->bytes + record.values_offset - 4U);
    if ((uint32_t)channel * 2U + 1U >= index_words ||
        index_words > (view->byte_count - record.indices_offset) / 2U ||
        value_words > (view->byte_count - record.values_offset) / 2U)
        return false;
    count = read_be16(view->bytes + record.indices_offset + (uint32_t)channel * 4U);
    offset = read_be16(view->bytes + record.indices_offset + (uint32_t)channel * 4U + 2U);
    selected = offset + (frame < count ? frame : count - 1U);
    if (count == 0U || selected >= value_words)
        return false;
    *sample = read_be_s16(view->bytes + record.values_offset + selected * 2U);
    return true;
}

bool sm64_saturn_actor_bank_joint(
    const sm64_saturn_actor_bank_view_t *view, uint16_t joint,
    sm64_saturn_actor_joint_t *out)
{
    const uint32_t offset = GEOMETRY_HEADER_SIZE + (uint32_t)joint * JOINT_RECORD_SIZE;
    const uint8_t *record;
    if (view == NULL || out == NULL || joint >= view->bank.joint_count ||
        view->meshlets_size < offset + JOINT_RECORD_SIZE)
        return false;
    record = view->bytes + view->meshlets_offset + offset;
    out->parent_ordinal = read_be_s16(record);
    out->translation[0] = read_be_s16(record + 2U);
    out->translation[1] = read_be_s16(record + 4U);
    out->translation[2] = read_be_s16(record + 6U);
    out->node_ordinal = read_be_s16(record + 8U);
    out->branch_ordinal = read_be16(record + 10U);
    return true;
}

bool sm64_saturn_actor_bank_vertex(
    const sm64_saturn_actor_bank_view_t *view, uint16_t vertex,
    sm64_saturn_actor_vertex_t *out)
{
    const uint32_t offset = view == NULL ? 0U : view->vertices_offset +
        (uint32_t)vertex * 10U;
    if (view == NULL || out == NULL || vertex >= view->bank.vertex_count ||
        offset > view->byte_count || 10U > view->byte_count - offset)
        return false;
    out->local[0] = read_be_s16(view->bytes + offset);
    out->local[1] = read_be_s16(view->bytes + offset + 2U);
    out->local[2] = read_be_s16(view->bytes + offset + 4U);
    out->joint_ordinal = read_be16(view->bytes + offset + 6U);
    out->branch_ordinal = read_be16(view->bytes + offset + 8U);
    return out->joint_ordinal < view->bank.joint_count;
}

static bool family_span(uint32_t offset, uint32_t size, uint32_t blob_size)
{
    return offset <= blob_size && size <= blob_size - offset;
}

typedef struct family_sha256 {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    uint8_t block_size;
} family_sha256_t;

static uint32_t family_rotr(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32U - amount));
}

static void family_sha256_transform(family_sha256_t *context)
{
    static const uint32_t constants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t words[64], a, b, c, d, e, f, g, h;
    uint32_t index;
    for (index = 0U; index < 16U; index++) {
        const uint8_t *word = context->block + index * 4U;
        words[index] = ((uint32_t)word[0] << 24) | ((uint32_t)word[1] << 16) |
                       ((uint32_t)word[2] << 8) | word[3];
    }
    for (; index < 64U; index++) {
        uint32_t x = words[index - 15U], y = words[index - 2U];
        words[index] = (family_rotr(x, 7U) ^ family_rotr(x, 18U) ^ (x >> 3)) +
                       words[index - 16U] +
                       (family_rotr(y, 17U) ^ family_rotr(y, 19U) ^ (y >> 10)) +
                       words[index - 7U];
    }
    a = context->state[0]; b = context->state[1]; c = context->state[2];
    d = context->state[3]; e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (index = 0U; index < 64U; index++) {
        uint32_t sigma1 = family_rotr(e, 6U) ^ family_rotr(e, 11U) ^ family_rotr(e, 25U);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temporary1 = h + sigma1 + choose + constants[index] + words[index];
        uint32_t sigma0 = family_rotr(a, 2U) ^ family_rotr(a, 13U) ^ family_rotr(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sigma0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c;
    context->state[3] += d; context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

static void family_sha256_init(family_sha256_t *context)
{
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    memcpy(context->state, initial, sizeof(initial));
    context->bit_count = 0U;
    context->block_size = 0U;
}

static void family_sha256_update(family_sha256_t *context,
                                 const uint8_t *data, size_t byte_count)
{
    while (byte_count != 0U) {
        size_t available = 64U - context->block_size;
        size_t take = byte_count < available ? byte_count : available;
        memcpy(context->block + context->block_size, data, take);
        context->block_size = (uint8_t)(context->block_size + take);
        context->bit_count += (uint64_t)take * 8U;
        data += take;
        byte_count -= take;
        if (context->block_size == 64U) {
            family_sha256_transform(context);
            context->block_size = 0U;
        }
    }
}

static void family_sha256_final(family_sha256_t *context, uint8_t digest[32])
{
    uint8_t length[8];
    uint32_t index;
    uint64_t bit_count = context->bit_count;
    for (index = 0U; index < 8U; index++)
        length[7U - index] = (uint8_t)(bit_count >> (index * 8U));
    family_sha256_update(context, (const uint8_t *)"\x80", 1U);
    while (context->block_size != 56U) {
        uint8_t zero = 0U;
        family_sha256_update(context, &zero, 1U);
    }
    family_sha256_update(context, length, sizeof(length));
    for (index = 0U; index < 8U; index++) {
        digest[index * 4U] = (uint8_t)(context->state[index] >> 24);
        digest[index * 4U + 1U] = (uint8_t)(context->state[index] >> 16);
        digest[index * 4U + 2U] = (uint8_t)(context->state[index] >> 8);
        digest[index * 4U + 3U] = (uint8_t)context->state[index];
    }
}

static void family_sha256_payload(const uint8_t *bytes, size_t byte_count,
                                  uint8_t digest[32])
{
    family_sha256_t context;
    static const uint8_t zero_digest[32] = {0};
    family_sha256_init(&context);
    family_sha256_update(&context, bytes, 24U);
    family_sha256_update(&context, zero_digest, sizeof(zero_digest));
    family_sha256_update(&context, bytes + 56U, byte_count - 56U);
    family_sha256_final(&context, digest);
}

bool sm64_saturn_actor_family_bank_validate_expected(
    const void *data, size_t byte_count,
    const uint32_t expected_hash[8], sm64_saturn_actor_family_bank_view_t *view)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t count;
    uint32_t records_offset, records_size, blob_offset, blob_size;
    uint16_t index;
    uint8_t digest[32];
    bool hash_nonzero = false;
    if (bytes == NULL || view == NULL || byte_count < SM64_SATURN_ACTOR_FAMILY_BANK_HEADER_SIZE ||
        read_be32(bytes) != SM64_SATURN_ACTOR_FAMILY_BANK_MAGIC ||
        read_be16(bytes + 4U) != SM64_SATURN_ACTOR_FAMILY_BANK_VERSION)
        return false;
    count = read_be16(bytes + 6U);
    records_offset = read_be32(bytes + 8U);
    records_size = read_be32(bytes + 12U);
    blob_offset = read_be32(bytes + 16U);
    blob_size = read_be32(bytes + 20U);
    for (index = 0U; index < 8U; index++)
        hash_nonzero |= read_be32(bytes + 24U + index * 4U) != 0U;
    if (!hash_nonzero || count == 0U)
        return false;
    family_sha256_payload(bytes, byte_count, digest);
    for (index = 0U; index < 8U; index++) {
        uint32_t actual = ((uint32_t)digest[index * 4U] << 24) |
                          ((uint32_t)digest[index * 4U + 1U] << 16) |
                          ((uint32_t)digest[index * 4U + 2U] << 8) |
                          digest[index * 4U + 3U];
        if (actual != read_be32(bytes + 24U + index * 4U) ||
            (expected_hash != NULL && actual != expected_hash[index]))
            return false;
    }
    if (records_offset != SM64_SATURN_ACTOR_FAMILY_BANK_HEADER_SIZE ||
        records_size != (uint32_t)count * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE ||
        blob_offset != records_offset + records_size ||
        blob_offset > byte_count || blob_size != byte_count - blob_offset)
        return false;
    for (index = 0U; index < count; index++) {
        const uint8_t *record = bytes + records_offset +
            (uint32_t)index * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE;
        uint16_t prior;
        uint32_t family_id = read_be32(record);
        if (family_id == 0U || (read_be32(record + 16U) &
                               ~(SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED |
                                 SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY)) != 0U ||
            (read_be32(record + 52U) &
             ~SM64_SATURN_ACTOR_RUNTIME_CAPABILITY_MASK) != 0U)
            return false;
        for (prior = 0U; prior < index; prior++) {
            const uint8_t *other = bytes + records_offset +
                (uint32_t)prior * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE;
            if (read_be32(other) == family_id)
                return false;
        }
        if (!family_span(read_be32(record + 20U), read_be32(record + 24U), blob_size) ||
            !family_span(read_be32(record + 28U), read_be32(record + 32U), blob_size) ||
            !family_span(read_be32(record + 36U), read_be32(record + 40U), blob_size) ||
            !family_span(read_be32(record + 44U), read_be32(record + 48U), blob_size))
            return false;
    }
    view->bytes = bytes;
    view->byte_count = byte_count;
    view->version = SM64_SATURN_ACTOR_FAMILY_BANK_VERSION;
    view->family_count = count;
    view->records_offset = records_offset;
    view->records_size = records_size;
    view->blob_offset = blob_offset;
    view->blob_size = blob_size;
    return true;
}

bool sm64_saturn_actor_family_bank_validate(
    const void *data, size_t byte_count,
    sm64_saturn_actor_family_bank_view_t *view)
{
    return sm64_saturn_actor_family_bank_validate_expected(data, byte_count, NULL, view);
}

bool sm64_saturn_actor_family_bank_record(
    const sm64_saturn_actor_family_bank_view_t *view, uint16_t index,
    sm64_saturn_actor_family_record_t *out)
{
    const uint8_t *record;
    if (view == NULL || out == NULL || index >= view->family_count)
        return false;
    record = view->bytes + view->records_offset +
        (uint32_t)index * SM64_SATURN_ACTOR_FAMILY_RECORD_SIZE;
    out->family_id = read_be32(record);
    out->capability_mask = read_be32(record + 4U);
    out->maximum_live_instances = read_be32(record + 8U);
    out->actor_count = read_be32(record + 12U);
    out->flags = read_be32(record + 16U);
    out->name_offset = read_be32(record + 20U);
    out->name_size = read_be32(record + 24U);
    out->source_offset = read_be32(record + 28U);
    out->source_size = read_be32(record + 32U);
    out->unsupported_offset = read_be32(record + 36U);
    out->unsupported_size = read_be32(record + 40U);
    out->metadata_offset = read_be32(record + 44U);
    out->metadata_size = read_be32(record + 48U);
    out->runtime_capability_mask = read_be32(record + 52U);
    return family_span(out->name_offset, out->name_size, view->blob_size) &&
           family_span(out->source_offset, out->source_size, view->blob_size) &&
           family_span(out->unsupported_offset, out->unsupported_size, view->blob_size) &&
           family_span(out->metadata_offset, out->metadata_size, view->blob_size);
}

bool sm64_saturn_actor_family_capability_supported(
    const sm64_saturn_actor_family_record_t *record,
    uint32_t required_runtime_capability_mask)
{
    if (record == NULL ||
        (required_runtime_capability_mask &
         ~SM64_SATURN_ACTOR_RUNTIME_CAPABILITY_MASK) != 0U)
        return false;
    return (record->runtime_capability_mask &
            required_runtime_capability_mask) == required_runtime_capability_mask;
}

int sm64_saturn_actor_family_bank_select(
    const sm64_saturn_actor_family_bank_view_t *view,
    uint32_t required_capability_mask, uint32_t multiplicity)
{
    uint16_t index;
    int selected = -1;
    uint32_t selected_bits = UINT32_MAX, selected_capacity = UINT32_MAX,
             selected_id = UINT32_MAX;
    if (view == NULL)
        return -1;
    for (index = 0U; index < view->family_count; index++) {
        sm64_saturn_actor_family_record_t record;
        uint32_t bits;
        if (!sm64_saturn_actor_family_bank_record(view, index, &record) ||
            (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_SUPPORTED) == 0U ||
            (record.flags & SM64_SATURN_ACTOR_FAMILY_FLAG_GEOMETRY) == 0U ||
            (record.capability_mask & required_capability_mask) != required_capability_mask ||
            record.maximum_live_instances < multiplicity)
            continue;
        bits = record.capability_mask;
        bits = bits == 0U ? 0U : (uint32_t)__builtin_popcount(bits);
        if (bits < selected_bits ||
            (bits == selected_bits && record.maximum_live_instances < selected_capacity) ||
            (bits == selected_bits && record.maximum_live_instances == selected_capacity &&
             record.family_id < selected_id)) {
            selected = (int)index;
            selected_bits = bits;
            selected_capacity = record.maximum_live_instances;
            selected_id = record.family_id;
        }
    }
    return selected;
}
