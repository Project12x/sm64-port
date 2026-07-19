/* Execute SM64's original GeoLayout parser on SH-2 and expose its selections. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <ultra64.h>
#include "sm64.h"
#include "geo_commands.h"
#include "engine/geo_layout.h"
#include "engine/graph_node.h"
#include "game/geo_misc.h"
#include "castle_graph_bridge.h"

/* The current static Fast3D compiler resolves these payloads into the Saturn
 * IR. Their target addresses are retained here only as the identities carried
 * by original GraphNodeDisplayList nodes. */
const Gfx inside_castle_seg7_dl_07028FD0[] = {0};
const Gfx inside_castle_seg7_dl_07029578[] = {0};
const Gfx inside_castle_seg7_dl_0702A650[] = {0};
const Gfx inside_castle_seg7_dl_0702AA10[] = {0};
const Gfx inside_castle_seg7_dl_0702AB20[] = {0};

Gfx *geo_exec_inside_castle_light(
        s32 call_context __unused, struct GraphNode *node __unused,
        f32 mtx[4][4] __unused) {
    return NULL;
}

#include "castle_geo_root.h"

static uint8_t graph_arena[4096] __aligned(8);
static struct AllocOnlyPool graph_pool;

static const void *const source_root_display_lists[] = {
    inside_castle_seg7_dl_07028FD0,
    inside_castle_seg7_dl_07029578,
    inside_castle_seg7_dl_0702A650,
    inside_castle_seg7_dl_0702AA10,
    inside_castle_seg7_dl_0702AB20,
};

void *alloc_only_pool_alloc(struct AllocOnlyPool *pool, s32 size) {
    if (size <= 0) return NULL;
    const s32 aligned = (size + 3) & ~3;
    if (pool->usedSpace + aligned > pool->totalSpace) return NULL;
    void *result = pool->freePtr;
    pool->freePtr += aligned;
    pool->usedSpace += aligned;
    (void)memset(result, 0, (size_t)aligned);
    return result;
}

void *segmented_to_virtual(const void *address) {
    return (void *)address;
}

static void inspect_nodes(
        struct GraphNode *first, sm64_saturn_castle_graph_state_t *state) {
    if (first == NULL) return;
    struct GraphNode *node = first;
    do {
        if ((node->type & 0xFFU) == GRAPH_NODE_TYPE_DISPLAY_LIST) {
            const uint8_t layer = (uint8_t)(node->flags >> 8);
            const void *display_list =
                ((const struct GraphNodeDisplayList *)node)->displayList;
            state->display_lists++;
            if (layer == LAYER_OPAQUE) state->opaque_lists++;
            else if (layer == LAYER_ALPHA) state->alpha_lists++;
            else if (layer == LAYER_TRANSPARENT_DECAL) state->decal_lists++;
            for (uint8_t root = 0;
                 root < (uint8_t)(sizeof(source_root_display_lists) /
                                  sizeof(source_root_display_lists[0]));
                 root++) {
                if (display_list == source_root_display_lists[root]) {
                    state->selected_root_mask |= (uint8_t)(1U << root);
                    state->selected_root_layers[root] = layer;
                    break;
                }
            }
        }
        inspect_nodes(node->children, state);
        node = node->next;
    } while (node != first);
}

sm64_saturn_castle_graph_state_t sm64_saturn_castle_graph_init(void) {
    (void)memset(graph_arena, 0, sizeof(graph_arena));
    graph_pool.totalSpace = (s32)sizeof(graph_arena);
    graph_pool.usedSpace = 0;
    graph_pool.startPtr = graph_arena;
    graph_pool.freePtr = graph_arena;
    struct GraphNode *root = process_geo_layout(&graph_pool,
        (void *)sm64_saturn_castle_geo_root);
    sm64_saturn_castle_graph_state_t state = {0};
    inspect_nodes(root, &state);
    state.valid = root != NULL &&
        state.display_lists == SM64_SATURN_CASTLE_GRAPH_DISPLAY_LIST_COUNT &&
        state.opaque_lists == 2U && state.alpha_lists == 2U &&
        state.decal_lists == 1U && state.selected_root_mask == 0x1FU &&
        state.selected_root_layers[0] == LAYER_OPAQUE &&
        state.selected_root_layers[1] == LAYER_ALPHA &&
        state.selected_root_layers[2] == LAYER_OPAQUE &&
        state.selected_root_layers[3] == LAYER_TRANSPARENT_DECAL &&
        state.selected_root_layers[4] == LAYER_ALPHA;
    return state;
}
