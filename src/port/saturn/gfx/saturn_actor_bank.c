#include "saturn_actor_bank.h"

#include <string.h>

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

bool sm64_saturn_actor_bank_validate(const void *data, size_t byte_count,
                                     sm64_saturn_actor_bank_view_t *view)
{
    const uint8_t *bytes = data;
    sm64_saturn_actor_bank_view_t parsed;
    uint32_t header_size, record_size;
    uint32_t records_size;
    uint16_t geometry_part_count, geometry_joint_count;
    uint32_t geometry_joint_offset, geometry_part_offset;
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
        parsed.meshlets_size < 46U ||
        read_be32(bytes + parsed.meshlets_offset) != 0x47454F31UL)
        return false;
    geometry_part_count = read_be16(bytes + parsed.meshlets_offset + 4U);
    geometry_joint_count = read_be16(bytes + parsed.meshlets_offset + 6U);
    geometry_joint_offset = read_be32(bytes + parsed.meshlets_offset + 18U);
    geometry_part_offset = read_be32(bytes + parsed.meshlets_offset + 22U);
    if (geometry_part_count == 0U || geometry_joint_count != parsed.bank.joint_count ||
        geometry_joint_offset < 46U ||
        geometry_part_offset < geometry_joint_offset + (uint32_t)geometry_joint_count * 12U ||
        geometry_part_offset > parsed.meshlets_size)
        return false;
    for (uint16_t joint = 0U; joint < geometry_joint_count; joint++) {
        const uint8_t *record = bytes + parsed.meshlets_offset + geometry_joint_offset +
                                (uint32_t)joint * 12U;
        int16_t parent = read_be_s16(record);
        if ((joint == 0U && parent != -1) ||
            (joint != 0U && (parent < 0 || parent >= (int16_t)joint)))
            return false;
    }
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
        if (joint >= parsed.bank.joint_count || branch >= geometry_part_count)
            return false;
    }
    *view = parsed;
    return true;
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
