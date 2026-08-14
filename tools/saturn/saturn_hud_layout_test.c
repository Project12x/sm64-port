/* tools/saturn/saturn_hud_layout_test.c */
#include <string.h>
#include <stdio.h>

#include "saturn_hud.h"
#include "saturn_hud_atlas.h"
#include "saturn_hud_layout.h"
#include "saturn_hud_publish.h"

static int
test_layout_places_lives_digit_and_glyphs(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.lives = 4;
    snapshot.flags = 0x0001; /* HUD_DISPLAY_FLAG_LIVES */

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    int found_head = 0, found_x = 0, found_digit = 0;
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_MARIO_HEAD) found_head = 1;
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_MULTIPLY) found_x = 1;
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_DIGIT_4) found_digit = 1;
    }
    if (!found_head || !found_x || !found_digit) {
        fprintf(stderr, "lives readout missing head/x/digit cells\n");
        return 1;
    }
    return 0;
}

static int
test_layout_omits_lives_when_flag_clear(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.lives = 4;
    snapshot.flags = 0x0000;

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_DIGIT_4) {
            fprintf(stderr, "lives digit rendered with HUD_DISPLAY_NONE\n");
            return 1;
        }
    }
    return 0;
}

static int
test_layout_never_exceeds_capacity(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x004F;
    snapshot.lives = 9999;
    snapshot.coins = 9999;
    snapshot.stars = 9999;
    snapshot.timer = 0xFFFFU;

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    if (count > SM64_SATURN_HUD_LAYOUT_MAX_CELLS) {
        fprintf(stderr, "layout wrote past the fixed cell buffer\n");
        return 1;
    }
    return 0;
}

static int
test_layout_reads_power_meter_from_snapshot_not_recomputed(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x0008; /* HUD_DISPLAY_FLAG_CAMERA_AND_POWER */
    snapshot.wedges = 3;
    /* Layout gates the power meter on power_meter_animation != 0, i.e.
     * != POWER_METER_HIDDEN (src/game/hud.h's enum PowerMeterAnimation:
     * HIDDEN=0, EMPHASIZED=1, DEEMPHASIZING=2, HIDING=3, VISIBLE=4). That
     * gate exactly matches hud.c's real render_hud_power_meter(), which
     * returns early only when animation == POWER_METER_HIDDEN and renders
     * for all four other phases -- so any non-zero value here is a valid
     * "meter should be showing" state, not just POWER_METER_VISIBLE.
     * memset already zeroed power_meter_animation to POWER_METER_HIDDEN,
     * which would correctly hide the meter; this test wants it visible, so
     * it must set a non-hidden phase explicitly. POWER_METER_EMPHASIZED (1)
     * is used here, matching the same numeric-literal convention Task 3's
     * saturn_hud_snapshot_test.c already established for this field (that
     * file also can't include src/game/hud.h -- it would pull in the full
     * N64 PR/ultratypes.h dependency chain into a host-only test binary). */
    snapshot.power_meter_animation = 1; /* POWER_METER_EMPHASIZED */

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    int found = 0;
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_POWER_METER_3) found = 1;
    }
    if (!found) {
        fprintf(stderr, "power meter wedge count not reflected in layout\n");
        return 1;
    }
    return 0;
}

/* Task 7 coverage-gap closure: the implementer who wrote the four tests
 * above left three TODO(Task 7) comments in saturn_hud_layout.c flagging
 * that real mutation testing (flip/disable the branch, rerun the suite)
 * found zero coverage for the cannon reticle gate, the two camera-status
 * switch statements, and the stars<100 branch -- each mutated independently
 * and the full suite above still passed. The five tests below close those
 * three gaps. Each was itself verified the same way: temporarily mutate the
 * corresponding saturn_hud_layout.c code path (disable/invert the gate,
 * swap a case label's glyph, force the sibling branch), rebuild, and
 * confirm the specific new test below -- and only that test -- fails; then
 * revert. See this task's commit message / handoff report for the mutation
 * log. */

static int
test_layout_places_cannon_reticle_when_active(void)
{
    /* Closes TODO(Task 7) on the `if (snapshot->cannon_active)` gate
     * (saturn_hud_layout.c, bottom of sm64_saturn_hud_layout_build()): no
     * prior test ever set cannon_active, so a gate inverted to
     * `if (!snapshot->cannon_active)` -- which would suppress the reticle
     * exactly when it should show -- still passed every test above. Every
     * other flag is left clear so the cannon reticle is the only cell this
     * layout can possibly emit, making the assertion unambiguous. */
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.cannon_active = 1;

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    if (count != 1U) {
        fprintf(stderr, "cannon_active=1 with no other flags should place exactly 1 cell, placed %u\n",
               count);
        return 1;
    }
    if (cells[0].glyph != SM64_SATURN_HUD_GLYPH_CANNON_RETICLE) {
        fprintf(stderr, "cannon_active=1 did not place the cannon reticle glyph\n");
        return 1;
    }
    return 0;
}

static int
test_layout_omits_cannon_reticle_when_inactive(void)
{
    /* Companion to the test above: closes the other direction of the same
     * gate mutation. A gate forced to always-true (e.g. `if (1)`, ignoring
     * cannon_active) would slip past
     * test_layout_places_cannon_reticle_when_active unnoticed, since that
     * test only ever sets cannon_active=1. This test sets every field to
     * its default (cannon_active=0 included) and requires the reticle glyph
     * be completely absent -- mirroring the existing
     * test_layout_omits_lives_when_flag_clear pattern for the lives group. */
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].glyph == SM64_SATURN_HUD_GLYPH_CANNON_RETICLE) {
            fprintf(stderr, "cannon reticle rendered despite cannon_active == 0\n");
            return 1;
        }
    }
    return 0;
}

static int
test_layout_camera_mode_switch_selects_correct_glyph(void)
{
    /* Closes TODO(Task 7) on the CAM_STATUS_MODE_GROUP switch
     * (MARIO/LAKITU/FIXED case labels): no prior test ever set
     * camera_status to a value matching any case label, so every test above
     * only ever exercised the switch's default: break arm. Swapping one
     * case's glyph for another's would have passed every test above.
     *
     * Numeric camera_status values below mirror saturn_hud_layout.c's own
     * local CAM_STATUS_* macros (CAM_STATUS_MARIO=1, LAKITU=2, FIXED=4),
     * which themselves mirror src/game/camera.h -- this test file can't
     * include either header for the same standalone-host-build reason
     * documented on test_layout_reads_power_meter_from_snapshot_not_recomputed
     * above (it would pull in the N64 camera/ultratypes dependency chain).
     *
     * Each case asserts two things: the expected glyph for that
     * camera_status IS present (catches the switch producing nothing, or
     * producing the wrong glyph), and neither of the *other* two mode
     * glyphs is present (catches a swapped-glyph mutation that still
     * produces "a" camera-mode glyph, just the wrong one). */
    static const struct {
        int16_t camera_status;
        sm64_saturn_hud_glyph_t expected_glyph;
        const char *label;
    } cases[] = {
        { 1, SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD,  "CAM_STATUS_MARIO"  },
        { 2, SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD, "CAM_STATUS_LAKITU" },
        { 4, SM64_SATURN_HUD_GLYPH_CAM_FIXED,       "CAM_STATUS_FIXED"  },
    };
    const uint32_t case_count = (uint32_t)(sizeof(cases) / sizeof(cases[0]));

    for (uint32_t case_index = 0U; case_index < case_count; case_index++) {
        sm64_saturn_hud_snapshot_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.flags = 0x0008; /* HUD_DISPLAY_FLAG_CAMERA_AND_POWER */
        snapshot.camera_status = cases[case_index].camera_status;

        sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
        const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                             SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
        int found_expected = 0;
        for (uint32_t index = 0U; index < count; index++) {
            const sm64_saturn_hud_glyph_t glyph = cells[index].glyph;
            if (glyph == cases[case_index].expected_glyph) {
                found_expected = 1;
            } else if (glyph == SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD ||
                       glyph == SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD ||
                       glyph == SM64_SATURN_HUD_GLYPH_CAM_FIXED) {
                fprintf(stderr, "%s produced a sibling camera-mode glyph instead of its own\n",
                       cases[case_index].label);
                return 1;
            }
        }
        if (!found_expected) {
            fprintf(stderr, "%s did not produce its expected camera-mode glyph\n",
                   cases[case_index].label);
            return 1;
        }
    }
    return 0;
}

static int
test_layout_camera_cbutton_switch_selects_correct_glyph(void)
{
    /* Same closure as test_layout_camera_mode_switch_selects_correct_glyph
     * above, for the sibling CAM_STATUS_C_MODE_GROUP switch
     * (C_DOWN/C_UP case labels -- CAM_STATUS_C_DOWN=8, CAM_STATUS_C_UP=16,
     * same numeric-literal convention). */
    static const struct {
        int16_t camera_status;
        sm64_saturn_hud_glyph_t expected_glyph;
        const char *label;
    } cases[] = {
        { 8,  SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN, "CAM_STATUS_C_DOWN" },
        { 16, SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP,   "CAM_STATUS_C_UP"   },
    };
    const uint32_t case_count = (uint32_t)(sizeof(cases) / sizeof(cases[0]));

    for (uint32_t case_index = 0U; case_index < case_count; case_index++) {
        sm64_saturn_hud_snapshot_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.flags = 0x0008; /* HUD_DISPLAY_FLAG_CAMERA_AND_POWER */
        snapshot.camera_status = cases[case_index].camera_status;

        sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
        const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                             SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
        int found_expected = 0;
        for (uint32_t index = 0U; index < count; index++) {
            const sm64_saturn_hud_glyph_t glyph = cells[index].glyph;
            if (glyph == cases[case_index].expected_glyph) {
                found_expected = 1;
            } else if (glyph == SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN ||
                       glyph == SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP) {
                fprintf(stderr, "%s produced the sibling C-button glyph instead of its own\n",
                       cases[case_index].label);
                return 1;
            }
        }
        if (!found_expected) {
            fprintf(stderr, "%s did not produce its expected C-button glyph\n",
                   cases[case_index].label);
            return 1;
        }
    }
    return 0;
}

static int
test_layout_star_count_below_100_uses_two_digit_field(void)
{
    /* Closes TODO(Task 7) on the `if (snapshot->stars < 100)` branch: only
     * the >=100 sibling was ever exercised (test_layout_never_exceeds_capacity's
     * stars=9999), so disabling this branch entirely (always taking the
     * >=100 path) still passed every test above.
     *
     * The two branches are deliberately hard to tell apart by cell COUNT
     * alone: <100 pushes star-icon + multiply + 2 digits (4 cells), >=100
     * pushes star-icon + 3 digits (also 4 cells) -- same total either way.
     * What only the <100 branch ever produces is the MULTIPLY glyph (the
     * >=100 branch never pushes one) and exactly 2 digit cells instead of 3.
     * stars=42 makes both signals unambiguous: the correct <100 rendering
     * is "x42" (multiply, then digits 4,2); a mutant that always took the
     * >=100 path would zero-pad the same value to 3 digits as "042" with no
     * multiply glyph at all -- found_multiply would be false and
     * digit_cell_count would be 3, so either assertion below independently
     * catches that mutation. */
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x0004; /* HUD_DISPLAY_FLAG_STAR_COUNT */
    snapshot.stars = 42;

    sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t count = sm64_saturn_hud_layout_build(&snapshot, cells,
                                                         SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    int found_star = 0, found_multiply = 0, found_digit_4 = 0, found_digit_2 = 0;
    uint32_t digit_cell_count = 0U;
    for (uint32_t index = 0U; index < count; index++) {
        const sm64_saturn_hud_glyph_t glyph = cells[index].glyph;
        if (glyph == SM64_SATURN_HUD_GLYPH_STAR) found_star = 1;
        if (glyph == SM64_SATURN_HUD_GLYPH_MULTIPLY) found_multiply = 1;
        if (glyph == SM64_SATURN_HUD_GLYPH_DIGIT_4) found_digit_4 = 1;
        if (glyph == SM64_SATURN_HUD_GLYPH_DIGIT_2) found_digit_2 = 1;
        if (glyph >= SM64_SATURN_HUD_GLYPH_DIGIT_0 && glyph <= SM64_SATURN_HUD_GLYPH_DIGIT_9)
            digit_cell_count++;
    }
    if (!found_star) {
        fprintf(stderr, "star count layout missing the star icon glyph\n");
        return 1;
    }
    if (!found_multiply) {
        fprintf(stderr, "stars=42 (< 100) did not render the multiply glyph -- "
               "did the <100 branch stop running?\n");
        return 1;
    }
    if (digit_cell_count != 2U) {
        fprintf(stderr, "stars=42 (< 100) rendered %u digit cells, expected exactly 2\n",
               digit_cell_count);
        return 1;
    }
    if (!found_digit_4 || !found_digit_2) {
        fprintf(stderr, "stars=42 did not render its expected '4' and '2' digit glyphs\n");
        return 1;
    }
    return 0;
}

/* Systematic (col,row) collision check, added alongside the 2026-08-09
 * row-placement fix (HUD_ROW_COUNTERS and HUD_ROW_TIMER moved from the
 * bottom cluster to the top; HUD_ROW_POWER_METER moved to share the
 * timer's row -- see saturn_hud_layout.c's placement-derivation comment on
 * sm64_saturn_hud_layout_build() for the pixel math). The 2026-08-07 spec
 * review that first checked this property brute-forced all 1,638,400
 * possible input combinations by hand and never committed a reusable test;
 * this is a permanent, re-runnable replacement covering every dimension
 * that can change which (col,row) cells get written: all 32 HUD_FLAG_*
 * bit combinations, cannon_active (2), every camera mode/C-button case
 * pair hud.c's two switches recognize including "neither matched" (12),
 * power_meter_animation hidden vs. any visible phase (2), and both
 * branches of the stars<100 column-width switch (2) -- 32*2*12*2*2 = 3,072
 * snapshots. lives/coins/timer digit *values* are left fixed because
 * every digit's column offset is a fixed-width field independent of the
 * value it holds (2 digits for lives, 3 for coins, a fixed 6-cell layout
 * for the timer) -- varying them cannot change which cells get targeted,
 * only which glyph lands in an already-covered cell, which is out of
 * scope for a placement collision check. For every one of the 3,072
 * snapshots, every (col,row) pair the layout writes is required to be
 * unique within that single build() call: two glyphs targeting the same
 * cell in the same frame is exactly what a real collision looks like (the
 * second write silently clobbers the first cell in VRAM). */
static int
test_layout_no_collisions_across_realistic_snapshots(void)
{
    static const int16_t camera_statuses[] = {
        0,               /* no mode, no C-button */
        1, 2, 4,         /* CAM_STATUS_MARIO / LAKITU / FIXED alone */
        8, 16,           /* CAM_STATUS_C_DOWN / C_UP alone */
        1 | 8, 1 | 16,   /* MARIO + C_DOWN / C_UP */
        2 | 8, 2 | 16,   /* LAKITU + C_DOWN / C_UP */
        4 | 8, 4 | 16,   /* FIXED + C_DOWN / C_UP */
    };
    static const int32_t star_values[] = { 42, 9999 };  /* <100 branch, >=100 branch */
    static const int8_t power_anims[] = { 0, 1 };        /* POWER_METER_HIDDEN, any visible phase */
    static const uint8_t cannon_states[] = { 0, 1 };
    static const int16_t flag_bits[] = { 0x0001, 0x0002, 0x0004, 0x0008, 0x0040 };

    const uint32_t flag_bit_count = (uint32_t)(sizeof(flag_bits) / sizeof(flag_bits[0]));
    const uint32_t flag_combo_count = 1U << flag_bit_count;
    const uint32_t camera_status_count = (uint32_t)(sizeof(camera_statuses) / sizeof(camera_statuses[0]));
    uint32_t combos_checked = 0U;

    for (uint32_t flag_mask = 0U; flag_mask < flag_combo_count; flag_mask++) {
        int16_t flags = 0;
        for (uint32_t bit = 0U; bit < flag_bit_count; bit++) {
            if (flag_mask & (1U << bit))
                flags = (int16_t)(flags | flag_bits[bit]);
        }

        for (uint32_t cannon_index = 0U; cannon_index < 2U; cannon_index++) {
            for (uint32_t cam_index = 0U; cam_index < camera_status_count; cam_index++) {
                for (uint32_t pm_index = 0U; pm_index < 2U; pm_index++) {
                    for (uint32_t star_index = 0U; star_index < 2U; star_index++) {
                        sm64_saturn_hud_snapshot_t snapshot;
                        memset(&snapshot, 0, sizeof(snapshot));
                        snapshot.flags = flags;
                        snapshot.lives = 4;
                        snapshot.coins = 55;
                        snapshot.stars = (int16_t)star_values[star_index];
                        snapshot.timer = 12345U;
                        snapshot.wedges = 3;
                        snapshot.camera_status = camera_statuses[cam_index];
                        snapshot.power_meter_animation = power_anims[pm_index];
                        snapshot.cannon_active = cannon_states[cannon_index];

                        sm64_saturn_hud_cell_t cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
                        const uint32_t count = sm64_saturn_hud_layout_build(
                            &snapshot, cells, SM64_SATURN_HUD_LAYOUT_MAX_CELLS);

                        for (uint32_t a = 0U; a < count; a++) {
                            for (uint32_t b = a + 1U; b < count; b++) {
                                if (cells[a].col == cells[b].col && cells[a].row == cells[b].row) {
                                    fprintf(stderr,
                                           "collision at (col=%u,row=%u): glyph %d and glyph %d "
                                           "both written (flags=0x%04x cannon_active=%u "
                                           "camera_status=%d power_meter_animation=%d stars=%d)\n",
                                           cells[a].col, cells[a].row, (int)cells[a].glyph,
                                           (int)cells[b].glyph, (unsigned)(uint16_t)flags,
                                           cannon_states[cannon_index],
                                           (int)camera_statuses[cam_index],
                                           (int)power_anims[pm_index],
                                           (int)star_values[star_index]);
                                    return 1;
                                }
                            }
                        }
                        combos_checked++;
                    }
                }
            }
        }
    }

    if (combos_checked != 3072U) {
        fprintf(stderr, "expected to check 3072 snapshot combinations, checked %u\n",
               combos_checked);
        return 1;
    }
    return 0;
}

/* Test double for the real target-only atlas writer -- linked instead of
 * saturn_hud_atlas.c for this fixture, so no Yaul headers are needed.
 * g_blank_write_count additionally records how many of those writes used
 * SM64_SATURN_HUD_GLYPH_BLANK specifically, which
 * test_publish_writes_blank_for_vacated_cells below needs; g_write_count
 * alone (the plan's original double) cannot distinguish "wrote the right
 * glyph" from "wrote some glyph". */
static uint32_t g_write_count;
static uint32_t g_blank_write_count;

void
sm64_saturn_hud_atlas_write_cell(uint8_t col, uint8_t row, sm64_saturn_hud_glyph_t glyph)
{
    (void)col; (void)row;
    g_write_count++;
    if (glyph == SM64_SATURN_HUD_GLYPH_BLANK)
        g_blank_write_count++;
}

/* The normal generic BOB target has no spare LWRAM: its final 16 KiB is the
 * slave stack. HUD publication deliberately trades the old 200-byte history
 * cache for a bounded 20x14 VDP2 PND refresh with no retained state. */
static int
test_publish_rewrites_bounded_grid_without_history(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    sm64_saturn_hud_cell_t expected[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x0001; /* HUD_DISPLAY_FLAG_LIVES */
    snapshot.lives = 3;

    const uint32_t expected_count = sm64_saturn_hud_layout_build(
        &snapshot, expected, SM64_SATURN_HUD_LAYOUT_MAX_CELLS);
    const uint32_t grid_cells = 20U * 14U;
    if (expected_count == 0U) {
        fprintf(stderr, "lives layout unexpectedly has no cells\n");
        return 1;
    }
    g_write_count = 0U;
    g_blank_write_count = 0U;
    sm64_saturn_hud_publish(&snapshot);
    if (g_write_count != grid_cells + expected_count ||
        g_blank_write_count != grid_cells) {
        fprintf(stderr, "stateless publish wrote %u/%u blank (expected %u/%u)\n",
                g_write_count, g_blank_write_count,
                grid_cells + expected_count, grid_cells);
        return 1;
    }

    g_write_count = 0U;
    g_blank_write_count = 0U;
    sm64_saturn_hud_publish(&snapshot);
    if (g_write_count != grid_cells + expected_count ||
        g_blank_write_count != grid_cells) {
        fprintf(stderr, "same stateless publish wrote %u/%u blank (expected %u/%u)\n",
                g_write_count, g_blank_write_count,
                grid_cells + expected_count, grid_cells);
        return 1;
    }
    return 0;
}

static int
test_publish_blanks_grid_when_layout_becomes_empty(void)
{
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    g_write_count = 0U;
    g_blank_write_count = 0U;
    sm64_saturn_hud_publish(&snapshot);
    if (g_write_count != 20U * 14U || g_blank_write_count != 20U * 14U) {
        fprintf(stderr, "empty stateless publish wrote %u/%u blank (expected 280/280)\n",
                g_write_count, g_blank_write_count);
        return 1;
    }
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_layout_places_lives_digit_and_glyphs();
    failures += test_layout_omits_lives_when_flag_clear();
    failures += test_layout_never_exceeds_capacity();
    failures += test_layout_reads_power_meter_from_snapshot_not_recomputed();
    failures += test_layout_places_cannon_reticle_when_active();
    failures += test_layout_omits_cannon_reticle_when_inactive();
    failures += test_layout_camera_mode_switch_selects_correct_glyph();
    failures += test_layout_camera_cbutton_switch_selects_correct_glyph();
    failures += test_layout_star_count_below_100_uses_two_digit_field();
    failures += test_layout_no_collisions_across_realistic_snapshots();
    failures += test_publish_rewrites_bounded_grid_without_history();
    failures += test_publish_blanks_grid_when_layout_becomes_empty();
    return failures;
}
