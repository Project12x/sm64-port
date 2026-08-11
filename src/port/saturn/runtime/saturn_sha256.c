#include "saturn_sha256.h"

#include <limits.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
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

static void sha256_block(sm64_saturn_sha256_t *state, const uint8_t block[64])
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
    a=state->state[0]; b=state->state[1]; c=state->state[2]; d=state->state[3];
    e=state->state[4]; f=state->state[5]; g=state->state[6]; h=state->state[7];
    for (index = 0U; index < 64U; index++) {
        uint32_t s1 = rotate_right(e,6U)^rotate_right(e,11U)^rotate_right(e,25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choice + constants[index] + schedule[index];
        uint32_t s0 = rotate_right(a,2U)^rotate_right(a,13U)^rotate_right(a,22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state->state[0]+=a; state->state[1]+=b; state->state[2]+=c; state->state[3]+=d;
    state->state[4]+=e; state->state[5]+=f; state->state[6]+=g; state->state[7]+=h;
}

void sm64_saturn_sha256_init(sm64_saturn_sha256_t *state)
{
    static const uint32_t initial[8] = {0x6A09E667U,0xBB67AE85U,0x3C6EF372U,0xA54FF53AU,
        0x510E527FU,0x9B05688CU,0x1F83D9ABU,0x5BE0CD19U};
    if (state == NULL) return;
    memcpy(state->state, initial, sizeof(initial));
    state->total_bytes = 0U;
    state->used = 0U;
}

bool sm64_saturn_sha256_update(sm64_saturn_sha256_t *state,
                               const void *source, uint32_t byte_count)
{
    const uint8_t *bytes = (const uint8_t *)source;
    if (state == NULL || (bytes == NULL && byte_count != 0U) ||
        (uint64_t)byte_count > UINT64_MAX - state->total_bytes)
        return false;
    state->total_bytes += byte_count;
    while (byte_count != 0U) {
        uint32_t take = 64U - state->used;
        if (take > byte_count) take = byte_count;
        memcpy(state->block + state->used, bytes, take);
        state->used += take; bytes += take; byte_count -= take;
        if (state->used == 64U) { sha256_block(state, state->block); state->used = 0U; }
    }
    return true;
}

bool sm64_saturn_sha256_finish(sm64_saturn_sha256_t *state, uint8_t digest[32])
{
    uint64_t bits;
    uint32_t index;
    if (state == NULL || digest == NULL || state->used >= 64U) return false;
    bits = state->total_bytes << 3U;
    state->block[state->used++] = 0x80U;
    if (state->used > 56U) {
        memset(state->block + state->used, 0, 64U - state->used);
        sha256_block(state, state->block); state->used = 0U;
    }
    memset(state->block + state->used, 0, 56U - state->used);
    for (index = 0U; index < 8U; index++) state->block[63U-index] = (uint8_t)(bits >> (index*8U));
    sha256_block(state, state->block);
    for (index = 0U; index < 8U; index++) write_u32(digest + index*4U, state->state[index]);
    return true;
}

bool sm64_saturn_sha256_digest(const void *bytes, uint32_t byte_count,
                               uint8_t digest[32])
{
    sm64_saturn_sha256_t state;
    if (digest == NULL || (bytes == NULL && byte_count != 0U)) return false;
    sm64_saturn_sha256_init(&state);
    return sm64_saturn_sha256_update(&state, bytes, byte_count) &&
           sm64_saturn_sha256_finish(&state, digest);
}
