/*
 * Fixed terrain painter bins.
 *
 * The PS1 renderer research is pattern-only: it motivates bounded ordering
 * tables, but this is a project-owned VDP1 stream over SlaveDriver-style
 * master/slave result arenas.  The master remains the sole owner of the
 * resulting command order.
 */
#ifndef SM64_SATURN_TERRAIN_DEPTH_BINS_H
#define SM64_SATURN_TERRAIN_DEPTH_BINS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../gpl/slavedriver_terrain_result.h"

#define SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT 64U
#define SM64_SATURN_TERRAIN_DEPTH_BIN_SHIFT 7U

typedef struct sm64_saturn_terrain_emit_ref {
    const sm64_saturn_terrain_result_t *record;
    union {
        uint32_t sort_key;
        const uint8_t *command;
    };
} sm64_saturn_terrain_emit_ref_t;

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(sm64_saturn_terrain_emit_ref_t) == 8U,
               "SH-2 terrain bin reference must remain eight bytes");
#endif

/* painter_key is an unsigned view-space depth.  The fixed 128-unit range
 * gives BOB's 128..8192 interval the complete 64-bin table and clamps wider
 * scenes safely at the far end. */
static inline uint8_t sm64_saturn_terrain_depth_bin(uint32_t painter_key)
{
    const uint32_t bin = painter_key >> SM64_SATURN_TERRAIN_DEPTH_BIN_SHIFT;
    return (uint8_t)(bin >= SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT
        ? SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT - 1U : bin);
}

static inline uint32_t sm64_saturn_terrain_depth_bins_sort_key(
    const sm64_saturn_terrain_result_t *record)
{
    if (record == NULL) return 0U;
    /* Kept for existing diagnostics.  The full ordering contract also uses
     * bsp_leaf and primitive_id in the radix stream below. */
    return ((uint32_t)sm64_saturn_terrain_depth_bin(record->painter_key)
            << 16) |
           (uint16_t)(UINT16_MAX - record->primitive_id);
}

static inline uint8_t sm64_saturn_terrain_depth_bins_digit(
    const sm64_saturn_terrain_emit_ref_t *ref, uint8_t component,
    uint8_t shift)
{
    const sm64_saturn_terrain_result_t *const record = ref->record;
    if (component == 0U)
        return (uint8_t)((record->primitive_id >> shift) & 0x3FU);
    if (component == 1U)
        return (uint8_t)((record->bsp_leaf >> shift) & 0x3FU);
    /* Invert the depth digit so a normal increasing prefix scatter emits
     * far-to-near records. */
    return (uint8_t)((SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT - 1U) -
        sm64_saturn_terrain_depth_bin(record->painter_key));
}

/* One stable radix stage: first count/prefix, then scatter.  Every stage is
 * a fixed 64-bin table; no comparison sort or dynamic allocation enters the
 * target frame path. */
static inline void sm64_saturn_terrain_depth_bins_scatter(
    const sm64_saturn_terrain_emit_ref_t *src,
    sm64_saturn_terrain_emit_ref_t *dst, size_t count, uint8_t component,
    uint8_t shift)
{
    size_t offsets[SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT] = {0U};
    for (size_t i = 0U; i < count; i++)
        offsets[sm64_saturn_terrain_depth_bins_digit(
            &src[i], component, shift)]++;
    size_t prefix = 0U;
    for (uint8_t bin = 0U; bin < SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT; bin++) {
        const size_t bin_count = offsets[bin];
        offsets[bin] = prefix;
        prefix += bin_count;
    }
    for (size_t i = 0U; i < count; i++) {
        const uint8_t bin = sm64_saturn_terrain_depth_bins_digit(
            &src[i], component, shift);
        dst[offsets[bin]++] = src[i];
    }
}

static inline size_t sm64_saturn_terrain_depth_bins_sort_refs(
    sm64_saturn_terrain_emit_ref_t *refs,
    sm64_saturn_terrain_emit_ref_t *scratch, size_t count, size_t capacity)
{
    if (count == 0U) return 0U;
    if (refs == NULL || scratch == NULL || count > capacity) return SIZE_MAX;

    sm64_saturn_terrain_emit_ref_t *src = refs;
    sm64_saturn_terrain_emit_ref_t *dst = scratch;
    /* Least-significant first makes the final depth pass stable over the
     * (bsp_leaf, primitive_id) order.  Three 6-bit passes cover each uint16.
     */
    for (uint8_t shift = 0U; shift < 18U; shift += 6U) {
        sm64_saturn_terrain_depth_bins_scatter(src, dst, count, 0U, shift);
        sm64_saturn_terrain_emit_ref_t *const swap = src;
        src = dst;
        dst = swap;
    }
    for (uint8_t shift = 0U; shift < 18U; shift += 6U) {
        sm64_saturn_terrain_depth_bins_scatter(src, dst, count, 1U, shift);
        sm64_saturn_terrain_emit_ref_t *const swap = src;
        src = dst;
        dst = swap;
    }
    sm64_saturn_terrain_depth_bins_scatter(src, dst, count, 2U, 0U);
    src = dst;
    if (src != refs)
        memcpy(refs, src, count * sizeof(refs[0]));
    return count;
}

static inline size_t sm64_saturn_terrain_depth_bins_build(
    const sm64_saturn_terrain_result_t *records, size_t count,
    sm64_saturn_terrain_emit_ref_t *refs,
    sm64_saturn_terrain_emit_ref_t *scratch, size_t capacity)
{
    if (count == 0U) return 0U;
    if (records == NULL || refs == NULL || scratch == NULL || count > capacity)
        return SIZE_MAX;
    for (size_t i = 0U; i < count; i++) {
        if (!sm64_saturn_terrain_result_validate(&records[i])) return SIZE_MAX;
        refs[i] = (sm64_saturn_terrain_emit_ref_t){
            .record = &records[i],
            .sort_key = sm64_saturn_terrain_depth_bins_sort_key(&records[i]),
        };
    }
    return sm64_saturn_terrain_depth_bins_sort_refs(
        refs, scratch, count, capacity);
}

/* The lane order is the producer order for equal keys: master first, then
 * slave.  No records are copied; the stream carries direct immutable result
 * pointers until the master emits its final VDP1 list. */
static inline size_t sm64_saturn_terrain_depth_bins_build_streams(
    const sm64_saturn_terrain_result_t *const streams[], const size_t counts[],
    size_t stream_count, sm64_saturn_terrain_emit_ref_t *refs,
    sm64_saturn_terrain_emit_ref_t *scratch, size_t capacity)
{
    if (stream_count == 0U) return 0U;
    if (streams == NULL || counts == NULL || refs == NULL || scratch == NULL)
        return SIZE_MAX;
    size_t total = 0U;
    for (size_t stream = 0U; stream < stream_count; stream++) {
        if (counts[stream] > capacity - total ||
            (counts[stream] != 0U && streams[stream] == NULL))
            return SIZE_MAX;
        for (size_t i = 0U; i < counts[stream]; i++) {
            const sm64_saturn_terrain_result_t *const record =
                &streams[stream][i];
            if (!sm64_saturn_terrain_result_validate(record)) return SIZE_MAX;
            refs[total++] = (sm64_saturn_terrain_emit_ref_t){
                .record = record,
                .sort_key = sm64_saturn_terrain_depth_bins_sort_key(record),
            };
        }
    }
    return sm64_saturn_terrain_depth_bins_sort_refs(
        refs, scratch, total, capacity);
}

/* Build one final-order stream while retaining the exact descriptor-local
 * command image paired with every result. Sorting moves the pair together;
 * the master can lower VDP1 commands without reverse-inferencing a legacy
 * master/slave arena from the result pointer. */
static inline size_t sm64_saturn_terrain_depth_bins_build_command_streams(
    const sm64_saturn_terrain_result_t *const records[],
    const uint8_t *const commands[], const size_t counts[],
    size_t stream_count, sm64_saturn_terrain_emit_ref_t *refs,
    sm64_saturn_terrain_emit_ref_t *scratch, size_t capacity)
{
    if (stream_count == 0U) return 0U;
    if (records == NULL || commands == NULL || counts == NULL || refs == NULL ||
        scratch == NULL)
        return SIZE_MAX;
    size_t total = 0U;
    for (size_t stream = 0U; stream < stream_count; stream++) {
        if (counts[stream] > capacity - total ||
            (counts[stream] != 0U &&
             (records[stream] == NULL || commands[stream] == NULL)))
            return SIZE_MAX;
        for (size_t local = 0U; local < counts[stream]; local++) {
            const sm64_saturn_terrain_result_t *const record =
                &records[stream][local];
            if (!sm64_saturn_terrain_result_validate(record)) return SIZE_MAX;
            refs[total++] = (sm64_saturn_terrain_emit_ref_t){
                .record = record,
                .command = commands[stream] +
                    local * SM64_SATURN_TERRAIN_COMMAND_BYTES,
            };
        }
    }
    return sm64_saturn_terrain_depth_bins_sort_refs(
        refs, scratch, total, capacity);
}


#if defined(SM64_SATURN_TERRAIN_DEPTH_BINS_COMPARE)
/* Host-only predecessor stable merge oracle.  It deliberately preserves the
 * prior raw painter-key/primitive comparison rather than reimplementing the
 * binned key, so tests can name the intentional within-bin and leaf-tie
 * semantic differences.  It is not compiled into the target demo path. */
static inline uint32_t sm64_saturn_terrain_depth_bins_predecessor_sort_key(
    const sm64_saturn_terrain_result_t *record)
{
    const uint32_t depth = record->painter_key > UINT16_MAX
        ? UINT16_MAX : record->painter_key;
    return (depth << 16) | (uint16_t)(UINT16_MAX - record->primitive_id);
}

static inline bool sm64_saturn_terrain_depth_bins_predecessor_before(
    const sm64_saturn_terrain_emit_ref_t *left,
    const sm64_saturn_terrain_emit_ref_t *right)
{
    return left->sort_key >= right->sort_key;
}

static inline size_t sm64_saturn_terrain_depth_bins_predecessor_merge(
    const sm64_saturn_terrain_result_t *records, size_t count,
    sm64_saturn_terrain_emit_ref_t *refs,
    sm64_saturn_terrain_emit_ref_t *scratch, size_t capacity)
{
    if (count == 0U) return 0U;
    if (records == NULL || refs == NULL || scratch == NULL || count > capacity)
        return SIZE_MAX;
    for (size_t i = 0U; i < count; i++) {
        if (!sm64_saturn_terrain_result_validate(&records[i])) return SIZE_MAX;
        refs[i] = (sm64_saturn_terrain_emit_ref_t){
            .record = &records[i],
            .sort_key =
                sm64_saturn_terrain_depth_bins_predecessor_sort_key(
                    &records[i]),
        };
    }
    sm64_saturn_terrain_emit_ref_t *src = refs;
    sm64_saturn_terrain_emit_ref_t *dst = scratch;
    for (size_t width = 1U; width < count; width <<= 1U) {
        for (size_t start = 0U; start < count; start += width << 1U) {
            const size_t mid = start + width < count ? start + width : count;
            const size_t end = mid + width < count ? mid + width : count;
            size_t left = start;
            size_t right = mid;
            for (size_t out = start; out < end; out++) {
                const bool take_right = left >= mid ||
                    (right < end &&
                     !sm64_saturn_terrain_depth_bins_predecessor_before(
                        &src[left], &src[right]));
                dst[out] = take_right ? src[right++] : src[left++];
            }
        }
        sm64_saturn_terrain_emit_ref_t *const swap = src;
        src = dst;
        dst = swap;
        if (width > SIZE_MAX / 2U) break;
    }
    if (src != refs) memcpy(refs, src, count * sizeof(refs[0]));
    return count;
}
#endif

#endif
