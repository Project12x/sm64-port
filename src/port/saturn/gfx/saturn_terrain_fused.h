#ifndef SM64_SATURN_TERRAIN_FUSED_H
#define SM64_SATURN_TERRAIN_FUSED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "saturn_terrain_command_template.h"
#include "saturn_terrain_depth_bins.h"

_Static_assert(SM64_SATURN_TERRAIN_COMMAND_BYTES ==
                   SM64_SATURN_VDP1_COMMAND_BYTES,
               "private terrain image must match one VDP1 command");

static inline const uint8_t *sm64_saturn_terrain_emit_ref_command(
    const sm64_saturn_terrain_result_spans_t *spans,
    const sm64_saturn_terrain_emit_ref_t *ref)
{
    if (spans == NULL || ref == NULL || ref->record == NULL)
        return NULL;
    const sm64_saturn_terrain_result_t *const master =
        sm64_saturn_terrain_result_uncached_records(spans->master.records);
    const sm64_saturn_terrain_result_t *const slave =
        sm64_saturn_terrain_result_uncached_records(spans->slave.records);
    const uintptr_t address = (uintptr_t)ref->record;
    const uintptr_t master_begin = (uintptr_t)master;
    const uintptr_t master_end = master_begin +
        (size_t)spans->master.count * sizeof(master[0]);
    const uintptr_t slave_begin = (uintptr_t)slave;
    const uintptr_t slave_end = slave_begin +
        (size_t)spans->slave.count * sizeof(slave[0]);
    const sm64_saturn_terrain_result_arena_t *arena;
    if (address >= master_begin && address < master_end)
        arena = &spans->master;
    else if (address >= slave_begin && address < slave_end)
        arena = &spans->slave;
    else
        return NULL;
    if (ref->record->command_index >= arena->count)
        return NULL;
    return sm64_saturn_terrain_result_uncached_commands(arena->commands) +
        (size_t)ref->record->command_index *
            SM64_SATURN_TERRAIN_COMMAND_BYTES;
}

static inline bool sm64_saturn_terrain_result_write(
    sm64_saturn_terrain_result_arena_t *arena,
    sm64_saturn_terrain_result_t *record, uint8_t *command,
    uint16_t command_index,
    uint16_t primitive_id, uint16_t bsp_leaf, uint32_t painter_key,
    uint8_t corner_count, uint8_t clip_class,
    const sm64_saturn_terrain_resolved_command_t *resolved,
    const int16_t vertices[4][2])
{
    if (arena == NULL || record == NULL || command == NULL ||
        vertices == NULL || command_index >= arena->count ||
        corner_count < 3U || corner_count > 4U)
        return false;
    /* The worker publishes only dynamic patch data.  Immutable VDP1 words
     * remain in the load-resolved template and are copied by the master after
     * the cross-CPU join; no worker may reconstruct material state. */
    const bool patched = resolved != NULL;
    memset(command, 0, SM64_SATURN_TERRAIN_COMMAND_BYTES);
    memcpy(command + 12U, vertices, 8U * sizeof(int16_t));
    record->primitive_id = primitive_id;
    record->command_index = command_index;
    record->painter_key = painter_key;
    record->bsp_leaf = bsp_leaf;
    record->corner_count = (uint8_t)(corner_count |
        (patched ? SM64_SATURN_TERRAIN_RESULT_TEMPLATE_PATCHED : 0U));
    record->clip_class = clip_class;
    return sm64_saturn_terrain_result_commit(arena, record);
}

static inline bool sm64_saturn_terrain_result_publish(
    sm64_saturn_terrain_result_arena_t *arena,
    uint16_t primitive_id, uint16_t bsp_leaf, uint32_t painter_key,
    uint8_t corner_count, uint8_t clip_class,
    const sm64_saturn_terrain_resolved_command_t *resolved,
    const int16_t vertices[4][2])
{
    sm64_saturn_terrain_result_t *record = NULL;
    uint8_t *command = NULL;
    if (!sm64_saturn_terrain_result_reserve(
            arena, 1U, &record, &command))
        return false;
    return sm64_saturn_terrain_result_write(
        arena, record, command, (uint16_t)(arena->count - 1U),
        primitive_id, bsp_leaf, painter_key, corner_count, clip_class,
        resolved, vertices);
}

/* Master-only join of the two immutable worker spans.  The only ordering
 * algorithm reachable from the target is the fixed 64-bin radix stream;
 * the historical comparison merge is compiled only by the host fixture. */
static inline size_t sm64_saturn_terrain_depth_bins_visible(
    const sm64_saturn_terrain_result_spans_t *spans,
    uint32_t expected_sequence,
    sm64_saturn_terrain_emit_ref_t *refs,
    sm64_saturn_terrain_emit_ref_t *scratch,
    size_t capacity)
{
    if (spans == NULL || refs == NULL || scratch == NULL ||
        expected_sequence == 0U ||
        spans->master.published_sequence != expected_sequence ||
        spans->slave.published_sequence != expected_sequence)
        return SIZE_MAX;
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif

    const size_t master_count = spans->master.count;
    const size_t slave_count = spans->slave.count;
    if (master_count > spans->master.capacity ||
        slave_count > spans->slave.capacity ||
        master_count > capacity || slave_count > capacity - master_count)
        return SIZE_MAX;

    const sm64_saturn_terrain_result_t *const lane_records[2] = {
        sm64_saturn_terrain_result_uncached_records(spans->master.records),
        sm64_saturn_terrain_result_uncached_records(spans->slave.records)};
    const uint8_t *const lane_commands[2] = {
        sm64_saturn_terrain_result_uncached_commands(spans->master.commands),
        sm64_saturn_terrain_result_uncached_commands(spans->slave.commands)};
    const size_t lane_counts[2] = {master_count, slave_count};

    for (uint8_t lane = 0U; lane < 2U; lane++) {
        if ((lane_counts[lane] != 0U) &&
            (lane_records[lane] == NULL || lane_commands[lane] == NULL))
            return SIZE_MAX;
        for (size_t i = 0U; i < lane_counts[lane]; i++) {
            const sm64_saturn_terrain_result_t *record = &lane_records[lane][i];
            if (!sm64_saturn_terrain_result_validate(record) ||
                record->command_index >= lane_counts[lane])
                return SIZE_MAX;
        }
    }
    return sm64_saturn_terrain_depth_bins_build_streams(
        lane_records, lane_counts, 2U, refs, scratch, capacity);
}

#endif
