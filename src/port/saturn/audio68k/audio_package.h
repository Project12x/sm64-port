#ifndef SM64_SATURN_AUDIO68K_PACKAGE_H
#define SM64_SATURN_AUDIO68K_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "../audio/saturn_audio_package.h"

/* MC68000 side observes only scalar package identities and bounded spans. */
typedef struct sm64_saturn_audio_package_token {
    uint32_t generation;
    uint32_t source_crc;
} sm64_saturn_audio_package_token_t;

bool sm64_saturn_audio68k_package_accept(
    const sm64_saturn_audio_package_view_t *package,
    const sm64_saturn_audio_residency_plan_t *plan,
    sm64_saturn_audio_package_token_t *token);

#endif
