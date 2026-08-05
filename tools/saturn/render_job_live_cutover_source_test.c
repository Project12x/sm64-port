/* Host-executable source contract for the A5 atomic renderer cutover.
 *
 * This deliberately checks the accepted frame entry point rather than the
 * existence of legacy diagnostic helpers elsewhere in the translation unit.
 * A regression that reintroduces the old range dispatcher or a second
 * CPU-DUAL registration into the default frame path must fail before target
 * compilation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0L, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
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
    const char *function = strstr(source, name);
    const char *end = function == NULL ? NULL : function_end(function);
    const char *found = function == NULL ? NULL : strstr(function, needle);
    return found != NULL && end != NULL && found < end;
}

static unsigned function_count(const char *source, const char *name,
                               const char *needle)
{
    const char *function = strstr(source, name);
    const char *end = function == NULL ? NULL : function_end(function);
    unsigned count = 0U;
    if (function == NULL || end == NULL) return 0U;
    for (const char *found = strstr(function, needle);
         found != NULL && found < end; found = strstr(found + 1, needle))
        count++;
    return count;
}

static int init_contains(const char *source, const char *needle)
{
    const char *init = strstr(source, "void sm64_saturn_demo_render_init(");
    const char *end = init == NULL ? NULL : function_end(init);
    const char *found = init == NULL ? NULL : strstr(init, needle);
    return found != NULL && end != NULL && found < end;
}

int main(void)
{
    char *source = read_file("src/port/saturn/gfx/saturn_demo_render.c");
    if (source == NULL) {
        fputs("cannot read saturn_demo_render.c\n", stderr);
        return 2;
    }
    const int ok =
        strstr(source, "#include \"saturn_render_job_bridge.h\"") != NULL &&
        strstr(source, "#include \"saturn_render_job_runtime.h\"") != NULL &&
        init_contains(source, "sm64_saturn_render_job_runtime_activate_graph") &&
        function_contains(source, "demo_render_prepare_publish(",
                          "demo_render_queue_reset_frame_banks") &&
        function_contains(source, "demo_render_prepare_publish(",
                          ".dual_phase = false") &&
        function_contains(source, "demo_render_prepare_publish(",
                          "sm64_saturn_render_job_graph_publish") &&
        function_count(source, "demo_render_prepare_publish(",
                       ".output_capacity = DEMO_TERRAIN_RESULT_CAPACITY") == 2U &&
        function_contains(source, "demo_render_prepare_publish(",
                          "demo_render_queue_prepare_contexts") &&
        function_contains(source, "static void demo_render_quarantine(",
                          "sm64_saturn_render_job_queue_quarantine_ready") &&
        function_contains(source, "demo_render_notify(",
                          "sm64_saturn_render_job_runtime_notify") &&
        function_contains(source, "demo_render_drain_master(",
                          "sm64_saturn_render_job_runtime_drain_master") &&
        function_contains(source, "demo_render_finalize(",
                          "sm64_saturn_render_job_queue_all_terminal") &&
        function_contains(source, "demo_render_slave_retired(",
                          "sm64_saturn_render_job_runtime_slave_retired") &&
        function_contains(source, "demo_render_finalize(",
                          "demo_terrain_queue_assemble_merge_spans") &&
        function_contains(source, "demo_render_finalize(",
                          "demo_actor_queue_assemble_done") &&
        function_contains(source, "demo_render_finalize(",
                          "sm64_saturn_render_job_queue_reset_retired") &&
        !function_contains(source, "demo_render_prepare_publish(",
                           "sm64_saturn_terrain_worker_run") &&
        !function_contains(source, "demo_render_prepare_publish(",
                           "demo_dispatch_mario_transform") &&
        strstr(source, "sm64_saturn_demo_render_frame(") == NULL;
    free(source);
    if (!ok) {
        fputs("A5 renderer has not atomically cut over to graph runtime\n", stderr);
        return 1;
    }
    return 0;
}
