#include "saturn_actor_bundle_runtime.h"

#include <limits.h>
#include <string.h>

static inline void actor_bundle_fence(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" ::: "memory");
#endif
}

static bool generation_is_newer(uint32_t candidate, uint32_t current)
{
    const uint32_t distance = candidate - current;
    return candidate != 0U && current != 0U && distance != 0U &&
           distance < UINT32_C(0x80000000);
}

static bool words_equal(const uint32_t left[8], const uint32_t right[8])
{
    uint32_t different = 0U;
    for (uint16_t word = 0U; word < 8U; word++)
        different |= left[word] ^ right[word];
    return different == 0U;
}

static bool publication_valid(
    const sm64_saturn_actor_bundle_publication_t *publication)
{
    uint32_t expected_scratch;
    if (publication == NULL || publication->committed != 1U ||
        publication->reserved != 0U ||
        publication->residency_generation == 0U ||
        publication->package_generation == 0U ||
        publication->byte_count < SM64_SATURN_ACTOR_BUNDLE_HEADER_SIZE ||
        publication->family_count == 0U ||
        publication->family_count > SM64_SATURN_ACTOR_BUNDLE_MAX_FAMILIES ||
        publication->variant_count == 0U ||
        publication->variant_count > SM64_SATURN_ACTOR_BUNDLE_MAX_VARIANTS ||
        publication->workspace_lane_stride == 0U ||
        (publication->workspace_lane_stride &
         (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U)) != 0U ||
        publication->workspace_lane_stride > (UINT32_MAX -
            (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U)) /
                SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT ||
        publication->cart_offset > UINT32_MAX - publication->byte_count ||
        (publication->cart_offset & 3U) != 0U ||
        publication->lane_claim[0] > 1U ||
        publication->lane_claim[1] > 1U)
        return false;
    expected_scratch = publication->workspace_lane_stride *
        SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT +
        (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U);
    return publication->maximum_scratch == expected_scratch;
}

static bool bundle_view_equal(const sm64_saturn_actor_bundle_view_t *left,
                              const sm64_saturn_actor_bundle_view_t *right)
{
    return left != NULL && right != NULL && left->bytes == right->bytes &&
        left->byte_count == right->byte_count &&
        left->family_records_offset == right->family_records_offset &&
        left->variant_records_offset == right->variant_records_offset &&
        left->metadata_offset == right->metadata_offset &&
        left->metadata_size == right->metadata_size &&
        left->bank_payloads_offset == right->bank_payloads_offset &&
        left->bank_payloads_size == right->bank_payloads_size &&
        left->workspace_lane_stride == right->workspace_lane_stride &&
        left->maximum_scratch == right->maximum_scratch &&
        left->package_generation == right->package_generation &&
        left->family_count == right->family_count &&
        left->variant_count == right->variant_count &&
        words_equal(left->content_hash_words, right->content_hash_words);
}

static bool publication_matches_bundle(
    const sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle)
{
    return publication_valid(publication) && bundle != NULL &&
        bundle->bytes != NULL && publication->byte_count == bundle->byte_count &&
        publication->package_generation == bundle->package_generation &&
        publication->family_count == bundle->family_count &&
        publication->variant_count == bundle->variant_count &&
        publication->workspace_lane_stride == bundle->workspace_lane_stride &&
        publication->maximum_scratch == bundle->maximum_scratch &&
        words_equal(publication->content_hash_words,
                    bundle->content_hash_words);
}

bool sm64_saturn_actor_bundle_runtime_publish(
    sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    uint32_t residency_generation, uint32_t cart_offset)
{
    sm64_saturn_actor_bundle_view_t validated;
    if (publication == NULL || bundle == NULL || bundle->bytes == NULL ||
        residency_generation == 0U || (cart_offset & 3U) != 0U ||
        cart_offset > UINT32_MAX - bundle->byte_count ||
        !sm64_saturn_actor_bundle_validate(
            bundle->bytes, bundle->byte_count, &validated) ||
        !bundle_view_equal(bundle, &validated))
        return false;
    if (publication->committed == 1U) {
        if (!publication_valid(publication)) {
            memset(publication, 0, sizeof(*publication));
            return false;
        }
        if (publication->lane_claim[0] != 0U ||
            publication->lane_claim[1] != 0U ||
            !generation_is_newer(residency_generation,
                                 publication->residency_generation))
            return false;
    }
    memset(publication, 0, sizeof(*publication));
    publication->residency_generation = residency_generation;
    publication->package_generation = validated.package_generation;
    publication->cart_offset = cart_offset;
    publication->byte_count = validated.byte_count;
    memcpy(publication->content_hash_words, validated.content_hash_words,
           sizeof(publication->content_hash_words));
    publication->workspace_lane_stride = validated.workspace_lane_stride;
    publication->maximum_scratch = validated.maximum_scratch;
    publication->family_count = validated.family_count;
    publication->variant_count = validated.variant_count;
    actor_bundle_fence();
    publication->committed = 1U;
    actor_bundle_fence();
    return true;
}

bool sm64_saturn_actor_bundle_runtime_claim(
    sm64_saturn_actor_bundle_publication_t *publication,
    uint32_t residency_generation, uint8_t lane)
{
    if (!publication_valid(publication) ||
        residency_generation != publication->residency_generation ||
        lane >= SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT ||
        publication->lane_claim[lane] != 0U)
        return false;
    publication->lane_claim[lane] = 1U;
    actor_bundle_fence();
    return true;
}

bool sm64_saturn_actor_bundle_runtime_resolve(
    const sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    void *workspace, uint32_t workspace_capacity,
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_bundle_resolution_t *output)
{
    if (output != NULL) memset(output, 0, sizeof(*output));
    if (output == NULL || snapshot == NULL || records == NULL ||
        !publication_matches_bundle(publication, bundle) ||
        snapshot->scene_package_generation !=
            publication->residency_generation ||
        snapshot->active == 0U || snapshot->render_active == 0U ||
        lane >= SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT ||
        publication->lane_claim[lane] != 1U ||
        workspace_capacity < publication->maximum_scratch ||
        !sm64_saturn_actor_bundle_variant(
            bundle, snapshot->family_id, snapshot->model_id,
            &output->variant) ||
        !sm64_saturn_actor_bundle_resolve(
            bundle, snapshot->family_id, snapshot->model_id,
            snapshot->actor_bank_id, snapshot->actor_bank_hash_words,
            &output->bank) ||
        !sm64_saturn_actor_meshlets_bind_bundle_workspace(
            &output->bank, workspace, workspace_capacity,
            publication->workspace_lane_stride, lane, records, draw_capacity,
            &output->workspace)) {
        memset(output, 0, sizeof(*output));
        return false;
    }
    output->residency_generation = publication->residency_generation;
    output->package_generation = publication->package_generation;
    output->lane = lane;
    return true;
}

bool sm64_saturn_actor_bundle_runtime_release(
    sm64_saturn_actor_bundle_publication_t *publication,
    uint32_t residency_generation, uint8_t lane)
{
    if (!publication_valid(publication) ||
        residency_generation != publication->residency_generation ||
        lane >= SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT ||
        publication->lane_claim[lane] != 1U)
        return false;
    actor_bundle_fence();
    publication->lane_claim[lane] = 0U;
    actor_bundle_fence();
    return true;
}
