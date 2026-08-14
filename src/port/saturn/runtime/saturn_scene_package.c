#include "saturn_scene_package.h"
#include "../platform/saturn_cart_code.h"
#include "saturn_sha256.h"

#include <stddef.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

bool sm64_saturn_scene_package_sha256(const void *bytes, uint32_t byte_count,
                                      uint8_t digest[32])
{
    return sm64_saturn_sha256_digest(bytes, byte_count, digest);
}

static bool digest_equal(const uint8_t a[32], const uint8_t b[32])
{
    uint8_t different = 0U;
    uint32_t index;
    for (index = 0U; index < 32U; index++) different |= (uint8_t)(a[index] ^ b[index]);
    return different == 0U;
}

static bool power_of_two(uint32_t value)
{
    return value != 0U && value <= 4096U && (value & (value - 1U)) == 0U;
}

static bool valid_destination(uint8_t value) { return value < SM64_SATURN_SCENE_DESTINATION_COUNT; }
static bool valid_lifetime(uint8_t value) { return value >= 1U && value <= 5U; }

static bool stable_id_valid(const uint8_t id[32])
{
    uint32_t index = 0U, cursor;
    while (index < 32U && id[index] != 0U) index++;
    if (index == 0U || index == 32U) return false;
    for (cursor = index; cursor < 32U; cursor++) if (id[cursor] != 0U) return false;
    cursor = 0U;
    while (cursor < index) {
        uint8_t lead = id[cursor++];
        uint32_t need, codepoint, minimum;
        if (lead < 0x80U) continue;
        if (lead >= 0xC2U && lead <= 0xDFU) { need=1U; codepoint=lead&0x1FU; minimum=0x80U; }
        else if (lead >= 0xE0U && lead <= 0xEFU) { need=2U; codepoint=lead&0x0FU; minimum=0x800U; }
        else if (lead >= 0xF0U && lead <= 0xF4U) { need=3U; codepoint=lead&0x07U; minimum=0x10000U; }
        else return false;
        if (need > index - cursor) return false;
        while (need-- != 0U) {
            uint8_t continuation=id[cursor++];
            if ((continuation&0xC0U)!=0x80U) return false;
            codepoint=(codepoint<<6)|(continuation&0x3FU);
        }
        if (codepoint<minimum || codepoint>0x10FFFFU ||
            (codepoint>=0xD800U && codepoint<=0xDFFFU)) return false;
    }
    return true;
}

static bool parse_dependencies(const uint8_t *bytes,
                               const sm64_saturn_scene_section_view_t *section,
                               sm64_saturn_scene_package_view_t *view)
{
    uint32_t count, index, cursor;
    if (section->byte_size < 4U) return false;
    count = read_u32(bytes + section->offset);
    if (count > (uint32_t)SM64_SATURN_SCENE_MAX_DEPENDENCIES - view->dependency_count ||
        count > (section->byte_size - 4U) / SM64_SATURN_SCENE_DEPENDENCY_DESCRIPTOR_SIZE ||
        4U + count * SM64_SATURN_SCENE_DEPENDENCY_DESCRIPTOR_SIZE != section->byte_size) return false;
    cursor = section->offset + 4U;
    for (index = 0U; index < count; index++, cursor += SM64_SATURN_SCENE_DEPENDENCY_DESCRIPTOR_SIZE) {
        const uint8_t *raw = bytes + cursor;
        sm64_saturn_scene_dependency_view_t *dependency = &view->dependencies[view->dependency_count++];
        uint32_t earlier;
        dependency->payload_kind = read_u16(raw);
        dependency->destination_class = raw[2]; dependency->lifetime = raw[3];
        memcpy(dependency->stable_id, raw + 4U, 32U);
        dependency->byte_count = read_u32(raw + 36U);
        dependency->alignment = read_u32(raw + 40U);
        dependency->dependency_mask = read_u32(raw + 44U);
        dependency->maximum_scratch = read_u32(raw + 48U);
        memcpy(dependency->content_sha256, raw + 52U, 32U);
        dependency->generation = read_u32(raw + 84U);
        if (dependency->payload_kind != section->kind ||
            dependency->payload_kind < SM64_SATURN_SCENE_ACTOR_DEPENDENCIES ||
            dependency->payload_kind > SM64_SATURN_SCENE_AUDIO_DEPENDENCIES ||
            dependency->destination_class == SM64_SATURN_SCENE_DESTINATION_NONE ||
            !valid_destination(dependency->destination_class) || !valid_lifetime(dependency->lifetime) ||
            !stable_id_valid(dependency->stable_id) ||
            !power_of_two(dependency->alignment) || read_u32(raw + 88U) != 0U || read_u32(raw + 92U) != 0U) return false;
        for (earlier = 0U; earlier + 1U < view->dependency_count; earlier++)
            if (memcmp(view->dependencies[earlier].stable_id, dependency->stable_id, 32U) == 0) return false;
        if (view->dependency_count >= 2U) {
            const sm64_saturn_scene_dependency_view_t *previous =
                &view->dependencies[view->dependency_count - 2U];
            if (previous->payload_kind == dependency->payload_kind &&
                memcmp(previous->stable_id, dependency->stable_id, 32U) >= 0) return false;
        }
    }
    return true;
}

static bool dependency_graph_valid(const sm64_saturn_scene_package_view_t *view)
{
    uint32_t present = view->dependency_count == 32U ? 0xFFFFFFFFU :
        ((1U << view->dependency_count) - 1U);
    uint32_t closure[SM64_SATURN_SCENE_MAX_DEPENDENCIES];
    uint32_t index, pass;
    for (index = 0U; index < view->dependency_count; index++) {
        if ((view->dependencies[index].dependency_mask & ~present) != 0U ||
            (view->dependencies[index].dependency_mask & (1U << index)) != 0U) return false;
        closure[index] = view->dependencies[index].dependency_mask;
    }
    for (pass = 0U; pass < view->dependency_count; pass++) {
        bool changed = false;
        for (index = 0U; index < view->dependency_count; index++) {
            uint32_t bits = closure[index], bit;
            for (bit = 0U; bit < view->dependency_count; bit++) if ((bits & (1U << bit)) != 0U)
                closure[index] |= closure[bit];
            if ((closure[index] & (1U << index)) != 0U) return false;
            changed |= closure[index] != bits;
        }
        if (!changed) break;
    }
    return true;
}

static bool section_graph_valid(const sm64_saturn_scene_package_view_t *view)
{
    uint32_t present = (1U << view->section_count) - 1U;
    uint32_t closure[SM64_SATURN_SCENE_MAX_SECTIONS];
    uint32_t index, pass;
    if (view->section_count == 0U) return true;
    for (index = 0U; index < view->section_count; index++) {
        if ((view->sections[index].dependency_mask & ~present) != 0U ||
            (view->sections[index].dependency_mask & (1U << index)) != 0U) return false;
        closure[index] = view->sections[index].dependency_mask;
    }
    for (pass = 0U; pass < view->section_count; pass++) {
        bool changed = false;
        for (index = 0U; index < view->section_count; index++) {
            uint32_t bits = closure[index], bit;
            for (bit = 0U; bit < view->section_count; bit++)
                if ((bits & (1U << bit)) != 0U) closure[index] |= closure[bit];
            if ((closure[index] & (1U << index)) != 0U) return false;
            changed |= closure[index] != bits;
        }
        if (!changed) break;
    }
    return true;
}

static bool dependency_digest_valid(const sm64_saturn_scene_package_view_t *view)
{
    static const uint8_t prefix[11] = {'S','6','4','P','-','D','E','P','S',0,1};
    sm64_saturn_sha256_t sha;
    uint8_t record[70], digest[32];
    uint32_t index;
    sm64_saturn_sha256_init(&sha);
    if (!sm64_saturn_sha256_update(&sha, prefix, sizeof(prefix))) return false;
    for (index = 0U; index < view->dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *item = &view->dependencies[index];
        record[0] = (uint8_t)(item->payload_kind >> 8); record[1] = (uint8_t)item->payload_kind;
        memcpy(record + 2U, item->stable_id, 32U); write_u32(record + 34U, item->generation);
        memcpy(record + 38U, item->content_sha256, 32U);
        if (!sm64_saturn_sha256_update(&sha, record, sizeof(record))) return false;
    }
    return sm64_saturn_sha256_finish(&sha, digest) &&
           digest_equal(digest, view->dependency_set_sha256);
}

SM64_SATURN_CART_COLD
bool sm64_saturn_scene_package_validate(const void *source, uint32_t byte_count,
                                        sm64_saturn_scene_package_view_t *view)
{
    sm64_saturn_scene_package_view_t local_view;
    sm64_saturn_scene_package_view_t *const caller_view = view;
    const uint8_t *bytes = (const uint8_t *)source;
    uint8_t digest[32];
    uint8_t saved_hash[32];
    uint8_t root_copy_hash_input[32];
    uint32_t section_count, table_end, previous_end, index;
    sm64_saturn_sha256_t root_sha;
    if (caller_view != NULL) memset(caller_view, 0, sizeof(*caller_view));
    if (bytes == NULL || caller_view == NULL || byte_count < SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE) return false;
    memset(&local_view, 0, sizeof(local_view));
    view = &local_view;
    if (read_u32(bytes) != SM64_SATURN_SCENE_PACKAGE_MAGIC || read_u16(bytes+4U) != SM64_SATURN_SCENE_PACKAGE_VERSION ||
        read_u16(bytes+6U) != SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE || read_u32(bytes+8U) != byte_count ||
        read_u16(bytes+14U) == 0U || (read_u16(bytes+18U) & ~SM64_SATURN_SCENE_PACKAGE_FLAG_PROVISIONAL) != 0U) return false;
    section_count = read_u16(bytes+16U);
    if ((section_count != 0U && section_count != SM64_SATURN_SCENE_MAX_SECTIONS) ||
        section_count > (UINT32_MAX - SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE) / SM64_SATURN_SCENE_SECTION_DESCRIPTOR_SIZE) return false;
    table_end = SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE + section_count * SM64_SATURN_SCENE_SECTION_DESCRIPTOR_SIZE;
    if (table_end > byte_count) return false;
    memcpy(saved_hash, bytes+20U, 32U); memset(root_copy_hash_input, 0, sizeof(root_copy_hash_input));
    sm64_saturn_sha256_init(&root_sha);
    if (!sm64_saturn_sha256_update(&root_sha, bytes, 20U) ||
        !sm64_saturn_sha256_update(&root_sha, root_copy_hash_input, 32U) ||
        !sm64_saturn_sha256_update(&root_sha, bytes+52U, byte_count-52U) ||
        !sm64_saturn_sha256_finish(&root_sha, digest)) return false;
    if (!digest_equal(digest, saved_hash)) return false;
    view->root_bytes = bytes; view->package_size = byte_count; view->level_id = read_u16(bytes+12U);
    view->area_id = read_u16(bytes+14U); view->section_count = (uint16_t)section_count; view->flags = read_u16(bytes+18U);
    memcpy(view->package_sha256, saved_hash, 32U); memcpy(view->dependency_set_sha256, bytes+52U, 32U);
    previous_end = table_end;
    for (index = 0U; index < section_count; index++) {
        const uint8_t *raw = bytes + SM64_SATURN_SCENE_PACKAGE_HEADER_SIZE + index*SM64_SATURN_SCENE_SECTION_DESCRIPTOR_SIZE;
        sm64_saturn_scene_section_view_t *section = &view->sections[index];
        uint32_t end, gap;
        section->kind=read_u16(raw); section->destination_class=raw[2]; section->lifetime=raw[3];
        section->offset=read_u32(raw+8U); section->byte_size=read_u32(raw+12U); section->alignment=read_u32(raw+16U);
        section->dependency_mask=read_u32(raw+20U); section->maximum_scratch=read_u32(raw+24U); memcpy(section->content_sha256,raw+28U,32U);
        if (section->kind != index+1U || read_u16(raw+4U)!=1U || read_u16(raw+6U)!=0U || read_u32(raw+60U)!=0U ||
            !valid_destination(section->destination_class) || !valid_lifetime(section->lifetime) || !power_of_two(section->alignment) ||
            section->offset < previous_end || section->offset > byte_count ||
            (section->offset & (section->alignment-1U)) != 0U ||
            section->byte_size > byte_count-section->offset) return false;
        end = section->offset + section->byte_size;
        for (gap = previous_end; gap < section->offset; gap++) if (bytes[gap] != 0U) return false;
        sm64_saturn_scene_package_sha256(bytes+section->offset,section->byte_size,digest);
        if (!digest_equal(digest,section->content_sha256)) return false;
        previous_end=end;
        if (section->kind >= SM64_SATURN_SCENE_ACTOR_DEPENDENCIES && section->kind <= SM64_SATURN_SCENE_AUDIO_DEPENDENCIES &&
            !parse_dependencies(bytes,section,view)) return false;
    }
    if (previous_end != byte_count || !section_graph_valid(view) ||
        !dependency_graph_valid(view) || !dependency_digest_valid(view)) return false;
    *caller_view = *view;
    return true;
}

bool sm64_saturn_scene_package_is_provisional(const sm64_saturn_scene_package_view_t *view)
{
    return view != NULL && (view->flags & SM64_SATURN_SCENE_PACKAGE_FLAG_PROVISIONAL) != 0U;
}

SM64_SATURN_CART_COLD
bool sm64_saturn_scene_package_validate_target(
    const void *bytes, uint32_t byte_count,
    sm64_saturn_scene_package_view_t *view)
{
    sm64_saturn_scene_package_view_t candidate;
    if (view != NULL) memset(view, 0, sizeof(*view));
    if (view == NULL || !sm64_saturn_scene_package_validate(bytes, byte_count, &candidate) ||
        sm64_saturn_scene_package_is_provisional(&candidate)) return false;
    *view = candidate;
    return true;
}

bool sm64_saturn_scene_package_identity_for_kind(
    const sm64_saturn_scene_package_view_t *view, uint16_t payload_kind,
    uint8_t digest[32])
{
    sm64_saturn_sha256_t sha;
    uint8_t generation[4];
    uint32_t index;
    if (view == NULL || digest == NULL || payload_kind < SM64_SATURN_SCENE_ACTOR_DEPENDENCIES ||
        payload_kind > SM64_SATURN_SCENE_AUDIO_DEPENDENCIES) return false;
    sm64_saturn_sha256_init(&sha);
    for (index = 0U; index < view->dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *item = &view->dependencies[index];
        if (item->payload_kind != payload_kind) continue;
        if (!sm64_saturn_sha256_update(&sha, item->stable_id, 32U) ||
            !sm64_saturn_sha256_update(&sha, item->content_sha256, 32U)) return false;
        write_u32(generation, item->generation);
        if (!sm64_saturn_sha256_update(&sha, generation, 4U)) return false;
    }
    return sm64_saturn_sha256_finish(&sha, digest);
}
