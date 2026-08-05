#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "slavedriver_dual_worker.h"
#include "saturn_dual_frame_bank.h"
#include "saturn_mario_actor_mesh.h"

typedef struct actor_vertex {
    int16_t x;
    int16_t y;
    int16_t z;
} actor_vertex_t;

typedef struct actor_command {
    uint16_t primitive;
    uint16_t color;
    actor_vertex_t corners[4];
} actor_command_t;

static void transform_range(actor_vertex_t *out, uint16_t begin, uint16_t end)
{
    for (uint16_t i = begin; i < end; i++) {
        out[i].x = (int16_t)(sm64_mario_vertices[i][0] + 17);
        out[i].y = (int16_t)(sm64_mario_vertices[i][1] - 23);
        out[i].z = (int16_t)(sm64_mario_vertices[i][2] + 101);
    }
}

static uint16_t classify(const actor_vertex_t *vertices, actor_command_t *out)
{
    uint16_t count = 0U;
    for (uint16_t primitive = 0U;
         primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        const uint16_t *indices = sm64_mario_primitives[primitive];
        actor_command_t *command = &out[count++];
        command->primitive = primitive;
        command->color = (uint16_t)((sm64_mario_material_rgb[indices[0]][0] << 10) |
                                    (sm64_mario_material_rgb[indices[0]][1] << 5) |
                                    sm64_mario_material_rgb[indices[0]][2]);
        for (uint8_t corner = 0U; corner < 4U; corner++)
            command->corners[corner] = vertices[indices[corner + 1U]];
    }
    return count;
}

typedef struct count_context {
    volatile LONG master_count;
    volatile LONG slave_count;
} count_context_t;

typedef struct retirement_probe {
    HANDLE slave_entered;
    volatile LONG cancel_seen;
    volatile LONG worker_writes;
    volatile LONG fallback_started;
    volatile LONG writes_after_fallback;
} retirement_probe_t;

static void count_callback(void *opaque, uint16_t begin, uint16_t end)
{
    count_context_t *count = opaque;
    volatile LONG *const slot = begin == 0U ? &count->master_count :
        &count->slave_count;
    InterlockedExchange(slot, (LONG)(end - begin));
}

static void deterministic_cancel_callback(void *opaque, uint16_t begin,
                                          uint16_t end)
{
    (void)end;
    retirement_probe_t *probe = opaque;
    if (begin == 0U) {
        if (WaitForSingleObject(probe->slave_entered, INFINITE) != WAIT_OBJECT_0)
            return;
        sm64_saturn_dual_worker_test_force_timeout(true);
        return;
    }
    SetEvent(probe->slave_entered);
    while (!sm64_saturn_dual_worker_cancelled()) SwitchToThread();
    InterlockedExchange(&probe->cancel_seen, 1L);
    if (probe->fallback_started != 0L)
        InterlockedIncrement(&probe->writes_after_fallback);
}

static int worker_context_has_no_live_game_pointers(void)
{
    FILE *source = fopen("src/port/saturn/gfx/saturn_demo_render.c", "rb");
    if (source == NULL) return 0;
    if (fseek(source, 0L, SEEK_END) != 0) return fclose(source), 0;
    const long bytes = ftell(source);
    if (bytes <= 0L || fseek(source, 0L, SEEK_SET) != 0) return fclose(source), 0;
    char *text = malloc((size_t)bytes + 1U);
    if (text == NULL) return fclose(source), 0;
    const size_t read = fread(text, 1U, (size_t)bytes, source);
    fclose(source);
    text[read] = '\0';
    const int has_compact_worker_refs =
        strstr(text, "typedef struct demo_actor_primitive_ref") != NULL &&
        strstr(text, "static void demo_classify_mario_range") != NULL &&
        strstr(text, "s_actor_ref_frame_bank") != NULL &&
        strstr(text, "demo_actor_ref_read") != NULL;
    const char *const context = strstr(text, "typedef struct demo_mario_transform_context");
    char *const end = context == NULL ? NULL :
        strstr(context, "} demo_mario_transform_context_t;");
    if (end != NULL)
        end[strlen("} demo_mario_transform_context_t;")] = '\0';
    const int valid = has_compact_worker_refs && end != NULL &&
        strstr(context, "sm64_saturn_mario_actor_snapshot_t snapshot;") != NULL &&
        strstr(context, "int16_t vertices[SM64_MARIO_VERTEX_COUNT][3];") != NULL &&
        strstr(context, "const uint16_t (*primitives)[5];") != NULL &&
        strstr(context, "const uint8_t (*material_rgb)[3];") != NULL &&
        strstr(context, "const sm64_saturn_mario_actor_snapshot_t *") == NULL &&
        strstr(context, "MarioState") == NULL && strstr(context, "gMario") == NULL &&
        strstr(context, "GraphNode") == NULL && strstr(context, "vdp1_") == NULL;
    free(text);
    return valid;
}

static int all_master_ref_fallback_uses_cached_owner(void)
{
    for (uint16_t primitive = 0U;
         primitive < SM64_MARIO_PRIMITIVE_COUNT; primitive++) {
        if (sm64_saturn_dual_frame_owner_for_split(
                primitive, SM64_MARIO_PRIMITIVE_COUNT) != 0U)
            return 0;
    }
    return sm64_saturn_dual_frame_owner_for_split(
               SM64_MARIO_PRIMITIVE_COUNT / 2U,
               SM64_MARIO_PRIMITIVE_COUNT / 2U) == 1U;
}

static int text_range_contains(const char *begin, const char *end,
                               const char *needle)
{
    const size_t length = strlen(needle);
    if (begin == NULL || end == NULL || needle == NULL || begin > end) return 0;
    for (const char *cursor = begin; cursor + length <= end; cursor++)
        if (memcmp(cursor, needle, length) == 0) return 1;
    return 0;
}

static int renderer_uses_bounded_meshlet_order(void)
{
    FILE *source = fopen("src/port/saturn/gfx/saturn_demo_render.c", "rb");
    if (source == NULL) return 0;
    if (fseek(source, 0L, SEEK_END) != 0) return fclose(source), 0;
    const long bytes = ftell(source);
    if (bytes <= 0L || fseek(source, 0L, SEEK_SET) != 0) return fclose(source), 0;
    char *text = malloc((size_t)bytes + 1U);
    if (text == NULL) return fclose(source), 0;
    const size_t read = fread(text, 1U, (size_t)bytes, source);
    fclose(source);
    text[read] = '\0';
    char *const prepare = strstr(text, "static uint16_t demo_prepare_mario");
    char *const reserve = prepare == NULL ? NULL :
        strstr(prepare, "static void demo_reserve_mario_gouraud");
    char *const dispatch = strstr(text, "static void demo_dispatch_mario_transform");
    char *const dispatch_end = dispatch == NULL ? NULL :
        strstr(dispatch, "static uint16_t demo_prepare_mario");
    char *const frame = strstr(text, "bool sm64_saturn_demo_render_frame");
    char *const prepare_call = frame == NULL ? NULL :
        strstr(frame, "demo_prepare_mario(");
    char *const queue_merge = frame == NULL ? NULL :
        strstr(frame, "demo_actor_queue_assemble_done(");
    char *const dispatch_call = frame == NULL ? NULL :
        strstr(frame, "demo_dispatch_mario_transform(");
    const int valid = prepare != NULL && reserve != NULL && dispatch != NULL &&
        dispatch_end != NULL &&
        prepare_call != NULL && queue_merge != NULL &&
        prepare_call < queue_merge && dispatch_call == NULL &&
        strstr(prepare, "sm64_saturn_actor_meshlets_prepare(") != NULL &&
        text_range_contains(prepare, reserve,
                            ".positions = s_actor_transform_refs") &&
        text_range_contains(prepare, reserve,
                            "s_actor_transform_ref_count = meshlet_output.position_count") &&
        !text_range_contains(prepare, reserve, "primitive[corner]") &&
        strstr(text, "s_actor_order") == NULL &&
        strstr(text, "while (j > 0U)") == NULL &&
        strstr(dispatch, "transform_ref_count") != NULL &&
        !text_range_contains(dispatch, dispatch_end,
                             "i < SM64_MARIO_VERTEX_COUNT");
    free(text);
    return valid;
}

int main(void)
{
    actor_vertex_t serial_vertices[SM64_MARIO_VERTEX_COUNT];
    actor_vertex_t split_vertices[SM64_MARIO_VERTEX_COUNT];
    actor_command_t serial_commands[SM64_MARIO_PRIMITIVE_COUNT];
    actor_command_t split_commands[SM64_MARIO_PRIMITIVE_COUNT];
    sm64_saturn_dual_worker_stats_t worker_stats;
    count_context_t callback_count = {0};
    retirement_probe_t retirement_probe = {
        .slave_entered = CreateEvent(NULL, TRUE, FALSE, NULL)};

    if (retirement_probe.slave_entered == NULL) {
        fprintf(stderr, "could not create deterministic cancellation barrier\n");
        return 1;
    }

    if (!sm64_saturn_dual_worker_is_idle()) {
        fprintf(stderr, "fresh worker must be idle\n");
        return 1;
    }
    const int worker_completed = sm64_saturn_dual_worker_run(
        count_callback, &callback_count, 4U, 2U, &worker_stats);
    if (!worker_completed || worker_stats.slave_timeouts != 0U ||
        callback_count.master_count != 2L || callback_count.slave_count != 2L) {
        fprintf(stderr, "host worker split completion contract failed\n");
        return 1;
    }
    if (sm64_saturn_dual_worker_run(deterministic_cancel_callback, &retirement_probe,
                                    2U, 1U, &worker_stats) ||
        worker_stats.slave_timeouts != 1U ||
        retirement_probe.cancel_seen == 0L ||
        retirement_probe.worker_writes != 0L ||
        !sm64_saturn_dual_worker_is_idle()) {
        fprintf(stderr, "cancel must retire the delayed slave before fallback\n");
        return 1;
    }
    InterlockedExchange(&retirement_probe.fallback_started, 1L);
    if (retirement_probe.worker_writes != 0L ||
        retirement_probe.writes_after_fallback != 0L) {
        fprintf(stderr, "worker wrote after fallback began\n");
        return 1;
    }
    CloseHandle(retirement_probe.slave_entered);
    if (!worker_context_has_no_live_game_pointers()) {
        fprintf(stderr, "actor worker context exposes live game or VDP state\n");
        return 1;
    }
    if (!all_master_ref_fallback_uses_cached_owner()) {
        fprintf(stderr, "fallback compact refs can still select a stale peer alias\n");
        return 1;
    }
    if (!renderer_uses_bounded_meshlet_order()) {
        fprintf(stderr, "accepted Mario path retains a full transform or insertion sort\n");
        return 1;
    }
    transform_range(serial_vertices, 0U, SM64_MARIO_VERTEX_COUNT);
    transform_range(split_vertices, 0U, SM64_MARIO_VERTEX_COUNT / 2U);
    transform_range(split_vertices, SM64_MARIO_VERTEX_COUNT / 2U,
                    SM64_MARIO_VERTEX_COUNT);
    if (memcmp(serial_vertices, split_vertices, sizeof(serial_vertices)) != 0) {
        fprintf(stderr, "Mario vertex transform differs between serial and split roles\n");
        return 1;
    }
    const uint16_t serial_count = classify(serial_vertices, serial_commands);
    const uint16_t split_count = classify(split_vertices, split_commands);
    if (serial_count != SM64_MARIO_PRIMITIVE_COUNT ||
        split_count != serial_count ||
        memcmp(serial_commands, split_commands, sizeof(serial_commands)) != 0) {
        fprintf(stderr, "Mario primitive order, colors, or coordinates differ\n");
        return 1;
    }
    puts("dual actor worker fixture: PASS");
    return 0;
}
