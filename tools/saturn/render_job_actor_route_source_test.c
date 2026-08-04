/* Source contract for the dormant A5.8 Mario queue route. */
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
        function_contains(source, "demo_snapshot_mario_transform_context(",
                          "memcpy(context->vertices, pose->vertices") &&
        function_contains(source, "demo_snapshot_mario_transform_context(",
                          "memcpy(context->vertex_refs, s_actor_transform_refs") &&
        !function_contains(source, "demo_snapshot_mario_transform_context(",
                           "context->primitives =") &&
        !function_contains(source, "demo_snapshot_mario_transform_context(",
                           "context->material_rgb =") &&
        !function_contains(source, "demo_snapshot_mario_transform_context(",
                           "context->vertex_refs =") &&
        function_contains(source, "demo_snapshot_mario_transform_context(",
                          "context->snapshot = *snapshot") &&
        function_contains(source, "demo_dispatch_mario_transform(",
                          "demo_snapshot_mario_transform_context") &&
        function_contains(source, "demo_transform_mario_range(",
                          "sm64_saturn_sins_q16") &&
        function_contains(source, "demo_transform_mario_range(",
                          "sm64_saturn_coss_q16") &&
        !function_contains(source, "demo_transform_mario_vertex(",
                           "sm64_saturn_sins_q16") &&
        function_contains(source, "demo_actor_queue_transform(",
                          "demo_actor_queue_bind_output") &&
        function_contains(source, "demo_actor_queue_transform(",
                          "demo_render_queue_context_open") &&
        function_contains(source, "demo_actor_queue_transform(",
                          "demo_actor_queue_publish_metadata") &&
        function_contains(source, "demo_actor_queue_transform(",
                          "job->input_offset") &&
        !function_contains(source, "demo_actor_queue_transform(",
                           "begin == 0U") &&
        !function_contains(source, "demo_actor_queue_transform(",
                           "s_actor_slave_begin") &&
        function_contains(source, "demo_actor_queue_classify(",
                          "sm64_saturn_render_job_graph_actor_lower_admit_done") &&
        function_contains(source, "demo_actor_queue_classify(",
                          "demo_render_queue_context_open") &&
        function_contains(source, "demo_actor_queue_classify(",
                          "demo_actor_queue_read_vertices_done") &&
        function_contains(source, "demo_actor_queue_classify(",
                          "demo_actor_queue_publish_metadata") &&
        !function_contains(source, "demo_actor_queue_classify(",
                           "s_actor_primitive_slave_begin") &&
        !function_contains(source, "demo_actor_queue_classify(",
                           "owner_for_split") &&
        function_contains(source, "demo_actor_queue_assemble_done(",
                          "sm64_saturn_render_job_graph_collect_done_actor_lower") &&
        function_contains(source, "demo_actor_queue_assemble_done(",
                          "sm64_saturn_render_job_graph_validate_actor_merge") &&
        function_contains(source, "demo_actor_queue_assemble_done(",
                          "demo_actor_queue_read_refs_done") &&
        function_contains(source, "demo_actor_queue_assemble_done(",
                          "demo_actor_queue_validate_payloads") &&
        function_contains(source, "demo_actor_queue_assemble_done(",
                          "s_actor_refs") &&
        !function_contains(source, "demo_actor_queue_assemble_done(",
                           "s_actor_slave_begin") &&
        !function_contains(source, "demo_actor_queue_assemble_done(",
                           "owner_for_split");
    free(source);
    if (!ok) {
        source = read_file("src/port/saturn/gfx/saturn_demo_render.c");
        fprintf(stderr, "transform bind=%d publish=%d offset=%d fixed=%d\n",
                function_contains(source, "demo_actor_queue_transform(",
                                  "demo_actor_queue_bind_output"),
                function_contains(source, "demo_actor_queue_transform(",
                                  "demo_actor_queue_publish_metadata"),
                function_contains(source, "demo_actor_queue_transform(",
                                  "job->input_offset"),
                function_contains(source, "demo_actor_queue_transform(",
                                  "s_actor_slave_begin"));
        fprintf(stderr, "classify pred=%d read=%d publish=%d fixed=%d owner=%d\n",
                function_contains(source, "demo_actor_queue_classify(",
                                  "sm64_saturn_render_job_graph_actor_lower_admit_done"),
                function_contains(source, "demo_actor_queue_classify(",
                                  "demo_actor_queue_read_vertices_done"),
                function_contains(source, "demo_actor_queue_classify(",
                                  "demo_actor_queue_publish_metadata"),
                function_contains(source, "demo_actor_queue_classify(",
                                  "s_actor_primitive_slave_begin"),
                function_contains(source, "demo_actor_queue_classify(",
                                  "owner_for_split"));
        fprintf(stderr, "merge collect=%d validate=%d read=%d refs=%d fixed=%d owner=%d\n",
                function_contains(source, "demo_actor_queue_assemble_done(",
                                  "sm64_saturn_render_job_graph_collect_done_actor_lower"),
                function_contains(source, "demo_actor_queue_assemble_done(",
                                  "sm64_saturn_render_job_graph_validate_actor_merge"),
                function_contains(source, "demo_actor_queue_assemble_done(",
                                  "demo_actor_queue_read_refs_done"),
                function_contains(source, "demo_actor_queue_assemble_done(",
                                  "s_actor_refs"),
                function_contains(source, "demo_actor_queue_assemble_done(",
                                  "s_actor_slave_begin"),
                function_contains(source, "demo_actor_queue_assemble_done(",
                                  "owner_for_split"));
        free(source);
        fputs("Mario queue route is not descriptor-owned end to end\n", stderr);
        return 1;
    }
    puts("render job actor route source fixture: PASS");
    return 0;
}
