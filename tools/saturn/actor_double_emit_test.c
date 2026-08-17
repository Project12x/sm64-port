/* Sprint 2 T2.19a -- the Mario "double-emit" is an atomic composite, not debt.
 *
 * saturn_demo_render.c:demo_emit_mario_range() writes TWO VDP1 commands for a
 * Mario primitive that carries a texture tile: a Gouraud polygon into
 * s_actor_slots[ordinal], then an alpha-keyed RGB1555 distorted sprite into
 * s_actor_texture_slots[ordinal], both tagged with the same painter bin
 * (ref->sort_key >> 16).  T2.8 section 9 item 6 filed that as debt to retire
 * and T2.19a was scoped to delete the redundant one.
 *
 * There is no redundant one.  This fixture pins the three facts that make the
 * pair a composite, so the deletion cannot be re-proposed without a red gate:
 *
 *   1. GEOMETRY -- for every one of the 50 textured primitives the polygon's
 *      quad and the sprite's quad are the same four projected vertices, in the
 *      same order.  This is a property of the committed generated data
 *      (saturn_mario_actor_mesh.h), asserted here over the whole table rather
 *      than sampled.
 *   2. ORDER -- after the real painter relink the sprite is visited
 *      immediately after its polygon, so the sprite is on top.
 *   3. TRANSPARENCY -- the sprite does NOT set CMDPMOD's SPD bit, while the
 *      polygon does.  vdp1_cmdt_draw_mode_set() forces ECD|SPD on for
 *      non-textured commands only (cmdt.h:270-279, `cmd_ctrl & 0x0004`;
 *      VDP1_CMDT_POLYGON == 4, VDP1_CMDT_DISTORTED_SPRITE == 2).  With SPD
 *      clear, every texture word equal to the VDP1 transparent code 0x0000 is
 *      skipped and the polygon underneath is what reaches the screen.
 *
 * Fact 3 is only load-bearing because the texture data really does contain
 * that code: tools/saturn/extract_mario_textures.py:saturn_rgb1555() emits
 * exactly 0x0000 whenever the N64 A1 bit is clear, and the generated manifest
 * records vdp1_transparent_word_count = 7216 of 12800 (56.38%), with 6 of the
 * 50 tiles entirely transparent.  That census is ROM-derived and therefore not
 * committed; test_mario_texture_alpha_key.py checks it when the generated
 * header is present.  This fixture asserts the part that holds from committed
 * data alone.
 *
 * Deliberately NOT using libyaul's vdp1_cmdt_draw_mode_t bitfield union: its
 * field order maps to CMDPMOD bits only under a big-endian MSB-first bitfield
 * allocation (SH-2), and a host x86 compile would place the same declaration
 * at the opposite end of the word.  The bit positions below are taken from the
 * header's own comments (cmdt.h:108-124) and the setter body is reproduced
 * verbatim, so this fixture tests the target's arithmetic rather than the
 * host's ABI.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <yaul.h>

#include "saturn_mario_actor_mesh.h"

typedef struct int16_vec2 {
    int16_t x;
    int16_t y;
} int16_vec2_t;

typedef uint16_t vdp1_link_t;

typedef struct vdp1_cmdt {
    uint16_t cmd_ctrl;
    uint16_t cmd_link;
    uint16_t cmd_pmod;
    uint16_t cmd_colr;
    uint16_t cmd_srca;
    uint16_t cmd_size;
    int16_vec2_t cmd_vertices[4];
    uint16_t cmd_grda;
    uint16_t reserved;
} vdp1_cmdt_t;

typedef struct vdp1_cmdt_list {
    vdp1_cmdt_t *cmdts;
    uint16_t count;
} vdp1_cmdt_list_t;

void *memalign(size_t alignment, size_t size);

#define CPU_ADDRESS_PARTITION_MASK ((uintptr_t)0xE0000000U)
#define LWRAM(address) ((uintptr_t)(address))
#define LWRAM_SIZE 0x00100000U
#define VDP1_VRAM(address) ((uintptr_t)(address))

/* CMDPMOD bit positions, cmdt.h:108-124. */
#define CMDPMOD_ECD (1U << 7) /* end code disable */
#define CMDPMOD_SPD (1U << 6) /* transparent pixel disable */
#define CMDPMOD_CM_RGB_32768 (5U << 3)
#define CMDPMOD_CC_REPLACE (0U << 0)
#define CMDPMOD_CC_GOURAUD (4U << 0)

/* cmdt.h:28-38. */
#define VDP1_CMDT_DISTORTED_SPRITE 2U
#define VDP1_CMDT_POLYGON 4U

static void vdp1_cmdt_command_set(vdp1_cmdt_t *cmdt, uint16_t command)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0xFFF0U) | command);
}

static void vdp1_cmdt_polygon_set(vdp1_cmdt_t *cmdt)
{
    vdp1_cmdt_command_set(cmdt, VDP1_CMDT_POLYGON);
}

static void vdp1_cmdt_distorted_sprite_set(vdp1_cmdt_t *cmdt)
{
    vdp1_cmdt_command_set(cmdt, VDP1_CMDT_DISTORTED_SPRITE);
}

/* cmdt.h:269-279, verbatim. The comment there is the whole point: "Values
 * 0x4, 0x5, 0x6 for comm indicate a non-textured command table, and we want to
 * set the bits 7 and 6 without branching". */
static void vdp1_cmdt_draw_mode_raw_set(vdp1_cmdt_t *cmdt, uint16_t draw_mode_raw)
{
    const uint16_t comm = (uint16_t)(cmdt->cmd_ctrl & 0x0004U);
    const uint16_t pmod_bits = (uint16_t)((comm << 5) | (comm << 4));

    cmdt->cmd_pmod = (uint16_t)(pmod_bits | draw_mode_raw);
}

static void vdp1_cmdt_vtx_set(vdp1_cmdt_t *cmdt, const int16_vec2_t vertices[4])
{
    memcpy(cmdt->cmd_vertices, vertices, sizeof(cmdt->cmd_vertices));
}

static void vdp1_cmdt_list_init(vdp1_cmdt_list_t *list, vdp1_cmdt_t *cmdts)
{
    list->cmdts = cmdts;
    list->count = 0U;
}

static void vdp1_cmdt_end_set(vdp1_cmdt_t *cmdt) { cmdt->cmd_ctrl |= 0x8000U; }
static void vdp1_cmdt_end_clear(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl &= (uint16_t)~0x8000U;
}
static void vdp1_cmdt_jump_next(vdp1_cmdt_t *cmdt) { cmdt->cmd_ctrl &= 0x8FFFU; }
static void vdp1_cmdt_jump_assign(vdp1_cmdt_t *cmdt, vdp1_link_t link)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0x8FFFU) | 0x1000U);
    cmdt->cmd_link = (uint16_t)(link << 2);
}

static void vdp1_cmdt_system_clip_coord_set(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0xFFF0U) | 0x0009U);
}
static void vdp1_cmdt_vtx_system_clip_coord_set(vdp1_cmdt_t *cmdt,
                                                int16_vec2_t clip)
{
    (void)cmdt;
    (void)clip;
}
static void vdp1_cmdt_local_coord_set(vdp1_cmdt_t *cmdt)
{
    cmdt->cmd_ctrl = (uint16_t)((cmdt->cmd_ctrl & 0xFFF0U) | 0x000AU);
}
static void vdp1_cmdt_vtx_local_coord_set(vdp1_cmdt_t *cmdt, int16_vec2_t local)
{
    (void)cmdt;
    (void)local;
}
static void vdp1_sync_wait(void) {}
static bool vdp1_sync_busy(void) { return false; }
static void vdp1_sync_force_put(void) {}

#define SM64_SATURN_VDP1_LWRAM_STAGING 1
#include "../../src/port/saturn/gfx/saturn_vdp1_backend.h"
#include "../../src/port/saturn/gfx/saturn_terrain_depth_bins.h"

/* ------------------------------------------------------------------------ */

#define TEXTURED_PRIMITIVE_CAP 64U
#define TEST_BIN 7U

typedef struct pair {
    uint16_t primitive;
    uint16_t rank;
    uint16_t polygon_slot;
    uint16_t sprite_slot;
} pair_t;

/* A deterministic stand-in for demo_actor_projected_read(): the fixture only
 * needs a projection that is injective over source vertex indices, so that
 * "the two commands received the same four screen vertices" is a real
 * statement about the index tables rather than an accident of collisions. */
static int16_vec2_t project(uint16_t source_vertex)
{
    int16_vec2_t out;
    out.x = (int16_t)(17 * (int32_t)source_vertex % 311 - 155);
    out.y = (int16_t)(29 * (int32_t)source_vertex % 223 - 111);
    return out;
}

/* Mirrors demo_emit_mario_range()'s textured arm exactly: polygon first into
 * the lower slot, then the texture detail into the slot immediately after,
 * both carrying the same painter bin. */
static void emit_pair(vdp1_cmdt_t *cmdts, const pair_t *pair, uint16_t bin)
{
    const uint16_t *indices = sm64_mario_primitives[pair->primitive];
    const uint16_t *texture_indices = sm64_mario_textured_source_vertices[pair->rank];
    const int16_vec2_t vertices[4] = {
        project(indices[1]), project(indices[2]),
        project(indices[3]), project(indices[4])};
    const int16_vec2_t texture_vertices[4] = {
        project(texture_indices[0]), project(texture_indices[1]),
        project(texture_indices[2]), project(texture_indices[2])};

    /* Both arrays stay referenced so a DROP_* mutation still compiles; a
     * mutation that cannot be built proves nothing. */
    (void)vertices;
    (void)texture_vertices;

#if !defined(SM64_SATURN_ACTOR_DOUBLE_EMIT_TEST_DROP_POLYGON)
    {
        vdp1_cmdt_t *const cmdt = &cmdts[pair->polygon_slot];
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_raw_set(
            cmdt, CMDPMOD_CM_RGB_32768 | CMDPMOD_CC_GOURAUD);
        vdp1_cmdt_vtx_set(cmdt, vertices);
        cmdt->cmd_link = bin;
    }
#endif
#if !defined(SM64_SATURN_ACTOR_DOUBLE_EMIT_TEST_DROP_SPRITE)
    {
        vdp1_cmdt_t *const detail = &cmdts[pair->sprite_slot];
        uint16_t draw_mode = CMDPMOD_ECD | CMDPMOD_CM_RGB_32768 | CMDPMOD_CC_REPLACE;
#if defined(SM64_SATURN_ACTOR_DOUBLE_EMIT_TEST_SPRITE_OPAQUE)
        draw_mode |= CMDPMOD_SPD;
#endif
        vdp1_cmdt_distorted_sprite_set(detail);
        vdp1_cmdt_draw_mode_raw_set(detail, draw_mode);
        vdp1_cmdt_vtx_set(detail, texture_vertices);
#if defined(SM64_SATURN_ACTOR_DOUBLE_EMIT_TEST_SPRITE_BIN_SHIFT)
        detail->cmd_link = (uint16_t)(bin + 1U);
#else
        detail->cmd_link = bin;
#endif
    }
#endif
}

static uint16_t collect_textured(pair_t *pairs, uint16_t capacity)
{
    uint16_t count = 0U;
    for (uint16_t primitive = 0U; primitive < SM64_MARIO_PRIMITIVE_COUNT;
         primitive++) {
        const uint16_t start = sm64_mario_texture_tile_start[primitive];
        if (start == SM64_MARIO_TEXTURE_TILE_NONE) continue;
        assert(count < capacity);
        pairs[count].primitive = primitive;
        pairs[count].rank = (uint16_t)(start / 4U);
        assert(pairs[count].rank < SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT);
        count++;
    }
    return count;
}

/* The mechanism behind fact 3, isolated from the actor path: the same
 * draw-mode request produces different CMDPMOD words for the two command
 * types, because the setter branchlessly forces ECD|SPD on for comm values
 * with bit 2 set.  Asserted here so the asymmetry cannot be mistaken for
 * something demo_emit_mario_range() chose. */
static void test_setter_forces_opacity_on_polygons_only(void)
{
    const uint16_t request = CMDPMOD_ECD | CMDPMOD_CM_RGB_32768 | CMDPMOD_CC_REPLACE;
    vdp1_cmdt_t polygon;
    vdp1_cmdt_t sprite;

    memset(&polygon, 0, sizeof(polygon));
    memset(&sprite, 0, sizeof(sprite));
    vdp1_cmdt_polygon_set(&polygon);
    vdp1_cmdt_distorted_sprite_set(&sprite);
    assert((polygon.cmd_ctrl & 0x000FU) == VDP1_CMDT_POLYGON);
    assert((sprite.cmd_ctrl & 0x000FU) == VDP1_CMDT_DISTORTED_SPRITE);

    vdp1_cmdt_draw_mode_raw_set(&polygon, request);
    vdp1_cmdt_draw_mode_raw_set(&sprite, request);
    assert((polygon.cmd_pmod & CMDPMOD_SPD) != 0U);
    assert((sprite.cmd_pmod & CMDPMOD_SPD) == 0U);
}

/* Fact 1.  Not a sample: every textured primitive in the compiled table. */
static void test_polygon_and_sprite_cover_the_same_quad(void)
{
    pair_t pairs[TEXTURED_PRIMITIVE_CAP];
    const uint16_t count = collect_textured(pairs, TEXTURED_PRIMITIVE_CAP);

    assert(count == SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT);
    assert(count == 50U);
    for (uint16_t i = 0U; i < count; i++) {
        const uint16_t *const indices = sm64_mario_primitives[pairs[i].primitive];
        const uint16_t *const texture =
            sm64_mario_textured_source_vertices[pairs[i].rank];
        /* extract_mario_actor.py forbids pairing a textured source triangle
         * into a quad, so the polygon's fourth corner repeats its third --
         * exactly the degeneracy the sprite's fourth corner also carries. */
        assert(indices[1] == texture[0]);
        assert(indices[2] == texture[1]);
        assert(indices[3] == texture[2]);
        assert(indices[4] == texture[2]);
    }
}

/* Facts 2 and 3, through the real painter relink and the real setter
 * arithmetic.  One pair per bank keeps the chain unambiguous. */
static void test_sprite_is_drawn_over_a_polygon_that_keeps_transparency(void)
{
    pair_t pairs[TEXTURED_PRIMITIVE_CAP];
    const uint16_t count = collect_textured(pairs, TEXTURED_PRIMITIVE_CAP);

    for (uint16_t i = 0U; i < count; i++) {
        sm64_saturn_vdp1_backend_t backend;
        vdp1_cmdt_t cmdts[8];
        const vdp1_cmdt_t *polygon;
        const vdp1_cmdt_t *sprite;

        memset(cmdts, 0, sizeof(cmdts));
        sm64_saturn_vdp1_backend_bind_storage(&backend, cmdts, 8U);
        cmdts[1].cmd_ctrl = 0x000AU;
        sm64_saturn_vdp1_backend_begin(&backend);
        assert(sm64_saturn_vdp1_backend_reserve(&backend, 2U) == &cmdts[2]);
        pairs[i].polygon_slot = 2U;
        pairs[i].sprite_slot = 3U;
        emit_pair(cmdts, &pairs[i], TEST_BIN);
        sm64_saturn_vdp1_backend_finish(&backend);
        assert(sm64_saturn_vdp1_backend_link_depth_bins(
            &backend, SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT));

        polygon = &cmdts[2];
        sprite = &cmdts[3];

        /* The pair is atomic: both commands exist. */
        assert((polygon->cmd_ctrl & 0x000FU) == VDP1_CMDT_POLYGON);
        assert((sprite->cmd_ctrl & 0x000FU) == VDP1_CMDT_DISTORTED_SPRITE);

        /* Same four screen vertices -- the sprite is not a different
         * surface, it is a decal on this one. */
        assert(memcmp(polygon->cmd_vertices, sprite->cmd_vertices,
                      sizeof(polygon->cmd_vertices)) == 0);

        /* Order: the prefix jumps to the polygon, the polygon jumps to the
         * sprite.  Same bin, ascending producer order (backend header's
         * "stable in original producer order within a bin"), so the sprite
         * is on top. */
        assert((cmdts[1].cmd_ctrl & 0x7000U) == 0x1000U);
        assert(cmdts[1].cmd_link == (uint16_t)(2U << 2));
        assert((polygon->cmd_ctrl & 0x7000U) == 0x1000U);
        assert(polygon->cmd_link == (uint16_t)(3U << 2));

        /* Transparency asymmetry.  The polygon is opaque over its whole
         * quad; the sprite leaves the transparent code unwritten.  This is
         * the reason the polygon is not "overdrawn by construction". */
        assert((polygon->cmd_pmod & CMDPMOD_SPD) != 0U);
        assert((polygon->cmd_pmod & CMDPMOD_ECD) != 0U);
        assert((sprite->cmd_pmod & CMDPMOD_ECD) != 0U);
        assert((sprite->cmd_pmod & CMDPMOD_SPD) == 0U);
    }
}

int main(void)
{
    test_setter_forces_opacity_on_polygons_only();
    test_polygon_and_sprite_cover_the_same_quad();
    test_sprite_is_drawn_over_a_polygon_that_keeps_transparency();
    printf("actor double-emit composite: RESULT OK\n");
    return 0;
}
