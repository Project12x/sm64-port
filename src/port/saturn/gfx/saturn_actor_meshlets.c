#include "saturn_actor_meshlets.h"

#include <limits.h>
#include <string.h>

#include "saturn_matrix_kernels.h"
#include "saturn_mario_actor_mesh.h"

#define SM64_SATURN_ACTOR_DEPTH_BIN_COUNT 64U
#define SM64_SATURN_ACTOR_DEPTH_BIN_SHIFT 7U
#define ACTOR_GEOMETRY_HEADER_SIZE 46U
#define ACTOR_MESHLET_RECORD_SIZE 66U

typedef enum actor_meshlet_source_kind {
    ACTOR_MESHLET_SOURCE_MARIO = 0U,
    ACTOR_MESHLET_SOURCE_BANK = 1U,
} actor_meshlet_source_kind_t;

typedef struct actor_meshlet_source {
    actor_meshlet_source_kind_t kind;
    const uint8_t *geometry;
    uint32_t meshlet_offset;
    uint32_t primitive_ref_offset;
    uint32_t position_ref_offset;
    uint32_t primitive_ref_count;
    uint32_t position_ref_count;
    uint16_t meshlet_count;
    uint16_t primitive_count;
    uint16_t vertex_count;
} actor_meshlet_source_t;

typedef struct actor_meshlet_span {
    uint32_t primitive_offset;
    uint32_t primitive_count;
    uint32_t position_offset;
    uint32_t position_count;
    uint8_t opacity;
} actor_meshlet_span_t;

typedef struct actor_meshlet_transform {
    const int16_t (*vertices)[3];
    uint16_t vertex_count;
    int64_t position_q16[3];
    int32_t scale_q16[3];
    int16_t yaw;
} actor_meshlet_transform_t;

typedef struct actor_meshlet_depth_bounds {
    int32_t nearest_q16;
    int32_t furthest_q16;
} actor_meshlet_depth_bounds_t;

static uint16_t actor_read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t actor_read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool actor_relative_span(uint32_t offset, uint32_t size,
                                uint32_t byte_count)
{
    return offset <= byte_count && size <= byte_count - offset;
}

static uint8_t actor_depth_bin(int32_t depth_q16)
{
    if (depth_q16 <= 0) return 0U;
    const uint32_t depth = (uint32_t)depth_q16 >> 16;
    const uint32_t bin = depth >> SM64_SATURN_ACTOR_DEPTH_BIN_SHIFT;
    return (uint8_t)(bin >= SM64_SATURN_ACTOR_DEPTH_BIN_COUNT
        ? SM64_SATURN_ACTOR_DEPTH_BIN_COUNT - 1U : bin);
}

static uint8_t actor_lod_tier(int32_t depth_q16)
{
    const uint32_t depth = depth_q16 > 0 ? (uint32_t)depth_q16 >> 16 : 0U;
    if (depth >= 4096U) return 2U;
    if (depth >= 2048U) return 1U;
    return 0U;
}

static int64_t actor_saturating_add_i64(int64_t left, int64_t right)
{
    if (right > 0 && left > INT64_MAX - right) return INT64_MAX;
    if (right < 0 && left < INT64_MIN - right) return INT64_MIN;
    return left + right;
}

static int64_t actor_saturating_mul_i64(int64_t left, int64_t right)
{
    if (left == 0 || right == 0) return 0;
    if (left == -1) return right == INT64_MIN ? INT64_MAX : -right;
    if (right == -1) return left == INT64_MIN ? INT64_MAX : -left;
    if (left > 0) {
        if (right > 0 && left > INT64_MAX / right) return INT64_MAX;
        if (right < 0 && right < INT64_MIN / left) return INT64_MIN;
    } else {
        if (right > 0 && left < INT64_MIN / right) return INT64_MIN;
        if (right < 0 && left < INT64_MAX / right) return INT64_MAX;
    }
    return left * right;
}

static int32_t actor_clamp_i64_i32(int64_t value)
{
    return value > INT32_MAX ? INT32_MAX :
        value < INT32_MIN ? INT32_MIN : (int32_t)value;
}

static bool actor_hash_equal(const uint32_t left[8], const uint32_t right[8])
{
    uint32_t different = 0U;
    for (uint16_t word = 0U; word < 8U; word++)
        different |= left[word] ^ right[word];
    return different == 0U;
}

static void actor_output_reset(sm64_saturn_actor_meshlet_output_t *output)
{
    if (output == NULL) return;
    output->opaque_count = 0U;
    output->translucent_count = 0U;
    output->position_count = 0U;
}

static actor_meshlet_source_t actor_mario_source(void)
{
    actor_meshlet_source_t source;
    memset(&source, 0, sizeof(source));
    source.kind = ACTOR_MESHLET_SOURCE_MARIO;
    source.meshlet_count = SM64_MARIO_MESHLET_COUNT;
    source.primitive_count = SM64_MARIO_PRIMITIVE_COUNT;
    source.vertex_count = SM64_MARIO_VERTEX_COUNT;
    source.primitive_ref_count = SM64_MARIO_MESHLET_LOD_PRIMITIVE_LIST_COUNT;
    source.position_ref_count = SM64_MARIO_MESHLET_LOD_POSITION_LIST_COUNT;
    return source;
}

static bool actor_bank_source(const sm64_saturn_actor_bank_view_t *bank,
                              actor_meshlet_source_t *source)
{
    const uint8_t *geometry;
    uint32_t meshlet_offset, primitive_ref_offset, position_ref_offset;
    uint32_t primitive_ref_count, position_ref_count;
    if (bank == NULL || source == NULL || bank->bytes == NULL ||
        bank->meshlets_size < ACTOR_GEOMETRY_HEADER_SIZE ||
        bank->meshlets_offset > bank->byte_count ||
        bank->meshlets_size > bank->byte_count - bank->meshlets_offset ||
        bank->bank.meshlet_count == 0U || bank->bank.primitive_count == 0U ||
        bank->bank.vertex_count == 0U)
        return false;
    geometry = bank->bytes + bank->meshlets_offset;
    if (actor_read_be32(geometry) != 0x47454F31UL ||
        actor_read_be16(geometry + 10U) != bank->bank.meshlet_count ||
        actor_read_be16(geometry + 12U) != bank->bank.primitive_count)
        return false;
    primitive_ref_count = actor_read_be16(geometry + 14U);
    position_ref_count = actor_read_be16(geometry + 16U);
    meshlet_offset = actor_read_be32(geometry + 30U);
    primitive_ref_offset = actor_read_be32(geometry + 38U);
    position_ref_offset = actor_read_be32(geometry + 42U);
    if (!actor_relative_span(meshlet_offset,
            (uint32_t)bank->bank.meshlet_count * ACTOR_MESHLET_RECORD_SIZE,
            bank->meshlets_size) ||
        !actor_relative_span(primitive_ref_offset, primitive_ref_count * 2U,
                             bank->meshlets_size) ||
        !actor_relative_span(position_ref_offset, position_ref_count * 2U,
                             bank->meshlets_size))
        return false;
    memset(source, 0, sizeof(*source));
    source->kind = ACTOR_MESHLET_SOURCE_BANK;
    source->geometry = geometry;
    source->meshlet_offset = meshlet_offset;
    source->primitive_ref_offset = primitive_ref_offset;
    source->position_ref_offset = position_ref_offset;
    source->primitive_ref_count = primitive_ref_count;
    source->position_ref_count = position_ref_count;
    source->meshlet_count = bank->bank.meshlet_count;
    source->primitive_count = bank->bank.primitive_count;
    source->vertex_count = bank->bank.vertex_count;
    return true;
}

static bool actor_meshlet_span(const actor_meshlet_source_t *source,
                               uint16_t meshlet, uint8_t tier,
                               actor_meshlet_span_t *span)
{
    if (source == NULL || span == NULL || meshlet >= source->meshlet_count ||
        tier >= 3U)
        return false;
    if (source->kind == ACTOR_MESHLET_SOURCE_MARIO) {
#if defined(SM64_SATURN_ACTOR_MESHLET_TEST_INVALID_SPAN)
        if (meshlet == 0U && tier == 0U) return false;
#endif
        span->primitive_offset =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier];
        span->primitive_count =
            sm64_mario_meshlet_lod_primitive_offsets[meshlet * 3U + tier + 1U] -
            span->primitive_offset;
        span->position_offset =
            sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + tier];
        span->position_count =
            sm64_mario_meshlet_lod_position_offsets[meshlet * 3U + tier + 1U] -
            span->position_offset;
        span->opacity = sm64_mario_meshlet_opacity[meshlet];
    } else {
        const uint8_t *record = source->geometry + source->meshlet_offset +
            (uint32_t)meshlet * ACTOR_MESHLET_RECORD_SIZE;
        const uint8_t *fields = record + 18U + (uint32_t)tier * 16U;
        span->primitive_offset = actor_read_be32(fields);
        span->primitive_count = actor_read_be32(fields + 4U);
        span->position_offset = actor_read_be32(fields + 8U);
        span->position_count = actor_read_be32(fields + 12U);
        span->opacity = record[4U];
    }
    return span->opacity <= 1U &&
        span->primitive_offset <= source->primitive_ref_count &&
        span->primitive_count <=
            source->primitive_ref_count - span->primitive_offset &&
        span->position_offset <= source->position_ref_count &&
        span->position_count <=
            source->position_ref_count - span->position_offset;
}

static bool actor_primitive_ref(const actor_meshlet_source_t *source,
                                uint32_t index, uint16_t *primitive)
{
    if (source == NULL || primitive == NULL ||
        index >= source->primitive_ref_count)
        return false;
    *primitive = source->kind == ACTOR_MESHLET_SOURCE_MARIO
        ? sm64_mario_meshlet_lod_primitive_list[index]
        : actor_read_be16(source->geometry + source->primitive_ref_offset +
                          index * 2U);
    return *primitive < source->primitive_count;
}

static bool actor_position_ref(const actor_meshlet_source_t *source,
                               uint32_t index, uint16_t *position)
{
    if (source == NULL || position == NULL ||
        index >= source->position_ref_count)
        return false;
    *position = source->kind == ACTOR_MESHLET_SOURCE_MARIO
        ? sm64_mario_meshlet_lod_position_list[index]
        : actor_read_be16(source->geometry + source->position_ref_offset +
                          index * 2U);
    return *position < source->vertex_count;
}

/* Admission reads the selected live pose. Furthest depth rejects only wholly
 * behind meshlets and orders translucent bins; nearest depth chooses a
 * conservative LOD for any visible extent. Rotation is quantized to integer
 * actor units exactly as the legacy Mario path before conversion to Q16.16. */
static bool actor_meshlet_live_depth_bounds(
    const actor_meshlet_source_t *source,
    const actor_meshlet_transform_t *transform,
    const sm64_saturn_render_view_t *view, uint16_t meshlet,
    actor_meshlet_depth_bounds_t *bounds)
{
    actor_meshlet_span_t tier_zero;
    int64_t nearest = INT64_MAX, furthest = INT64_MIN;
    int32_t sine, cosine;
    if (bounds == NULL || transform == NULL || transform->vertices == NULL ||
        !actor_meshlet_span(source, meshlet, 0U, &tier_zero) ||
        tier_zero.position_count == 0U)
        return false;
    sine = sm64_saturn_sins_q16(transform->yaw);
    cosine = sm64_saturn_coss_q16(transform->yaw);
    for (uint32_t local = 0U; local < tier_zero.position_count; local++) {
        uint16_t vertex;
        int64_t scaled_x, scaled_y, scaled_z;
        int64_t rotated_x, rotated_z;
        int64_t world[3], depth = 0;
        if (!actor_position_ref(source, tier_zero.position_offset + local,
                                &vertex) || vertex >= transform->vertex_count)
            return false;
        scaled_x = (int64_t)transform->vertices[vertex][0] *
                   transform->scale_q16[0];
        scaled_y = (int64_t)transform->vertices[vertex][1] *
                   transform->scale_q16[1];
        scaled_z = (int64_t)transform->vertices[vertex][2] *
                   transform->scale_q16[2];
        rotated_x = actor_saturating_add_i64(
            actor_saturating_mul_i64(scaled_x, cosine),
            actor_saturating_mul_i64(scaled_z, sine)) >> 16;
        rotated_z = actor_saturating_add_i64(
            actor_saturating_mul_i64(-scaled_x, sine),
            actor_saturating_mul_i64(scaled_z, cosine)) >> 16;
        world[0] = actor_saturating_add_i64(
            transform->position_q16[0],
            actor_saturating_mul_i64(rotated_x >> 16, 65536));
        world[1] = actor_saturating_add_i64(
            transform->position_q16[1],
            actor_saturating_mul_i64(scaled_y >> 16, 65536));
        world[2] = actor_saturating_add_i64(
            transform->position_q16[2],
            actor_saturating_mul_i64(rotated_z >> 16, 65536));
        for (uint16_t axis = 0U; axis < 3U; axis++) {
            const int64_t relative = actor_saturating_add_i64(
                world[axis], -(int64_t)view->camera_position_q16[axis]);
            const int64_t term = actor_saturating_mul_i64(
                relative, view->view_forward_q16[axis]) >> 16;
            depth = actor_saturating_add_i64(depth, term);
        }
        if (depth < nearest) nearest = depth;
        if (depth > furthest) furthest = depth;
    }
    bounds->nearest_q16 = actor_clamp_i64_i32(nearest);
    bounds->furthest_q16 = actor_clamp_i64_i32(furthest);
    return true;
}

static bool actor_meshlet_core(
    const actor_meshlet_source_t *source,
    const actor_meshlet_transform_t *transform,
    const sm64_saturn_render_view_t *view, uint8_t *position_seen,
    uint32_t position_seen_capacity, sm64_saturn_actor_meshlet_output_t *output,
    sm64_saturn_actor_draw_ref_t *combined_records, uint16_t draw_capacity,
    bool atomic_output, uint8_t *failure_reason,
    sm64_saturn_fast3d_profile_t *stats)
{
    uint32_t opaque_count = 0U, translucent_count = 0U;
    uint16_t admitted_positions = 0U, admitted_meshlets = 0U, culled = 0U;
    uint16_t translucent_bins[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT] = {0};

    if (draw_capacity == 0U && failure_reason != NULL)
        *failure_reason =
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW;
    if (source == NULL || transform == NULL || view == NULL || output == NULL ||
        ((combined_records != NULL && draw_capacity == 0U) ||
         (combined_records == NULL &&
          (output->opaque == NULL || output->translucent == NULL))) ||
        output->positions == NULL || position_seen == NULL ||
        draw_capacity == 0U || output->position_capacity == 0U ||
        position_seen_capacity < source->vertex_count ||
        transform->vertices == NULL ||
        transform->vertex_count != source->vertex_count ||
        view->generation == 0U ||
        (view->view_forward_q16[0] == 0 && view->view_forward_q16[1] == 0 &&
         view->view_forward_q16[2] == 0))
        return false;

    memset(position_seen, 0, source->vertex_count);
    for (uint16_t meshlet = 0U; meshlet < source->meshlet_count; meshlet++) {
        actor_meshlet_depth_bounds_t depth_bounds;
        actor_meshlet_span_t span;
        if (!atomic_output && stats != NULL)
            stats->demo_actor_meshlets_tested++;
        if (!actor_meshlet_live_depth_bounds(source, transform, view, meshlet,
                                             &depth_bounds))
            return false;
        if (depth_bounds.furthest_q16 <= 0) {
            culled++;
            continue;
        }
        if (!actor_meshlet_span(source, meshlet,
                                actor_lod_tier(depth_bounds.nearest_q16), &span))
            return false;
        if (span.primitive_count == 0U) continue;
        admitted_meshlets++;
        for (uint32_t local = 0U; local < span.position_count; local++) {
            uint16_t position;
            if (!actor_position_ref(source, span.position_offset + local,
                                    &position))
                return false;
            if (position_seen[position] != 0U) continue;
            if (!atomic_output) {
                if (admitted_positions >= output->position_capacity)
                    return false;
                output->positions[admitted_positions] = position;
            }
            position_seen[position] = 1U;
            if (admitted_positions == UINT16_MAX) return false;
            admitted_positions++;
        }
        if (span.opacity != 0U) {
            const uint8_t bin = actor_depth_bin(depth_bounds.furthest_q16);
            translucent_count += span.primitive_count;
            if (translucent_count > UINT16_MAX ||
                span.primitive_count >
                    (uint32_t)UINT16_MAX - translucent_bins[bin])
                return false;
            translucent_bins[bin] = (uint16_t)(translucent_bins[bin] +
                                                span.primitive_count);
        } else {
            opaque_count += span.primitive_count;
            if (opaque_count > UINT16_MAX) return false;
        }
        if (opaque_count + translucent_count > draw_capacity) {
            if (failure_reason != NULL)
                *failure_reason =
                    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW;
            return false;
        }
    }
    if (admitted_positions > output->position_capacity) {
        if (failure_reason != NULL)
            *failure_reason =
                SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW;
        return false;
    }

    {
        uint16_t opaque_cursor = 0U, position_cursor = 0U;
        uint16_t translucent_cursor[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT];
        uint16_t cursor = 0U;
        sm64_saturn_actor_draw_ref_t *opaque_output = combined_records != NULL
            ? combined_records : output->opaque;
        sm64_saturn_actor_draw_ref_t *translucent_output =
            combined_records != NULL
                ? combined_records + opaque_count : output->translucent;
        for (int16_t bin = SM64_SATURN_ACTOR_DEPTH_BIN_COUNT - 1U;
             bin >= 0; bin--) {
            translucent_cursor[bin] = cursor;
            cursor = (uint16_t)(cursor + translucent_bins[bin]);
        }
        memset(position_seen, 0, source->vertex_count);
        for (uint16_t meshlet = 0U; meshlet < source->meshlet_count; meshlet++) {
            actor_meshlet_depth_bounds_t depth_bounds;
            actor_meshlet_span_t span;
            (void)actor_meshlet_live_depth_bounds(source, transform, view,
                                                  meshlet, &depth_bounds);
            if (depth_bounds.furthest_q16 <= 0) continue;
            (void)actor_meshlet_span(source, meshlet,
                actor_lod_tier(depth_bounds.nearest_q16), &span);
            if (span.primitive_count == 0U) continue;
            if (atomic_output)
                for (uint32_t local = 0U; local < span.position_count; local++) {
                    uint16_t position;
                    (void)actor_position_ref(
                        source, span.position_offset + local, &position);
                    if (position_seen[position] != 0U) continue;
                    position_seen[position] = 1U;
                    output->positions[position_cursor++] = position;
                }
            for (uint32_t local = 0U; local < span.primitive_count; local++) {
                uint16_t primitive;
                sm64_saturn_actor_draw_ref_t ref;
                const uint8_t bin = actor_depth_bin(depth_bounds.furthest_q16);
                (void)actor_primitive_ref(source, span.primitive_offset + local,
                                          &primitive);
                ref.meshlet_id = meshlet;
                ref.primitive_id = primitive;
                ref.sort_key = ((uint32_t)bin << 16) | primitive;
                if (span.opacity != 0U)
                    translucent_output[translucent_cursor[bin]++] = ref;
                else
                    opaque_output[opaque_cursor++] = ref;
            }
        }
        if (combined_records != NULL) {
            output->opaque = opaque_output;
            output->translucent = translucent_output;
        }
    }
    output->opaque_count = (uint16_t)opaque_count;
    output->translucent_count = (uint16_t)translucent_count;
    output->position_count = admitted_positions;
    if (stats != NULL) {
        if (atomic_output)
            stats->demo_actor_meshlets_tested += source->meshlet_count;
        stats->demo_actor_meshlets_admitted += admitted_meshlets;
        stats->demo_actor_meshlets_culled += culled;
        stats->demo_actor_positions_admitted += admitted_positions;
    }
    return true;
}

bool sm64_saturn_actor_meshlets_prepare(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats)
{
    uint8_t position_seen[SM64_MARIO_VERTEX_COUNT];
    actor_meshlet_source_t source = actor_mario_source();
    actor_meshlet_transform_t transform;
    actor_output_reset(output);
    if (snapshot == NULL || pose == NULL || output == NULL ||
        snapshot->generation == 0U || snapshot->actor_generation == 0U ||
        snapshot->mario.valid == 0U || pose->vertices == NULL ||
        pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return false;
    memset(&transform, 0, sizeof(transform));
    transform.vertices = pose->vertices;
    transform.vertex_count = pose->vertex_count;
    transform.position_q16[0] =
        (int64_t)snapshot->mario.position[0] * 65536;
    transform.position_q16[1] =
        (int64_t)snapshot->mario.position[1] * 65536;
    transform.position_q16[2] =
        (int64_t)snapshot->mario.position[2] * 65536;
    transform.scale_q16[0] = 1 << 16;
    transform.scale_q16[1] = 1 << 16;
    transform.scale_q16[2] = 1 << 16;
    transform.yaw = snapshot->mario.yaw;
    return actor_meshlet_core(&source, &transform, view, position_seen,
                              SM64_MARIO_VERTEX_COUNT, output, NULL, capacity,
                              false, NULL, stats);
}

bool sm64_saturn_actor_meshlets_prepare_bank(
    const sm64_saturn_actor_bank_view_t *bank,
    const sm64_saturn_actor_instance_snapshot_t *instance,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_pose_work_t *pose_work,
    sm64_saturn_actor_meshlet_bank_output_t *output,
    sm64_saturn_fast3d_profile_t *stats)
{
    sm64_saturn_actor_bank_view_t validated;
    sm64_saturn_actor_pose_view_t pose;
    actor_meshlet_source_t source;
    actor_meshlet_transform_t transform;
    uint8_t failure_reason =
        SM64_SATURN_ACTOR_MESHLET_QUARANTINE_CLAIMANT_FAILURE;

    if (output != NULL) actor_output_reset(&output->output);
    if (output != NULL && output->abi ==
            SM64_SATURN_ACTOR_MESHLET_BANK_OUTPUT_ABI)
        output->quarantine_reason =
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_CLAIMANT_FAILURE;
    if (bank == NULL || instance == NULL || view == NULL || pose_work == NULL ||
        output == NULL || output->abi !=
            SM64_SATURN_ACTOR_MESHLET_BANK_OUTPUT_ABI ||
        output->records == NULL || output->position_seen == NULL ||
        output->position_seen_capacity == 0U || bank->bytes == NULL ||
        instance->generation == 0U ||
        instance->generation != view->generation) {
        if (output != NULL && output->abi ==
                SM64_SATURN_ACTOR_MESHLET_BANK_OUTPUT_ABI &&
            instance != NULL && view != NULL &&
            instance->generation != view->generation)
            output->quarantine_reason =
                SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_GENERATION;
        return false;
    }
    if (!sm64_saturn_actor_bank_validate(bank->bytes, bank->byte_count,
                                         &validated) ||
        instance->actor_bank_id == 0U ||
        instance->family_id != validated.bank.family_id ||
        instance->model_id != validated.bank.model_id ||
        !actor_hash_equal(instance->actor_bank_hash_words,
                          validated.bank.source_hash_words)) {
        output->quarantine_reason =
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_BANK;
        return false;
    }
    if (instance->active == 0U || instance->render_active == 0U ||
        instance->scale_q16[0] == 0 || instance->scale_q16[1] == 0 ||
        instance->scale_q16[2] == 0 || pose_work->light_intensity == NULL ||
        pose_work->light_capacity < validated.bank.vertex_count ||
        pose_work->joint_matrices_q16 == NULL ||
        !actor_bank_source(&validated, &source) ||
        !sm64_saturn_actor_pose_evaluate(
            &validated, instance->animation_id, instance->animation_frame,
            pose_work, &pose))
        return false;
    memset(&transform, 0, sizeof(transform));
    transform.vertices = pose.vertices;
    transform.vertex_count = pose.vertex_count;
    for (uint16_t axis = 0U; axis < 3U; axis++)
        transform.position_q16[axis] = instance->position_q16[axis];
    memcpy(transform.scale_q16, instance->scale_q16,
           sizeof(transform.scale_q16));
    transform.yaw = instance->angle[1];
    if (!actor_meshlet_core(
            &source, &transform, view, output->position_seen,
            output->position_seen_capacity, &output->output, output->records,
            output->draw_capacity,
            true, &failure_reason, stats)) {
        output->quarantine_reason = failure_reason;
        return false;
    }
    output->quarantine_reason =
        SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE;
    return true;
}
