/* Source contract for the dormant A5.8 terrain queue route.
 *
 * The callback is intentionally not registered with CPU-DUAL yet.  This
 * checks the source-level ownership boundary that must exist before the
 * atomic terrain+Mario cutover: a WORLD_LOWER claimant provides its exact
 * descriptor span, and the compact producer receives that lane explicitly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0L, SEEK_END) != 0) { fclose(file); return NULL; }
    const long length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *text = calloc((size_t)length + 1U, 1U);
    if (text != NULL && fread(text, 1U, (size_t)length, file) !=
                            (size_t)length) {
        free(text);
        text = NULL;
    }
    fclose(file);
    return text;
}

static const char *function_end(const char *start)
{
    const char *cursor = strchr(start, '{');
    unsigned depth = 0U;
    if (cursor == NULL) return NULL;
    for (; *cursor != '\0'; cursor++) {
        if (*cursor == '{') depth++;
        else if (*cursor == '}' && --depth == 0U) return cursor + 1;
    }
    return NULL;
}

static int function_contains(const char *source, const char *name,
                             const char *needle)
{
    const char *start = strstr(source, name);
    const char *end = start == NULL ? NULL : function_end(start);
    const char *found = start == NULL ? NULL : strstr(start, needle);
    return found != NULL && end != NULL && found < end;
}

int main(void)
{
    char *source = read_file("src/port/saturn/gfx/saturn_demo_render.c");
    if (source == NULL) return 2;
    const int ok =
        function_contains(source, "demo_terrain_compact_transformed(",
                          "sm64_saturn_terrain_result_arena_seal") &&
        function_contains(source, "demo_terrain_compact_transformed(",
                          "demo_classify_exact") &&
        !function_contains(source, "demo_terrain_compact_transformed(",
                           "begin == 0U") &&
        !function_contains(source, "demo_classify_exact(",
                           "begin == 0U") &&
        function_contains(source, "demo_classify_exact(",
                          "demo_projected_read(lane,") &&
        !function_contains(source, "demo_classify_exact(",
                           "demo_projected_read(0U,") &&
        function_contains(source, "demo_terrain_queue_world_lower(",
                          "demo_terrain_queue_bind_output") &&
        function_contains(source, "demo_terrain_queue_world_lower(",
                          "demo_terrain_compact_transformed") &&
        function_contains(source, "demo_terrain_queue_world_lower(",
                          "sm64_saturn_render_job_graph_world_lower_admit_done") &&
        function_contains(source, "demo_terrain_queue_world_lower(",
                          "demo_terrain_queue_admit_metadata") &&
        function_contains(source, "demo_terrain_queue_world_lower(",
                          "job->input_offset") &&
        !function_contains(source, "demo_terrain_queue_world_lower(",
                           "s_slave_begin") &&
        !function_contains(source, "demo_terrain_queue_world_lower(",
                           "begin == 0U") &&
        function_contains(source, "demo_terrain_queue_read_done(",
                          "sm64_saturn_render_job_queue_done_job") &&
        function_contains(source, "demo_terrain_queue_read_done(",
                          "sm64_saturn_render_payload_bank_read") &&
        function_contains(source, "demo_terrain_queue_world_admit(",
                          "SM64_SATURN_RENDER_JOB_WORLD_ADMIT") &&
        function_contains(source, "demo_terrain_queue_world_admit(",
                          "demo_terrain_queue_bind_output") &&
        function_contains(source, "demo_terrain_queue_world_admit(",
                          "demo_terrain_queue_publish_admit") &&
        function_contains(source, "demo_terrain_queue_world_lower(",
                          "demo_terrain_queue_publish_result") &&
        function_contains(source, "demo_terrain_queue_read_done(",
                          "demo_terrain_queue_result_metadata") &&
        !function_contains(source, "demo_terrain_queue_read_done(",
                           "uint16_t record_count") &&
        function_contains(source, "demo_terrain_queue_assemble_merge_spans(",
                          "demo_terrain_queue_read_done") &&
        function_contains(source, "demo_terrain_queue_assemble_merge_spans(",
                          "sm64_saturn_render_job_graph_validate_terrain_merge") &&
        function_contains(source, "demo_terrain_queue_assemble_merge_spans(",
                          "sm64_saturn_terrain_depth_bins_build_streams") &&
        function_contains(source, "demo_terrain_queue_assemble_merge_spans(",
                          "metadata->claimed_state") &&
        !function_contains(source, "demo_terrain_queue_assemble_merge_spans(",
                           "s_terrain_spans_shared") &&
        !function_contains(source, "demo_terrain_queue_assemble_merge_spans(",
                           "begin == 0U");
    free(source);
    if (!ok) {
        fputs("terrain queue route does not bind exact descriptor ownership\n",
              stderr);
        return 1;
    }
    puts("render job terrain route source fixture: PASS");
    return 0;
}
