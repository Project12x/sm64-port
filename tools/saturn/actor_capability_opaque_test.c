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

typedef struct test_sha256 {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    uint8_t block_size;
} test_sha256_t;

static uint32_t test_rotr(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32U - amount));
}

static void test_sha256_transform(test_sha256_t *context)
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
    uint32_t words[64], a, b, c, d, e, f, g, h, index;
    for (index = 0U; index < 16U; index++) {
        const uint8_t *word = context->block + index * 4U;
        words[index] = ((uint32_t)word[0] << 24) | ((uint32_t)word[1] << 16) |
                       ((uint32_t)word[2] << 8) | word[3];
    }
    for (; index < 64U; index++) {
        uint32_t x = words[index - 15U], y = words[index - 2U];
        words[index] = (test_rotr(x, 7U) ^ test_rotr(x, 18U) ^ (x >> 3)) +
                       words[index - 16U] +
                       (test_rotr(y, 17U) ^ test_rotr(y, 19U) ^ (y >> 10)) +
                       words[index - 7U];
    }
    a = context->state[0]; b = context->state[1]; c = context->state[2];
    d = context->state[3]; e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (index = 0U; index < 64U; index++) {
        uint32_t sigma1 = test_rotr(e, 6U) ^ test_rotr(e, 11U) ^ test_rotr(e, 25U);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t temporary1 = h + sigma1 + choose + constants[index] + words[index];
        uint32_t sigma0 = test_rotr(a, 2U) ^ test_rotr(a, 13U) ^ test_rotr(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = sigma0 + majority;
        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c;
    context->state[3] += d; context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

static void test_sha256_init(test_sha256_t *context)
{
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    memcpy(context->state, initial, sizeof(initial));
    context->bit_count = 0U;
    context->block_size = 0U;
}

static void test_sha256_update(test_sha256_t *context,
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
            test_sha256_transform(context);
            context->block_size = 0U;
        }
    }
}

static void test_sha256_final(test_sha256_t *context, uint8_t digest[32])
{
    uint8_t length[8];
    uint64_t bit_count = context->bit_count;
    uint32_t index;
    for (index = 0U; index < 8U; index++)
        length[7U - index] = (uint8_t)(bit_count >> (index * 8U));
    test_sha256_update(context, (const uint8_t *)"\x80", 1U);
    while (context->block_size != 56U) {
        uint8_t zero = 0U;
        test_sha256_update(context, &zero, 1U);
    }
    test_sha256_update(context, length, sizeof(length));
    for (index = 0U; index < 8U; index++) {
        digest[index * 4U] = (uint8_t)(context->state[index] >> 24);
        digest[index * 4U + 1U] = (uint8_t)(context->state[index] >> 16);
        digest[index * 4U + 2U] = (uint8_t)(context->state[index] >> 8);
        digest[index * 4U + 3U] = (uint8_t)context->state[index];
    }
}

static void reseal_payload(unsigned char *bytes, size_t size)
{
    static const uint8_t zero_digest[32] = {0};
    uint8_t digest[32];
    test_sha256_t context;
    test_sha256_init(&context);
    test_sha256_update(&context, bytes, 24U);
    test_sha256_update(&context, zero_digest, sizeof(zero_digest));
    test_sha256_update(&context, bytes + 56U, size - 56U);
    test_sha256_final(&context, digest);
    memcpy(bytes + 24U, digest, sizeof(digest));
}

/* Header content digest for the BOB Area 1 S64F-v2 family bank regenerated
 * from the reviewed PNG-attested closure at f5a03808. */
static const uint32_t TRUSTED_BOB_V2_HASH[8] = {
    0x60c329abU, 0x3e8bcd8bU, 0xc7d13869U, 0xc123706aU,
    0xf5517f5bU, 0xd7efd3b3U, 0xa539b702U, 0x12e28b73U,
};

/* Header content digest from the pre-change 52-byte-record S64F v1 bank. */
static const uint32_t STALE_BOB_V1_HASH[8] = {
    0xb20d271aU, 0xe5a8808fU, 0xa4b35aacU, 0x1dcb374bU,
    0xdccd43c4U, 0xddf704dcU, 0xe1a5173cU, 0x12a5e697U,
};

int main(int argc, char **argv)
{
    unsigned char *bytes, *copy;
    size_t size;
    sm64_saturn_actor_family_bank_view_t view;
    sm64_saturn_actor_family_record_t record;
    unsigned int index;
    unsigned int geometry_count = 0U;

    if (argc != 2 || !read_file(argv[1], &bytes, &size) ||
        !sm64_saturn_actor_family_bank_validate(bytes, size, &view)) return 1;
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
    if (!sm64_saturn_actor_family_bank_validate_expected(
            bytes, size, TRUSTED_BOB_V2_HASH, &view) ||
        sm64_saturn_actor_family_bank_validate_expected(
            bytes, size, STALE_BOB_V1_HASH, &view))
        return 5;
    copy = (unsigned char *)malloc(size);
    if (copy == NULL) return 6;
    memcpy(copy, bytes, size);
    copy[view.records_offset + 52U] |= 0x80U;
    reseal_payload(copy, size);
    if (sm64_saturn_actor_family_bank_validate(copy, size, &view)) return 7;
    free(copy);
    free(bytes);
    puts("actor capability opaque: PASS");
    return 0;
}
