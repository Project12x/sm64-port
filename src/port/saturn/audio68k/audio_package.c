#include "audio_package.h"

bool sm64_saturn_audio68k_package_accept(
    const sm64_saturn_audio_package_view_t *package,
    const sm64_saturn_audio_residency_plan_t *active,
    const sm64_saturn_audio_residency_plan_t *plan,
    sm64_saturn_audio_package_token_t *token)
{
    if (package == 0 || active == 0 || plan == 0 || token == 0 ||
        package->sequence_count != 35U || package->bank_count != 38U ||
        package->sample_count != 219U || plan->generation == 0U ||
        !sm64_saturn_audio_residency_validate_plan(plan) ||
        !sm64_saturn_audio_residency_plans_disjoint(active, plan) ||
        plan->total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT) {
        return false;
    }
    token->generation = plan->generation;
    for (uint32_t i = 0U; i < 32U; ++i) {
        token->source_sha256[i] = package->source_sha256[i];
    }
    return true;
}
