#include "saturn_actor_meshlets.h"

#include <limits.h>
#include <string.h>

#include "../runtime/saturn_prenotify_profile.h"
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

/* T2.6 step 1.  The meshlet core walks every meshlet twice -- once to
 * admit and bin, once to emit -- and before this change the emission pass
 * recomputed the live depth bounds from scratch for every meshlet.
 * T2.5 measured the two walks at 20,491.7 and 20,491.9 FRT ticks, 0.001%
 * apart: an exact, unconditional recomputation worth 5.72 VBlanks/frame.
 *
 * The two passes are input-identical by construction -- `source`, `transform`
 * and `view` are const parameters that neither pass writes, and the only state
 * mutated between them (the bin cursors and the position_seen re-clear) does
 * not alias the pose vertices or the geometry tables.  Carrying pass 1's
 * result into pass 2 is therefore a memoisation of identical inputs, not a
 * numeric change.
 *
 * The stamp is belt-and-braces rather than load-bearing: nothing inside a
 * single core call can invalidate it, so a mismatch means an invariant this
 * code does not control has been broken, and the answer is to recompute
 * rather than serve stale bounds.
 *
 * NOTE: do not write a function name followed by "(" anywhere in a comment
 * above its definition in this file.  tools/saturn/test_render_snapshot_source.py
 * extracts function bodies with text.index("<name>(") and would then read the
 * comment instead of the code. */
#define ACTOR_MESHLET_DEPTH_CARRY_CAPACITY 64U

typedef struct actor_meshlet_depth_carry {
    const int16_t (*vertices)[3];
    uint32_t generation;
    uint16_t meshlet_count;
    uint16_t valid;
    actor_meshlet_depth_bounds_t bounds[ACTOR_MESHLET_DEPTH_CARRY_CAPACITY];
} actor_meshlet_depth_carry_t;

/* Master-only.  Bound solely by the Mario prepare entry point, which
 * runs on the master SH-2 inside the pre-notification window alongside the
 * rest of saturn_demo_render.c's file-scope frame state.  The bank entry point
 * is dispatched on either CPU (saturn_demo_render.c:3129 picks a workspace
 * lane from the claim), so it passes NULL and keeps recomputing -- a shared
 * static would be a cross-CPU race, and the bank path is not on the measured
 * hot path. */
static actor_meshlet_depth_carry_t s_mario_depth_carry;

_Static_assert(SM64_MARIO_MESHLET_COUNT <= ACTOR_MESHLET_DEPTH_CARRY_CAPACITY,
               "Mario meshlet count outgrew the depth carry: raise the "
               "capacity rather than silently falling back to recomputation");

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

static bool actor_bank_version_supported(uint16_t version)
{
    return version == SM64_SATURN_ACTOR_BANK_VERSION_V1 ||
           version == SM64_SATURN_ACTOR_BANK_VERSION_V2;
}

bool sm64_saturn_actor_meshlets_workspace_query(
    const sm64_saturn_actor_bank_view_t *bank, uint32_t *lane_bytes,
    uint32_t *usable_bytes, uint32_t *reserved_bytes)
{
    uint32_t lane, usable, minimum_reserved;
    if (bank == NULL || lane_bytes == NULL || usable_bytes == NULL ||
        reserved_bytes == NULL ||
        bank->bytes == NULL || bank->bank.magic != SM64_SATURN_ACTOR_BANK_MAGIC ||
        !actor_bank_version_supported(bank->bank.version) ||
        !sm64_saturn_actor_bank_workspace_requirements(
            bank->bank.vertex_count, bank->bank.joint_count, &lane, &usable,
            &minimum_reserved) || bank->max_scratch < minimum_reserved)
        return false;
    *lane_bytes = lane;
    *usable_bytes = usable;
    *reserved_bytes = bank->max_scratch;
    return true;
}

static uint32_t actor_align_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static bool actor_pointer_spans_overlap(const void *left, uint32_t left_size,
                                        const void *right,
                                        uint32_t right_size)
{
    const uintptr_t left_start = (uintptr_t)left;
    const uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end, right_end;
    if (left_size > UINTPTR_MAX - left_start ||
        right_size > UINTPTR_MAX - right_start)
        return true;
    left_end = left_start + left_size;
    right_end = right_start + right_size;
    return left_start < right_end && right_start < left_end;
}

static bool actor_bind_workspace_with_stride(
    const sm64_saturn_actor_bank_view_t *bank, void *scratch,
    uint32_t scratch_capacity, uint32_t lane_stride, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_meshlet_workspace_t *workspace)
{
    uint8_t *bytes = (uint8_t *)scratch;
    uintptr_t raw_address, aligned_address;
    uint32_t lane_bytes, usable_bytes, reserved_bytes, minimum_capacity;
    uint32_t leading_bytes, cursor;
    uint32_t record_bytes = (uint32_t)draw_capacity * sizeof(*records);
    if (workspace == NULL || scratch == NULL || records == NULL ||
        draw_capacity == 0U ||
        lane >= SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT ||
        !sm64_saturn_actor_meshlets_workspace_query(
            bank, &lane_bytes, &usable_bytes, &reserved_bytes) ||
        lane_stride < lane_bytes ||
        (lane_stride & (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U)) != 0U ||
        lane_stride > (UINT32_MAX -
            (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U)) /
                SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT)
        return false;
    minimum_capacity = lane_stride * SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT +
        (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U);
    if (scratch_capacity < minimum_capacity ||
        actor_pointer_spans_overlap(
            scratch, minimum_capacity, records, record_bytes))
        return false;

    raw_address = (uintptr_t)scratch;
    if (raw_address > UINTPTR_MAX -
            (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U))
        return false;
    aligned_address = (raw_address +
        (SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U)) &
        ~(uintptr_t)(SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT - 1U);
    leading_bytes = (uint32_t)(aligned_address - raw_address);
    if (leading_bytes > minimum_capacity ||
        lane_stride * SM64_SATURN_ACTOR_MESHLET_WORK_LANE_COUNT >
            minimum_capacity - leading_bytes)
        return false;

    memset(workspace, 0, sizeof(*workspace));
    bytes += leading_bytes;
    cursor = (uint32_t)lane * lane_stride;
    workspace->scratch_offset = leading_bytes + cursor;
    workspace->scratch_size = lane_stride;
    workspace->lane = lane;
    cursor = actor_align_u32(cursor, _Alignof(int16_t));
    workspace->pose_work.vertices = (int16_t (*)[3])(void *)(bytes + cursor);
    cursor += (uint32_t)bank->bank.vertex_count * 3U * sizeof(int16_t);
    workspace->pose_work.light_intensity = bytes + cursor;
    cursor += (uint32_t)bank->bank.vertex_count * sizeof(uint8_t);
    cursor = actor_align_u32(cursor, _Alignof(int32_t));
    workspace->pose_work.joint_matrices_q16 =
        (int32_t *)(void *)(bytes + cursor);
    cursor += (uint32_t)bank->bank.joint_count * 16U * sizeof(int32_t);
    cursor = actor_align_u32(cursor, _Alignof(uint16_t));
    workspace->output.output.positions = (uint16_t *)(void *)(bytes + cursor);
    cursor += (uint32_t)bank->bank.vertex_count * sizeof(uint16_t);
    workspace->output.position_seen = bytes + cursor;
    cursor += (uint32_t)bank->bank.vertex_count * sizeof(uint8_t);
    if (actor_align_u32(cursor, SM64_SATURN_ACTOR_MESHLET_WORK_ALIGNMENT) >
            ((uint32_t)lane + 1U) * lane_stride)
        return false;
    workspace->pose_work.vertex_capacity = bank->bank.vertex_count;
    workspace->pose_work.joint_capacity = bank->bank.joint_count;
    workspace->pose_work.light_capacity = bank->bank.vertex_count;
    workspace->output.output.position_capacity = bank->bank.vertex_count;
    workspace->output.records = records;
    workspace->output.abi = SM64_SATURN_ACTOR_MESHLET_BANK_OUTPUT_ABI;
    workspace->output.draw_capacity = draw_capacity;
    workspace->output.position_seen_capacity = bank->bank.vertex_count;
    workspace->output.quarantine_reason =
        SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE;
    return true;
}

bool sm64_saturn_actor_meshlets_bind_workspace(
    const sm64_saturn_actor_bank_view_t *bank, void *scratch,
    uint32_t scratch_capacity, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_meshlet_workspace_t *workspace)
{
    uint32_t lane_bytes, usable_bytes, reserved_bytes;
    if (!sm64_saturn_actor_meshlets_workspace_query(
            bank, &lane_bytes, &usable_bytes, &reserved_bytes) ||
        scratch_capacity < reserved_bytes)
        return false;
    return actor_bind_workspace_with_stride(
        bank, scratch, scratch_capacity, lane_bytes, lane, records,
        draw_capacity, workspace);
}

bool sm64_saturn_actor_meshlets_bind_bundle_workspace(
    const sm64_saturn_actor_bank_view_t *bank, void *scratch,
    uint32_t scratch_capacity, uint32_t lane_stride, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_meshlet_workspace_t *workspace)
{
    return actor_bind_workspace_with_stride(
        bank, scratch, scratch_capacity, lane_stride, lane, records,
        draw_capacity, workspace);
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

/* T2.6 step 0.  The per-vertex arithmetic of the depth walk, lifted verbatim
 * out of the pre-T2.6 loop body.  Nothing about the computation changed in the
 * extraction: the same helper calls happen in the same order on the same
 * operands, so this function IS the pre-T2.6 semantics rather than a model of
 * them.  Extracting it gives the equivalence oracle a name to pin and gives a
 * later fast kernel a definition to be proved equal to. */
static int64_t actor_depth_reference(const int16_t vertex[3],
                                     const actor_meshlet_transform_t *transform,
                                     const sm64_saturn_render_view_t *view,
                                     int32_t sine, int32_t cosine)
{
    int64_t scaled_x, scaled_y, scaled_z;
    int64_t rotated_x, rotated_z;
    int64_t world[3], depth = 0;
    scaled_x = (int64_t)vertex[0] * transform->scale_q16[0];
    scaled_y = (int64_t)vertex[1] * transform->scale_q16[1];
    scaled_z = (int64_t)vertex[2] * transform->scale_q16[2];
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
    return depth;
}

/* T2.6 step 2.  Everything the reference recomputes per vertex that is in fact
 * per-actor: the yaw sine/cosine (T2.5 caught these being recomputed once per
 * meshlet from a yaw that cannot change within the frame) and the constant
 * (actor_position - camera_position) . view_forward term.
 *
 * The reference computes, per axis,
 *     term_a = ((B_a + (q_a << 16)) * F_a) >> 16
 * where B_a = P_a - C_a is per-actor and q_a is the integer-quantised rotated
 * vertex component.  Since q_a * F_a is an exact integer it pulls straight out
 * of the floor:
 *     term_a = ((B_a * F_a) >> 16) + q_a * F_a
 * so SUM_a ((B_a * F_a) >> 16) is a per-actor constant -- base_depth -- and the
 * per-vertex work collapses to four dmuls.l for the rotation and three for the
 * dot product.  No division, no 64-bit multiply, no libgcc call.
 *
 * This is an exact rewrite, NOT an approximation, but only while no saturating
 * helper in the reference would actually have saturated -- otherwise the
 * reference clamps where the fast form wraps.  The preconditions below are
 * checked once per actor and bound every intermediate:
 *   |v| <= 2^15 by type; |sin|,|cos| <= 2^16 checked; unit scale checked, so
 *   |v * trig| <= 2^31 and the rotation sum <= 2^32, giving |q| <= 2^16;
 *   |P| <= 2^40 checked and |C| <= 2^31 by type, so |B| < 2^41;
 *   |F| <= 2^20 checked, so |B * F| <= 2^61 and |relative * F| <= 2^62.
 * Every product therefore stays inside int64 and the saturating helpers are
 * exact addition and multiplication.  Outside the preconditions the reference
 * still runs, which is why it was kept rather than deleted -- the generic bank
 * path admits arbitrary per-axis scales and does not qualify. */
#define ACTOR_DEPTH_FAST_UNIT_SCALE_Q16 INT32_C(65536)
#define ACTOR_DEPTH_FAST_POSITION_LIMIT INT64_C(0x10000000000) /* 2^40 */
#define ACTOR_DEPTH_FAST_FORWARD_LIMIT  INT32_C(0x100000)      /* 2^20 */

typedef struct actor_depth_kernel {
    int64_t base_depth;
    int32_t forward_q16[3];
    int32_t sine;
    int32_t cosine;
    bool fast;
} actor_depth_kernel_t;

/* Written as a widening 32x32 -> 64 product so the SH-2 emits a single
 * dmuls.l, the primitive saturn_q16_sh2.h's sm64_saturn_q16_mul_sh2() is built
 * on, rather than a __muldi3 call. */
static int64_t actor_widen_mul(int32_t left, int32_t right)
{
    return (int64_t)left * (int64_t)right;
}

static void actor_depth_kernel_prepare(
    const actor_meshlet_transform_t *transform,
    const sm64_saturn_render_view_t *view, actor_depth_kernel_t *kernel)
{
    kernel->base_depth = 0;
    kernel->fast = false;
    kernel->sine = sm64_saturn_sins_q16(transform->yaw);
    kernel->cosine = sm64_saturn_coss_q16(transform->yaw);
    for (uint16_t axis = 0U; axis < 3U; axis++)
        kernel->forward_q16[axis] = view->view_forward_q16[axis];
    if (kernel->sine > 65536 || kernel->sine < -65536 ||
        kernel->cosine > 65536 || kernel->cosine < -65536)
        return;
    for (uint16_t axis = 0U; axis < 3U; axis++) {
        if (transform->scale_q16[axis] != ACTOR_DEPTH_FAST_UNIT_SCALE_Q16 ||
            transform->position_q16[axis] > ACTOR_DEPTH_FAST_POSITION_LIMIT ||
            transform->position_q16[axis] < -ACTOR_DEPTH_FAST_POSITION_LIMIT ||
            view->view_forward_q16[axis] > ACTOR_DEPTH_FAST_FORWARD_LIMIT ||
            view->view_forward_q16[axis] < -ACTOR_DEPTH_FAST_FORWARD_LIMIT)
            return;
    }
    for (uint16_t axis = 0U; axis < 3U; axis++) {
        const int64_t base = transform->position_q16[axis] -
            (int64_t)view->camera_position_q16[axis];
        kernel->base_depth +=
            (base * (int64_t)view->view_forward_q16[axis]) >> 16;
    }
    kernel->fast = true;
}

/* Unit scale means the reference's scaled_* are exactly vertex << 16, so its
 * two successive >>16 narrowings compose into one >>32 and the 2^16 factors
 * straight out of the numerator -- leaving a single >>16 over 32x32 products.
 * scaled_y >> 16 is then exactly vertex[1]. */
static int64_t actor_depth_fast(const int16_t vertex[3],
                                const actor_depth_kernel_t *kernel)
{
    const int32_t rotated_x = (int32_t)(
        (actor_widen_mul(vertex[0], kernel->cosine) +
         actor_widen_mul(vertex[2], kernel->sine)) >> 16);
    const int32_t rotated_z = (int32_t)(
        (actor_widen_mul(vertex[2], kernel->cosine) -
         actor_widen_mul(vertex[0], kernel->sine)) >> 16);
    return kernel->base_depth +
        actor_widen_mul(rotated_x, kernel->forward_q16[0]) +
        actor_widen_mul(vertex[1], kernel->forward_q16[1]) +
        actor_widen_mul(rotated_z, kernel->forward_q16[2]);
}

/* Admission reads the selected live pose. Furthest depth rejects only wholly
 * behind meshlets and orders translucent bins; nearest depth chooses a
 * conservative LOD for any visible extent. Rotation is quantized to integer
 * actor units exactly as the legacy Mario path before conversion to Q16.16. */
static bool actor_meshlet_live_depth_bounds(
    const actor_meshlet_source_t *source,
    const actor_meshlet_transform_t *transform,
    const sm64_saturn_render_view_t *view,
    const actor_depth_kernel_t *kernel, uint16_t meshlet,
    actor_meshlet_depth_bounds_t *bounds)
{
    actor_meshlet_span_t tier_zero;
    int64_t nearest = INT64_MAX, furthest = INT64_MIN;
    if (bounds == NULL || transform == NULL || transform->vertices == NULL ||
        kernel == NULL ||
        !actor_meshlet_span(source, meshlet, 0U, &tier_zero) ||
        tier_zero.position_count == 0U)
        return false;
    for (uint32_t local = 0U; local < tier_zero.position_count; local++) {
        uint16_t vertex;
        int64_t depth;
        if (!actor_position_ref(source, tier_zero.position_offset + local,
                                &vertex) || vertex >= transform->vertex_count)
            return false;
        depth = kernel->fast
            ? actor_depth_fast(transform->vertices[vertex], kernel)
            : actor_depth_reference(transform->vertices[vertex], transform,
                                    view, kernel->sine, kernel->cosine);
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
    sm64_saturn_fast3d_profile_t *stats, actor_meshlet_depth_carry_t *carry)
{
    uint32_t opaque_count = 0U, translucent_count = 0U;
    uint16_t admitted_positions = 0U, admitted_meshlets = 0U, culled = 0U;
    uint16_t opaque_bins[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT] = {0};
    uint16_t translucent_bins[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT] = {0};
    actor_depth_kernel_t depth_kernel;

    if (draw_capacity == 0U && failure_reason != NULL)
        *failure_reason =
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW;
    if (source == NULL || transform == NULL || view == NULL || output == NULL ||
        source->kind > ACTOR_MESHLET_SOURCE_BANK ||
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
    /* T2.6 step 2.  Prepared once per actor -- not once per meshlet, and
     * certainly not once per vertex.  The yaw trig and the
     * (actor_position - camera_position) . view_forward term are invariant
     * across all 704 tier-0 position visits of the walk. */
    actor_depth_kernel_prepare(transform, view, &depth_kernel);
    /* A source with more meshlets than the carry holds simply keeps the old
     * two-walk behaviour; correctness never depends on the carry being live. */
    if (carry != NULL &&
        source->meshlet_count > ACTOR_MESHLET_DEPTH_CARRY_CAPACITY)
        carry = NULL;
    if (carry != NULL) {
        carry->vertices = transform->vertices;
        carry->generation = view->generation;
        carry->meshlet_count = source->meshlet_count;
        carry->valid = 0U;
    }
    /* T2.5.  The depth node is pushed once per meshlet, never per vertex: an
     * FRT read costs tens of cycles and there are 704 tier-0 position visits
     * per pass, so a per-vertex probe would have measured mostly itself.
     * Per-vertex cost is derived by dividing by that 704, which is a
     * compile-time property of sm64_mario_meshlet_lod_position_offsets rather
     * than a runtime quantity: actor_meshlet_live_depth_bounds always walks
     * the whole tier-0 span of every meshlet, before any cull test or LOD
     * choice can shorten it. */
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_ADMIT);
    for (uint16_t meshlet = 0U; meshlet < source->meshlet_count; meshlet++) {
        actor_meshlet_depth_bounds_t depth_bounds;
        actor_meshlet_span_t span;
        bool bounds_ok;
        if (!atomic_output && stats != NULL)
            stats->demo_actor_meshlets_tested++;
        SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
            SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_DEPTH_ADMIT);
        bounds_ok = actor_meshlet_live_depth_bounds(source, transform, view,
                                                    &depth_kernel, meshlet,
                                                    &depth_bounds);
        SM64_SATURN_PRENOTIFY_PROFILE_POP();
        if (!bounds_ok) {
            SM64_SATURN_PRENOTIFY_PROFILE_POP();
            return false;
        }
        if (carry != NULL) {
            carry->bounds[meshlet] = depth_bounds;
            carry->valid = (uint16_t)(meshlet + 1U);
        }
        if (depth_bounds.furthest_q16 <= 0) {
            culled++;
            continue;
        }
        if (!actor_meshlet_span(source, meshlet,
                                actor_lod_tier(depth_bounds.nearest_q16),
                                &span)) {
            SM64_SATURN_PRENOTIFY_PROFILE_POP();
            return false;
        }
        if (span.primitive_count == 0U) continue;
        admitted_meshlets++;
        for (uint32_t local = 0U; local < span.position_count; local++) {
            uint16_t position;
            if (!actor_position_ref(source, span.position_offset + local,
                                    &position)) {
                SM64_SATURN_PRENOTIFY_PROFILE_POP();
                return false;
            }
            if (position_seen[position] != 0U) continue;
            if (!atomic_output) {
                if (admitted_positions >= output->position_capacity) {
                    SM64_SATURN_PRENOTIFY_PROFILE_POP();
                    return false;
                }
                output->positions[admitted_positions] = position;
            }
            position_seen[position] = 1U;
            if (admitted_positions == UINT16_MAX) {
                SM64_SATURN_PRENOTIFY_PROFILE_POP();
                return false;
            }
            admitted_positions++;
        }
        {
            const uint8_t bin = actor_depth_bin(depth_bounds.furthest_q16);
            if (span.opacity != 0U) {
            translucent_count += span.primitive_count;
            if (translucent_count > UINT16_MAX ||
                span.primitive_count >
                    (uint32_t)UINT16_MAX - translucent_bins[bin]) {
                SM64_SATURN_PRENOTIFY_PROFILE_POP();
                return false;
            }
            translucent_bins[bin] = (uint16_t)(translucent_bins[bin] +
                                                span.primitive_count);
            } else {
                opaque_count += span.primitive_count;
                if (opaque_count > UINT16_MAX ||
                    span.primitive_count >
                        (uint32_t)UINT16_MAX - opaque_bins[bin]) {
                    SM64_SATURN_PRENOTIFY_PROFILE_POP();
                    return false;
                }
                opaque_bins[bin] = (uint16_t)(opaque_bins[bin] +
                                               span.primitive_count);
            }
        }
        if (opaque_count + translucent_count > draw_capacity) {
            if (failure_reason != NULL)
                *failure_reason =
                    SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW;
            SM64_SATURN_PRENOTIFY_PROFILE_POP();
            return false;
        }
    }
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    if (admitted_positions > output->position_capacity) {
        if (failure_reason != NULL)
            *failure_reason =
                SM64_SATURN_ACTOR_MESHLET_QUARANTINE_OUTPUT_OVERFLOW;
        return false;
    }

    {
        uint16_t opaque_cursor[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT];
        uint16_t position_cursor = 0U;
        uint16_t translucent_cursor[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT];
        uint16_t opaque_offset = 0U, translucent_offset = 0U;
        sm64_saturn_actor_draw_ref_t *opaque_output = combined_records != NULL
            ? combined_records : output->opaque;
        sm64_saturn_actor_draw_ref_t *translucent_output =
            combined_records != NULL
                ? combined_records + opaque_count : output->translucent;
        SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
            SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_PREFIX);
        for (int16_t bin = SM64_SATURN_ACTOR_DEPTH_BIN_COUNT - 1U;
             bin >= 0; bin--) {
            opaque_cursor[bin] = opaque_offset;
            opaque_offset = (uint16_t)(opaque_offset + opaque_bins[bin]);
            translucent_cursor[bin] = translucent_offset;
            translucent_offset =
                (uint16_t)(translucent_offset + translucent_bins[bin]);
        }
        memset(position_seen, 0, source->vertex_count);
        SM64_SATURN_PRENOTIFY_PROFILE_POP();
        /* The emission pass has no error return, so this bracket is
         * unconditionally balanced. */
        SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
            SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_EMIT);
        for (uint16_t meshlet = 0U; meshlet < source->meshlet_count; meshlet++) {
            /* Zero-initialised because the recompute fallback below discards
             * its own success flag, exactly as the pre-T2.6 code did.  Pass 1
             * has already validated every meshlet, so that path is
             * unreachable; if it were ever reached, {0,0} skips the meshlet at
             * the smallest safe object instead of reading a stack value. */
            actor_meshlet_depth_bounds_t depth_bounds = {0, 0};
            actor_meshlet_span_t span;
            /* T2.6 step 1.  Pass 1 computed exactly this, from exactly these
             * inputs, and threw it away.  Serve its answer when the stamp still
             * describes this call; otherwise fall back to recomputation rather
             * than trust a carry that cannot be shown to be current. */
            const bool carry_current = carry != NULL &&
                meshlet < carry->valid &&
                carry->generation == view->generation &&
                carry->meshlet_count == source->meshlet_count &&
                carry->vertices == transform->vertices;
            if (carry_current) {
                depth_bounds = carry->bounds[meshlet];
            } else {
                SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
                    SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_DEPTH_EMIT);
                (void)actor_meshlet_live_depth_bounds(source, transform, view,
                                                      &depth_kernel, meshlet,
                                                      &depth_bounds);
                SM64_SATURN_PRENOTIFY_PROFILE_POP();
            }
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
                /* The first pass has already validated every reference, but
                 * keep this emission pass defined even under the target
                 * compiler's interprocedural warning model. */
                uint16_t primitive = 0U;
                sm64_saturn_actor_draw_ref_t ref;
                const uint8_t bin = actor_depth_bin(depth_bounds.furthest_q16);
                (void)actor_primitive_ref(source, span.primitive_offset + local,
                                          &primitive);
                ref.meshlet_id = meshlet;
                ref.primitive_id = primitive;
                /* Keep the frozen static-Mario output byte contract.  Queue
                 * records have their primitive ID in a separate immutable
                 * field, so their low sort bits can carry the one admission
                 * fact needed for final lowering without another bank walk. */
                ref.sort_key = ((uint32_t)bin << 16) |
                    (source->kind == ACTOR_MESHLET_SOURCE_MARIO
                        ? primitive : 0U);
                if (span.opacity != 0U)
                    translucent_output[translucent_cursor[bin]++] = ref;
                else
                    opaque_output[opaque_cursor[bin]++] = ref;
            }
        }
        SM64_SATURN_PRENOTIFY_PROFILE_POP();
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

static bool actor_mario_prepare_with_carry(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats, actor_meshlet_depth_carry_t *carry)
{
    uint8_t position_seen[SM64_MARIO_VERTEX_COUNT];
    actor_meshlet_source_t source = actor_mario_source();
    actor_meshlet_transform_t transform;
    bool admitted;
    actor_output_reset(output);
    if (snapshot == NULL || pose == NULL || output == NULL ||
        snapshot->generation == 0U || snapshot->actor_generation == 0U ||
        snapshot->mario.valid == 0U || pose->vertices == NULL ||
        pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return false;
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_MESHLET_PREPARE);
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
    admitted = actor_meshlet_core(&source, &transform, view, position_seen,
                                  SM64_MARIO_VERTEX_COUNT, output, NULL,
                                  capacity, false, NULL, stats, carry);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    return admitted;
}

bool sm64_saturn_actor_meshlets_prepare(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats)
{
    return actor_mario_prepare_with_carry(snapshot, pose, view, output,
                                          capacity, stats,
                                          &s_mario_depth_carry);
}

bool sm64_saturn_actor_meshlets_prepare_bank(
    const sm64_saturn_actor_bank_view_t *bank,
    const sm64_saturn_actor_instance_snapshot_t *instance,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_pose_work_t *pose_work,
    sm64_saturn_actor_meshlet_bank_output_t *output,
    sm64_saturn_fast3d_profile_t *stats)
{
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
        bank->bank.magic != SM64_SATURN_ACTOR_BANK_MAGIC ||
        !actor_bank_version_supported(bank->bank.version) ||
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
    if (instance->family_id != bank->bank.family_id ||
        instance->model_id != bank->bank.model_id ||
        !actor_hash_equal(instance->actor_bank_hash_words,
                          bank->bank.source_hash_words)) {
        output->quarantine_reason =
            SM64_SATURN_ACTOR_MESHLET_QUARANTINE_STALE_BANK;
        return false;
    }
    if (instance->active == 0U || instance->render_active == 0U ||
        instance->scale_q16[0] == 0 || instance->scale_q16[1] == 0 ||
        instance->scale_q16[2] == 0 || pose_work->light_intensity == NULL ||
        pose_work->light_capacity < bank->bank.vertex_count ||
        pose_work->joint_matrices_q16 == NULL ||
        !actor_bank_source(bank, &source) ||
        !sm64_saturn_actor_pose_evaluate(
            bank, instance->animation_id, instance->animation_frame,
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
            true, &failure_reason, stats, NULL)) {
        output->quarantine_reason = failure_reason;
        return false;
    }
    output->quarantine_reason =
        SM64_SATURN_ACTOR_MESHLET_QUARANTINE_NONE;
    return true;
}

#if defined(SM64_SATURN_ACTOR_MESHLET_DEPTH_REFERENCE)
/* T2.6 step 1 oracle.  Test-only.  The shipped Mario entry point serves pass
 * 2 from pass 1's carried depth bounds; this one forces the pre-T2.6 two-walk
 * behaviour by withholding the carry, so the host test can require that the
 * carried and the freshly recomputed emission produce identical output bytes
 * rather than merely asserting that they should. */
bool sm64_saturn_actor_meshlets_prepare_recompute(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats);

bool sm64_saturn_actor_meshlets_prepare_recompute(
    const sm64_saturn_render_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
    sm64_saturn_fast3d_profile_t *stats)
{
    return actor_mario_prepare_with_carry(snapshot, pose, view, output,
                                          capacity, stats, NULL);
}

/* T2.6 equivalence probe.  Test-only: no Saturn image defines this macro, so
 * this entry point exists only in tools/saturn/actor_meshlet_test.c's link.
 * It reaches the two file-private per-vertex depth implementations through
 * plain scalars so the oracle does not need the private transform/source
 * types.  Same in-tree convention as the T2.3 painter-chain reference and
 * saturn_terrain_depth_bins.h's merge oracle. */
void sm64_saturn_actor_meshlet_depth_probe(
    const int16_t vertex[3], const int32_t scale_q16[3], int16_t yaw,
    const int64_t position_q16[3], const int32_t camera_position_q16[3],
    const int32_t forward_q16[3], int64_t *reference_depth,
    int64_t *candidate_depth, uint8_t *fast_taken);

void sm64_saturn_actor_meshlet_depth_probe(
    const int16_t vertex[3], const int32_t scale_q16[3], int16_t yaw,
    const int64_t position_q16[3], const int32_t camera_position_q16[3],
    const int32_t forward_q16[3], int64_t *reference_depth,
    int64_t *candidate_depth, uint8_t *fast_taken)
{
    actor_meshlet_transform_t transform;
    sm64_saturn_render_view_t view;
    actor_depth_kernel_t kernel;
    memset(&transform, 0, sizeof(transform));
    memset(&view, 0, sizeof(view));
    for (uint16_t axis = 0U; axis < 3U; axis++) {
        transform.position_q16[axis] = position_q16[axis];
        transform.scale_q16[axis] = scale_q16[axis];
        view.camera_position_q16[axis] = camera_position_q16[axis];
        view.view_forward_q16[axis] = forward_q16[axis];
    }
    transform.yaw = yaw;
    view.generation = 1U;
    actor_depth_kernel_prepare(&transform, &view, &kernel);
    if (reference_depth != NULL)
        *reference_depth = actor_depth_reference(vertex, &transform, &view,
                                                 kernel.sine, kernel.cosine);
    if (fast_taken != NULL) *fast_taken = kernel.fast ? 1U : 0U;
    /* Exactly the dispatch actor_meshlet_live_depth_bounds() performs, so the
     * oracle measures what ships rather than a restatement of it. */
    if (candidate_depth != NULL)
        *candidate_depth = kernel.fast
            ? actor_depth_fast(vertex, &kernel)
            : actor_depth_reference(vertex, &transform, &view, kernel.sine,
                                    kernel.cosine);
}
#endif
