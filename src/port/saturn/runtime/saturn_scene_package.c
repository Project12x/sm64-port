#include "saturn_scene_package.h"

#include <stddef.h>
#include <string.h>

typedef struct sha256_state {
    uint32_t word[8];
    uint64_t bit_count;
    uint8_t block[64];
    uint32_t used;
} sha256_state_t;

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

static uint32_t rotate_right(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32U - shift));
}

static void sha256_block(sha256_state_t *state, const uint8_t block[64])
{
    static const uint32_t constants[64] = {
        0x428A2F98U,0x71374491U,0xB5C0FBCFU,0xE9B5DBA5U,0x3956C25BU,0x59F111F1U,0x923F82A4U,0xAB1C5ED5U,
        0xD807AA98U,0x12835B01U,0x243185BEU,0x550C7DC3U,0x72BE5D74U,0x80DEB1FEU,0x9BDC06A7U,0xC19BF174U,
        0xE49B69C1U,0xEFBE4786U,0x0FC19DC6U,0x240CA1CCU,0x2DE92C6FU,0x4A7484AAU,0x5CB0A9DCU,0x76F988DAU,
        0x983E5152U,0xA831C66DU,0xB00327C8U,0xBF597FC7U,0xC6E00BF3U,0xD5A79147U,0x06CA6351U,0x14292967U,
        0x27B70A85U,0x2E1B2138U,0x4D2C6DFCU,0x53380D13U,0x650A7354U,0x766A0ABBU,0x81C2C92EU,0x92722C85U,
        0xA2BFE8A1U,0xA81A664BU,0xC24B8B70U,0xC76C51A3U,0xD192E819U,0xD6990624U,0xF40E3585U,0x106AA070U,
        0x19A4C116U,0x1E376C08U,0x2748774CU,0x34B0BCB5U,0x391C0CB3U,0x4ED8AA4AU,0x5B9CCA4FU,0x682E6FF3U,
        0x748F82EEU,0x78A5636FU,0x84C87814U,0x8CC70208U,0x90BEFFFAU,0xA4506CEBU,0xBEF9A3F7U,0xC67178F2U,
    };
    uint32_t schedule[64];
    uint32_t a, b, c, d, e, f, g, h, index;

    for (index = 0U; index < 16U; index++) schedule[index] = read_u32(block + index * 4U);
    for (; index < 64U; index++) {
        uint32_t x = schedule[index - 15U];
        uint32_t y = schedule[index - 2U];
        uint32_t s0 = rotate_right(x, 7U) ^ rotate_right(x, 18U) ^ (x >> 3U);
        uint32_t s1 = rotate_right(y, 17U) ^ rotate_right(y, 19U) ^ (y >> 10U);
        schedule[index] = schedule[index - 16U] + s0 + schedule[index - 7U] + s1;
    }
    a=state->word[0]; b=state->word[1]; c=state->word[2]; d=state->word[3];
    e=state->word[4]; f=state->word[5]; g=state->word[6]; h=state->word[7];
    for (index = 0U; index < 64U; index++) {
        uint32_t s1 = rotate_right(e,6U)^rotate_right(e,11U)^rotate_right(e,25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choice + constants[index] + schedule[index];
        uint32_t s0 = rotate_right(a,2U)^rotate_right(a,13U)^rotate_right(a,22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state->word[0]+=a; state->word[1]+=b; state->word[2]+=c; state->word[3]+=d;
    state->word[4]+=e; state->word[5]+=f; state->word[6]+=g; state->word[7]+=h;
}

static void sha256_init(sha256_state_t *state)
{
    static const uint32_t initial[8] = {0x6A09E667U,0xBB67AE85U,0x3C6EF372U,0xA54FF53AU,
        0x510E527FU,0x9B05688CU,0x1F83D9ABU,0x5BE0CD19U};
    memcpy(state->word, initial, sizeof(initial));
    state->bit_count = 0U;
    state->used = 0U;
}

static void sha256_update(sha256_state_t *state, const uint8_t *bytes, uint32_t count)
{
    state->bit_count += (uint64_t)count * 8U;
    while (count != 0U) {
        uint32_t take = 64U - state->used;
        if (take > count) take = count;
        memcpy(state->block + state->used, bytes, take);
        state->used += take; bytes += take; count -= take;
        if (state->used == 64U) { sha256_block(state, state->block); state->used = 0U; }
    }
}

static void sha256_finish(sha256_state_t *state, uint8_t digest[32])
{
    uint64_t bits = state->bit_count;
    uint32_t index;
    state->block[state->used++] = 0x80U;
    if (state->used > 56U) {
        memset(state->block + state->used, 0, 64U - state->used);
        sha256_block(state, state->block); state->used = 0U;
    }
    memset(state->block + state->used, 0, 56U - state->used);
    for (index = 0U; index < 8U; index++) state->block[63U-index] = (uint8_t)(bits >> (index*8U));
    sha256_block(state, state->block);
    for (index = 0U; index < 8U; index++) write_u32(digest + index*4U, state->word[index]);
}

void sm64_saturn_scene_package_sha256(const void *bytes, uint32_t byte_count,
                                      uint8_t digest[32])
{
    sha256_state_t state;
    sha256_init(&state);
    if (byte_count != 0U && bytes != NULL) sha256_update(&state, bytes, byte_count);
    sha256_finish(&state, digest);
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
    uint32_t index = 0U;
    while (index < 32U && id[index] != 0U) index++;
    if (index == 0U || index == 32U) return false;
    for (; index < 32U; index++) if (id[index] != 0U) return false;
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
            !stable_id_valid(dependency->stable_id) || dependency->generation == 0U ||
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
    sha256_state_t sha;
    uint8_t record[70], digest[32];
    uint32_t index;
    sha256_init(&sha); sha256_update(&sha, prefix, sizeof(prefix));
    for (index = 0U; index < view->dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *item = &view->dependencies[index];
        record[0] = (uint8_t)(item->payload_kind >> 8); record[1] = (uint8_t)item->payload_kind;
        memcpy(record + 2U, item->stable_id, 32U); write_u32(record + 34U, item->generation);
        memcpy(record + 38U, item->content_sha256, 32U); sha256_update(&sha, record, sizeof(record));
    }
    sha256_finish(&sha, digest);
    return digest_equal(digest, view->dependency_set_sha256);
}

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
    sha256_state_t root_sha;
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
    sha256_init(&root_sha); sha256_update(&root_sha, bytes, 20U); sha256_update(&root_sha, root_copy_hash_input, 32U);
    sha256_update(&root_sha, bytes+52U, byte_count-52U); sha256_finish(&root_sha, digest);
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
            section->offset < previous_end || (section->offset & (section->alignment-1U)) != 0U || section->byte_size > byte_count-section->offset) return false;
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

bool sm64_saturn_scene_package_identity_for_kind(
    const sm64_saturn_scene_package_view_t *view, uint16_t payload_kind,
    uint8_t digest[32])
{
    sha256_state_t sha;
    uint8_t generation[4];
    uint32_t index;
    if (view == NULL || digest == NULL || payload_kind < SM64_SATURN_SCENE_ACTOR_DEPENDENCIES ||
        payload_kind > SM64_SATURN_SCENE_AUDIO_DEPENDENCIES) return false;
    sha256_init(&sha);
    for (index = 0U; index < view->dependency_count; index++) {
        const sm64_saturn_scene_dependency_view_t *item = &view->dependencies[index];
        if (item->payload_kind != payload_kind) continue;
        sha256_update(&sha, item->stable_id, 32U);
        sha256_update(&sha, item->content_sha256, 32U);
        write_u32(generation, item->generation);
        sha256_update(&sha, generation, 4U);
    }
    sha256_finish(&sha, digest);
    return true;
}
