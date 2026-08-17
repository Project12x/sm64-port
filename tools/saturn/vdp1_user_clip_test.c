/* Sprint 2 T2.19b -- VDP1 user-clipping equivalence oracle.
 *
 * WHAT THIS PROVES
 *
 * The port programs exactly one system-clip command per command list
 * (src/port/saturn/gfx/saturn_vdp1_backend.h:69-70 and the caller-storage
 * variant at :105-106) with the lower-right corner (319, 223), supplied by
 * src/port/saturn/sourceboot/main.c:2328.  It never sets `user_clipping_mode`
 * on any command and never emits a user-clip-coordinates command.
 *
 * T2.19b asked whether turning user clipping on removes fill.  This fixture is
 * the executable answer.  It models VDP1's pixel-acceptance predicate and
 * shows that a user clip box equal to the system clip box is *bit-identical*
 * to no user clipping at all -- for primitives fully inside, fully outside,
 * straddling each of the four edges, straddling each of the four corners, and
 * overhanging the whole screen.  Zero pixels are removed, so zero framebuffer
 * writes are saved.
 *
 * It is retained as the gate a future per-package clip rectangle (the
 * SlaveDriver arrangement in
 * docs/saturn/evidence/reports/sprint2-reference-technique-gaps.md
 * section 5 item 4) must pass before it may narrow that box.  The
 * `positive control` below is what makes the equivalence claim meaningful:
 * it proves the modelled clip is genuinely in force, so equivalence is a
 * property of the box, not an inert test.
 *
 * MODE, PROVEN NOT INFERRED
 *
 * CMDPMOD bits 10-9 select the user-clipping mode; libyaul spells the pair as
 * one field, `user_clipping_mode:2`
 * (third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1/cmdt.h:114).  Sega documents
 * the three states as NoWindow (default), WindowIn ("Display inside a
 * specified window") and WindowOut ("Display outside a specified window") --
 * work/upstream/sonic-z-treme/Documentation/DOC/210A_US/SPRITE.TXT:392-394.
 * The numeric encoding is first-party and unambiguous.  Sega's own SGL header
 * spells all three states out:
 *
 *     #define No_Window   (0 << 9)
 *     #define Window_In   (2 << 9)
 *     #define Window_Out  (3 << 9)
 *
 * (work/upstream/sonic-z-treme/Compiler/SGL_302j/INC/SL_DEF.H:190-192), and
 * SlaveDriver's own header names the same two constants with the same values,
 * commented "CLIP IN enable" and "CLIP OUT enable" -- `UCLPIN_ENABLE 0x0400`
 * and `UCLPOUT_ENABLE 0x0600`
 * (work/upstream/slavedriver-engine/SPR.H:87-88).  Both engines set the
 * draw-inside state on effectively every command.  Two independent emulators
 * agree with that reading and were used as cross-checks only -- both are GPL
 * and no code from either was copied into this file:
 *   - Ymir (GPL-3.0) libs/ymir-core/src/ymir/hw/vdp/renderer/vdp_renderer_sw.cpp
 *     :965-1006, where system clipping rejects x<0, x>sysClipH, y<0 and
 *     y>sysClipV, and user clipping is a second per-pixel test layered on top;
 *   - Kronos/Yabause (GPL-2.0)
 *     yabause/src/core/video/opengl/compute_shader/include/vdp1_prog_compute.h
 *     :42-53, whose shader branches on `((CMDPMOD >> 9) & 0x3) == 2` for
 *     "Draw inside" and `== 3` for "Draw outside".
 * Jo Engine (MIT) states the same encoding with the semantics spelled out in
 * its comment -- `#define Window_In (2 << 9)` commented "Clip everything
 * outside bounds", work/upstream/joengine/jo_engine/jo/sgl_prototypes.h:79.
 * Jo Engine also emits a user clip box built from the same JO_TV_WIDTH /
 * JO_TV_HEIGHT constants as its system clip
 * (jo_engine/vdp1_command_pipeline.c:100-112) -- i.e. exactly the redundant
 * pair this fixture proves is inert.
 * `mode_encoding_matches_references()` below pins that encoding so a future
 * edit cannot silently swap draw-inside for draw-outside, which is the
 * mutation that blanks the screen.
 *
 * Reuse mode: cross-check (behaviour restated from Sega documentation and
 * validated against two GPL emulators without copying either).
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------- */
/* The geometry the port actually programs.                               */
/* ---------------------------------------------------------------------- */

/* sourceboot/main.c:2328 builds `clip = INT16_VEC2_INITIALIZER(319, 223)` and
 * `local = INT16_VEC2_INITIALIZER(0, 0)`.  Local coordinates of (0,0) mean
 * command coordinates are framebuffer coordinates with no offset, so the
 * numbers below are directly comparable to the 320x224 progressive display
 * configured at sourceboot/main.c:2027-2029. */
#define SYS_CLIP_H 319
#define SYS_CLIP_V 223

#define FB_W (SYS_CLIP_H + 1)
#define FB_H (SYS_CLIP_V + 1)

/* CMDPMOD bits 10-9, as a value of libyaul's `user_clipping_mode:2` field. */
#define USER_CLIP_NONE    0u
#define USER_CLIP_INSIDE  2u /* WindowIn  == Window_In == UCLPIN_ENABLE */
#define USER_CLIP_OUTSIDE 3u /* WindowOut */

#define PMOD_USER_CLIP_SHIFT 9

/* Mutation 1 -- invert the clip mode.  Must break equivalence. */
#if defined(SM64_SATURN_TEST_MUTATE_USER_CLIP_MODE)
#define PORT_CLIP_FIELD_BASE USER_CLIP_OUTSIDE
#else
#define PORT_CLIP_FIELD_BASE USER_CLIP_INSIDE
#endif

/* Mutation 3 -- clear the enable bit (bit 10) while leaving everything else
 * in place.  Equivalence still holds trivially, so this must instead break
 * the positive control that proves the clip is in force. */
#if defined(SM64_SATURN_TEST_MUTATE_USER_CLIP_ENABLE)
#define PORT_CLIP_FIELD (PORT_CLIP_FIELD_BASE & 1u)
#else
#define PORT_CLIP_FIELD PORT_CLIP_FIELD_BASE
#endif

/* Mutation 2 -- shrink the candidate clip rectangle by one pixel.  Must break
 * equivalence, because column 319 and row 223 are displayed. */
#if defined(SM64_SATURN_TEST_MUTATE_USER_CLIP_RECT)
#define PORT_USER_CLIP_X1 (SYS_CLIP_H - 1)
#define PORT_USER_CLIP_Y1 (SYS_CLIP_V - 1)
#else
#define PORT_USER_CLIP_X1 SYS_CLIP_H
#define PORT_USER_CLIP_Y1 SYS_CLIP_V
#endif

/* ---------------------------------------------------------------------- */
/* The modelled hardware predicate.                                       */
/* ---------------------------------------------------------------------- */

typedef struct vdp1_clip_state {
    int32_t sys_clip_h;
    int32_t sys_clip_v;
    int32_t user_x0;
    int32_t user_y0;
    int32_t user_x1;
    int32_t user_y1;
    uint16_t pmod;
} vdp1_clip_state_t;

/* System clipping is a full rectangle anchored at (0,0): the command carries
 * only the lower-right corner, and coordinates below zero are outside it on
 * the same footing as coordinates past the corner.  That is the whole reason
 * a screen-sized user clip box buys nothing -- the top and left edges are
 * already covered. */
static bool inside_system_clip(const vdp1_clip_state_t *state,
                               int32_t x, int32_t y)
{
    return x >= 0 && x <= state->sys_clip_h &&
           y >= 0 && y <= state->sys_clip_v;
}

static bool inside_user_clip(const vdp1_clip_state_t *state,
                             int32_t x, int32_t y)
{
    return x >= state->user_x0 && x <= state->user_x1 &&
           y >= state->user_y0 && y <= state->user_y1;
}

static bool vdp1_pixel_is_drawn(const vdp1_clip_state_t *state,
                                int32_t x, int32_t y)
{
    if (!inside_system_clip(state, x, y))
        return false;

    const unsigned int mode =
        (unsigned int)((state->pmod >> PMOD_USER_CLIP_SHIFT) & 3u);

    /* Bit 10 clear (field 0 or 1) is NoWindow: user clipping is off and the
     * user clip coordinates are ignored entirely. */
    if ((mode & 2u) == 0u)
        return true;

    /* Bit 9 selects WindowOut over WindowIn. */
    const bool draw_outside = (mode & 1u) != 0u;
    return inside_user_clip(state, x, y) != draw_outside;
}

static uint16_t pmod_with_clip_field(unsigned int field)
{
    /* Colour mode / colour-calculation bits are irrelevant to clipping; a
     * non-zero filler keeps the shift-and-mask honest. */
    return (uint16_t)(0x0088u | (field << PMOD_USER_CLIP_SHIFT));
}

/* ---------------------------------------------------------------------- */
/* Primitives and rasterisation.                                          */
/* ---------------------------------------------------------------------- */

typedef enum prim_kind {
    PRIM_QUAD, /* axis-aligned span set: normal/scaled sprite, screen quad */
    PRIM_LINE  /* diagonal span: line / polyline edge, non-axis-aligned cover */
} prim_kind_t;

typedef struct prim {
    const char *name;
    prim_kind_t kind;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
} prim_t;

typedef struct raster_result {
    uint8_t fb[FB_H][FB_W];
    unsigned long walked;  /* pixels the plotter steps over */
    unsigned long written; /* pixels that survive clipping */
} raster_result_t;

static bool raster_plot(raster_result_t *out, const vdp1_clip_state_t *state,
                        int32_t x, int32_t y, uint8_t tag)
{
    out->walked++;
    if (!vdp1_pixel_is_drawn(state, x, y))
        return true;
    /* Any accepted pixel must land inside the framebuffer.  If this ever
     * trips, the modelled system clip is not covering all four edges and
     * every conclusion drawn from this fixture is void. */
    if (x < 0 || x >= FB_W || y < 0 || y >= FB_H)
        return false;
    out->fb[y][x] = tag;
    out->written++;
    return true;
}

static bool raster_prim(raster_result_t *out, const vdp1_clip_state_t *state,
                        const prim_t *prim, uint8_t tag)
{
    if (prim->kind == PRIM_QUAD) {
        for (int32_t y = prim->y0; y <= prim->y1; y++) {
            for (int32_t x = prim->x0; x <= prim->x1; x++) {
                if (!raster_plot(out, state, x, y, tag))
                    return false;
            }
        }
        return true;
    }

    {
        const int32_t dx = prim->x1 - prim->x0;
        const int32_t dy = prim->y1 - prim->y0;
        const int32_t adx = dx < 0 ? -dx : dx;
        const int32_t ady = dy < 0 ? -dy : dy;
        const int32_t steps = adx > ady ? adx : ady;
        for (int32_t step = 0; step <= steps; step++) {
            const int32_t x = prim->x0 + (dx * step) / (steps == 0 ? 1 : steps);
            const int32_t y = prim->y0 + (dy * step) / (steps == 0 ? 1 : steps);
            if (!raster_plot(out, state, x, y, tag))
                return false;
        }
    }
    return true;
}

/* The representative set.  Fully inside and fully outside are the cheap
 * cases; the straddlers and the corner cases are the ones that decide the
 * question, so every edge and every corner is covered in both directions,
 * and the one-pixel boundary rows and columns are named explicitly because a
 * shrink-by-one mutation lives or dies on them. */
static const prim_t k_primitives[] = {
    {"fully-inside",              PRIM_QUAD,   64,   48,  255,  175},
    {"fills-exactly-the-screen",  PRIM_QUAD,    0,    0,  319,  223},

    {"fully-offscreen-left",      PRIM_QUAD, -200,   48, -100,  175},
    {"fully-offscreen-right",     PRIM_QUAD,  400,   48,  500,  175},
    {"fully-offscreen-above",     PRIM_QUAD,   64, -200,  255, -100},
    {"fully-offscreen-below",     PRIM_QUAD,   64,  300,  255,  400},

    {"straddles-left-edge",       PRIM_QUAD,  -80,   48,   96,  175},
    {"straddles-right-edge",      PRIM_QUAD,  240,   48,  420,  175},
    {"straddles-top-edge",        PRIM_QUAD,   64,  -70,  255,   64},
    {"straddles-bottom-edge",     PRIM_QUAD,   64,  160,  255,  330},

    {"straddles-top-left",        PRIM_QUAD,  -48,  -32,   72,   56},
    {"straddles-top-right",       PRIM_QUAD,  272,  -32,  392,   56},
    {"straddles-bottom-left",     PRIM_QUAD,  -48,  184,   72,  272},
    {"straddles-bottom-right",    PRIM_QUAD,  272,  184,  392,  272},

    {"overhangs-every-edge",      PRIM_QUAD,  -96,  -64,  416,  288},

    {"boundary-column-0",         PRIM_QUAD,  -16,  -16,    0,  239},
    {"boundary-column-319",       PRIM_QUAD,  319,  -16,  336,  239},
    {"boundary-row-0",            PRIM_QUAD,  -16,  -16,  336,    0},
    {"boundary-row-223",          PRIM_QUAD,  -16,  223,  336,  240},

    {"diagonal-through-corner",   PRIM_LINE, -120,  -90,  200,  160},
    {"diagonal-exit-right",       PRIM_LINE,  100,   30,  460,  210},
    {"diagonal-exit-bottom",      PRIM_LINE,  -60,  120,  260,  380},
};

#define PRIM_COUNT ((int)(sizeof(k_primitives) / sizeof(k_primitives[0])))

static bool raster_all(raster_result_t *out, const vdp1_clip_state_t *state)
{
    (void)memset(out, 0, sizeof(*out));
    for (int index = 0; index < PRIM_COUNT; index++) {
        if (!raster_prim(out, state, &k_primitives[index],
                         (uint8_t)(index + 1))) {
            (void)fprintf(stderr,
                          "vdp1-user-clip: primitive '%s' produced an accepted "
                          "pixel outside the framebuffer\n",
                          k_primitives[index].name);
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Configurations under test.                                             */
/* ---------------------------------------------------------------------- */

/* What the port ships today: system clipping only. */
static vdp1_clip_state_t state_shipped(void)
{
    const vdp1_clip_state_t state = {
        .sys_clip_h = SYS_CLIP_H,
        .sys_clip_v = SYS_CLIP_V,
        .user_x0 = 0, .user_y0 = 0,
        .user_x1 = SYS_CLIP_H, .user_y1 = SYS_CLIP_V,
        .pmod = pmod_with_clip_field(USER_CLIP_NONE),
    };
    return state;
}

/* T2.19b's candidate: user clipping enabled, box equal to the screen. */
static vdp1_clip_state_t state_candidate(void)
{
    const vdp1_clip_state_t state = {
        .sys_clip_h = SYS_CLIP_H,
        .sys_clip_v = SYS_CLIP_V,
        .user_x0 = 0, .user_y0 = 0,
        .user_x1 = PORT_USER_CLIP_X1, .user_y1 = PORT_USER_CLIP_Y1,
        .pmod = pmod_with_clip_field(PORT_CLIP_FIELD),
    };
    return state;
}

/* Positive control: the same enable bit and mode the candidate uses, but with
 * a deliberately narrowed box.  This must NOT be equivalent -- if it is, the
 * clip is not in force and the equivalence result above proves nothing. */
static vdp1_clip_state_t state_narrowed(void)
{
    vdp1_clip_state_t state = state_candidate();
    state.user_x0 = 32;
    state.user_y0 = 24;
    state.user_x1 = SYS_CLIP_H - 32;
    state.user_y1 = SYS_CLIP_V - 24;
    return state;
}

/* ---------------------------------------------------------------------- */
/* Checks.                                                                */
/* ---------------------------------------------------------------------- */

static bool mode_encoding_matches_references(void)
{
    /* Draw-inside is bit 10 alone.  This is the literal constant both
     * reference engines put in CMDPMOD. */
    if ((USER_CLIP_INSIDE << PMOD_USER_CLIP_SHIFT) != 0x0400u) {
        (void)fprintf(stderr, "vdp1-user-clip: draw-inside is not 0x0400\n");
        return false;
    }
    if ((USER_CLIP_OUTSIDE << PMOD_USER_CLIP_SHIFT) != 0x0600u) {
        (void)fprintf(stderr, "vdp1-user-clip: draw-outside is not 0x0600\n");
        return false;
    }
    /* Round-trip through the same shift-and-mask the hardware model uses. */
    if (((pmod_with_clip_field(USER_CLIP_INSIDE) >> PMOD_USER_CLIP_SHIFT) & 3u)
            != 2u ||
        ((pmod_with_clip_field(USER_CLIP_OUTSIDE) >> PMOD_USER_CLIP_SHIFT) & 3u)
            != 3u) {
        (void)fprintf(stderr, "vdp1-user-clip: mode field round-trip failed\n");
        return false;
    }

    /* Direction check, independent of the raster: with draw-inside, a pixel
     * inside the box is drawn and one outside it is not; with draw-outside,
     * exactly the reverse.  Getting this pair backwards is what blanks the
     * screen. */
    {
        vdp1_clip_state_t inside = state_shipped();
        inside.user_x0 = 100; inside.user_y0 = 100;
        inside.user_x1 = 200; inside.user_y1 = 150;
        inside.pmod = pmod_with_clip_field(USER_CLIP_INSIDE);
        if (!vdp1_pixel_is_drawn(&inside, 150, 120) ||
            vdp1_pixel_is_drawn(&inside, 10, 10)) {
            (void)fprintf(stderr,
                          "vdp1-user-clip: WindowIn does not draw inside\n");
            return false;
        }
        inside.pmod = pmod_with_clip_field(USER_CLIP_OUTSIDE);
        if (vdp1_pixel_is_drawn(&inside, 150, 120) ||
            !vdp1_pixel_is_drawn(&inside, 10, 10)) {
            (void)fprintf(stderr,
                          "vdp1-user-clip: WindowOut does not draw outside\n");
            return false;
        }
    }
    return true;
}

/* The claim T2.19b turns on: system clipping alone already rejects every
 * pixel outside the displayed 320x224, top and left included. */
static bool system_clip_covers_all_four_edges(void)
{
    const vdp1_clip_state_t state = state_shipped();
    static const struct { int32_t x, y; bool drawn; } k_probes[] = {
        {   0,    0, true  },
        { 319,  223, true  },
        {  -1,    0, false },
        {   0,   -1, false },
        { 320,    0, false },
        {   0,  224, false },
        {-500, -500, false },
        { 900,  900, false },
    };
    for (size_t index = 0; index < sizeof(k_probes) / sizeof(k_probes[0]);
         index++) {
        if (vdp1_pixel_is_drawn(&state, k_probes[index].x, k_probes[index].y) !=
            k_probes[index].drawn) {
            (void)fprintf(stderr,
                          "vdp1-user-clip: system clip wrong at (%ld, %ld)\n",
                          (long)k_probes[index].x, (long)k_probes[index].y);
            return false;
        }
    }
    return true;
}

static bool equivalence_holds(unsigned long *walked,
                              unsigned long *written)
{
    raster_result_t shipped;
    raster_result_t candidate;
    const vdp1_clip_state_t shipped_state = state_shipped();
    const vdp1_clip_state_t candidate_state = state_candidate();

    if (!raster_all(&shipped, &shipped_state))
        return false;
    if (!raster_all(&candidate, &candidate_state))
        return false;

    if (memcmp(shipped.fb, candidate.fb, sizeof(shipped.fb)) != 0) {
        for (int y = 0; y < FB_H; y++) {
            for (int x = 0; x < FB_W; x++) {
                if (shipped.fb[y][x] != candidate.fb[y][x]) {
                    (void)fprintf(stderr,
                                  "vdp1-user-clip: raster differs at (%d, %d): "
                                  "shipped=%u candidate=%u\n",
                                  x, y, (unsigned)shipped.fb[y][x],
                                  (unsigned)candidate.fb[y][x]);
                    return false;
                }
            }
        }
        return false;
    }
    if (shipped.written != candidate.written) {
        (void)fprintf(stderr,
                      "vdp1-user-clip: written-pixel count differs: "
                      "shipped=%lu candidate=%lu\n",
                      shipped.written, candidate.written);
        return false;
    }
    if (shipped.walked != candidate.walked) {
        (void)fprintf(stderr,
                      "vdp1-user-clip: walked-pixel count differs: "
                      "shipped=%lu candidate=%lu\n",
                      shipped.walked, candidate.walked);
        return false;
    }

    *walked = shipped.walked;
    *written = shipped.written;
    return true;
}

/* Positive control.  A narrowed box, with the candidate's own enable bit and
 * mode, must remove pixels.  Without this, `equivalence_holds` would pass just
 * as happily against a clip that is switched off. */
static bool narrowed_clip_is_in_force(unsigned long *removed)
{
    raster_result_t shipped;
    raster_result_t narrowed;
    const vdp1_clip_state_t shipped_state = state_shipped();
    const vdp1_clip_state_t narrowed_state = state_narrowed();

    if (!raster_all(&shipped, &shipped_state))
        return false;
    if (!raster_all(&narrowed, &narrowed_state))
        return false;

    if (memcmp(shipped.fb, narrowed.fb, sizeof(shipped.fb)) == 0 ||
        narrowed.written >= shipped.written) {
        (void)fprintf(stderr,
                      "vdp1-user-clip: a narrowed clip box removed nothing "
                      "(shipped=%lu narrowed=%lu written) -- the enable bit "
                      "or the mode is not in force\n",
                      shipped.written, narrowed.written);
        return false;
    }
    *removed = shipped.written - narrowed.written;
    return true;
}

int main(void)
{
    unsigned long walked = 0UL;
    unsigned long written = 0UL;
    unsigned long removed = 0UL;

    if (!mode_encoding_matches_references())
        return 1;
    if (!system_clip_covers_all_four_edges())
        return 1;
    if (!equivalence_holds(&walked, &written))
        return 1;
    if (!narrowed_clip_is_in_force(&removed))
        return 1;

    (void)printf("vdp1-user-clip: %d primitives, %lu pixels walked, "
                 "%lu written\n", PRIM_COUNT, walked, written);
    (void)printf("vdp1-user-clip: user clip (0,0)-(%d,%d) is bit-identical to "
                 "system clipping alone -- 0 writes saved\n",
                 PORT_USER_CLIP_X1, PORT_USER_CLIP_Y1);
    (void)printf("vdp1-user-clip: positive control -- a narrowed box removes "
                 "%lu writes, so the clip is genuinely in force\n", removed);
    (void)printf("vdp1-user-clip: OK\n");
    return 0;
}
