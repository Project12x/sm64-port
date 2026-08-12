#include "saturn_actor_bundle.h"
#include "saturn_sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned mutation_count;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }

static void reseal(uint8_t *bytes, uint32_t size)
{
    uint8_t digest[32];
    sm64_saturn_sha256_t sha;
    static const uint8_t zero[32] = {0};
    sm64_saturn_sha256_init(&sha);
    assert(sm64_saturn_sha256_update(&sha, bytes, 64U));
    assert(sm64_saturn_sha256_update(&sha, zero, 32U));
    assert(sm64_saturn_sha256_update(&sha, bytes + 96U, size - 96U));
    assert(sm64_saturn_sha256_finish(&sha, digest));
    memcpy(bytes + 64U, digest, 32U);
}

static void assert_zero(const void *value, size_t size)
{
    const uint8_t *p = value;
    for (size_t i = 0; i < size; i++) assert(p[i] == 0U);
}

static void reject(uint8_t *copy, uint32_t size)
{
    sm64_saturn_actor_bundle_view_t view;
    mutation_count++;
    memset(&view, 0xA5, sizeof(view));
    assert(!sm64_saturn_actor_bundle_validate(copy, size, &view));
    assert_zero(&view, sizeof(view));
}

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    uint8_t *valid, *copy;
    sm64_saturn_actor_bundle_view_t view;
    sm64_saturn_actor_bundle_variant_t variant;
    sm64_saturn_actor_bank_view_t bank;
    assert(argc == 2 || argc == 3);
    file = fopen(argv[1], "rb"); assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0); length = ftell(file); assert(length > 0);
    rewind(file); valid = malloc((size_t)length); copy = malloc((size_t)length);
    assert(valid != NULL && copy != NULL && fread(valid, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    memset(&view, 0, sizeof(view));
    assert(sm64_saturn_actor_bundle_validate(valid, (uint32_t)length, &view));
    assert(view.family_count == 2U && view.variant_count == 3U);
    memset(&variant, 0, sizeof(variant));
    assert(sm64_saturn_actor_bundle_variant(&view, 1U, 9U, &variant));
    memset(&bank, 0, sizeof(bank));
    assert(sm64_saturn_actor_bundle_resolve(&view, 1U, 9U,
        variant.source_hash_words[0], variant.source_hash_words, &bank));
    assert(bank.bank.family_id == 1U && bank.bank.model_id == 9U);
    memset(&bank, 0xA5, sizeof(bank));
    assert(!sm64_saturn_actor_bundle_resolve(&view, 1U, 9U,
        variant.source_hash_words[0] ^ 1U, variant.source_hash_words, &bank));
    assert_zero(&bank, sizeof(bank));
    {
        uint32_t wrong_hash[8];
        memcpy(wrong_hash, variant.source_hash_words, sizeof(wrong_hash));
        wrong_hash[7] ^= 1U;
        memset(&bank, 0xA5, sizeof(bank));
        assert(!sm64_saturn_actor_bundle_resolve(&view, 1U, 9U,
            variant.source_hash_words[0], wrong_hash, &bank));
        assert_zero(&bank, sizeof(bank));
    }
    {
        sm64_saturn_actor_bundle_view_t short_view = view;
        short_view.bank_payloads_size = variant.bank_offset + variant.bank_size - 1U;
        sm64_saturn_actor_bundle_variant_t short_variant;
        memset(&short_variant, 0xA5, sizeof(short_variant));
        assert(!sm64_saturn_actor_bundle_variant(&short_view, 1U, 9U,
                                                  &short_variant));
        assert_zero(&short_variant, sizeof(short_variant));
        memset(&bank, 0xA5, sizeof(bank));
        assert(!sm64_saturn_actor_bundle_resolve(&short_view, 1U, 9U,
            variant.source_hash_words[0], variant.source_hash_words, &bank));
        assert_zero(&bank, sizeof(bank));
    }

    { const uint32_t fields[][2] = {
        {0U, 1U}, {4U, 1U}, {6U, 1U}, {8U, 1U}, {10U, 1U}, {12U, 1U},
        {14U, 1U}, {16U, 4U}, {20U, 4U}, {24U, 4U}, {28U, 4U},
        {32U, 4U}, {36U, 4U}, {40U, 4U}, {44U, 4U}, {48U, 4U},
        {52U, 4U}, {56U, 4U}, {60U, 4U}, {64U, 1U}};
      for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); i++) {
        memcpy(copy, valid, (size_t)length);
        if (fields[i][0] == 48U) put32(copy + 48U, 0U);
        else copy[fields[i][0]] ^= 1U;
        if (fields[i][0] != 64U) reseal(copy, (uint32_t)length);
        reject(copy, (uint32_t)length);
      }
    }
    {
        uint32_t family = be32(valid + 16U), variants = be32(valid + 20U);
        const uint32_t family_fields[] = {0U,4U,8U,12U,16U,20U,24U,28U,32U,36U,40U,44U,48U,52U,56U,58U,60U};
        const uint32_t variant_fields[] = {0U,2U,4U,8U,12U,16U,20U,24U,56U};
        for (size_t i=0;i<sizeof(family_fields)/sizeof(family_fields[0]);i++) {
            uint32_t field = family_fields[i];
            memcpy(copy,valid,(size_t)length);
            if (field == 0U || field == 12U || field == 16U) put32(copy + family + field, 0U);
            else if (field == 4U || field == 8U) put32(copy + family + field, 0x80000000U);
            else copy[family+field]^=1U;
            reseal(copy,(uint32_t)length); reject(copy,(uint32_t)length);
        }
        for (size_t i=0;i<sizeof(variant_fields)/sizeof(variant_fields[0]);i++) {
            memcpy(copy,valid,(size_t)length); copy[variants+variant_fields[i]]^=1U; reseal(copy,(uint32_t)length); reject(copy,(uint32_t)length);
        }
        {
            uint32_t bank_root=be32(valid+32U), rel=be32(valid+variants+8U), bank_size=be32(valid+variants+12U), bank_at=bank_root+rel;
            const uint32_t bank_fields[] = {6U,8U,26U,98U,
                                            be32(valid+bank_at+62U)+8U};
            for (size_t i=0;i<sizeof(bank_fields)/sizeof(bank_fields[0]);i++) {
                uint8_t digest[32]; memcpy(copy,valid,(size_t)length);
                if (i == 4U) { copy[bank_at+bank_fields[i]]=0U; copy[bank_at+bank_fields[i]+1U]=0U; }
                else copy[bank_at+bank_fields[i]]^=1U;
                assert(sm64_saturn_sha256_digest(copy+bank_at,bank_size,digest)); memcpy(copy+variants+24U,digest,32U);
                reseal(copy,(uint32_t)length); reject(copy,(uint32_t)length);
            }
            memcpy(copy,valid,(size_t)length); copy[bank_at+be32(copy+bank_at+90U)]^=1U;
            { uint8_t digest[32]; assert(sm64_saturn_sha256_digest(copy+bank_at,bank_size,digest)); memcpy(copy+variants+24U,digest,32U); }
            reseal(copy,(uint32_t)length); reject(copy,(uint32_t)length);
        }
    }
    memcpy(copy, valid, (size_t)length); put32(copy + 16U, 0xFFFFFFFCU); reseal(copy,(uint32_t)length); reject(copy,(uint32_t)length);
    memset(&variant, 0xA5, sizeof(variant)); assert(!sm64_saturn_actor_bundle_variant(&view, 0U, 1U, &variant)); assert_zero(&variant,sizeof(variant));
    memset(&variant, 0xA5, sizeof(variant)); assert(!sm64_saturn_actor_bundle_variant(&view, 2U, 99U, &variant)); assert_zero(&variant,sizeof(variant));
    free(copy); free(valid);
    if (argc == 3) {
        uint32_t mixed_size;
        uint8_t *mixed = NULL;
        file = fopen(argv[2], "rb"); assert(file != NULL);
        assert(fseek(file, 0, SEEK_END) == 0); length = ftell(file); assert(length > 0);
        rewind(file); mixed = malloc((size_t)length); assert(mixed != NULL);
        assert(fread(mixed, 1, (size_t)length, file) == (size_t)length);
        fclose(file); mixed_size = (uint32_t)length;
        assert(sm64_saturn_actor_bundle_validate(mixed, mixed_size, &view));
        assert(sm64_saturn_actor_bundle_variant(&view, 2U, 3U, &variant));
        assert(sm64_saturn_actor_bundle_resolve(&view, 2U, 3U,
            variant.source_hash_words[0], variant.source_hash_words, &bank));
        assert(bank.bank.version == SM64_SATURN_ACTOR_BANK_VERSION_V2);
        assert(bank.texture_payload_size == 16U && bank.clut_payload_size == 32U);
        {
            uint8_t digest[32];
            uint32_t bank_at = view.bank_payloads_offset + variant.bank_offset;
            uint32_t variant_at = view.variant_records_offset + 88U;
            uint8_t *mixed_copy = malloc(mixed_size);
            assert(mixed_copy != NULL);
            memcpy(mixed_copy, mixed, mixed_size);
            mixed_copy[bank_at + 180U] = 1U;
            assert(sm64_saturn_sha256_digest(mixed_copy + bank_at,
                                              variant.bank_size, digest));
            memcpy(mixed_copy + variant_at + 24U, digest, 32U);
            reseal(mixed_copy, mixed_size);
            reject(mixed_copy, mixed_size);
            free(mixed_copy);
        }
        free(mixed);
    }
    assert(mutation_count == (argc == 3 ? 54U : 53U));
    printf("actor family bundle: PASS (%u mutations)\n", mutation_count);
    return 0;
}
