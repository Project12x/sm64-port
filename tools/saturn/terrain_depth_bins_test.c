#include <assert.h>
#include <stdint.h>
#include <string.h>

/* The reference merge is host-only.  The target renderer only links the
 * fixed-bin path. */
#define SM64_SATURN_TERRAIN_DEPTH_BINS_COMPARE 1
#include "saturn_terrain_fused.h"

enum { TEST_CAPACITY = 16U };

static sm64_saturn_terrain_result_t record(uint32_t depth, uint16_t leaf,
                                           uint16_t primitive)
{
    return (sm64_saturn_terrain_result_t){
        .primitive_id = primitive,
        .command_index = 0U,
        .painter_key = depth,
        .bsp_leaf = leaf,
        .corner_count = 4U,
        .clip_class = 0U,
    };
}

static void assert_same_stream(const sm64_saturn_terrain_result_t *records,
                               size_t count)
{
    sm64_saturn_terrain_emit_ref_t bins[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t scratch[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t reference[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t reference_scratch[TEST_CAPACITY];
    const size_t actual = sm64_saturn_terrain_depth_bins_build(
        records, count, bins, scratch, TEST_CAPACITY);
    const size_t expected = sm64_saturn_terrain_depth_bins_reference_merge(
        records, count, reference, reference_scratch, TEST_CAPACITY);
    assert(actual == count);
    assert(expected == count);
    for (size_t i = 0U; i < count; i++) {
        assert(bins[i].record == reference[i].record);
        assert(bins[i].sort_key == reference[i].sort_key);
    }
}

static void test_empty_stream(void)
{
    sm64_saturn_terrain_emit_ref_t refs[1];
    sm64_saturn_terrain_emit_ref_t scratch[1];
    assert(sm64_saturn_terrain_depth_bins_build(NULL, 0U, refs, scratch, 1U)
           == 0U);
}

static void test_one_record(void)
{
    const sm64_saturn_terrain_result_t records[] = {record(512U, 3U, 9U)};
    assert_same_stream(records, 1U);
}

static void test_equal_keys_preserve_producer_order(void)
{
    const sm64_saturn_terrain_result_t records[] = {
        record(2048U, 7U, 42U), record(2048U, 7U, 42U),
        record(2048U, 7U, 42U),
    };
    sm64_saturn_terrain_emit_ref_t refs[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t scratch[TEST_CAPACITY];
    assert(sm64_saturn_terrain_depth_bins_build(records, 3U, refs, scratch,
                                                TEST_CAPACITY) == 3U);
    for (size_t i = 0U; i < 3U; i++)
        assert(refs[i].record == &records[i]);
    assert_same_stream(records, 3U);
}

static void test_reverse_depth_is_far_to_near(void)
{
    const sm64_saturn_terrain_result_t records[] = {
        record(128U, 0U, 1U), record(4096U, 0U, 2U),
        record(8192U, 0U, 3U),
    };
    sm64_saturn_terrain_emit_ref_t refs[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t scratch[TEST_CAPACITY];
    assert(sm64_saturn_terrain_depth_bins_build(records, 3U, refs, scratch,
                                                TEST_CAPACITY) == 3U);
    assert(refs[0].record == &records[2]);
    assert(refs[1].record == &records[1]);
    assert(refs[2].record == &records[0]);
    assert_same_stream(records, 3U);
}

static void test_clipped_fan_siblings_sort_by_leaf_and_primitive(void)
{
    const sm64_saturn_terrain_result_t records[] = {
        record(3000U, 9U, 18U), record(3000U, 4U, 99U),
        record(3000U, 4U, 17U), record(3000U, 4U, 17U),
    };
    sm64_saturn_terrain_emit_ref_t refs[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t scratch[TEST_CAPACITY];
    assert(sm64_saturn_terrain_depth_bins_build(records, 4U, refs, scratch,
                                                TEST_CAPACITY) == 4U);
    assert(refs[0].record == &records[2]);
    assert(refs[1].record == &records[3]);
    assert(refs[2].record == &records[1]);
    assert(refs[3].record == &records[0]);
    assert_same_stream(records, 4U);
}

static void test_mixed_master_slave_streams(void)
{
    const sm64_saturn_terrain_result_t master[] = {
        record(1024U, 2U, 8U), record(7168U, 3U, 4U),
    };
    const sm64_saturn_terrain_result_t slave[] = {
        record(7168U, 1U, 10U), record(1024U, 2U, 8U),
    };
    const sm64_saturn_terrain_result_t *streams[] = {master, slave};
    const size_t counts[] = {2U, 2U};
    sm64_saturn_terrain_emit_ref_t refs[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t scratch[TEST_CAPACITY];
    assert(sm64_saturn_terrain_depth_bins_build_streams(
               streams, counts, 2U, refs, scratch, TEST_CAPACITY) == 4U);
    assert(refs[0].record == &slave[0]);
    assert(refs[1].record == &master[1]);
    assert(refs[2].record == &master[0]);
    assert(refs[3].record == &slave[1]);

    sm64_saturn_terrain_result_t combined[4];
    memcpy(combined, master, sizeof(master));
    memcpy(combined + 2U, slave, sizeof(slave));
    assert_same_stream(combined, 4U);
}

static void test_master_join_uses_only_published_streams(void)
{
    sm64_saturn_terrain_result_t master[] = {
        record(1024U, 5U, 9U), record(4096U, 4U, 7U),
    };
    sm64_saturn_terrain_result_t slave[] = {
        record(4096U, 3U, 11U), record(1024U, 5U, 9U),
    };
    uint8_t master_commands[2U * SM64_SATURN_TERRAIN_COMMAND_BYTES] = {0};
    uint8_t slave_commands[2U * SM64_SATURN_TERRAIN_COMMAND_BYTES] = {0};
    sm64_saturn_terrain_result_spans_t spans;
    sm64_saturn_terrain_emit_ref_t refs[TEST_CAPACITY];
    sm64_saturn_terrain_emit_ref_t scratch[TEST_CAPACITY];
    master[1].command_index = 1U;
    slave[1].command_index = 1U;
    sm64_saturn_terrain_result_spans_init(
        &spans, master, master_commands, 2U, slave, slave_commands, 2U, 0U);
    spans.master.count = 2U;
    spans.slave.count = 2U;
    sm64_saturn_terrain_result_arena_seal(&spans.master, 23U);
    sm64_saturn_terrain_result_arena_seal(&spans.slave, 23U);
    assert(sm64_saturn_terrain_depth_bins_visible(
               &spans, 23U, refs, scratch, TEST_CAPACITY) == 4U);
    assert(refs[0].record == &slave[0]);
    assert(refs[1].record == &master[1]);
    assert(refs[2].record == &master[0]);
    assert(refs[3].record == &slave[1]);
    spans.slave.published_sequence = 0U;
    assert(sm64_saturn_terrain_depth_bins_visible(
               &spans, 23U, refs, scratch, TEST_CAPACITY) == SIZE_MAX);
}

int main(void)
{
    test_empty_stream();
    test_one_record();
    test_equal_keys_preserve_producer_order();
    test_reverse_depth_is_far_to_near();
    test_clipped_fan_siblings_sort_by_leaf_and_primitive();
    test_mixed_master_slave_streams();
    test_master_join_uses_only_published_streams();
    return 0;
}
