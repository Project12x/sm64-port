#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../src/port/saturn/audio/saturn_audio_package.h"
#include "../../src/port/saturn/audio68k/audio_package.h"

int main(int argc, char **argv)
{
    uint8_t raw[2200];
    sm64_saturn_audio_residency_plan_t active = {
        .generation = 1U, .driver_offset = 0U, .driver_size = 8192U,
        .mailbox_offset = 8192U, .mailbox_size = 0U,
        .sample_offset = 16384U, .sample_size = 32768U,
        .scratch_offset = 49152U, .scratch_size = 0U,
        .total_bytes = 65536U, .active_generation_retained = true,
        .post_boot_clear_rejected = true};
    sm64_saturn_audio_residency_plan_t replacement;
    sm64_saturn_audio_package_view_t package;
    sm64_saturn_audio_package_token_t token;
    memset(raw, 0, sizeof(raw));
    raw[0] = 'S'; raw[1] = '6'; raw[2] = '4'; raw[3] = 'A';
    raw[7] = 96U;
    raw[10] = 8U; raw[11] = (uint8_t)(sizeof(raw) & 0xFFU);
    raw[15] = 35U; raw[19] = 38U; raw[23] = 219U; raw[27] = 1U;
    raw[102] = 8U; raw[103] = 0U;
    raw[107] = 4U;
    raw[110] = 8U; raw[111] = 0U;
    raw[2048] = 0x12U;
    assert(!sm64_saturn_audio_package_validate_header(raw, sizeof(raw), &package));
    if (argc > 1) {
        FILE *file = fopen(argv[1], "rb");
        long size;
        uint8_t *bytes;
        assert(file != NULL);
        assert(fseek(file, 0L, SEEK_END) == 0);
        size = ftell(file); assert(size > 0L);
        assert(fseek(file, 0L, SEEK_SET) == 0);
        bytes = (uint8_t *)malloc((size_t)size); assert(bytes != NULL);
        assert(fread(bytes, 1U, (size_t)size, file) == (size_t)size);
        fclose(file);
        assert(sm64_saturn_audio_package_validate_header(bytes, (uint32_t)size, &package));
        /* A descriptor alias must fail independently of package provenance. */
        memcpy(bytes + 96U + 56U + 4U, bytes + 96U + 4U, 4U);
        assert(!sm64_saturn_audio_package_validate_header(bytes, (uint32_t)size, &package));
        free(bytes);
    }
    memset(&package, 0, sizeof(package));
    package.sequence_count = 35U;
    package.bank_count = 38U;
    package.sample_count = 219U;
    assert(sm64_saturn_audio_residency_prepare(&active, 2U, 8192U, 4096U,
                                                256U * 1024U, 0U, &replacement));
    assert(replacement.active_generation_retained);
    assert(!sm64_saturn_audio_residency_commit(&active, &replacement, 1U));
    assert(sm64_saturn_audio_residency_commit(&active, &replacement, 2U));
    assert(!sm64_saturn_audio_residency_clear_sound_ram(&active, true));
    assert(!sm64_saturn_audio_residency_prepare(&active, 1U, 1U, 1U, 1U, 0U,
                                                 &replacement));
    assert(!sm64_saturn_audio_residency_prepare(&active, 3U, 400000U, 400000U,
                                                 400000U, 400000U, &replacement));
    assert(sm64_saturn_audio68k_package_accept(&package, &replacement, &token));
    return 0;
}
