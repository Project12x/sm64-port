#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <yaul.h>

#include "saturn_ir_transform.h"

typedef struct sm64_saturn_dual_smoke_result {
    uint16_t magic;
    uint16_t version;
    uint16_t size;
    uint16_t status;
    uint16_t fixture_count;
    uint16_t mismatch_index;
    uint16_t master_cpu_seen;
    uint16_t slave_cpu_seen;
} sm64_saturn_dual_smoke_result_t;

#define SM64_SATURN_DUAL_SMOKE_MAGIC 0x4453U /* `DS` */
#define SM64_SATURN_DUAL_SMOKE_VERSION 1U
#define SM64_SATURN_DUAL_SMOKE_COUNT 16U
#define SM64_SATURN_DUAL_SMOKE_PASS 1U

static sm64_saturn_ir_transform_job_t smoke_job __uncached;
static sm64_saturn_vec3i_t smoke_world[SM64_SATURN_DUAL_SMOKE_COUNT] __uncached;
static sm64_saturn_vec3i_t smoke_view[SM64_SATURN_DUAL_SMOKE_COUNT] __uncached;
static sm64_saturn_projected_vertex_t
    smoke_projected[SM64_SATURN_DUAL_SMOKE_COUNT] __uncached;
static sm64_saturn_vec3i_t smoke_expected_view[SM64_SATURN_DUAL_SMOKE_COUNT];
static sm64_saturn_projected_vertex_t
    smoke_expected_projected[SM64_SATURN_DUAL_SMOKE_COUNT];
static volatile uint16_t smoke_slave_done __uncached;

volatile sm64_saturn_dual_smoke_result_t saturn_dual_smoke_result;

static void smoke_slave_entry(void)
{
    saturn_dual_smoke_result.slave_cpu_seen =
        cpu_dual_executor_get() == CPU_SLAVE ? 1U : 0U;
    (void)sm64_saturn_ir_transform_batch(
        &smoke_job, smoke_world, smoke_view, smoke_projected,
        SM64_SATURN_DUAL_SMOKE_COUNT);
    smoke_slave_done = 1U;
    cpu_dual_master_notify();
}

void user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
        VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
        RGB1555(1, 0, 0, 4));
    vdp2_tvmd_display_set();
}

int main(void)
{
    smoke_job = (sm64_saturn_ir_transform_job_t){
        .camera = {
            .position = {13, -7, 19},
            .right = {65536, 0, 0},
            .up = {0, 65536, 0},
            .forward = {0, 0, 65536}
        },
        .focal_length = 256,
        .near_depth = 128,
        .center_x = 160,
        .center_y = 112,
        .coord_min = -1024,
        .coord_max = 1023
    };
    for (uint16_t i = 0; i < SM64_SATURN_DUAL_SMOKE_COUNT; i++) {
        smoke_world[i] = (sm64_saturn_vec3i_t){
            (int32_t)i * 37 - 211, (int32_t)i * 19 - 143,
            256 + (int32_t)i * 23
        };
    }

    saturn_dual_smoke_result.magic = SM64_SATURN_DUAL_SMOKE_MAGIC;
    saturn_dual_smoke_result.version = SM64_SATURN_DUAL_SMOKE_VERSION;
    saturn_dual_smoke_result.size = sizeof(saturn_dual_smoke_result);
    saturn_dual_smoke_result.fixture_count = SM64_SATURN_DUAL_SMOKE_COUNT;
    saturn_dual_smoke_result.mismatch_index = 0xFFFFU;
    saturn_dual_smoke_result.master_cpu_seen =
        cpu_dual_executor_get() == CPU_MASTER ? 1U : 0U;
    (void)sm64_saturn_ir_transform_batch(
        &smoke_job, smoke_world, smoke_expected_view,
        smoke_expected_projected, SM64_SATURN_DUAL_SMOKE_COUNT);

    /* Polling is the deliberately boring smoke path: it exercises the same
     * Yaul slave-entry trampoline without depending on FRT ICI timing. */
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(smoke_slave_entry);
    cpu_dual_slave_notify();
    while (smoke_slave_done == 0U) {
    }

    saturn_dual_smoke_result.status = SM64_SATURN_DUAL_SMOKE_PASS;
    for (uint16_t i = 0; i < SM64_SATURN_DUAL_SMOKE_COUNT; i++) {
        if (memcmp(&smoke_expected_view[i], &smoke_view[i],
                   sizeof(smoke_view[i])) != 0 ||
            memcmp(&smoke_expected_projected[i], &smoke_projected[i],
                   sizeof(smoke_projected[i])) != 0) {
            saturn_dual_smoke_result.status = 0U;
            saturn_dual_smoke_result.mismatch_index = i;
            break;
        }
    }

    for (;;) {
    }
    return 0;
}
