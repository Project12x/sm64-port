#include <assert.h>
#include <string.h>

#include "../../src/port/saturn/audio/saturn_audio_package.h"
#include "../../src/port/saturn/audio68k/audio_package.h"

int main(void)
{
    uint8_t raw[2200];
    sm64_saturn_audio_residency_plan_t active = {1U, 0U, 8192U, 8192U, 0U,
                                                  16384U, 32768U, 65536U, true, true};
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
    assert(sm64_saturn_audio_package_validate_header(raw, sizeof(raw), &package));
    memset(&package, 0, sizeof(package));
    package.sequence_count = 35U;
    package.bank_count = 38U;
    package.sample_count = 219U;
    assert(sm64_saturn_audio_residency_prepare(&active, 2U, 8192U, 4096U,
                                                256U * 1024U, &replacement));
    assert(replacement.active_generation_retained);
    assert(!sm64_saturn_audio_residency_commit(&active, &replacement, 1U));
    assert(sm64_saturn_audio_residency_commit(&active, &replacement, 2U));
    assert(!sm64_saturn_audio_residency_clear_sound_ram(&active, true));
    assert(!sm64_saturn_audio_residency_prepare(&active, 1U, 1U, 1U, 1U,
                                                 &replacement));
    assert(!sm64_saturn_audio_residency_prepare(&active, 3U, 400000U, 400000U,
                                                 400000U, &replacement));
    assert(sm64_saturn_audio68k_package_accept(&package, &replacement, &token));
    return 0;
}
