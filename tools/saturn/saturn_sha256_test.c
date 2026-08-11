#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_sha256.h"

static void test_known_vectors_and_block_boundary(void)
{
    static const uint8_t empty_sha256[32] = {
        0xe3U, 0xb0U, 0xc4U, 0x42U, 0x98U, 0xfcU, 0x1cU, 0x14U,
        0x9aU, 0xfbU, 0xf4U, 0xc8U, 0x99U, 0x6fU, 0xb9U, 0x24U,
        0x27U, 0xaeU, 0x41U, 0xe4U, 0x64U, 0x9bU, 0x93U, 0x4cU,
        0xa4U, 0x95U, 0x99U, 0x1bU, 0x78U, 0x52U, 0xb8U, 0x55U,
    };
    static const uint8_t abc_sha256[32] = {
        0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
        0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x22U, 0x23U,
        0xb0U, 0x03U, 0x61U, 0xa3U, 0x96U, 0x17U, 0x7aU, 0x9cU,
        0xb4U, 0x10U, 0xffU, 0x61U, 0xf2U, 0x00U, 0x15U, 0xadU,
    };
    static const uint8_t sixty_four_a_sha256[32] = {
        0xffU, 0xe0U, 0x54U, 0xfeU, 0x7aU, 0xe0U, 0xcbU, 0x6dU,
        0xc6U, 0x5cU, 0x3aU, 0xf9U, 0xb6U, 0x1dU, 0x52U, 0x09U,
        0xf4U, 0x39U, 0x85U, 0x1dU, 0xb4U, 0x3dU, 0x0bU, 0xa5U,
        0x99U, 0x73U, 0x37U, 0xdfU, 0x15U, 0x46U, 0x68U, 0xebU,
    };
    uint8_t block[64];
    uint8_t digest[32];

    memset(block, 'a', sizeof(block));
    assert(sm64_saturn_sha256_digest(NULL, 0U, digest));
    assert(memcmp(digest, empty_sha256, sizeof(digest)) == 0);
    assert(sm64_saturn_sha256_digest("abc", 3U, digest));
    assert(memcmp(digest, abc_sha256, sizeof(digest)) == 0);
    assert(sm64_saturn_sha256_digest(block, sizeof(block), digest));
    assert(memcmp(digest, sixty_four_a_sha256, sizeof(digest)) == 0);
}

static void test_segmented_updates_match_one_shot(void)
{
    sm64_saturn_sha256_t state;
    uint8_t digest[32];
    uint8_t segmented[32];

    assert(sm64_saturn_sha256_digest("abc", 3U, digest));
    sm64_saturn_sha256_init(&state);
    assert(sm64_saturn_sha256_update(&state, "a", 1U));
    assert(sm64_saturn_sha256_update(&state, "bc", 2U));
    assert(sm64_saturn_sha256_finish(&state, segmented));
    assert(memcmp(digest, segmented, sizeof(digest)) == 0);
}

static void test_rejects_invalid_or_overflowed_input(void)
{
    sm64_saturn_sha256_t state;
    uint8_t byte = 0U;
    uint8_t digest[32];
    const uint64_t before = UINT64_MAX - (uint64_t)UINT32_MAX + 1U;

    assert(!sm64_saturn_sha256_digest(NULL, 1U, digest));
    sm64_saturn_sha256_init(&state);
    state.total_bytes = before;
    assert(!sm64_saturn_sha256_update(&state, &byte, UINT32_MAX));
    assert(state.total_bytes == before);
}

int main(void)
{
    test_known_vectors_and_block_boundary();
    test_segmented_updates_match_one_shot();
    test_rejects_invalid_or_overflowed_input();
    return 0;
}
