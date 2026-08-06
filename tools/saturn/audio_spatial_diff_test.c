#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "saturn_audio_spatial.h"

static void test_pointer_identity_tokens_are_bounded_stable_and_reused(void)
{
    sm64_saturn_audio_spatial_table_t table;
    float source_a[3] = {0.0f, 0.0f, 0.0f};
    float source_b[3] = {100.0f, 0.0f, 0.0f};
    uint16_t token_a;
    uint16_t token_b;

    sm64_saturn_audio_spatial_init(&table);
    token_a = sm64_saturn_audio_spatial_acquire(&table, source_a, 3U);
    assert(token_a != 0U);
    assert(sm64_saturn_audio_spatial_acquire(&table, source_a, 3U) == token_a);
    assert(sm64_saturn_audio_spatial_acquire(&table, source_a, 4U) == 0U);
    assert(sm64_saturn_audio_spatial_resolve(&table, token_a, 3U) == source_a);
    assert(sm64_saturn_audio_spatial_resolve(&table, token_a, 4U) == NULL);
    assert(sm64_saturn_audio_spatial_release(&table, source_a));
    token_b = sm64_saturn_audio_spatial_acquire(&table, source_b, 4U);
    assert(token_b != token_a);
    assert((token_b & 0x007FU) == (token_a & 0x007FU));
    assert(sm64_saturn_audio_spatial_generation(&table, token_b) == 2U);
    assert(sm64_saturn_audio_spatial_generation(&table, token_a) == 0U);
}

static void test_exhausted_generation_retires_slot_instead_of_aba_wrap(void)
{
    sm64_saturn_audio_spatial_table_t table;
    float source[3] = {0.0f, 0.0f, 0.0f};
    uint16_t first;
    uint16_t token;
    uint16_t generation;

    sm64_saturn_audio_spatial_init(&table);
    first = sm64_saturn_audio_spatial_acquire(&table, source, 1U);
    assert(first != 0U);
    assert(sm64_saturn_audio_spatial_release(&table, source));
    for (generation = 2U; generation <= 0x01FFU; ++generation) {
        token = sm64_saturn_audio_spatial_acquire(&table, source, 1U);
        assert((token & 0x007FU) == (first & 0x007FU));
        assert(sm64_saturn_audio_spatial_release(&table, source));
    }
    token = sm64_saturn_audio_spatial_acquire(&table, source, 1U);
    assert(token != 0U);
    assert((token & 0x007FU) != (first & 0x007FU));
    assert(sm64_saturn_audio_spatial_generation(&table, first) == 0U);
}

static void test_origin_and_moving_source_match_hand_derived_quantization(void)
{
    sm64_saturn_audio_spatial_params_t params;

    sm64_saturn_audio_spatial_quantize(0x00008001U, 0U, 0U, 15000U, 0U,
                                       0.0f, 0.0f, 0.0f, &params);
    assert(params.volume == 255U);
    assert(params.pan == 64U);
    assert(params.pitch == 4096U);
    assert(params.priority_score == 9652U);

    sm64_saturn_audio_spatial_quantize(0x00008001U, 0U, 0U, 15000U, 0U,
                                       22000.0f, 0.0f, 0.0f, &params);
    assert(params.volume == 26U);
    assert(params.pan == 106U);
    assert(params.pitch == 4369U);
    assert(params.priority_score == 31652U);

    sm64_saturn_audio_spatial_quantize(0x11008001U, 1U, 100U, 15000U, 0U,
                                       -22000.0f, 0.0f, 0.0f, &params);
    assert(params.volume == 255U);
    assert(params.pan == 21U);
    assert(params.pitch == 5393U);
    assert(params.priority_score == 31652U);

    sm64_saturn_audio_spatial_quantize(0x19008001U, 1U, 100U, 15000U, 0U,
                                       18000.0f, 0.0f, 0.0f, &params);
    assert(params.volume == 255U);
    assert(params.pan == 64U);
    assert(params.pitch == 4096U);
}

static void test_play_refresh_wire_words_are_pointer_free_and_complete(void)
{
    sm64_saturn_audio_play_refresh_t refresh = {
        0x8123ABCDU, 0x0042U, 0x0102U, 0xE7U, 0x19U, 0x1234U, 0x5678U
    };
    uint16_t words[7];

    memset(words, 0, sizeof(words));
    sm64_saturn_audio_spatial_encode_play_refresh(&refresh, words);
    assert(words[0] == 0x8123U);
    assert(words[1] == 0xABCDU);
    assert(words[2] == 0x0042U);
    assert(words[3] == 0x0102U);
    assert(words[4] == 0xE719U);
    assert(words[5] == 0x1234U);
    assert(words[6] == 0x5678U);
    assert(sizeof(refresh.source_token) == 2U);
}

int main(void)
{
    test_pointer_identity_tokens_are_bounded_stable_and_reused();
    test_exhausted_generation_retires_slot_instead_of_aba_wrap();
    test_origin_and_moving_source_match_hand_derived_quantization();
    test_play_refresh_wire_words_are_pointer_free_and_complete();
    return 0;
}
