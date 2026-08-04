#include <stdint.h>
#include <stdio.h>

#include "../../src/port/saturn/gfx/saturn_terrain_depth_bins.h"

static int expect(int condition, const char *message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

int main(void)
{
    sm64_saturn_terrain_result_t records_a[2] = {0};
    sm64_saturn_terrain_result_t records_b[1] = {0};
    uint8_t commands_a[2][SM64_SATURN_TERRAIN_COMMAND_BYTES] = {{0}};
    uint8_t commands_b[1][SM64_SATURN_TERRAIN_COMMAND_BYTES] = {{0}};
    records_a[0].primitive_id = 10U;
    records_a[0].corner_count = 4U;
    records_a[0].painter_key = 128U;
    records_a[1].primitive_id = 11U;
    records_a[1].corner_count = 4U;
    records_a[1].painter_key = 512U;
    records_b[0].primitive_id = 12U;
    records_b[0].corner_count = 4U;
    records_b[0].painter_key = 384U;
    commands_a[0][0] = 0xA0U;
    commands_a[1][0] = 0xA1U;
    commands_b[0][0] = 0xB0U;
    const sm64_saturn_terrain_result_t *const records[] = {
        records_a, records_b};
    const uint8_t *const commands[] = {
        &commands_a[0][0], &commands_b[0][0]};
    const size_t counts[] = {2U, 1U};
    sm64_saturn_terrain_emit_ref_t refs[3];
    sm64_saturn_terrain_emit_ref_t scratch[3];

    const size_t count = sm64_saturn_terrain_depth_bins_build_command_streams(
        records, commands, counts, 2U, refs, scratch, 3U);
    if (!expect(count == 3U, "three records must remain in the final stream") ||
        !expect(refs[0].record == &records_a[1] && refs[0].command[0] == 0xA1U,
                "farthest result must retain its descriptor-local command") ||
        !expect(refs[1].record == &records_b[0] && refs[1].command[0] == 0xB0U,
                "middle result must retain its cross-stream command") ||
        !expect(refs[2].record == &records_a[0] && refs[2].command[0] == 0xA0U,
                "nearest result must retain its descriptor-local command"))
        return 1;

    const uint8_t *bad_commands[] = {&commands_a[0][0], NULL};
    if (!expect(sm64_saturn_terrain_depth_bins_build_command_streams(
                    records, bad_commands, counts, 2U, refs, scratch, 3U) ==
                    SIZE_MAX,
                "non-empty descriptor stream without commands must fail"))
        return 1;
    puts("terrain command stream fixture: PASS");
    return 0;
}
