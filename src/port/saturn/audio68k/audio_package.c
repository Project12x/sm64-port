#include "audio_package.h"

bool sm64_saturn_audio68k_package_accept(
    const sm64_saturn_audio_package_view_t *package,
    const sm64_saturn_audio_residency_plan_t *plan,
    sm64_saturn_audio_package_token_t *token)
{
    if (package == 0 || plan == 0 || token == 0 ||
        package->sequence_count != 35U || package->bank_count != 38U ||
        package->sample_count != 219U || plan->generation == 0U ||
        !plan->active_generation_retained || !plan->post_boot_clear_rejected ||
        plan->total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT) {
        return false;
    }
    token->generation = plan->generation;
    token->source_crc = ((uint32_t)package->source_sha256[0] << 24) |
                        ((uint32_t)package->source_sha256[1] << 16) |
                        ((uint32_t)package->source_sha256[2] << 8) |
                        (uint32_t)package->source_sha256[3];
    return true;
}
