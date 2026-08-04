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

static int frame_contains(const char *source, const char *needle)
{
    const char *frame = strstr(source, "void sm64_saturn_demo_render_frame(");
    const char *end = frame == NULL ? NULL : function_end(frame);
    const char *found = frame == NULL ? NULL : strstr(frame, needle);
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
        frame_contains(source, "sm64_saturn_render_job_graph_publish") &&
        frame_contains(source, "sm64_saturn_render_job_runtime_notify") &&
        frame_contains(source, "sm64_saturn_render_job_runtime_drain_master") &&
        frame_contains(source, "sm64_saturn_render_job_queue_all_terminal") &&
        !frame_contains(source, "sm64_saturn_terrain_worker_run") &&
        !frame_contains(source, "demo_dispatch_mario_transform");
    free(source);
    if (!ok) {
        fputs("A5 renderer has not atomically cut over to graph runtime\n", stderr);
        return 1;
    }
    return 0;
}
