#include <PR/ultratypes.h>

#include "area.h"
#include "engine/math_util.h"
#include "game_init.h"
#include "gfx_dimensions.h"
#include "main.h"
#include "memory.h"
#include "print.h"
#include "rendering_graph_node.h"
#include "shadow.h"
#include "sm64.h"

/**
 * This file contains the code that processes the scene graph for rendering.
 * The scene graph is responsible for drawing everything except the HUD / text boxes.
 * First the root of the scene graph is processed when geo_process_root
 * is called from level_script.c. The rest of the tree is traversed recursively
 * using the function geo_process_node_and_siblings, which switches over all
 * geo node types and calls a specialized function accordingly.
 * The types are defined in engine/graph_node.h
 *
 * The scene graph typically looks like:
 * - Root (viewport)
 *  - Master list
 *   - Ortho projection
 *    - Background (skybox)
 *  - Master list
 *   - Perspective
 *    - Camera
 *     - <area-specific display lists>
 *     - Object parent
 *      - <group with 240 object nodes>
 *  - Master list
 *   - Script node (Cannon overlay)
 *
 */

s16 gMatStackIndex;
Mat4 gMatStack[32];
Mtx *gMatStackFixed[32];

#ifdef TARGET_SATURN
#include <string.h>

#include "port/saturn/gfx/saturn_matrix.h"
#include "port/saturn/gfx/saturn_matrix_ctors.h"
#include "port/saturn/gfx/saturn_render_native_math.h"
#include "port/saturn/gfx/saturn_geo_state_observer.h"
#include "port/saturn/runtime/saturn_geo_walk_runtime.h"
#include "port/saturn/runtime/saturn_geo_walk_storage.h"
#include "object_fields.h"
#include "object_list_processor.h"
#include "model_ids.h"

/* Task 14 wave 1: bounded iterative geo-walk integration. Converted
 * handlers call this instead of recursing through
 * geo_process_node_and_siblings; see the shared engine definitions
 * (saturn_geo_walk_process_children and friends) further down this file
 * for the enter/dispatch/leave binding and the wave-scoped reentrancy
 * rationale. */
static bool saturn_geo_walk_process_children(struct GraphNode *children);

#ifndef SATURN_MTX_IS_Q16
/* This TU's Saturn path is a Q16.16 WIRE PRODUCER (saturn_mtxq_write_wire
 * below writes raw s32[4][4] into display-list Mtx slots). Compiling it
 * into a target whose frontend decodes the float wire format (no
 * SATURN_MTX_IS_Q16) would silently feed Q16 integers to a float
 * decoder. Producer and consumer must share the one define -- see
 * guMtxF2L.c's matching guard and sourceboot/Makefile's SH_CFLAGS. */
#error "TARGET_SATURN rendering_graph_node.c writes Q16.16 wire data; this build's frontend would decode floats. Define SATURN_MTX_IS_Q16 target-wide."
#endif

/* Q16.16 shadow of gMatStack, maintained in lockstep at every
 * composition site below. On Saturn this is the AUTHORITATIVE matrix
 * state: the toolchain's soft-float is proven to corrupt the float
 * stack's math (docs/saturn/evidence/
 * e2-sourceboot-gmatstack-corruption-2026-07-22.md), so the float
 * gMatStack entries are REFRESHED FROM these Q16 results (exact
 * conversion, no float arithmetic) for the engine consumers that read
 * them (shadow positioning, culling, held-object math, positional
 * audio via cameraToObject). The wire Mtx (gMatStackFixed) is written
 * from Q16 directly -- see saturn_mtxq_write_wire below and the
 * frontend's matching native-Q16 decode (SATURN_MTX_IS_Q16, a later
 * task). */
static sm64_saturn_mtx_t gMatStackQ[32];

/* Resolve only source-owned scalar identity at the geo seam.  The generated
 * actor-family registry is not linked into this source closure yet, so the
 * family/bank fields intentionally remain zero and capture fails closed.  A
 * future registry binding can fill those fields without changing the object
 * walk or snapshot ABI. */
static uint16_t saturn_source_object_pool_slot(const struct Object *object)
{
    uint16_t slot;
    if (object == NULL) return UINT16_MAX;
    for (slot = 0U; slot < OBJECT_POOL_CAPACITY; slot++) {
        if (&gObjectPool[slot] == object) return slot;
    }
    return UINT16_MAX;
}

static uint16_t saturn_source_model_id(const struct GraphNode *shared_child)
{
    uint16_t model;
    if (shared_child == NULL || gLoadedGraphNodes == NULL) return MODEL_NONE;
    for (model = 1U; model < 0x100U; model++) {
        if (gLoadedGraphNodes[model] == shared_child) return model;
    }
    return MODEL_NONE;
}

static bool saturn_source_observe_object_begin(struct Object *node)
{
    sm64_saturn_actor_source_observation_t source;
    sm64_saturn_geo_state_observer_t *const observer =
        sm64_saturn_geo_state_observer_bound();
    uint16_t axis;
    const uint16_t pool_slot = saturn_source_object_pool_slot(node);
    if (pool_slot == UINT16_MAX) return false;
    memset(&source, 0, sizeof(source));
    source.source_generation = sm64_saturn_geo_state_observer_generation(
        observer);
    source.pool_slot = pool_slot;
    source.model_id = saturn_source_model_id(node->header.gfx.sharedChild);
    source.parent_index = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
    source.parent_node_ordinal = SM64_SATURN_ACTOR_INSTANCE_NO_PARENT;
    source.position_q16[0] = sm64_saturn_float_to_q16(
        node->header.gfx.pos[0]);
    source.position_q16[1] = sm64_saturn_float_to_q16(
        node->header.gfx.pos[1]);
    source.position_q16[2] = sm64_saturn_float_to_q16(
        node->header.gfx.pos[2]);
    source.scale_q16[0] = sm64_saturn_float_to_q16(
        node->header.gfx.scale[0]);
    source.scale_q16[1] = sm64_saturn_float_to_q16(
        node->header.gfx.scale[1]);
    source.scale_q16[2] = sm64_saturn_float_to_q16(
        node->header.gfx.scale[2]);
    for (axis = 0U; axis < 3U; axis++)
        source.angle[axis] = node->header.gfx.angle[axis];
    source.animation_id = node->header.gfx.animInfo.animID;
    source.animation_frame = node->header.gfx.animInfo.animFrame;
    source.animation_accel = node->header.gfx.animInfo.animAccel;
    source.anim_state = ((struct Object *)node)->oAnimState;
    source.area_index = node->header.gfx.areaIndex;
    source.active = 1U;
    source.render_active = 1U;
    /* Visibility/range/switch/opacity/held/parent/effect fields are not
     * source-owned at this narrow object seam, so their zero/default values
     * (and render_active=1) are never admitted. Scene/family/bank identity is
     * deliberately unresolved until the generated actor registry is
     * authoritative for this source object. */
    return sm64_saturn_geo_state_observer_begin_object(observer, &source);
}

/* The camera's look-at matrix is exposed to descendants (shadow nodes)
 * through GraphNodeCamera::matrixPtr, a `Mat4 *` into gMatStack. This
 * is its Q16 twin, set alongside matrixPtr in geo_process_camera and
 * read in geo_process_shadow. Only one camera is ever active at a time
 * (gCurGraphNodeCamera is a single global, not a stack), so a single
 * static pointer mirrors that invariant exactly. */
static sm64_saturn_mtx_t *sSaturnCameraMatrixQ;

static void saturn_mtxq_refresh_float_mirror(s16 index) {
    for (s32 i = 0; i < 4; i++) {
        for (s32 j = 0; j < 4; j++) {
            gMatStack[index][i][j] =
                sm64_saturn_q16_to_float(gMatStackQ[index].m[i][j]);
        }
    }
}

/* Write the Q16 matrix into the display-list Mtx slot. Mtx under
 * GBI_FLOATS is struct { float m[4][4] } -- 64 bytes, same size/shape
 * as s32[4][4]. On Saturn the wire format IS Q16.16 raw (the port's
 * own frontend is the only consumer; it decodes natively under
 * SATURN_MTX_IS_Q16, no float ever touched). memcpy avoids the
 * type-pun UB. */
static void saturn_mtxq_write_wire(Mtx *dest, const sm64_saturn_mtx_t *src) {
    memcpy(dest, src->m, sizeof(src->m));
}

static void saturn_vec3f_to_q16(int32_t out[3], Vec3f in) {
    out[0] = sm64_saturn_float_to_q16(in[0]);
    out[1] = sm64_saturn_float_to_q16(in[1]);
    out[2] = sm64_saturn_float_to_q16(in[2]);
}

static void saturn_vec3s_to_q16(int32_t out[3], Vec3s in) {
    out[0] = (int32_t) in[0] * (1 << 16);
    out[1] = (int32_t) in[1] * (1 << 16);
    out[2] = (int32_t) in[2] * (1 << 16);
}

static void saturn_mat4_to_q16(sm64_saturn_mtx_t *out, Mat4 in) {
    for (s32 i = 0; i < 4; i++) {
        for (s32 j = 0; j < 4; j++) {
            out->m[i][j] = sm64_saturn_float_to_q16(in[i][j]);
        }
    }
}

/* Recovers the gMatStackQ index matching a Mat4* that is known to
 * point into gMatStack -- e.g. a GraphNodeObject's throwMatrix, which
 * geo_process_object unconditionally sets to &gMatStack[N] (see the
 * object site below) before any child, including a held-object node,
 * can observe it. Pointer subtraction of two pointers into the same
 * array is well-defined and exact; the caller is responsible for the
 * "really points into gMatStack" precondition (true for throwMatrix at
 * the point geo_process_held_object reads it, NOT true in general --
 * throwMatrix can also be a gameplay-owned float matrix living
 * entirely outside gMatStack, e.g. mario.c's quicksand adjustment or
 * obj_behaviors.c's terrain-normal alignment; that case is handled by
 * converting at the read boundary instead, see the object site's
 * throwMatrix branch). */
static s32 saturn_mtxq_gmatstack_index(Mat4 *slot) {
    return (s32) (slot - gMatStack);
}
#endif

/**
 * Animation nodes have state in global variables, so this struct captures
 * the animation state so a 'context switch' can be made when rendering the
 * held object.
 */
struct GeoAnimState {
    /*0x00*/ u8 type;
    /*0x01*/ u8 enabled;
    /*0x02*/ s16 frame;
    /*0x04*/ f32 translationMultiplier;
    /*0x08*/ u16 *attribute;
    /*0x0C*/ s16 *data;
};

// For some reason, this is a GeoAnimState struct, but the current state consists
// of separate global variables. It won't match EU otherwise.
struct GeoAnimState gGeoTempState;

u8 gCurAnimType;
u8 gCurAnimEnabled;
s16 gCurrAnimFrame;
f32 gCurAnimTranslationMultiplier;
u16 *gCurrAnimAttribute;
s16 *gCurAnimData;

struct AllocOnlyPool *gDisplayListHeap;

struct RenderModeContainer {
    u32 modes[8];
};

/* Rendermode settings for cycle 1 for all 8 layers. */
struct RenderModeContainer renderModeTable_1Cycle[2] = { { {
    G_RM_OPA_SURF,
    G_RM_AA_OPA_SURF,
    G_RM_AA_OPA_SURF,
    G_RM_AA_OPA_SURF,
    G_RM_AA_TEX_EDGE,
    G_RM_AA_XLU_SURF,
    G_RM_AA_XLU_SURF,
    G_RM_AA_XLU_SURF,
    } },
    { {
    /* z-buffered */
    G_RM_ZB_OPA_SURF,
    G_RM_AA_ZB_OPA_SURF,
    G_RM_AA_ZB_OPA_DECAL,
    G_RM_AA_ZB_OPA_INTER,
    G_RM_AA_ZB_TEX_EDGE,
    G_RM_AA_ZB_XLU_SURF,
    G_RM_AA_ZB_XLU_DECAL,
    G_RM_AA_ZB_XLU_INTER,
    } } };

/* Rendermode settings for cycle 2 for all 8 layers. */
struct RenderModeContainer renderModeTable_2Cycle[2] = { { {
    G_RM_OPA_SURF2,
    G_RM_AA_OPA_SURF2,
    G_RM_AA_OPA_SURF2,
    G_RM_AA_OPA_SURF2,
    G_RM_AA_TEX_EDGE2,
    G_RM_AA_XLU_SURF2,
    G_RM_AA_XLU_SURF2,
    G_RM_AA_XLU_SURF2,
    } },
    { {
    /* z-buffered */
    G_RM_ZB_OPA_SURF2,
    G_RM_AA_ZB_OPA_SURF2,
    G_RM_AA_ZB_OPA_DECAL2,
    G_RM_AA_ZB_OPA_INTER2,
    G_RM_AA_ZB_TEX_EDGE2,
    G_RM_AA_ZB_XLU_SURF2,
    G_RM_AA_ZB_XLU_DECAL2,
    G_RM_AA_ZB_XLU_INTER2,
    } } };

struct GraphNodeRoot *gCurGraphNodeRoot = NULL;
struct GraphNodeMasterList *gCurGraphNodeMasterList = NULL;
struct GraphNodePerspective *gCurGraphNodeCamFrustum = NULL;
struct GraphNodeCamera *gCurGraphNodeCamera = NULL;
struct GraphNodeObject *gCurGraphNodeObject = NULL;
struct GraphNodeHeldObject *gCurGraphNodeHeldObject = NULL;
u16 gAreaUpdateCounter = 0;

#ifdef F3DEX_GBI_2
LookAt lookAt;
#endif

/**
 * Process a master list node.
 */
static void geo_process_master_list_sub(struct GraphNodeMasterList *node) {
    struct DisplayListNode *currList;
    s32 i;
    s32 enableZBuffer = (node->node.flags & GRAPH_RENDER_Z_BUFFER) != 0;
    struct RenderModeContainer *modeList = &renderModeTable_1Cycle[enableZBuffer];
    struct RenderModeContainer *mode2List = &renderModeTable_2Cycle[enableZBuffer];

    // @bug This is where the LookAt values should be calculated but aren't.
    // As a result, environment mapping is broken on Fast3DEX2 without the
    // changes below.
#ifdef F3DEX_GBI_2
#ifdef TARGET_SATURN
    /* The Saturn path always submits the same fixed reflection basis here:
     * eye=(0,0,0), at=(1,0,0), up=(0,0,1).  Materialising those two LookAt
     * lights directly avoids guLookAtReflect's three soft-float normalizations
     * on every master-list traversal.  These values are the exact FTOFRAC8
     * encodings of right=(0,-1,0) and up=(0,0,1). */
    lookAt.l[0].l.dir[0] = 0x00;
    lookAt.l[0].l.dir[1] = 0x80;
    lookAt.l[0].l.dir[2] = 0x00;
    lookAt.l[1].l.dir[0] = 0x00;
    lookAt.l[1].l.dir[1] = 0x00;
    lookAt.l[1].l.dir[2] = 0x7F;
    lookAt.l[0].l.col[0] = lookAt.l[0].l.col[1] = lookAt.l[0].l.col[2] = 0x00;
    lookAt.l[0].l.pad1 = 0x00;
    lookAt.l[0].l.colc[0] = lookAt.l[0].l.colc[1] = lookAt.l[0].l.colc[2] = 0x00;
    lookAt.l[0].l.pad2 = 0x00;
    lookAt.l[1].l.col[0] = 0x00;
    lookAt.l[1].l.col[1] = 0x80;
    lookAt.l[1].l.col[2] = 0x00;
    lookAt.l[1].l.pad1 = 0x00;
    lookAt.l[1].l.colc[0] = 0x00;
    lookAt.l[1].l.colc[1] = 0x80;
    lookAt.l[1].l.colc[2] = 0x00;
    lookAt.l[1].l.pad2 = 0x00;
#else
    Mtx lMtx;
    guLookAtReflect(&lMtx, &lookAt, 0, 0, 0, /* eye */ 0, 0, 1, /* at */ 1, 0, 0 /* up */);
#endif
#endif

    if (enableZBuffer != 0) {
        gDPPipeSync(gDisplayListHead++);
        gSPSetGeometryMode(gDisplayListHead++, G_ZBUFFER);
    }

    for (i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
        if ((currList = node->listHeads[i]) != NULL) {
            gDPSetRenderMode(gDisplayListHead++, modeList->modes[i], mode2List->modes[i]);
            while (currList != NULL) {
                gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(currList->transform),
                          G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
                gSPDisplayList(gDisplayListHead++, currList->displayList);
                currList = currList->next;
            }
        }
    }
    if (enableZBuffer != 0) {
        gDPPipeSync(gDisplayListHead++);
        gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER);
    }
}

/**
 * Appends the display list to one of the master lists based on the layer
 * parameter. Look at the RenderModeContainer struct to see the corresponding
 * render modes of layers.
 */
static void geo_append_display_list(void *displayList, s16 layer) {

#ifdef F3DEX_GBI_2
    gSPLookAt(gDisplayListHead++, &lookAt);
#endif
    if (gCurGraphNodeMasterList != 0) {
        struct DisplayListNode *listNode =
            alloc_only_pool_alloc(gDisplayListHeap, sizeof(struct DisplayListNode));

        listNode->transform = gMatStackFixed[gMatStackIndex];
        listNode->displayList = displayList;
        listNode->next = 0;
        if (gCurGraphNodeMasterList->listHeads[layer] == 0) {
            gCurGraphNodeMasterList->listHeads[layer] = listNode;
        } else {
            gCurGraphNodeMasterList->listTails[layer]->next = listNode;
        }
        gCurGraphNodeMasterList->listTails[layer] = listNode;
    }
}

/**
 * Master-list enter: the pre-child re-entrancy guard and listHeads reset.
 * Returns false (leaving all state untouched) when the guard rejects the
 * node, exactly matching the pre-conversion combined condition.
 */
static bool saturn_geo_enter_master_list(struct GraphNodeMasterList *node) {
    s32 i;

    if (gCurGraphNodeMasterList != NULL || node->node.children == NULL) {
        return false;
    }
    gCurGraphNodeMasterList = node;
    for (i = 0; i < GFX_NUM_MASTER_LISTS; i++) {
        node->listHeads[i] = NULL;
    }
    return true;
}

/**
 * Master-list leave: draws the accumulated lists and releases the guard.
 */
static void saturn_geo_leave_master_list(struct GraphNodeMasterList *node) {
    geo_process_master_list_sub(node);
    gCurGraphNodeMasterList = NULL;
}

/**
 * Process the master list node.
 */
static void geo_process_master_list(struct GraphNodeMasterList *node) {
    if (saturn_geo_enter_master_list(node)) {
        (void) saturn_geo_walk_process_children(node->node.children);
        saturn_geo_leave_master_list(node);
    }
}

/**
 * Ortho-projection enter: builds and submits the projection matrix. No
 * leave action is needed -- the original handler never touched the
 * matrix stack or any global that needs post-child restoration. Returns
 * false (no side effects) when there are no children to project for.
 */
static bool saturn_geo_enter_ortho_projection(struct GraphNodeOrthoProjection *node) {
    if (node->node.children != NULL) {
        Mtx *mtx = alloc_display_list(sizeof(*mtx));
#ifdef TARGET_SATURN
        sm64_saturn_mtx_t orthoQ;
        const int32_t scaleQ = sm64_saturn_float_to_q16(node->scale);
        const int32_t leftQ = (int32_t) (((int64_t)
            (gCurGraphNodeRoot->x - gCurGraphNodeRoot->width) * scaleQ) / 2);
        const int32_t rightQ = (int32_t) (((int64_t)
            (gCurGraphNodeRoot->x + gCurGraphNodeRoot->width) * scaleQ) / 2);
        const int32_t topQ = (int32_t) (((int64_t)
            (gCurGraphNodeRoot->y - gCurGraphNodeRoot->height) * scaleQ) / 2);
        const int32_t bottomQ = (int32_t) (((int64_t)
            (gCurGraphNodeRoot->y + gCurGraphNodeRoot->height) * scaleQ) / 2);

        (void) sm64_saturn_mtxq_ortho(&orthoQ, leftQ, rightQ, bottomQ, topQ,
                                      -(2 << 16), 2 << 16);
        saturn_mtxq_write_wire(mtx, &orthoQ);
#else
        f32 left = (gCurGraphNodeRoot->x - gCurGraphNodeRoot->width) / 2.0f * node->scale;
        f32 right = (gCurGraphNodeRoot->x + gCurGraphNodeRoot->width) / 2.0f * node->scale;
        f32 top = (gCurGraphNodeRoot->y - gCurGraphNodeRoot->height) / 2.0f * node->scale;
        f32 bottom = (gCurGraphNodeRoot->y + gCurGraphNodeRoot->height) / 2.0f * node->scale;

        guOrtho(mtx, left, right, bottom, top, -2.0f, 2.0f, 1.0f);
#endif
        gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

        return true;
    }
    return false;
}

/**
 * Process an orthographic projection node.
 */
static void geo_process_ortho_projection(struct GraphNodeOrthoProjection *node) {
    if (saturn_geo_enter_ortho_projection(node)) {
        (void) saturn_geo_walk_process_children(node->node.children);
    }
}

/**
 * Perspective enter: the func() callback runs unconditionally (matching
 * the pre-conversion code), then, if there are children, builds and
 * submits the projection matrix and claims gCurGraphNodeCamFrustum.
 * Returns false when there are no children -- the func() call above has
 * already happened by then, exactly as before.
 */
static bool saturn_geo_enter_perspective(struct GraphNodePerspective *node) {
    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    if (node->fnNode.node.children != NULL) {
        u16 perspNorm = UINT16_MAX;
        Mtx *mtx = alloc_display_list(sizeof(*mtx));

#ifdef TARGET_SATURN
        sm64_saturn_mtx_t perspectiveQ;
        int32_t aspectQ;

        if (!sm64_saturn_div_s64_s32(
                (int64_t) gCurGraphNodeRoot->width << 16,
                gCurGraphNodeRoot->height, &aspectQ)) {
            aspectQ = 1 << 16;
        }
#ifdef VERSION_EU
        aspectQ = sm64_saturn_q16_mul(
            aspectQ, sm64_saturn_float_to_q16(1.1f));
#endif
        (void) sm64_saturn_mtxq_perspective(
            &perspectiveQ, &perspNorm,
            sm64_saturn_float_to_q16(node->fov), aspectQ,
            node->near, node->far);
        saturn_mtxq_write_wire(mtx, &perspectiveQ);
#else
#ifdef VERSION_EU
        f32 aspect = ((f32) gCurGraphNodeRoot->width / (f32) gCurGraphNodeRoot->height) * 1.1f;
#else
        f32 aspect = (f32) gCurGraphNodeRoot->width / (f32) gCurGraphNodeRoot->height;
#endif

        guPerspective(mtx, &perspNorm, node->fov, aspect, node->near, node->far, 1.0f);
#endif
        gSPPerspNormalize(gDisplayListHead++, perspNorm);

        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);

        gCurGraphNodeCamFrustum = node;
        return true;
    }
    return false;
}

/**
 * Perspective leave: restores gCurGraphNodeCamFrustum.
 */
static void saturn_geo_leave_perspective(void) {
    gCurGraphNodeCamFrustum = NULL;
}

/**
 * Process a perspective projection node.
 */
static void geo_process_perspective(struct GraphNodePerspective *node) {
    if (saturn_geo_enter_perspective(node)) {
        (void) saturn_geo_walk_process_children(node->fnNode.node.children);
        saturn_geo_leave_perspective();
    }
}

/**
 * Level-of-detail enter: extracts the perpendicular distance to the camera
 * from the current transformation matrix and returns whether that distance
 * is within this node's render range AND it has children to descend into.
 * No leave action is needed -- the original handler never touched the
 * matrix stack or any global that needs post-child restoration.
 */
static bool saturn_geo_enter_level_of_detail(struct GraphNodeLevelOfDetail *node) {
#ifdef GBI_FLOATS
    Mtx *mtx = gMatStackFixed[gMatStackIndex];
    s16 distanceFromCam = (s32) -mtx->m[3][2]; // z-component of the translation column
#else
    // The fixed point Mtx type is defined as 16 longs, but it's actually 16
    // shorts for the integer parts followed by 16 shorts for the fraction parts
    Mtx *mtx = gMatStackFixed[gMatStackIndex];
    s16 distanceFromCam = -GET_HIGH_S16_OF_32(mtx->m[1][3]); // z-component of the translation column
#endif

#ifndef TARGET_N64
    // We assume modern hardware is powerful enough to draw the most detailed variant
    distanceFromCam = 0;
#endif

    if (node->minDistance <= distanceFromCam && distanceFromCam < node->maxDistance) {
        return node->node.children != 0;
    }
    return false;
}

/**
 * Process a level of detail node. From the current transformation matrix,
 * the perpendicular distance to the camera is extracted and the children
 * of this node are only processed if that distance is within the render
 * range of this node.
 */
static void geo_process_level_of_detail(struct GraphNodeLevelOfDetail *node) {
    if (saturn_geo_enter_level_of_detail(node)) {
        (void) saturn_geo_walk_process_children(node->node.children);
    }
}

/**
 * Switch-case enter: runs the selection callback (if any), then walks the
 * node's children ring to the selectedCase-th entry. Returns the selected
 * child, or NULL if there isn't one -- the dynamically-chosen child is
 * still exactly one resulting pointer at enter time, fitting the single-
 * child contract (same pattern as BACKGROUND's enter-time decision). No
 * leave action is needed -- the original handler never touched the matrix
 * stack or any global that needs post-child restoration. Note:
 * saturn_geo_walk_sibling_of already special-cases "parent->type ==
 * GRAPH_NODE_TYPE_SWITCH_CASE => no sibling chaining" for whichever child
 * gets selected here.
 */
static struct GraphNode *saturn_geo_enter_switch(struct GraphNodeSwitchCase *node) {
    struct GraphNode *selectedChild = node->fnNode.node.children;
    s32 i;

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    for (i = 0; selectedChild != NULL && node->selectedCase > i; i++) {
        selectedChild = selectedChild->next;
    }
    return selectedChild;
}

/**
 * Process a switch case node. The node's selection function is called
 * if it is 0, and among the node's children, only the selected child is
 * processed next.
 */
static void geo_process_switch(struct GraphNodeSwitchCase *node) {
    struct GraphNode *selectedChild = saturn_geo_enter_switch(node);
    if (selectedChild != NULL) {
        (void) saturn_geo_walk_process_children(selectedChild);
    }
}

/**
 * Camera enter: builds the roll/lookat matrices and pushes the matrix
 * stack unconditionally (matching the pre-conversion code, which always
 * incremented gMatStackIndex regardless of whether the node had
 * children). Returns the children pointer to descend into, or NULL if
 * there are none; the caller must ALWAYS pair this with
 * saturn_geo_leave_camera() to balance the unconditional push, exactly
 * as the original always ran gMatStackIndex-- at the end regardless of
 * the children check.
 */
static struct GraphNode *saturn_geo_enter_camera(struct GraphNodeCamera *node) {
    UNUSED Mat4 cameraTransform;
    Mtx *rollMtx = alloc_display_list(sizeof(*rollMtx));
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t rollQ;
        sm64_saturn_mtxq_rotate_xy(&rollQ, node->rollScreen);
        saturn_mtxq_write_wire(rollMtx, &rollQ);
    }
#else
    mtxf_rotate_xy(rollMtx, node->rollScreen);
#endif

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(rollMtx), G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);

#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t camQ;
        int32_t posQ[3], focusQ[3];
        saturn_vec3f_to_q16(posQ, node->pos);
        saturn_vec3f_to_q16(focusQ, node->focus);
        sm64_saturn_mtxq_lookat(&camQ, posQ, focusQ, node->roll);
        (void) sm64_saturn_matrix_mul(&camQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    mtxf_lookat(cameraTransform, node->pos, node->focus, node->roll);
    mtxf_mul(gMatStack[gMatStackIndex + 1], cameraTransform, gMatStack[gMatStackIndex]);
#endif
    gMatStackIndex++;
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->fnNode.node.children != 0) {
        gCurGraphNodeCamera = node;
        node->matrixPtr = &gMatStack[gMatStackIndex];
#ifdef TARGET_SATURN
        sSaturnCameraMatrixQ = &gMatStackQ[gMatStackIndex];
#endif
        return node->fnNode.node.children;
    }
    return NULL;
}

/**
 * Camera leave: always pops the matrix stack; clears gCurGraphNodeCamera
 * only when the enter phase actually claimed it (i.e. the node had
 * children), matching the original's unconditional
 * gMatStackIndex-- paired with a conditional gCurGraphNodeCamera clear.
 */
static void saturn_geo_leave_camera(bool had_children) {
    if (had_children) {
        gCurGraphNodeCamera = NULL;
    }
    gMatStackIndex--;
}

/**
 * Process a camera node.
 */
static void geo_process_camera(struct GraphNodeCamera *node) {
    struct GraphNode *children = saturn_geo_enter_camera(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_camera(children != NULL);
}

/**
 * Translation/rotation enter: builds the transform and pushes the matrix
 * stack unconditionally (matching the pre-conversion code, which always
 * incremented gMatStackIndex regardless of whether the node had children),
 * then appends the display list if any. Returns the children pointer to
 * descend into, or NULL if there are none; the caller must ALWAYS pair
 * this with saturn_geo_leave_translation_rotation() to balance the
 * unconditional push, exactly as the original always ran gMatStackIndex--
 * at the end regardless of the children check.
 */
static struct GraphNode *saturn_geo_enter_translation_rotation(
    struct GraphNodeTranslationRotation *node) {
    UNUSED Mat4 mtxf;
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t nodeQ;
        int32_t tQ[3];
        saturn_vec3s_to_q16(tQ, node->translation);
        sm64_saturn_mtxq_rotate_zxy_and_translate(&nodeQ, tQ, node->rotation[0],
                                                  node->rotation[1], node->rotation[2]);
        (void) sm64_saturn_matrix_mul(&nodeQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    Vec3f translation;
    vec3s_to_vec3f(translation, node->translation);
    mtxf_rotate_zxy_and_translate(mtxf, translation, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
#endif
    gMatStackIndex++;
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Translation/rotation leave: always pops the matrix stack, balancing the
 * unconditional push in saturn_geo_enter_translation_rotation().
 */
static void saturn_geo_leave_translation_rotation(void) {
    gMatStackIndex--;
}

/**
 * Process a translation / rotation node. A transformation matrix based
 * on the node's translation and rotation is created and pushed on both
 * the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_translation_rotation(struct GraphNodeTranslationRotation *node) {
    struct GraphNode *children = saturn_geo_enter_translation_rotation(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_translation_rotation();
}

/**
 * Translation enter: builds the transform and pushes the matrix stack
 * unconditionally (matching the pre-conversion code), then appends the
 * display list if any. Returns the children pointer to descend into, or
 * NULL if there are none; the caller must ALWAYS pair this with
 * saturn_geo_leave_translation() to balance the unconditional push.
 */
static struct GraphNode *saturn_geo_enter_translation(struct GraphNodeTranslation *node) {
    UNUSED Mat4 mtxf;
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t nodeQ;
        int32_t tQ[3];
        saturn_vec3s_to_q16(tQ, node->translation);
        sm64_saturn_mtxq_rotate_zxy_and_translate(&nodeQ, tQ, 0, 0, 0);
        (void) sm64_saturn_matrix_mul(&nodeQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    Vec3f translation;
    vec3s_to_vec3f(translation, node->translation);
    mtxf_rotate_zxy_and_translate(mtxf, translation, gVec3sZero);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
#endif
    gMatStackIndex++;
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Translation leave: always pops the matrix stack, balancing the
 * unconditional push in saturn_geo_enter_translation().
 */
static void saturn_geo_leave_translation(void) {
    gMatStackIndex--;
}

/**
 * Process a translation node. A transformation matrix based on the node's
 * translation is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_translation(struct GraphNodeTranslation *node) {
    struct GraphNode *children = saturn_geo_enter_translation(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_translation();
}

/**
 * Rotation enter: builds the transform and pushes the matrix stack
 * unconditionally (matching the pre-conversion code), then appends the
 * display list if any. Returns the children pointer to descend into, or
 * NULL if there are none; the caller must ALWAYS pair this with
 * saturn_geo_leave_rotation() to balance the unconditional push.
 */
static struct GraphNode *saturn_geo_enter_rotation(struct GraphNodeRotation *node) {
    UNUSED Mat4 mtxf;
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t nodeQ;
        static const int32_t sZeroQ[3] = { 0, 0, 0 };
        sm64_saturn_mtxq_rotate_zxy_and_translate(&nodeQ, sZeroQ, node->rotation[0],
                                                  node->rotation[1], node->rotation[2]);
        (void) sm64_saturn_matrix_mul(&nodeQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    mtxf_rotate_zxy_and_translate(mtxf, gVec3fZero, node->rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
#endif
    gMatStackIndex++;
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Rotation leave: always pops the matrix stack, balancing the
 * unconditional push in saturn_geo_enter_rotation().
 */
static void saturn_geo_leave_rotation(void) {
    gMatStackIndex--;
}

/**
 * Process a rotation node. A transformation matrix based on the node's
 * rotation is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_rotation(struct GraphNodeRotation *node) {
    struct GraphNode *children = saturn_geo_enter_rotation(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_rotation();
}

/**
 * Scale enter: builds the transform and pushes the matrix stack
 * unconditionally (matching the pre-conversion code), then appends the
 * display list if any. Returns the children pointer to descend into, or
 * NULL if there are none; the caller must ALWAYS pair this with
 * saturn_geo_leave_scale() to balance the unconditional push.
 */
static struct GraphNode *saturn_geo_enter_scale(struct GraphNodeScale *node) {
    UNUSED Mat4 transform;
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

#ifdef TARGET_SATURN
    {
        const int32_t scaleQ = sm64_saturn_float_to_q16(node->scale);
        int32_t sQ[3] = { scaleQ, scaleQ, scaleQ };
        sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex + 1],
                                     &gMatStackQ[gMatStackIndex], sQ);
    }
#else
    Vec3f scaleVec;
    vec3f_set(scaleVec, node->scale, node->scale, node->scale);
    mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex], scaleVec);
#endif
    gMatStackIndex++;
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Scale leave: always pops the matrix stack, balancing the unconditional
 * push in saturn_geo_enter_scale().
 */
static void saturn_geo_leave_scale(void) {
    gMatStackIndex--;
}

/**
 * Process a scaling node. A transformation matrix based on the node's
 * scale is created and pushed on both the float and fixed point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_scale(struct GraphNodeScale *node) {
    struct GraphNode *children = saturn_geo_enter_scale(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_scale();
}

/**
 * Billboard enter: pushes the matrix stack unconditionally (matching the
 * pre-conversion code) before building the billboard matrix (which also
 * reads gCurGraphNodeCamera/gCurGraphNodeHeldObject/gCurGraphNodeObject but
 * does not mutate them), then appends the display list if any. Returns the
 * children pointer to descend into, or NULL if there are none; the caller
 * must ALWAYS pair this with saturn_geo_leave_billboard() to balance the
 * unconditional push.
 */
static struct GraphNode *saturn_geo_enter_billboard(struct GraphNodeBillboard *node) {
    Mtx *mtx = alloc_display_list(sizeof(*mtx));

    gMatStackIndex++;
#ifdef TARGET_SATURN
    {
        int32_t tQ[3];
        saturn_vec3s_to_q16(tQ, node->translation);
        sm64_saturn_mtxq_billboard(&gMatStackQ[gMatStackIndex],
                                   &gMatStackQ[gMatStackIndex - 1], tQ,
                                   gCurGraphNodeCamera->roll);
        if (gCurGraphNodeHeldObject != NULL) {
            int32_t sQ[3];
            saturn_vec3f_to_q16(sQ, gCurGraphNodeHeldObject->objNode->header.gfx.scale);
            sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex],
                                         &gMatStackQ[gMatStackIndex], sQ);
        } else if (gCurGraphNodeObject != NULL) {
            int32_t sQ[3];
            saturn_vec3f_to_q16(sQ, gCurGraphNodeObject->scale);
            sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex],
                                         &gMatStackQ[gMatStackIndex], sQ);
        }
        saturn_mtxq_refresh_float_mirror(gMatStackIndex);
        saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
    }
#else
    Vec3f translation;
    vec3s_to_vec3f(translation, node->translation);
    mtxf_billboard(gMatStack[gMatStackIndex], gMatStack[gMatStackIndex - 1], translation,
                   gCurGraphNodeCamera->roll);
    if (gCurGraphNodeHeldObject != NULL) {
        mtxf_scale_vec3f(gMatStack[gMatStackIndex], gMatStack[gMatStackIndex],
                         gCurGraphNodeHeldObject->objNode->header.gfx.scale);
    } else if (gCurGraphNodeObject != NULL) {
        mtxf_scale_vec3f(gMatStack[gMatStackIndex], gMatStack[gMatStackIndex],
                         gCurGraphNodeObject->scale);
    }

    mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = mtx;
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Billboard leave: always pops the matrix stack, balancing the
 * unconditional push in saturn_geo_enter_billboard().
 */
static void saturn_geo_leave_billboard(void) {
    gMatStackIndex--;
}

/**
 * Process a billboard node. A transformation matrix is created that makes its
 * children face the camera, and it is pushed on the floating point and fixed
 * point matrix stacks.
 * For the rest it acts as a normal display list node.
 */
static void geo_process_billboard(struct GraphNodeBillboard *node) {
    struct GraphNode *children = saturn_geo_enter_billboard(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_billboard();
}

/**
 * Display-list enter: appends the display list if any -- no matrix stack
 * or global state touched at all, no leave action needed. Returns the
 * children pointer to descend into, or NULL if there are none.
 */
static struct GraphNode *saturn_geo_enter_display_list(struct GraphNodeDisplayList *node) {
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Process a display list node. It draws a display list without first pushing
 * a transformation on the stack, so all transformations are inherited from the
 * parent node. It processes its children if it has them.
 */
static void geo_process_display_list(struct GraphNodeDisplayList *node) {
    struct GraphNode *children = saturn_geo_enter_display_list(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
}

/**
 * Generated-list enter: runs fnNode.func to optionally build/append a
 * display list -- no matrix stack touched, no leave action needed. Returns
 * the children pointer to descend into, or NULL if there are none.
 */
static struct GraphNode *saturn_geo_enter_generated_list(struct GraphNodeGenerated *node) {
    if (node->fnNode.func != NULL) {
        Gfx *list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node,
                                     (struct AllocOnlyPool *) gMatStack[gMatStackIndex]);

        if (list != NULL) {
            geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
        }
    }
    return node->fnNode.node.children;
}

/**
 * Process a generated list. Instead of storing a pointer to a display list,
 * the list is generated on the fly by a function.
 */
static void geo_process_generated_list(struct GraphNodeGenerated *node) {
    struct GraphNode *children = saturn_geo_enter_generated_list(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
}

/**
 * Background enter: tries to retrieve a background display list from the
 * function of the node; if that function is null or returns null, a black
 * rectangle is drawn instead -- exactly matching the pre-conversion
 * unconditional (not gated on having children) append/fallback. No leave
 * action is needed -- the original handler never touched the matrix stack
 * or any global that needs post-child restoration. Returns whether there
 * are children to descend into.
 */
static bool saturn_geo_enter_background(struct GraphNodeBackground *node) {
    Gfx *list = NULL;

    if (node->fnNode.func != NULL) {
        list = node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node,
                                 (struct AllocOnlyPool *) gMatStack[gMatStackIndex]);
    }
    if (list != NULL) {
        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(list), node->fnNode.node.flags >> 8);
    } else if (gCurGraphNodeMasterList != NULL) {
#ifndef F3DEX_GBI_2E
        Gfx *gfxStart = alloc_display_list(sizeof(Gfx) * 7);
#else
        Gfx *gfxStart = alloc_display_list(sizeof(Gfx) * 8);
#endif
        Gfx *gfx = gfxStart;

        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_FILL);
        gDPSetFillColor(gfx++, node->background);
        gDPFillRectangle(gfx++, GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(0), BORDER_HEIGHT,
        GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(0) - 1, SCREEN_HEIGHT - BORDER_HEIGHT - 1);
        gDPPipeSync(gfx++);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gSPEndDisplayList(gfx++);

        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(gfxStart), 0);
    }
    return node->fnNode.node.children != NULL;
}

/**
 * Process a background node.
 */
static void geo_process_background(struct GraphNodeBackground *node) {
    if (saturn_geo_enter_background(node)) {
        (void) saturn_geo_walk_process_children(node->fnNode.node.children);
    }
}

/**
 * Animated-part enter: mutates gCurAnimType/gCurrAnimAttribute (animation
 * state that intentionally persists into the child subtree and beyond,
 * matching the pre-conversion behavior -- NOT saved/restored here), builds
 * the animated transform, and pushes the matrix stack unconditionally
 * (matching the pre-conversion code). Returns the children pointer to
 * descend into, or NULL if there are none; the caller must ALWAYS pair
 * this with saturn_geo_leave_animated_part() to balance the unconditional
 * push.
 */
static struct GraphNode *saturn_geo_enter_animated_part(struct GraphNodeAnimatedPart *node) {
    UNUSED Mat4 matrix;
    Vec3s rotation;
    Vec3f translation;
    Mtx *matrixPtr = alloc_display_list(sizeof(*matrixPtr));

    vec3s_copy(rotation, gVec3sZero);
    vec3f_set(translation, node->translation[0], node->translation[1], node->translation[2]);
    if (gCurAnimType == ANIM_TYPE_TRANSLATION) {
        translation[0] += gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                          * gCurAnimTranslationMultiplier;
        translation[1] += gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                          * gCurAnimTranslationMultiplier;
        translation[2] += gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                          * gCurAnimTranslationMultiplier;
        gCurAnimType = ANIM_TYPE_ROTATION;
    } else {
        if (gCurAnimType == ANIM_TYPE_LATERAL_TRANSLATION) {
            translation[0] +=
                gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                * gCurAnimTranslationMultiplier;
            gCurrAnimAttribute += 2;
            translation[2] +=
                gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                * gCurAnimTranslationMultiplier;
            gCurAnimType = ANIM_TYPE_ROTATION;
        } else {
            if (gCurAnimType == ANIM_TYPE_VERTICAL_TRANSLATION) {
                gCurrAnimAttribute += 2;
                translation[1] +=
                    gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                    * gCurAnimTranslationMultiplier;
                gCurrAnimAttribute += 2;
                gCurAnimType = ANIM_TYPE_ROTATION;
            } else if (gCurAnimType == ANIM_TYPE_NO_TRANSLATION) {
                gCurrAnimAttribute += 6;
                gCurAnimType = ANIM_TYPE_ROTATION;
            }
        }
    }

    if (gCurAnimType == ANIM_TYPE_ROTATION) {
        rotation[0] = gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
        rotation[1] = gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
        rotation[2] = gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)];
    }
#ifdef TARGET_SATURN
    {
        sm64_saturn_mtx_t nodeQ;
        int32_t tQ[3];
        saturn_vec3f_to_q16(tQ, translation);
        sm64_saturn_mtxq_rotate_xyz_and_translate(&nodeQ, tQ, rotation[0], rotation[1],
                                                  rotation[2]);
        (void) sm64_saturn_matrix_mul(&nodeQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    }
#else
    mtxf_rotate_xyz_and_translate(matrix, translation, rotation);
    mtxf_mul(gMatStack[gMatStackIndex + 1], matrix, gMatStack[gMatStackIndex]);
#endif
    gMatStackIndex++;
#ifdef TARGET_SATURN
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
    saturn_mtxq_write_wire(matrixPtr, &gMatStackQ[gMatStackIndex]);
#else
    mtxf_to_mtx(matrixPtr, gMatStack[gMatStackIndex]);
#endif
    gMatStackFixed[gMatStackIndex] = matrixPtr;
    if (node->displayList != NULL) {
        geo_append_display_list(node->displayList, node->node.flags >> 8);
    }
    return node->node.children;
}

/**
 * Animated-part leave: always pops the matrix stack, balancing the
 * unconditional push in saturn_geo_enter_animated_part(). The animation
 * globals mutated in the enter phase are deliberately NOT restored here
 * (matches pre-conversion behavior -- animation state is meant to flow
 * downward and persist).
 */
static void saturn_geo_leave_animated_part(void) {
    gMatStackIndex--;
}

/**
 * Render an animated part. The current animation state is not part of the node
 * but set in global variables. If an animated part is skipped, everything afterwards desyncs.
 */
static void geo_process_animated_part(struct GraphNodeAnimatedPart *node) {
    struct GraphNode *children = saturn_geo_enter_animated_part(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
    saturn_geo_leave_animated_part();
}

/**
 * Initialize the animation-related global variables for the currently drawn
 * object's animation.
 */
void geo_set_animation_globals(struct AnimInfo *node, s32 hasAnimation) {
    struct Animation *anim = node->curAnim;

    if (hasAnimation) {
        node->animFrame = geo_update_animation_frame(node, &node->animFrameAccelAssist);
    }
    node->animTimer = gAreaUpdateCounter;
    if (anim->flags & ANIM_FLAG_HOR_TRANS) {
        gCurAnimType = ANIM_TYPE_VERTICAL_TRANSLATION;
    } else if (anim->flags & ANIM_FLAG_VERT_TRANS) {
        gCurAnimType = ANIM_TYPE_LATERAL_TRANSLATION;
    } else if (anim->flags & ANIM_FLAG_6) {
        gCurAnimType = ANIM_TYPE_NO_TRANSLATION;
    } else {
        gCurAnimType = ANIM_TYPE_TRANSLATION;
    }

    gCurrAnimFrame = node->animFrame;
    gCurAnimEnabled = (anim->flags & ANIM_FLAG_5) == 0;
    gCurrAnimAttribute = segmented_to_virtual((void *) anim->index);
    gCurAnimData = segmented_to_virtual((void *) anim->values);

    if (anim->animYTransDivisor == 0) {
        gCurAnimTranslationMultiplier = 1.0f;
    } else {
        gCurAnimTranslationMultiplier = (f32) node->animYTrans / (f32) anim->animYTransDivisor;
    }
}

/**
 * Shadow enter: renders the shadow display list (if any), fully balancing
 * its own internal gMatStackIndex push/pop around the shadow display
 * list's transform BEFORE returning -- that push/pop is entirely self-
 * contained and unrelated to the child subtree below. No leave action is
 * needed for the child subtree itself. Returns the children pointer to
 * descend into, or NULL if there are none.
 */
static struct GraphNode *saturn_geo_enter_shadow(struct GraphNodeShadow *node) {
    Gfx *shadowList;
    UNUSED Mat4 mtxf;
    Vec3f shadowPos;
    Vec3f animOffset;
    f32 objScale;
    f32 shadowScale;
    f32 sinAng;
    f32 cosAng;
    struct GraphNode *geo;
    Mtx *mtx;

    if (gCurGraphNodeCamera != NULL && gCurGraphNodeObject != NULL) {
        if (gCurGraphNodeHeldObject != NULL) {
            get_pos_from_transform_mtx(shadowPos, gMatStack[gMatStackIndex],
                                       *gCurGraphNodeCamera->matrixPtr);
            shadowScale = node->shadowScale;
        } else {
            vec3f_copy(shadowPos, gCurGraphNodeObject->pos);
            shadowScale = node->shadowScale * gCurGraphNodeObject->scale[0];
        }

        objScale = 1.0f;
        if (gCurAnimEnabled) {
            if (gCurAnimType == ANIM_TYPE_TRANSLATION
                || gCurAnimType == ANIM_TYPE_LATERAL_TRANSLATION) {
                geo = node->node.children;
                if (geo != NULL && geo->type == GRAPH_NODE_TYPE_SCALE) {
                    objScale = ((struct GraphNodeScale *) geo)->scale;
                }
                animOffset[0] =
                    gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                    * gCurAnimTranslationMultiplier * objScale;
                animOffset[1] = 0.0f;
                gCurrAnimAttribute += 2;
                animOffset[2] =
                    gCurAnimData[retrieve_animation_index(gCurrAnimFrame, &gCurrAnimAttribute)]
                    * gCurAnimTranslationMultiplier * objScale;
                gCurrAnimAttribute -= 6;

                // simple matrix rotation so the shadow offset rotates along with the object
                sinAng = sins(gCurGraphNodeObject->angle[1]);
                cosAng = coss(gCurGraphNodeObject->angle[1]);

                shadowPos[0] += animOffset[0] * cosAng + animOffset[2] * sinAng;
                shadowPos[2] += -animOffset[0] * sinAng + animOffset[2] * cosAng;
            }
        }

        shadowList = create_shadow_below_xyz(shadowPos[0], shadowPos[1], shadowPos[2], shadowScale,
                                             node->shadowSolidity, node->shadowType);
        if (shadowList != NULL) {
            mtx = alloc_display_list(sizeof(*mtx));
            gMatStackIndex++;
#ifdef TARGET_SATURN
            {
                sm64_saturn_mtx_t tQm;
                int32_t tQ[3];
                saturn_vec3f_to_q16(tQ, shadowPos);
                sm64_saturn_mtxq_translate(&tQm, tQ);
                (void) sm64_saturn_matrix_mul(&tQm, sSaturnCameraMatrixQ,
                                              &gMatStackQ[gMatStackIndex]);
            }
            saturn_mtxq_refresh_float_mirror(gMatStackIndex);
            saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
            mtxf_translate(mtxf, shadowPos);
            mtxf_mul(gMatStack[gMatStackIndex], mtxf, *gCurGraphNodeCamera->matrixPtr);
            mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
            gMatStackFixed[gMatStackIndex] = mtx;
            if (gShadowAboveWaterOrLava == TRUE) {
                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowList), 4);
            } else if (gMarioOnIceOrCarpet == 1) {
                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowList), 5);
            } else {
                geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(shadowList), 6);
            }
            gMatStackIndex--;
        }
    }
    return node->node.children;
}

/**
 * Process a shadow node. Renders a shadow under an object offset by the
 * translation of the first animated component and rotated according to
 * the floor below it.
 */
static void geo_process_shadow(struct GraphNodeShadow *node) {
    struct GraphNode *children = saturn_geo_enter_shadow(node);
    if (children != NULL) {
        (void) saturn_geo_walk_process_children(children);
    }
}

/**
 * Check whether an object is in view to determine whether it should be drawn.
 * This is known as frustum culling.
 * It checks whether the object is far away, very close / behind the camera,
 * or horizontally out of view. It does not check whether it is vertically
 * out of view. It assumes a sphere of 300 units around the object's position
 * unless the object has a culling radius node that specifies otherwise.
 *
 * The matrix parameter should be the top of the matrix stack, which is the
 * object's transformation matrix times the camera 'look-at' matrix. The math
 * is counter-intuitive, but it checks column 3 (translation vector) of this
 * matrix to determine where the origin (0,0,0) in object space will be once
 * transformed to camera space (x+ = right, y+ = up, z = 'coming out the screen').
 * In 3D graphics, you typically model the world as being moved in front of a
 * static camera instead of a moving camera through a static world, which in
 * this case simplifies calculations. Note that the perspective matrix is not
 * on the matrix stack, so there are still calculations with the fov to compute
 * the slope of the lines of the frustum.
 *
 *        z-
 *
 *  \     |     /
 *   \    |    /
 *    \   |   /
 *     \  |  /
 *      \ | /
 *       \|/
 *        C       x+
 *
 * Since (0,0,0) is unaffected by rotation, columns 0, 1 and 2 are ignored.
 */
static s32 obj_is_in_view(struct GraphNodeObject *node, Mat4 matrix) {
    s16 cullingRadius;
    s16 halfFov; // half of the fov in in-game angle units instead of degrees
    struct GraphNode *geo;
    f32 hScreenEdge;

    if (node->node.flags & GRAPH_RENDER_INVISIBLE) {
        return FALSE;
    }

    geo = node->sharedChild;

    // ! @bug The aspect ratio is not accounted for. When the fov value is 45,
    // the horizontal effective fov is actually 60 degrees, so you can see objects
    // visibly pop in or out at the edge of the screen.
    halfFov = (gCurGraphNodeCamFrustum->fov / 2.0f + 1.0f) * 32768.0f / 180.0f + 0.5f;

    hScreenEdge = -matrix[3][2] * sins(halfFov) / coss(halfFov);
    // -matrix[3][2] is the depth, which gets multiplied by tan(halfFov) to get
    // the amount of units between the center of the screen and the horizontal edge
    // given the distance from the object to the camera.

#ifdef WIDESCREEN
    // This multiplication should really be performed on 4:3 as well,
    // but the issue will be more apparent on widescreen.
    hScreenEdge *= GFX_DIMENSIONS_ASPECT_RATIO;
#endif

    if (geo != NULL && geo->type == GRAPH_NODE_TYPE_CULLING_RADIUS) {
        cullingRadius =
            (f32)((struct GraphNodeCullingRadius *) geo)->cullingRadius; //! Why is there a f32 cast?
    } else {
        cullingRadius = 300;
    }

    // Don't render if the object is close to or behind the camera
    if (matrix[3][2] > -100.0f + cullingRadius) {
        return FALSE;
    }

    //! This makes the HOLP not update when the camera is far away, and it
    //  makes PU travel safe when the camera is locked on the main map.
    //  If Mario were rendered with a depth over 65536 it would cause overflow
    //  when converting the transformation matrix to a fixed point matrix.
    if (matrix[3][2] < -20000.0f - cullingRadius) {
        return FALSE;
    }

    // Check whether the object is horizontally in view
    if (matrix[3][0] > hScreenEdge + cullingRadius) {
        return FALSE;
    }
    if (matrix[3][0] < -hScreenEdge - cullingRadius) {
        return FALSE;
    }
    return TRUE;
}

/**
 * The two subtrees an object node walks, plus enough state to drive both
 * the boundary cleanup (needed only if sharedChild was walked) and the
 * final cleanup (needed whenever the node was admitted at all, i.e.
 * areaIndex matched -- see admitted below -- REGARDLESS of whether either
 * subtree pointer ended up non-NULL: an admitted-but-not-visible object
 * still pushed a matrix-stack slot and possibly began an actor
 * observation in saturn_geo_enter_object, both of which must still be
 * unwound).
 */
struct saturn_geo_object_children {
    struct GraphNode *shared_child;   /* NULL unless visible AND sharedChild != NULL */
    struct GraphNode *own_children;   /* NULL unless visible AND node.children != NULL */
    bool admitted;                    /* areaIndex == gCurGraphNodeRoot->areaIndex */
    bool actor_observed;              /* saturn_source_observe_object_begin's result */
};

/**
 * Object enter: the full pre-conversion body up through the
 * saturn_geo_visible gate, unchanged in substance and ordering (matrix
 * composition, matrix-stack push, cameraToObject, animation globals,
 * frustum-cull decision, actor-state observation) -- only the two
 * recursive calls at the very end are replaced, by reporting sharedChild/
 * node.children back to the caller instead of walking them directly. See
 * geo_process_object and saturn_geo_walk_enter's GRAPH_NODE_TYPE_OBJECT
 * case for the two contexts that consume this shared setup.
 *
 * Returns { NULL, NULL, false, false } (nothing else in this struct is
 * meaningful) when areaIndex does not match -- the pre-conversion
 * function's entire body was gated on that one check.
 */
static struct saturn_geo_object_children saturn_geo_enter_object(struct Object *node) {
    UNUSED Mat4 mtxf;
    s32 hasAnimation = (node->header.gfx.node.flags & GRAPH_RENDER_HAS_ANIMATION) != 0;
    struct saturn_geo_object_children out = { NULL, NULL, false, false };

    if (node->header.gfx.areaIndex != gCurGraphNodeRoot->areaIndex) {
        return out;
    }
    out.admitted = true;
#ifdef TARGET_SATURN
    out.actor_observed = saturn_source_observe_object_begin(node);
    if (node->header.gfx.throwMatrix != NULL) {
        /* throwMatrix here is always gameplay-owned float data
         * living OUTSIDE gMatStack at this read (mario.c quicksand,
         * obj_behaviors.c terrain-normal alignment, mario_actions_
         * moving.c floor align, object_helpers.c, tilting_inverted_
         * pyramid.inc.c) -- geo_process_object always overwrites the
         * pointer to alias gMatStack a few lines below, but that
         * hasn't happened yet at this read. Convert at the boundary
         * rather than trying to resolve a gMatStackQ index. */
        sm64_saturn_mtx_t throwQ;
        saturn_mat4_to_q16(&throwQ, *node->header.gfx.throwMatrix);
        (void) sm64_saturn_matrix_mul(&throwQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    } else if (node->header.gfx.node.flags & GRAPH_RENDER_BILLBOARD) {
        int32_t posQ[3];
        saturn_vec3f_to_q16(posQ, node->header.gfx.pos);
        sm64_saturn_mtxq_billboard(&gMatStackQ[gMatStackIndex + 1],
                                   &gMatStackQ[gMatStackIndex], posQ,
                                   gCurGraphNodeCamera->roll);
    } else {
        sm64_saturn_mtx_t nodeQ;
        int32_t posQ[3];
        saturn_vec3f_to_q16(posQ, node->header.gfx.pos);
        sm64_saturn_mtxq_rotate_zxy_and_translate(&nodeQ, posQ, node->header.gfx.angle[0],
                                                  node->header.gfx.angle[1],
                                                  node->header.gfx.angle[2]);
        (void) sm64_saturn_matrix_mul(&nodeQ, &gMatStackQ[gMatStackIndex],
                                      &gMatStackQ[gMatStackIndex + 1]);
    }

    {
        int32_t sQ[3];
        saturn_vec3f_to_q16(sQ, node->header.gfx.scale);
        sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex + 1],
                                     &gMatStackQ[gMatStackIndex + 1], sQ);
    }
    node->header.gfx.throwMatrix = &gMatStack[++gMatStackIndex];
    /* Refresh MUST happen here, immediately, not deferred to the
     * mtxf_to_mtx-equivalent point below: cameraToObject (read for
     * positional audio via play_sound) and obj_is_in_view (frustum
     * culling -- a gameplay-visible decision) both read
     * gMatStack[gMatStackIndex] before this function reaches that
     * point. Deferring the refresh would leave both reading stale
     * data left over from whatever previously occupied this stack
     * slot. */
    saturn_mtxq_refresh_float_mirror(gMatStackIndex);
#else
    if (node->header.gfx.throwMatrix != NULL) {
        mtxf_mul(gMatStack[gMatStackIndex + 1], *node->header.gfx.throwMatrix,
                 gMatStack[gMatStackIndex]);
    } else if (node->header.gfx.node.flags & GRAPH_RENDER_BILLBOARD) {
        mtxf_billboard(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex],
                       node->header.gfx.pos, gCurGraphNodeCamera->roll);
    } else {
        mtxf_rotate_zxy_and_translate(mtxf, node->header.gfx.pos, node->header.gfx.angle);
        mtxf_mul(gMatStack[gMatStackIndex + 1], mtxf, gMatStack[gMatStackIndex]);
    }

    mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1],
                     node->header.gfx.scale);
    node->header.gfx.throwMatrix = &gMatStack[++gMatStackIndex];
#endif
    node->header.gfx.cameraToObject[0] = gMatStack[gMatStackIndex][3][0];
    node->header.gfx.cameraToObject[1] = gMatStack[gMatStackIndex][3][1];
    node->header.gfx.cameraToObject[2] = gMatStack[gMatStackIndex][3][2];

    // FIXME: correct types
    if (node->header.gfx.animInfo.curAnim != NULL) {
        geo_set_animation_globals(&node->header.gfx.animInfo, hasAnimation);
    }
    {
        const s32 saturn_geo_visible =
            obj_is_in_view(&node->header.gfx, gMatStack[gMatStackIndex]);
#ifdef TARGET_SATURN
        /* Observe the already-authoritative culling decision only.  The
         * observer cannot select children, mutate the object, or replace the
         * normal geo walk; it is a scalar telemetry seam for snapshots. */
        sm64_saturn_geo_state_observer_record_authoritative_geo_decision(
            saturn_geo_visible != 0);
#endif
        if (saturn_geo_visible) {
            Mtx *mtx = alloc_display_list(sizeof(*mtx));

#ifdef TARGET_SATURN
            saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
            mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
            gMatStackFixed[gMatStackIndex] = mtx;
            if (node->header.gfx.sharedChild != NULL) {
                gCurGraphNodeObject = (struct GraphNodeObject *) node;
                node->header.gfx.sharedChild->parent = &node->header.gfx.node;
                out.shared_child = node->header.gfx.sharedChild;
            }
            if (node->header.gfx.node.children != NULL) {
                out.own_children = node->header.gfx.node.children;
            }
        }
    }
    return out;
}

/**
 * Object boundary: clears the temporary parent alias and gCurGraphNodeObject
 * claim set by saturn_geo_enter_object above, exactly matching the
 * pre-conversion post-sharedChild-subtree cleanup. Always safe to
 * dereference node->header.gfx.sharedChild here -- only ever called when
 * sharedChild was non-NULL at enter time.
 */
static void saturn_geo_leave_object_boundary(struct Object *node) {
    node->header.gfx.sharedChild->parent = NULL;
    gCurGraphNodeObject = NULL;
}

/**
 * Object final leave: pops the matrix-stack slot pushed in
 * saturn_geo_enter_object, ends the actor observation snapshot if one was
 * begun, and resets the per-object animation/throwMatrix globals -- exactly
 * matching the pre-conversion code, which ran this unconditionally once
 * admitted (areaIndex matched), regardless of saturn_geo_visible.
 */
static void saturn_geo_leave_object_final(struct Object *node, bool actor_observed) {
    gMatStackIndex--;
#ifdef TARGET_SATURN
    if (actor_observed) {
        (void) sm64_saturn_geo_state_observer_end_object(
            sm64_saturn_geo_state_observer_bound());
    }
#endif
    gCurAnimType = ANIM_TYPE_NONE;
    node->header.gfx.throwMatrix = NULL;
}

/**
 * Process an object node.
 *
 * This function's own sibling continuation (if any) is owned by whichever
 * caller reached it, exactly as geo_process_object_parent's own comment
 * explains -- so this walks each subtree directly via
 * saturn_geo_walk_process_children(child-list) rather than submitting
 * 'node' itself as a walk root.
 */
static void geo_process_object(struct Object *node) {
    struct saturn_geo_object_children children = saturn_geo_enter_object(node);

    if (!children.admitted) {
        return;
    }
    if (children.shared_child != NULL) {
        (void) saturn_geo_walk_process_children(children.shared_child);
        saturn_geo_leave_object_boundary(node);
    }
    if (children.own_children != NULL) {
        (void) saturn_geo_walk_process_children(children.own_children);
    }
    saturn_geo_leave_object_final(node, children.actor_observed);
}

/**
 * The two subtrees an object-parent node walks: 'sharedChild' (child A,
 * under a temporary parent alias) and 'node.children' (child B, in
 * practice always NULL -- the live object list is threaded through
 * sharedChild's own sibling chain, not this field). A child pointer of
 * NULL here means that subtree does not exist for this visit.
 */
struct saturn_geo_object_parent_children {
    struct GraphNode *shared_child;
    struct GraphNode *own_children;
};

/**
 * Object-parent enter: aliases sharedChild's parent pointer to this node
 * (matching the pre-conversion aliasing below, needed because sharedChild
 * is not really this node's child in the graph -- it is the live object
 * list's head, temporarily borrowed) and reports both subtrees. Does not
 * itself decide how the two subtrees get sequenced -- see
 * geo_process_object_parent (this node reached via real recursion or as a
 * true top-level call) and saturn_geo_walk_enter's GRAPH_NODE_TYPE_OBJECT_
 * PARENT case (this node reached as a child within an already-active
 * walk) for the two contexts that both consume this same setup.
 */
static struct saturn_geo_object_parent_children
saturn_geo_enter_object_parent(struct GraphNodeObjectParent *node) {
    struct saturn_geo_object_parent_children out = { NULL, node->node.children };
    if (node->sharedChild != NULL) {
        node->sharedChild->parent = (struct GraphNode *) node;
        out.shared_child = node->sharedChild;
    }
    return out;
}

/**
 * Object-parent boundary/leave: clears the temporary parent alias set by
 * saturn_geo_enter_object_parent above, exactly matching the pre-conversion
 * post-sharedChild-subtree cleanup. Always safe to dereference
 * node->sharedChild here -- this is only ever called when sharedChild was
 * non-NULL at enter time (see both call sites above).
 */
static void saturn_geo_leave_object_parent_boundary(struct GraphNodeObjectParent *node) {
    node->sharedChild->parent = NULL;
}

/**
 * Process an object parent node. Temporarily assigns itself as the parent of
 * the subtree rooted at 'sharedChild' and processes the subtree, after which
 * the actual children are be processed. (in practice they are null though)
 *
 * This function's own sibling continuation (if any) is owned by whichever
 * caller reached it (geo_process_node_and_siblings's own loop for a true
 * top-level/real-recursion call, or a legacy-dispatched handler's own
 * recursion) -- so, unlike saturn_geo_walk_enter's GRAPH_NODE_TYPE_OBJECT_
 * PARENT case below, this walks each subtree directly via
 * saturn_geo_walk_process_children(child-list) rather than submitting
 * 'node' itself as a walk root: the two subtrees are always DIFFERENT,
 * nested lists, so this never risks the double-processing a self-submit
 * would risk against this node's own already-owned sibling.
 */
static void geo_process_object_parent(struct GraphNodeObjectParent *node) {
    struct saturn_geo_object_parent_children children = saturn_geo_enter_object_parent(node);

    if (children.shared_child != NULL) {
        (void) saturn_geo_walk_process_children(children.shared_child);
        saturn_geo_leave_object_parent_boundary(node);
    }
    if (children.own_children != NULL) {
        (void) saturn_geo_walk_process_children(children.own_children);
    }
}

/**
 * The two subtrees a held-object node walks: the held object's own
 * skeleton (child A, node->objNode->header.gfx.sharedChild, only if
 * objNode is non-NULL and has one) and this node's own authored children
 * (child B, node->fnNode.node.children -- fires unconditionally,
 * regardless of whether child A existed, exactly matching the
 * pre-conversion code's own two independent if-blocks).
 */
struct saturn_geo_held_object_children {
    struct GraphNode *shared_child;   /* NULL unless objNode && objNode->...sharedChild */
    struct GraphNode *own_children;   /* node->fnNode.node.children, unconditionally as-is */
};

/**
 * Held-object enter: the full pre-conversion body up through (and
 * including) the matrix-stack push and anim-state save/claim, unchanged in
 * substance and ordering -- only the child-A recursive call at the end of
 * the objNode-gated block is replaced, by reporting the two subtrees back
 * to the caller instead of walking child A directly. See
 * geo_process_held_object and saturn_geo_walk_enter's GRAPH_NODE_TYPE_
 * HELD_OBJ case for the two contexts that consume this shared setup.
 *
 * NOTE the asymmetry versus saturn_geo_enter_object/_object_parent: the
 * matrix-stack PUSH (gMatStackIndex++) happens here, in enter, strictly
 * BEFORE child A is walked -- but the matching POP happens in
 * saturn_geo_leave_held_object_boundary below, which fires AFTER child A's
 * subtree drains (as either a true boundary_action, when child B also
 * exists, or a leave_action, when it does not -- see that function's own
 * comment). This mirrors the pre-conversion code exactly: gMatStackIndex++
 * appears before the recursive sharedChild call, gMatStackIndex-- appears
 * after it returns.
 */
static struct saturn_geo_held_object_children
saturn_geo_enter_held_object(struct GraphNodeHeldObject *node) {
#ifndef TARGET_SATURN
    Mat4 mat;
#endif
    Vec3f translation;
    Mtx *mtx = alloc_display_list(sizeof(*mtx));
    struct saturn_geo_held_object_children out = { NULL, node->fnNode.node.children };

#ifdef F3DEX_GBI_2
    gSPLookAt(gDisplayListHead++, &lookAt);
#endif

    if (node->fnNode.func != NULL) {
        node->fnNode.func(GEO_CONTEXT_RENDER, &node->fnNode.node, gMatStack[gMatStackIndex]);
    }
    if (node->objNode != NULL && node->objNode->header.gfx.sharedChild != NULL) {
        s32 hasAnimation = (node->objNode->header.gfx.node.flags & GRAPH_RENDER_HAS_ANIMATION) != 0;

        translation[0] = node->translation[0] / 4.0f;
        translation[1] = node->translation[1] / 4.0f;
        translation[2] = node->translation[2] / 4.0f;

#ifdef TARGET_SATURN
        {
            sm64_saturn_mtx_t matQ;
            int32_t tQ[3];
            int32_t sQ[3];
            /* gCurGraphNodeObject->throwMatrix is unconditionally set
             * to &gMatStack[N] by geo_process_object (see that
             * function) before any child -- including this held-object
             * node -- can run, so this pointer always aliases gMatStack
             * here (unlike the object site's OWN throwMatrix read,
             * which can observe a gameplay-owned external matrix
             * instead). Recover N to index the Q16 twin exactly. */
            s32 throwIdx = saturn_mtxq_gmatstack_index(gCurGraphNodeObject->throwMatrix);

            saturn_vec3f_to_q16(tQ, translation);
            sm64_saturn_mtxq_translate(&matQ, tQ);
            gMatStackQ[gMatStackIndex + 1] = gMatStackQ[throwIdx];
            gMatStackQ[gMatStackIndex + 1].m[3][0] = gMatStackQ[gMatStackIndex].m[3][0];
            gMatStackQ[gMatStackIndex + 1].m[3][1] = gMatStackQ[gMatStackIndex].m[3][1];
            gMatStackQ[gMatStackIndex + 1].m[3][2] = gMatStackQ[gMatStackIndex].m[3][2];
            (void) sm64_saturn_matrix_mul(&matQ, &gMatStackQ[gMatStackIndex + 1],
                                          &gMatStackQ[gMatStackIndex + 1]);
            saturn_vec3f_to_q16(sQ, node->objNode->header.gfx.scale);
            sm64_saturn_mtxq_scale_vec3f(&gMatStackQ[gMatStackIndex + 1],
                                         &gMatStackQ[gMatStackIndex + 1], sQ);
            /* Refresh now, before the GEO_CONTEXT_HELD_OBJ callback
             * below: graph_node.h documents that a GraphNodeFunc
             * receives "the top of the float matrix stack with type
             * Mat4" for this context and may read it as real float
             * data. */
            saturn_mtxq_refresh_float_mirror(gMatStackIndex + 1);
        }
#else
        mtxf_translate(mat, translation);
        mtxf_copy(gMatStack[gMatStackIndex + 1], *gCurGraphNodeObject->throwMatrix);
        gMatStack[gMatStackIndex + 1][3][0] = gMatStack[gMatStackIndex][3][0];
        gMatStack[gMatStackIndex + 1][3][1] = gMatStack[gMatStackIndex][3][1];
        gMatStack[gMatStackIndex + 1][3][2] = gMatStack[gMatStackIndex][3][2];
        mtxf_mul(gMatStack[gMatStackIndex + 1], mat, gMatStack[gMatStackIndex + 1]);
        mtxf_scale_vec3f(gMatStack[gMatStackIndex + 1], gMatStack[gMatStackIndex + 1],
                         node->objNode->header.gfx.scale);
#endif
        if (node->fnNode.func != NULL) {
            node->fnNode.func(GEO_CONTEXT_HELD_OBJ, &node->fnNode.node,
                              (struct AllocOnlyPool *) gMatStack[gMatStackIndex + 1]);
        }
        gMatStackIndex++;
#ifdef TARGET_SATURN
        saturn_mtxq_write_wire(mtx, &gMatStackQ[gMatStackIndex]);
#else
        mtxf_to_mtx(mtx, gMatStack[gMatStackIndex]);
#endif
        gMatStackFixed[gMatStackIndex] = mtx;
        gGeoTempState.type = gCurAnimType;
        gGeoTempState.enabled = gCurAnimEnabled;
        gGeoTempState.frame = gCurrAnimFrame;
        gGeoTempState.translationMultiplier = gCurAnimTranslationMultiplier;
        gGeoTempState.attribute = gCurrAnimAttribute;
        gGeoTempState.data = gCurAnimData;
        gCurAnimType = 0;
        gCurGraphNodeHeldObject = (void *) node;
        if (node->objNode->header.gfx.animInfo.curAnim != NULL) {
            geo_set_animation_globals(&node->objNode->header.gfx.animInfo, hasAnimation);
        }

        out.shared_child = node->objNode->header.gfx.sharedChild;
    }
    return out;
}

/**
 * Held-object boundary/leave: restores the anim-state save and
 * gCurGraphNodeHeldObject claim from saturn_geo_enter_held_object above,
 * and pops the matrix-stack slot pushed there -- exactly matching the
 * pre-conversion post-sharedChild-subtree cleanup. No node-specific state
 * is needed (only globals), so this takes no parameters. Only ever called
 * when child A (the held object's sharedChild) was actually walked.
 */
static void saturn_geo_leave_held_object_boundary(void) {
    gCurGraphNodeHeldObject = NULL;
    gCurAnimType = gGeoTempState.type;
    gCurAnimEnabled = gGeoTempState.enabled;
    gCurrAnimFrame = gGeoTempState.frame;
    gCurAnimTranslationMultiplier = gGeoTempState.translationMultiplier;
    gCurrAnimAttribute = gGeoTempState.attribute;
    gCurAnimData = gGeoTempState.data;
    gMatStackIndex--;
}

/**
 * Process a held object node.
 *
 * This function's own sibling continuation (if any) is owned by whichever
 * caller reached it, exactly as geo_process_object_parent's own comment
 * explains -- so this walks each subtree directly via
 * saturn_geo_walk_process_children(child-list) rather than submitting
 * 'node' itself as a walk root.
 */
void geo_process_held_object(struct GraphNodeHeldObject *node) {
    struct saturn_geo_held_object_children children = saturn_geo_enter_held_object(node);

    if (children.shared_child != NULL) {
        (void) saturn_geo_walk_process_children(children.shared_child);
        saturn_geo_leave_held_object_boundary();
    }
    if (children.own_children != NULL) {
        (void) saturn_geo_walk_process_children(children.own_children);
    }
}

/**
 * Processes the children of the given GraphNode if it has any
 */
void geo_try_process_children(struct GraphNode *node) {
    if (node->children != NULL) {
        geo_process_node_and_siblings(node->children);
    }
}

/* ---------------------------------------------------------------------
 * Task 14 wave 1: bounded iterative geo-walk engine.
 *
 * geo_process_master_list/geo_process_ortho_projection/geo_process_
 * perspective/geo_process_camera above no longer recurse through
 * geo_process_node_and_siblings for their children; they call
 * saturn_geo_walk_process_children() below, which drives
 * saturn_geo_walk_runtime.h's bounded enter/dispatch/leave scheduler
 * (LWRAM frame span: sourceboot_geo_walk_frames, sized by the generated
 * depth manifest) instead of the SH-2 C call stack.
 *
 * This engine's ops.enter dispatch below is intentionally general: it
 * mirrors geo_process_node_and_siblings's full node-type switch
 * further down this file so that ANY node type reachable beneath a
 * converted handler's children is handled correctly, not just the four
 * types this wave converts. For node types not yet converted, enter()
 * synchronously delegates to their EXISTING, unmodified handler function
 * (saturn_geo_walk_dispatch_legacy) -- those handlers still recurse
 * through the real geo_process_node_and_siblings for their own
 * children, entirely independent of this walk instance. This is safe
 * (no shared-state reentrancy) because:
 *   - Each call to saturn_geo_walk_process_children() takes a FRESH,
 *     from-scratch sm64_saturn_geo_walk_runtime_init() and runs its
 *     bounded drive loop to completion before returning -- it is never
 *     left "suspended" mid-drain the way a truly reentrant use would
 *     require.
 *   - This wave's four converted types (MASTER_LIST, ORTHO_PROJECTION,
 *     PERSPECTIVE, CAMERA) are the level_geo.c-authored top-level scene
 *     skeleton: MASTER_LIST/ORTHO_PROJECTION/PERSPECTIVE appear only as
 *     GraphNodeRoot's direct children, and MASTER_LIST additionally
 *     carries its own pre-existing re-entrancy guard
 *     (gCurGraphNodeMasterList == NULL). CAMERA appears once per
 *     perspective branch. None of the four are ever authored inside an
 *     actor/object geo layout, so none of them can appear NESTED beneath
 *     a still-unconverted handler's subtree (Object, TranslationRotation,
 *     AnimatedPart, ...) -- the one path that WOULD make
 *     saturn_geo_walk_process_children() reentrant against its own
 *     still-active LWRAM frame span. Deliberately excluded from this
 *     wave for exactly that reason: GRAPH_NODE_TYPE_SWITCH_CASE and
 *     GRAPH_NODE_TYPE_LEVEL_OF_DETAIL, both of which ARE pervasively
 *     authored nested inside actor geo layouts (e.g. cap-state/eye-blink
 *     switches), so converting them before every type that can contain
 *     them is also converted would introduce exactly that reentrancy
 *     hazard. This dispatcher still handles both types correctly today
 *     via the legacy bridge (unchanged geo_process_switch/geo_process_
 *     level_of_detail, unaffected by this walk instance).
 *
 * Task 14 wave 2 (2026-08-07) adds exactly one more converted type,
 * GRAPH_NODE_TYPE_BACKGROUND: `GEO_BACKGROUND` is authored only in
 * `levels/* /areas/*\/geo.inc.c` (verified: zero occurrences under
 * `actors/`), always as ORTHO_PROJECTION's child (already converted,
 * wave 1) for the area skybox -- it is level-authored top-level scene
 * skeleton in the same sense as wave 1's four types and cannot appear
 * nested beneath a still-unconverted handler's subtree, so it carries no
 * reentrancy risk. Its handler has a single child pointer and no
 * leave-phase restoration, fitting the established pattern exactly.
 *
 * Wave 2 evaluated the remaining 19 unconverted call sites (14 handler
 * types plus geo_try_process_children's own generic bridge and
 * geo_process_root's top-level kickoff call, neither of which is a
 * per-node-type handler) and found a real architectural blocker, not
 * merely a reentrancy-ordering one:
 * GRAPH_NODE_TYPE_OBJECT, GRAPH_NODE_TYPE_OBJECT_PARENT, and
 * GRAPH_NODE_TYPE_HELD_OBJ (geo_process_object/geo_process_object_parent/
 * geo_process_held_object) each process TWO independent, sequentially-
 * ordered child subtrees per node -- a `sharedChild` subtree under a
 * temporary `->parent` alias that must be cleared strictly *before* a
 * second, unrelated `->node.children` subtree begins -- and
 * `saturn_geo_walk_runtime.c`'s enter/dispatch/leave scheduler (verified
 * by reading its `push()`/`sm64_saturn_geo_walk_runtime_run()`
 * implementation) supports exactly one child subtree, one optional
 * dispatch, and one optional leave per ENTER event; there is no
 * mechanism to resume a node for a second, independently-restored child
 * subtree. This is a hard prerequisite blocker, not just an ordering
 * one: every other remaining node type (TRANSLATION_ROTATION,
 * TRANSLATION, ROTATION, SCALE, BILLBOARD, ANIMATED_PART, SHADOW,
 * DISPLAY_LIST, GENERATED_LIST, SWITCH_CASE, LEVEL_OF_DETAIL) is
 * authored pervasively inside actor geo layouts (verified per-macro
 * against `actors/*\/geo.inc.c`, e.g. GEO_DISPLAY_LIST: 89 actor files
 * vs. 341 level files; GEO_SCALE: 67 vs. 8; GEO_ANIMATED_PART: 56 vs. 1)
 * and is therefore reachable ONLY beneath OBJECT's `sharedChild`. None of
 * them can be safely converted (added to the ops.enter switch above)
 * until OBJECT/OBJECT_PARENT/HELD_OBJ themselves are -- doing so first
 * would let a nested occurrence, reached via the still-real-recursion
 * OBJECT/OBJECT_PARENT bridge from within an already-active outer drain
 * (e.g. CAMERA's), reinitialize the shared `sourceboot_geo_walk_frames`
 * span mid-drain, corrupting it -- and OBJECT/OBJECT_PARENT/HELD_OBJ
 * cannot themselves convert without either extending the runtime's frame
 * model to support a second, independently-restored child subtree per
 * node, or an owner-approved design accommodation. See
 * `docs/superpowers/plans/2026-08-07-task14-completion.md` Task 2's wave
 * 3 status for the reported blocker.
 *
 * Task 14 wave 3 lifts that blocker: `saturn_geo_walk_runtime.h`/`.c` now
 * support a second, independently-sequenced child subtree per ENTER event
 * (`second_child`, `boundary_action`/`boundary_required`, alongside the
 * existing `leave_action`/`leave_required` for the final action -- see
 * that header's `sm64_saturn_geo_walk_runtime_enter_t` comments for the
 * exact contract and `saturn_geo_walk_leave`'s own comment below for how
 * the three converted types below map their two independent cleanups onto
 * those two fields). GRAPH_NODE_TYPE_OBJECT, GRAPH_NODE_TYPE_OBJECT_PARENT,
 * and GRAPH_NODE_TYPE_HELD_OBJ are now converted, each via a
 * `saturn_geo_enter_*`/`saturn_geo_leave_*` pair defined next to their
 * respective `geo_process_*` function above (not down here) -- see those
 * functions' own comments for the exact per-type shape.
 *
 * This wave also found, and had to close, a SECOND reentrancy hazard --
 * the mirror image of the one wave 2 avoided by not converting the eleven
 * "skeleton" types (TRANSLATION_ROTATION, TRANSLATION, ROTATION, SCALE,
 * BILLBOARD, ANIMATED_PART, SHADOW, DISPLAY_LIST, GENERATED_LIST,
 * SWITCH_CASE, LEVEL_OF_DETAIL) yet. Converting OBJECT means an Object's
 * `sharedChild` subtree, when the Object is reached as a child within an
 * already-active walk (now the norm: OBJECT_PARENT is converted too, and
 * is always Camera's child -- wave 1), extends that SAME active walk
 * rather than starting a fresh one. But `sharedChild` leads straight into
 * those still-unconverted skeleton types -- verified against the real
 * shipped `mario_geo[]` layout (`actors/mario/geo.inc.c`), every
 * `GEO_HELD_OBJECT` occurrence is reached only via `GEO_SWITCH_CASE`/
 * `GEO_ANIMATED_PART`/`GEO_SCALE` ancestors, 13 levels deep from the
 * layout root -- which still dispatch through the legacy bridge below
 * into their real, unmodified, still-recursive handlers. If that real
 * recursion reaches another converted type (GRAPH_NODE_TYPE_HELD_OBJ, the
 * realistic "Mario holding something" case), `geo_process_node_and_
 * siblings`'s own switch calls `geo_process_held_object()` directly --
 * which, now that it is converted too, would otherwise call
 * `saturn_geo_walk_process_children()` again WHILE THE OUTER WALK IS
 * STILL ACTIVE many real C stack frames up, with real pending frame data
 * still sitting in `sourceboot_geo_walk_frames[0, outer_depth)`.
 * `sm64_saturn_geo_walk_runtime_init()` unconditionally resets depth to 0
 * and starts pushing at index 0 of that SAME shared array (the ONLY
 * production owner of these frames -- `saturn_geo_walk_storage.h`),
 * silently overwriting the outer walk's still-pending frames. This is
 * genuine memory corruption, not merely a capacity concern: once the
 * nested call returns and the outer walk's loop resumes, it pops frames
 * whose CONTENTS have been overwritten, using a depth counter that is
 * itself still correct but now indexes garbage.
 *
 * `sSaturnGeoWalkActive` (just below `saturn_geo_walk_process_children`)
 * guards against exactly this: any call arriving while it is already true
 * is, by construction, reached via a still-unconverted type's
 * real-recursion detour from within that outer call, and falls back to
 * plain recursion instead of touching the shared array -- exactly this
 * subtree's pre-conversion behavior. This does not regress anything
 * (a subtree reached this way already used real recursion before this
 * wave); it does mean this wave's bounded-stack benefit for a node
 * reached THIS way is deferred until the skeleton types convert in a
 * future wave, at which point this guard stops firing for that path
 * automatically, with no further changes needed to OBJECT/OBJECT_PARENT/
 * HELD_OBJ. See this task's completion report for the concrete capacity
 * analysis this guard was verified against.
 *
 * One more accounting note for whoever next runs
 * `geo_walk_source_policy_test.py`: converting these three handlers drops
 * the direct-call count by 6, not 3, because each of the three pre-
 * conversion functions contained TWO literal `geo_process_node_and_
 * siblings(` call sites (sharedChild and children), not one -- unlike
 * every other handler type, which had exactly one. 19 - 6 = 13, but the
 * sSaturnGeoWalkActive reentrancy-guard fallback above adds back exactly
 * one new, deliberate real-recursion call site (the fallback this guard
 * takes when it detects reentry), so the real live count is 13 + 1 = 14
 * -- confirmed by actually running the script, not just this arithmetic.
 * (Note for future editors of this comment: that policy script does a
 * raw text scan, not a comment-aware one -- spelling the dispatcher's
 * name out contiguously followed by "(" anywhere in this file, including
 * in a comment, adds a phantom match. This paragraph and the one above
 * it both avoid that deliberately.)
 *
 * Task 14's final sub-wave (2026-08-09) converts the eleven remaining
 * skeleton types wave 2 deliberately deferred: LEVEL_OF_DETAIL,
 * SWITCH_CASE, TRANSLATION_ROTATION, TRANSLATION, ROTATION, SCALE,
 * BILLBOARD, ANIMATED_PART, DISPLAY_LIST, GENERATED_LIST, and SHADOW.
 * Re-reading every one of their handlers in full found no two-subtree
 * shape among them (all eleven are true single-child, some with an
 * unconditional matrix-stack push/pop pair to balance, some with none)
 * and no ordering hazard: none of them themselves call `saturn_geo_walk_
 * process_children` directly, so none of them was ever a "fresh walk
 * starter" the way wave 1/2's level-authored types or wave 3's Object
 * family were feared to be if converted out of order. All eleven now
 * funnel through the exact same shared entry point every previously
 * converted type already used, which already carries the generic,
 * type-agnostic `sSaturnGeoWalkActive` reentrancy guard described above --
 * built in wave 3 specifically to remain correct once these skeleton
 * types converted, with no further changes needed here. Because this was
 * the last group of node types dispatched through the legacy bridge
 * (`saturn_geo_walk_dispatch_legacy`, just below), that bridge's switch is
 * now empty of real cases and has been collapsed to its default fallback;
 * see its own updated comment for what still legitimately reaches it.
 * `sSaturnGeoWalkActive`'s single deliberate real-recursion fallback call
 * site (the same one wave 3's accounting paragraph above counted) is now
 * the only one left in this file outside the dispatcher's own definition.
 *
 * Task 14's real final closure (2026-08-09) converts the last two node
 * types that could still route through that fallback in realistic
 * gameplay: GRAPH_NODE_TYPE_START and GRAPH_NODE_TYPE_CULLING_RADIUS.
 * Unlike every type converted above, neither ever had a dedicated
 * `geo_process_*()` wrapper to extract a `saturn_geo_enter_*` pair from --
 * confirmed by grep, `geo_process_node_and_siblings`'s own switch (further
 * down this file) has never had a case for either; both structurally fall
 * to ITS default, `geo_try_process_children`, which is unrelated to and
 * unchanged by this closure. What DOES change is `saturn_geo_walk_enter`'s
 * own switch just below: both types previously fell to ITS default case
 * too, `saturn_geo_walk_dispatch_legacy(node)`, which synchronously detours
 * into real, unbounded C recursion via `geo_try_process_children` ->
 * `geo_process_node_and_siblings`. `GRAPH_NODE_TYPE_START` is the one that
 * matters most: it is the literal first command of every actor's geo
 * layout (`GEO_NODE_START()`, confirmed as the entry command of 30+
 * `actors/*\/geo.inc.c` files and 8 level files), and `process_geo_layout`'s
 * return value for each becomes that actor Object's `sharedChild`
 * (`src/engine/level_script.c`/`src/engine/behavior_script.c`) -- meaning
 * `GRAPH_NODE_TYPE_OBJECT` (converted wave 3) has, until this commit, been
 * descending straight into an unconverted START and back into real
 * recursion on EVERY object render, including Mario's own body/limb/held-
 * object subtree (`actors/mario/geo.inc.c`'s `mario_geo_render_body`, 13
 * real `GraphNode` levels deep) during essentially all non-stationary
 * gameplay -- exactly the scenario the prior closure report (see this
 * file's CHANGELOG entry citing `task14-wave4-full-traversal-capacity-
 * margin-2026-08-09.md`) flagged as still real-recursion-reachable.
 * `GRAPH_NODE_TYPE_CULLING_RADIUS` carries the identical detour risk for
 * the same reason (34 occurrences across ~24 actor files, essentially
 * always wrapping real `GEO_OPEN_NODE()`/.../`GEO_CLOSE_NODE()` content,
 * e.g. `actors/toad/geo.inc.c`'s full body chain) and converts alongside
 * it in this same commit. Both are purely structural pass-through nodes
 * with zero per-visit side effects of their own -- `saturn_geo_enter_start`/
 * `saturn_geo_enter_culling_radius` below just hand back `node->children`,
 * no leave action needed, matching ORTHO_PROJECTION/BACKGROUND/DISPLAY_
 * LIST's no-leave shape rather than the matrix-stack-push types. With both
 * added as real cases, `saturn_geo_walk_enter`'s default case (still
 * `saturn_geo_walk_dispatch_legacy`) is unreachable for any node type that
 * can legitimately appear as a walk token -- the only type left unhandled
 * by that switch is `GRAPH_NODE_TYPE_ROOT`, which by construction never
 * appears as one (`geo_process_root` always drives its own children via
 * real recursion directly, never through `saturn_geo_walk_process_
 * children`). The default case is kept as a defensive fallback rather than
 * deleted, matching how the eleven-type sub-wave above already treated its
 * own now-unreachable bridge. `sSaturnGeoWalkActive`'s real-recursion
 * fallback itself is deliberately NOT removed by this commit -- it remains
 * the correct, safe behavior for the one call site that reaches it
 * (nested reentry from within an already-active walk), which this closure
 * does not change; this commit only removes the two node types that used
 * to reach real recursion via the OTHER path (the default-case detour),
 * not the reentrancy-guard path itself.
 * --------------------------------------------------------------------- */

enum {
    SATURN_GEO_LEAVE_NONE = 0U,
    SATURN_GEO_LEAVE_MASTER_LIST = 1U,
    SATURN_GEO_LEAVE_PERSPECTIVE = 2U,
    SATURN_GEO_LEAVE_CAMERA = 3U,
    /* Task 14 wave 3: object/object_parent/held_object action codes.
     * saturn_geo_walk_leave() below now receives calls carrying two
     * conceptually different meanings for these three types, distinguished
     * ONLY by which of these codes leave_action carries (see
     * saturn_geo_walk_runtime.h's sm64_saturn_geo_walk_runtime_leave_fn and
     * ops.leave doc comments for the general two-subtree contract): a
     * BOUNDARY call (fires strictly between the two subtrees) and a FINAL
     * call (fires once everything for the node has drained). When a node
     * has only one subtree at a given visit (second_child == 0), the
     * runtime's single-subtree path fires only ONE leave call for it, so
     * that call must do BOTH jobs -- SATURN_GEO_LEAVE_OBJECT_COMBINED
     * below is exactly that case for OBJECT; OBJECT_PARENT and HELD_OBJECT
     * have no separate final action, so their one boundary code already
     * serves as the combined code (requested via boundary_action OR
     * leave_action depending on which runtime path applies -- see each
     * type's saturn_geo_enter_* / geo_process_* comments). Keeping every
     * code numerically distinct in this one shared enum means a compiler
     * warning fires on an accidental duplicate, instead of two unrelated
     * restore paths silently cross-wiring. */
    SATURN_GEO_BOUNDARY_OBJECT_PARENT = 4U,
    SATURN_GEO_BOUNDARY_OBJECT = 5U,
    SATURN_GEO_LEAVE_OBJECT_FINAL = 6U,
    SATURN_GEO_LEAVE_OBJECT_COMBINED = 7U,
    SATURN_GEO_BOUNDARY_HELD_OBJECT = 8U,
    /* Task 14 final sub-wave: the six remaining single-child types that
     * push an unconditional matrix-stack slot in enter() and must pop it
     * in leave() regardless of whether the node had children (same shape
     * as CAMERA's SATURN_GEO_LEAVE_CAMERA above, minus the conditional
     * global clear -- these six only ever need the unconditional pop). */
    SATURN_GEO_LEAVE_TRANSLATION_ROTATION = 9U,
    SATURN_GEO_LEAVE_TRANSLATION = 10U,
    SATURN_GEO_LEAVE_ROTATION = 11U,
    SATURN_GEO_LEAVE_SCALE = 12U,
    SATURN_GEO_LEAVE_BILLBOARD = 13U,
    SATURN_GEO_LEAVE_ANIMATED_PART = 14U,
};

/* Named diagnostic for the runtime's fail-closed overflow latch (global
 * constraint: no silent truncation). The generated depth manifest sizes
 * sourceboot_geo_walk_frames to the proven max scene depth with margin,
 * so this should never increment in practice; it exists so an overflow
 * is observable rather than silently dropped. A dedicated telemetry/HUD
 * seam for this counter is left to a later task. */
static uint32_t sSaturnGeoWalkOverflowCount = 0U;

/**
 * Computes the sibling continuation for one node in a geo_add_child-built
 * ring, replicating geo_process_node_and_siblings's
 * "iterateChildren"/wraparound semantics without needing to thread the
 * chain's head pointer through the runtime's frame fields:
 *   - A switch-case's selected child is never chained to its sibling
 *     case options (matches the original's parent->type ==
 *     GRAPH_NODE_TYPE_SWITCH_CASE special case).
 *   - A self-looped node (geo_add_child's single-child encoding, and the
 *     temporary sharedChild/parent aliasing used by geo_process_object
 *     and geo_process_object_parent) has no sibling.
 *   - Every other node in a real ring shares node->parent, so
 *     node->parent->children is a time-invariant reference to the ring's
 *     head regardless of which member is currently being visited; this
 *     is exactly the original's `curGraphNode->next != firstNode` check.
 */
static uintptr_t saturn_geo_walk_sibling_of(const struct GraphNode *node) {
    const struct GraphNode *parent = node->parent;

    if (parent != NULL && parent->type == GRAPH_NODE_TYPE_SWITCH_CASE) {
        return 0U;
    }
    if (node->next == node) {
        return 0U;
    }
    if (parent != NULL && node->next == parent->children) {
        return 0U;
    }
    return (uintptr_t) node->next;
}

/**
 * Start-node enter: GraphNodeStart (`struct GraphNode node;` only, no
 * function pointer, no extra fields -- src/engine/graph_node.h:139-142) is
 * purely structural, exactly as its own doc comment already states ("Does
 * not have any additional functionality."). init_graph_node_start() does
 * nothing beyond allocating and stamping the type; a START node acquires
 * real children only via the ordinary geo_add_child()/register_scene_
 * graph_node() mechanism triggered by a following GEO_OPEN_NODE()/.../
 * GEO_CLOSE_NODE() block. No side effects, no leave action needed. Returns
 * the children pointer to descend into, or NULL if there are none.
 */
static struct GraphNode *saturn_geo_enter_start(struct GraphNodeStart *node) {
    return node->node.children;
}

/**
 * Culling-radius enter: GraphNodeCullingRadius (node + s16 cullingRadius +
 * padding -- src/engine/graph_node.h:342-347) carries no function pointer
 * and no per-visit dispatch logic; its only real-time effect is that
 * obj_is_in_view() peeks at ITS OWNER Object's sharedChild culling-radius
 * field before that Object is admitted at all (unrelated to this node's
 * own traversal). Walking into a CULLING_RADIUS node's own subtree, once
 * reached, is a pure structural pass-through exactly like START -- same
 * geo_add_child()-based child-acquisition mechanism, same "no case in
 * geo_process_node_and_siblings's switch either" fact. No side effects, no
 * leave action needed. Returns the children pointer to descend into, or
 * NULL if there are none.
 */
static struct GraphNode *saturn_geo_enter_culling_radius(struct GraphNodeCullingRadius *node) {
    return node->node.children;
}

/**
 * Bridge for node types not handled by a real case in saturn_geo_walk_
 * enter's own switch below, exactly mirroring geo_process_node_and_
 * siblings's own switch further down this file's default case.
 *
 * As of Task 14's real final closure, every node type that switch can
 * legitimately be handed as a walk token (MASTER_LIST, ORTHO_PROJECTION,
 * PERSPECTIVE, CAMERA, BACKGROUND, OBJECT, OBJECT_PARENT, HELD_OBJ, the
 * eleven skeleton types converted the sub-wave before this one, and now
 * START and CULLING_RADIUS) is intercepted directly by saturn_geo_walk_
 * enter's own switch before this bridge would ever be reached for it --
 * this function is now genuinely unreachable in realistic use. It remains
 * in place (rather than being deleted outright) as a defensive fallback:
 * the only node type saturn_geo_walk_enter's switch still has no case for
 * is GRAPH_NODE_TYPE_ROOT, which by construction never appears as a walk
 * token (geo_process_root always drives its own children via real
 * recursion directly, never through saturn_geo_walk_process_children).
 * Separately, geo_process_node_and_siblings's own switch -- which THIS
 * function's callers never touch, by construction -- can still reach a
 * fully-converted type via real recursion during the sSaturnGeoWalkActive
 * reentrancy-fallback path (see that flag's own comment above
 * saturn_geo_walk_process_children for the full mechanism); that fallback
 * calls geo_process_node_and_siblings directly, which owns its own switch,
 * not this one.
 */
static void saturn_geo_walk_dispatch_legacy(struct GraphNode *node) {
    geo_try_process_children(node);
}

/**
 * Runtime ops.enter: replicates geo_process_node_and_siblings's
 * RENDER_ACTIVE / CHILDREN_FIRST / inactive-object handling generically
 * for every node this walk instance visits, then dispatches by type --
 * real enter-phase logic for this wave's and prior waves' converted
 * types, the legacy bridge for everything else.
 */
static bool saturn_geo_walk_enter(uintptr_t node_token,
                                  sm64_saturn_geo_walk_runtime_enter_t *result,
                                  void *user) {
    struct GraphNode *node = (struct GraphNode *) node_token;
    (void) user;

    result->child = 0U;
    result->second_child = 0U;
    result->sibling = saturn_geo_walk_sibling_of(node);
    result->leave_action = SATURN_GEO_LEAVE_NONE;
    result->boundary_action = SATURN_GEO_LEAVE_NONE;
    result->matrix_depth = 0U;
    result->context_token = 0U;
    result->admitted = false;
    result->defer_dispatch = false;
    result->leave_required = false;
    result->boundary_required = false;

    if (!(node->flags & GRAPH_RENDER_ACTIVE)) {
        if (node->type == GRAPH_NODE_TYPE_OBJECT) {
            ((struct GraphNodeObject *) node)->throwMatrix = NULL;
        }
        return true;
    }

    if (node->flags & GRAPH_RENDER_CHILDREN_FIRST) {
        if (node->children != NULL) {
            result->admitted = true;
            result->child = (uintptr_t) node->children;
        }
        return true;
    }

    switch (node->type) {
        case GRAPH_NODE_TYPE_MASTER_LIST: {
            struct GraphNodeMasterList *ml = (struct GraphNodeMasterList *) node;
            if (saturn_geo_enter_master_list(ml)) {
                result->admitted = true;
                result->child = (uintptr_t) ml->node.children;
                result->leave_required = true;
                result->leave_action = SATURN_GEO_LEAVE_MASTER_LIST;
            }
            break;
        }
        case GRAPH_NODE_TYPE_ORTHO_PROJECTION: {
            struct GraphNodeOrthoProjection *op = (struct GraphNodeOrthoProjection *) node;
            if (saturn_geo_enter_ortho_projection(op)) {
                result->admitted = true;
                result->child = (uintptr_t) op->node.children;
            }
            break;
        }
        case GRAPH_NODE_TYPE_PERSPECTIVE: {
            struct GraphNodePerspective *pp = (struct GraphNodePerspective *) node;
            if (saturn_geo_enter_perspective(pp)) {
                result->admitted = true;
                result->child = (uintptr_t) pp->fnNode.node.children;
                result->leave_required = true;
                result->leave_action = SATURN_GEO_LEAVE_PERSPECTIVE;
            }
            break;
        }
        case GRAPH_NODE_TYPE_CAMERA: {
            struct GraphNodeCamera *cam = (struct GraphNodeCamera *) node;
            struct GraphNode *children = saturn_geo_enter_camera(cam);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_CAMERA;
            result->context_token = (children != NULL) ? 1U : 0U;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_BACKGROUND: {
            struct GraphNodeBackground *bg = (struct GraphNodeBackground *) node;
            if (saturn_geo_enter_background(bg)) {
                result->admitted = true;
                result->child = (uintptr_t) bg->fnNode.node.children;
            }
            break;
        }
        /* Task 14 final sub-wave: the eleven remaining single-child types.
         * Each saturn_geo_enter_* helper below already performs exactly
         * the same enter-phase work (matrix push, display list append,
         * selection callback, etc.) its pre-conversion geo_process_*
         * wrapper did -- unconditionally, before this switch examines the
         * returned child pointer -- then returns the one child pointer to
         * descend into (or NULL). See each helper's own comment (next to
         * its definition above) for the exact per-type shape and which of
         * these six need an unconditional leave-phase matrix-stack pop
         * (TRANSLATION_ROTATION, TRANSLATION, ROTATION, SCALE, BILLBOARD,
         * ANIMATED_PART -- same unconditional-push/unconditional-pop shape
         * as wave 1's CAMERA, minus the conditional global clear). The
         * other five (LEVEL_OF_DETAIL, SWITCH_CASE, DISPLAY_LIST,
         * GENERATED_LIST, SHADOW) need no leave action at all, matching
         * ORTHO_PROJECTION/BACKGROUND's shape above. */
        case GRAPH_NODE_TYPE_LEVEL_OF_DETAIL: {
            struct GraphNodeLevelOfDetail *lod = (struct GraphNodeLevelOfDetail *) node;
            if (saturn_geo_enter_level_of_detail(lod)) {
                result->admitted = true;
                result->child = (uintptr_t) lod->node.children;
            }
            break;
        }
        case GRAPH_NODE_TYPE_SWITCH_CASE: {
            struct GraphNodeSwitchCase *sw = (struct GraphNodeSwitchCase *) node;
            struct GraphNode *selectedChild = saturn_geo_enter_switch(sw);
            if (selectedChild != NULL) {
                result->admitted = true;
                result->child = (uintptr_t) selectedChild;
            }
            break;
        }
        case GRAPH_NODE_TYPE_TRANSLATION_ROTATION: {
            struct GraphNodeTranslationRotation *tr =
                (struct GraphNodeTranslationRotation *) node;
            struct GraphNode *children = saturn_geo_enter_translation_rotation(tr);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_TRANSLATION_ROTATION;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_TRANSLATION: {
            struct GraphNodeTranslation *tn = (struct GraphNodeTranslation *) node;
            struct GraphNode *children = saturn_geo_enter_translation(tn);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_TRANSLATION;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_ROTATION: {
            struct GraphNodeRotation *rn = (struct GraphNodeRotation *) node;
            struct GraphNode *children = saturn_geo_enter_rotation(rn);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_ROTATION;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_SCALE: {
            struct GraphNodeScale *sc = (struct GraphNodeScale *) node;
            struct GraphNode *children = saturn_geo_enter_scale(sc);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_SCALE;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_BILLBOARD: {
            struct GraphNodeBillboard *bb = (struct GraphNodeBillboard *) node;
            struct GraphNode *children = saturn_geo_enter_billboard(bb);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_BILLBOARD;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_ANIMATED_PART: {
            struct GraphNodeAnimatedPart *ap = (struct GraphNodeAnimatedPart *) node;
            struct GraphNode *children = saturn_geo_enter_animated_part(ap);
            result->admitted = true;
            result->leave_required = true;
            result->leave_action = SATURN_GEO_LEAVE_ANIMATED_PART;
            result->child = (uintptr_t) children;
            break;
        }
        case GRAPH_NODE_TYPE_DISPLAY_LIST: {
            struct GraphNodeDisplayList *dl = (struct GraphNodeDisplayList *) node;
            struct GraphNode *children = saturn_geo_enter_display_list(dl);
            if (children != NULL) {
                result->admitted = true;
                result->child = (uintptr_t) children;
            }
            break;
        }
        case GRAPH_NODE_TYPE_GENERATED_LIST: {
            struct GraphNodeGenerated *gl = (struct GraphNodeGenerated *) node;
            struct GraphNode *children = saturn_geo_enter_generated_list(gl);
            if (children != NULL) {
                result->admitted = true;
                result->child = (uintptr_t) children;
            }
            break;
        }
        case GRAPH_NODE_TYPE_SHADOW: {
            struct GraphNodeShadow *sh = (struct GraphNodeShadow *) node;
            struct GraphNode *children = saturn_geo_enter_shadow(sh);
            if (children != NULL) {
                result->admitted = true;
                result->child = (uintptr_t) children;
            }
            break;
        }
        /* Task 14 real final closure: START and CULLING_RADIUS, the last
         * two node types that could still reach this switch's default
         * case and detour into real, unbounded recursion via
         * saturn_geo_walk_dispatch_legacy. Both are pure structural
         * pass-through with no side effects of their own -- see each
         * saturn_geo_enter_* helper's own comment above for the full
         * per-type rationale, and this switch's own preceding block
         * comment for why GRAPH_NODE_TYPE_START in particular is the type
         * that actually closes the master-stack-overrun gap this whole
         * engine exists for. */
        case GRAPH_NODE_TYPE_START: {
            struct GraphNodeStart *st = (struct GraphNodeStart *) node;
            struct GraphNode *children = saturn_geo_enter_start(st);
            if (children != NULL) {
                result->admitted = true;
                result->child = (uintptr_t) children;
            }
            break;
        }
        case GRAPH_NODE_TYPE_CULLING_RADIUS: {
            struct GraphNodeCullingRadius *cr = (struct GraphNodeCullingRadius *) node;
            struct GraphNode *children = saturn_geo_enter_culling_radius(cr);
            if (children != NULL) {
                result->admitted = true;
                result->child = (uintptr_t) children;
            }
            break;
        }
        /* Task 14 wave 3: two-subtree types. Each case below translates
         * the type-specific saturn_geo_enter_* helper's simple
         * { shared_child, own_children, ... } result into this generic
         * child/second_child/boundary/leave shape, choosing between
         * the runtime's two-subtree path (both children present) and its
         * single-subtree path (see saturn_geo_walk_runtime.h's
         * second_child comment) the same way each type's own top-level
         * geo_process_*() function does -- see this file's wave 3 doc
         * comment above the SATURN_GEO_LEAVE_* enum for the full
         * reentrancy-safety argument these three types depend on. */
        case GRAPH_NODE_TYPE_OBJECT_PARENT: {
            struct GraphNodeObjectParent *op = (struct GraphNodeObjectParent *) node;
            struct saturn_geo_object_parent_children children =
                saturn_geo_enter_object_parent(op);
            result->admitted = (children.shared_child != NULL) ||
                               (children.own_children != NULL);
            if (children.shared_child != NULL) {
                result->child = (uintptr_t) children.shared_child;
                if (children.own_children != NULL) {
                    result->second_child = (uintptr_t) children.own_children;
                    result->boundary_required = true;
                    result->boundary_action = SATURN_GEO_BOUNDARY_OBJECT_PARENT;
                } else {
                    result->leave_required = true;
                    result->leave_action = SATURN_GEO_BOUNDARY_OBJECT_PARENT;
                }
            } else if (children.own_children != NULL) {
                result->child = (uintptr_t) children.own_children;
            }
            break;
        }
        case GRAPH_NODE_TYPE_OBJECT: {
            struct Object *obj = (struct Object *) node;
            struct saturn_geo_object_children children = saturn_geo_enter_object(obj);
            result->admitted = children.admitted;
            result->context_token = children.actor_observed ? 1U : 0U;
            if (children.admitted) {
                /* The final leave (matrix pop / observer-end / anim clear /
                 * throwMatrix=NULL) is needed unconditionally once
                 * admitted, per saturn_geo_enter_object's own contract --
                 * even when neither child ended up set (not visible). */
                if (children.shared_child != NULL) {
                    result->child = (uintptr_t) children.shared_child;
                    if (children.own_children != NULL) {
                        result->second_child = (uintptr_t) children.own_children;
                        result->boundary_required = true;
                        result->boundary_action = SATURN_GEO_BOUNDARY_OBJECT;
                        result->leave_required = true;
                        result->leave_action = SATURN_GEO_LEAVE_OBJECT_FINAL;
                    } else {
                        /* No second subtree this visit -- the runtime's
                         * single-subtree path fires only ONE leave call,
                         * so it must do both the boundary AND final work. */
                        result->leave_required = true;
                        result->leave_action = SATURN_GEO_LEAVE_OBJECT_COMBINED;
                    }
                } else {
                    if (children.own_children != NULL) {
                        result->child = (uintptr_t) children.own_children;
                    }
                    result->leave_required = true;
                    result->leave_action = SATURN_GEO_LEAVE_OBJECT_FINAL;
                }
            }
            break;
        }
        case GRAPH_NODE_TYPE_HELD_OBJ: {
            struct GraphNodeHeldObject *ho = (struct GraphNodeHeldObject *) node;
            struct saturn_geo_held_object_children children =
                saturn_geo_enter_held_object(ho);
            /* The enter-phase side effects (gSPLookAt, the GEO_CONTEXT_
             * RENDER callback) always run once this node is reached and
             * active, matching CAMERA's own unconditional admission
             * (wave 1) -- there is no "nothing to do" case for held-object
             * the way there is for e.g. ORTHO_PROJECTION. */
            result->admitted = true;
            if (children.shared_child != NULL) {
                result->child = (uintptr_t) children.shared_child;
                if (children.own_children != NULL) {
                    result->second_child = (uintptr_t) children.own_children;
                    result->boundary_required = true;
                    result->boundary_action = SATURN_GEO_BOUNDARY_HELD_OBJECT;
                } else {
                    result->leave_required = true;
                    result->leave_action = SATURN_GEO_BOUNDARY_HELD_OBJECT;
                }
            } else if (children.own_children != NULL) {
                result->child = (uintptr_t) children.own_children;
            }
            break;
        }
        default:
            saturn_geo_walk_dispatch_legacy(node);
            break;
    }
    return true;
}

/**
 * Runtime ops.dispatch: no node type converted this wave defers a
 * separate dispatch phase (their work happens in enter()/leave()).
 * Reserved for future waves' leaf display/callback nodes.
 */
static void saturn_geo_walk_dispatch(uintptr_t node_token, void *user) {
    (void) node_token;
    (void) user;
}

/**
 * Runtime ops.leave: restores the global state each converted handler's
 * enter phase mutated, exactly matching the pre-conversion post-child
 * code paths.
 *
 * As of Task 14 wave 3, this callback fires with two conceptually
 * different meanings for the two-subtree action codes (SATURN_GEO_
 * BOUNDARY_OBJECT_PARENT/_OBJECT/_HELD_OBJECT and SATURN_GEO_LEAVE_
 * OBJECT_FINAL/_COMBINED, all defined above): a BOUNDARY call, fired by
 * the runtime strictly between a node's two subtrees, and a FINAL call,
 * fired once everything for the node has drained -- distinguished only by
 * which code leave_action carries here (see
 * saturn_geo_walk_runtime.h's sm64_saturn_geo_walk_runtime_leave_fn
 * comment for the general contract, and each SATURN_GEO_LEAVE_ /
 * SATURN_GEO_BOUNDARY_ enumerator's own comment above for which meaning
 * it carries and why).
 */
static void saturn_geo_walk_leave(uintptr_t node_token, uint16_t leave_action,
                                  uint16_t matrix_depth, uint16_t context_token,
                                  void *user) {
    struct GraphNode *node = (struct GraphNode *) node_token;
    (void) matrix_depth;
    (void) user;

    switch (leave_action) {
        case SATURN_GEO_LEAVE_MASTER_LIST:
            saturn_geo_leave_master_list((struct GraphNodeMasterList *) node);
            break;
        case SATURN_GEO_LEAVE_PERSPECTIVE:
            saturn_geo_leave_perspective();
            break;
        case SATURN_GEO_LEAVE_CAMERA:
            saturn_geo_leave_camera(context_token != 0U);
            break;
        case SATURN_GEO_BOUNDARY_OBJECT_PARENT:
            saturn_geo_leave_object_parent_boundary((struct GraphNodeObjectParent *) node);
            break;
        case SATURN_GEO_BOUNDARY_OBJECT:
            saturn_geo_leave_object_boundary((struct Object *) node);
            break;
        case SATURN_GEO_LEAVE_OBJECT_FINAL:
            saturn_geo_leave_object_final((struct Object *) node, context_token != 0U);
            break;
        case SATURN_GEO_LEAVE_OBJECT_COMBINED:
            saturn_geo_leave_object_boundary((struct Object *) node);
            saturn_geo_leave_object_final((struct Object *) node, context_token != 0U);
            break;
        case SATURN_GEO_BOUNDARY_HELD_OBJECT:
            saturn_geo_leave_held_object_boundary();
            break;
        case SATURN_GEO_LEAVE_TRANSLATION_ROTATION:
            saturn_geo_leave_translation_rotation();
            break;
        case SATURN_GEO_LEAVE_TRANSLATION:
            saturn_geo_leave_translation();
            break;
        case SATURN_GEO_LEAVE_ROTATION:
            saturn_geo_leave_rotation();
            break;
        case SATURN_GEO_LEAVE_SCALE:
            saturn_geo_leave_scale();
            break;
        case SATURN_GEO_LEAVE_BILLBOARD:
            saturn_geo_leave_billboard();
            break;
        case SATURN_GEO_LEAVE_ANIMATED_PART:
            saturn_geo_leave_animated_part();
            break;
        default:
            break;
    }
}

/* Task 14 wave 3: non-reentrancy guard for saturn_geo_walk_process_children
 * below. See this file's wave 3 doc comment (above the SATURN_GEO_LEAVE_*
 * enum, near saturn_geo_walk_dispatch_legacy) for the full discovery
 * writeup; this is the short version needed to read the guard itself.
 *
 * sourceboot_geo_walk_frames is ONE global, fixed-capacity array (the
 * production traversal engine's ONLY frame storage -- saturn_geo_walk_
 * storage.h). Every call below to sm64_saturn_geo_walk_runtime_init()
 * resets depth to 0 and starts pushing at index 0 of that SAME array,
 * regardless of which C call frame makes the call. Converting OBJECT/
 * OBJECT_PARENT/HELD_OBJ means their subtrees can now be reached via real
 * recursion THROUGH a still-unconverted "skeleton" type (ANIMATED_PART,
 * SWITCH_CASE, SCALE, ...) that is itself nested beneath an Object's
 * sharedChild -- which is now walked as part of whatever OUTER walk
 * reached that Object (extending it, never starting a fresh one). If that
 * real-recursion detour reaches another converted type and this function
 * were called again from inside it, it would silently overwrite the outer
 * walk's still-pending frame data -- memory corruption, not a capacity
 * problem, and not hypothetical: verified against the real mario_geo[]
 * layout, this is exactly the "Mario holding something" path.
 *
 * sSaturnGeoWalkActive is set for the duration of the OUTERMOST call only.
 * Any call arriving while it is already true is, by construction, reached
 * via exactly that detour, and falls back to plain recursion instead --
 * this subtree's unconditionally-correct pre-conversion behavior, safe
 * because it never touches sourceboot_geo_walk_frames. A plain bool
 * (rather than a counter) is sufficient: this walk only ever runs on the
 * single master SH-2 core, synchronously, with no interrupt-driven or
 * concurrent entry -- the only "reentrancy" possible is this exact nested
 * real-recursion-detour call chain, which is inherently sequential (a
 * call cannot itself be re-entered before returning). This does not
 * regress anything (a subtree reached via such a detour already used
 * real recursion before this wave); it does mean the bounded-stack
 * benefit for a node reached this way is deferred until the skeleton
 * types convert in a future wave, at which point this guard stops firing
 * for that path automatically, with no further changes needed here. */
static bool sSaturnGeoWalkActive = false;

/**
 * Entry point used by this wave's converted handlers in place of a
 * direct recursive geo_process_node_and_siblings call on their children.
 * Owns a
 * fresh walk over sourceboot_geo_walk_frames for the duration of this
 * one call and drains it to completion (matching the original's
 * synchronous, blocking recursion semantics) before returning -- UNLESS
 * called reentrantly (sSaturnGeoWalkActive already true), in which case it
 * falls back to plain recursion instead of touching the shared frame
 * array; see sSaturnGeoWalkActive's own comment just above for why.
 */
static bool saturn_geo_walk_process_children(struct GraphNode *children) {
    sm64_saturn_geo_walk_runtime_t walk;
    static const sm64_saturn_geo_walk_runtime_ops_t ops = {
        saturn_geo_walk_enter, saturn_geo_walk_dispatch, saturn_geo_walk_leave
    };
    bool ok;

    if (children == NULL) {
        return true;
    }
    if (sSaturnGeoWalkActive) {
        geo_process_node_and_siblings(children);
        return true;
    }
    sSaturnGeoWalkActive = true;
    sm64_saturn_geo_walk_runtime_init(&walk, sourceboot_geo_walk_frames,
                                       sourceboot_geo_walk_frame_capacity);
    ok = sm64_saturn_geo_walk_runtime_run(&walk, (uintptr_t) children, &ops, NULL);
    sSaturnGeoWalkActive = false;
    if (!ok && walk.fail_reason == SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW) {
        sSaturnGeoWalkOverflowCount++;
    }
    return ok;
}

/**
 * Process a generic geo node and its siblings.
 * The first argument is the start node, and all its siblings will
 * be iterated over.
 */
void geo_process_node_and_siblings(struct GraphNode *firstNode) {
    s16 iterateChildren = TRUE;
    struct GraphNode *curGraphNode = firstNode;
    struct GraphNode *parent = curGraphNode->parent;

    // In the case of a switch node, exactly one of the children of the node is
    // processed instead of all children like usual
    if (parent != NULL) {
        iterateChildren = (parent->type != GRAPH_NODE_TYPE_SWITCH_CASE);
    }

    do {
        if (curGraphNode->flags & GRAPH_RENDER_ACTIVE) {
            if (curGraphNode->flags & GRAPH_RENDER_CHILDREN_FIRST) {
                geo_try_process_children(curGraphNode);
            } else {
                switch (curGraphNode->type) {
                    case GRAPH_NODE_TYPE_ORTHO_PROJECTION:
                        geo_process_ortho_projection((struct GraphNodeOrthoProjection *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_PERSPECTIVE:
                        geo_process_perspective((struct GraphNodePerspective *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_MASTER_LIST:
                        geo_process_master_list((struct GraphNodeMasterList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_LEVEL_OF_DETAIL:
                        geo_process_level_of_detail((struct GraphNodeLevelOfDetail *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SWITCH_CASE:
                        geo_process_switch((struct GraphNodeSwitchCase *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_CAMERA:
                        geo_process_camera((struct GraphNodeCamera *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION_ROTATION:
                        geo_process_translation_rotation(
                            (struct GraphNodeTranslationRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_TRANSLATION:
                        geo_process_translation((struct GraphNodeTranslation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ROTATION:
                        geo_process_rotation((struct GraphNodeRotation *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT:
                        geo_process_object((struct Object *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_ANIMATED_PART:
                        geo_process_animated_part((struct GraphNodeAnimatedPart *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BILLBOARD:
                        geo_process_billboard((struct GraphNodeBillboard *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_DISPLAY_LIST:
                        geo_process_display_list((struct GraphNodeDisplayList *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SCALE:
                        geo_process_scale((struct GraphNodeScale *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_SHADOW:
                        geo_process_shadow((struct GraphNodeShadow *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_OBJECT_PARENT:
                        geo_process_object_parent((struct GraphNodeObjectParent *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_GENERATED_LIST:
                        geo_process_generated_list((struct GraphNodeGenerated *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_BACKGROUND:
                        geo_process_background((struct GraphNodeBackground *) curGraphNode);
                        break;
                    case GRAPH_NODE_TYPE_HELD_OBJ:
                        geo_process_held_object((struct GraphNodeHeldObject *) curGraphNode);
                        break;
                    default:
                        geo_try_process_children((struct GraphNode *) curGraphNode);
                        break;
                }
            }
        } else {
            if (curGraphNode->type == GRAPH_NODE_TYPE_OBJECT) {
                ((struct GraphNodeObject *) curGraphNode)->throwMatrix = NULL;
            }
        }
    } while (iterateChildren && (curGraphNode = curGraphNode->next) != firstNode);
}

/**
 * Process a root node. This is the entry point for processing the scene graph.
 * The root node itself sets up the viewport, then all its children are processed
 * to set up the projection and draw display lists.
 */
void geo_process_root(struct GraphNodeRoot *node, Vp *b, Vp *c, s32 clearColor) {
    UNUSED s32 unused;

    if (node->node.flags & GRAPH_RENDER_ACTIVE) {
        Mtx *initialMatrix;
        Vp *viewport = alloc_display_list(sizeof(*viewport));

#ifdef USE_SYSTEM_MALLOC
        gDisplayListHeap = alloc_only_pool_init();
#else
        gDisplayListHeap = alloc_only_pool_init(main_pool_available() - sizeof(struct AllocOnlyPool),
                                                MEMORY_POOL_LEFT);
#endif
        initialMatrix = alloc_display_list(sizeof(*initialMatrix));
        gMatStackIndex = 0;
        gCurAnimType = 0;
        vec3s_set(viewport->vp.vtrans, node->x * 4, node->y * 4, 511);
        vec3s_set(viewport->vp.vscale, node->width * 4, node->height * 4, 511);
        if (b != NULL) {
            clear_frame_buffer(clearColor);
            make_viewport_clip_rect(b);
            *viewport = *b;
        }

        else if (c != NULL) {
            clear_frame_buffer(clearColor);
            make_viewport_clip_rect(c);
        }

#ifdef TARGET_SATURN
        sm64_saturn_matrix_identity(&gMatStackQ[gMatStackIndex]);
        saturn_mtxq_refresh_float_mirror(gMatStackIndex);
        saturn_mtxq_write_wire(initialMatrix, &gMatStackQ[gMatStackIndex]);
#else
        mtxf_identity(gMatStack[gMatStackIndex]);
        mtxf_to_mtx(initialMatrix, gMatStack[gMatStackIndex]);
#endif
        gMatStackFixed[gMatStackIndex] = initialMatrix;
        gSPViewport(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(viewport));
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(gMatStackFixed[gMatStackIndex]),
                  G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
        gCurGraphNodeRoot = node;
        if (node->node.children != NULL) {
            geo_process_node_and_siblings(node->node.children);
        }
        gCurGraphNodeRoot = NULL;
        if (gShowDebugText) {
#ifndef USE_SYSTEM_MALLOC
            print_text_fmt_int(180, 36, "MEM %d",
                               gDisplayListHeap->totalSpace - gDisplayListHeap->usedSpace);
#endif
        }
        main_pool_free(gDisplayListHeap);
    }
}
