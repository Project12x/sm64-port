#include "saturn_actor_bank.h"

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
    if (view == NULL || record == NULL || animation_id >= view->bank.animation_count)
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
    if (sample == NULL || !sm64_saturn_actor_bank_animation(view, animation_id, &record))
        return false;
    index_words = read_be32(view->bytes + record.indices_offset - 4U);
    value_words = read_be32(view->bytes + record.values_offset - 4U);
    if ((uint32_t)channel * 2U + 1U >= index_words)
        return false;
    count = read_be16(view->bytes + record.indices_offset + (uint32_t)channel * 4U);
    offset = read_be16(view->bytes + record.indices_offset + (uint32_t)channel * 4U + 2U);
    selected = offset + (frame < count ? frame : count - 1U);
    if (count == 0U || selected >= value_words)
        return false;
    *sample = read_be_s16(view->bytes + record.values_offset + selected * 2U);
    return true;
}
