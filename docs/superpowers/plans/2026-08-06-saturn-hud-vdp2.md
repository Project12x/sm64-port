# Task 23A: Source-Semantic Gameplay HUD on VDP2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the real Super Mario 64 gameplay HUD (lives, coins, stars, power meter, camera status, timer, keys, cannon reticle) on Saturn via a bounded VDP2 NBG0 character/tile plane with a real, source-derived glyph atlas — riding this codebase's own existing generation-keyed snapshot rail end to end, not a parallel one.

**Architecture:** `render_hud()` already runs, unmodified, every source tick (`area.c`'s scene-graph walk calls it whenever `scene_graph_suppressed` is false, which it is in the accepted build) — its Fast3D display-list output already lands in the bounded, already-reset-every-frame `gGfxPool` buffer and is already provably discarded (`display_suppressed` wraps the whole tick). Nothing needs to be reimplemented; it needs to be *read*. This plan adds a `hud` sub-struct to the two structs that already carry camera data from source tick to presentation — `sm64_saturn_render_snapshot_t` (filled once per source tick in the already-existing `sourceboot_capture_render_snapshot()`) and `sm64_saturn_vdp1_frame_bank_t` (filled once per render generation, mirroring the existing `camera_snapshot` copy at `main.c:1135-1143`) — so HUD data inherits that pipeline's existing generation-coherence checks for free instead of a second, parallel gate. A one-time-built glyph atlas (real N64 `main_hud_lut`/`main_hud_camera_lut` pixels, extracted locally from the user's own ROM, never committed — the same pattern already proven by `introface`'s title-screen font) is uploaded to VDP2 character-pattern VRAM once at boot. Every presented generation, the snapshot's `hud` field is diffed cell-by-cell against the last-published layout and only changed pattern-name-data words are rewritten.

**Tech Stack:** C11 (SH-2 target + host-testable fixtures), Python 3 (asset extraction + an automated headless Ymir capture/assert loop), GNU Make (`Makefile.saturn.mk` + `sourceboot/Makefile`), Yaul `vdp2_scrn_cell_format_set`/`vdp2_scrn_pnd_set` API (pinned commit, vendored at `third_party/libyaul`, verified against source — not Context7, which has no Saturn/Yaul coverage).

---

## Design decisions locked in by this plan (read before implementing)

**On touching SM64 source**: the owner's explicit direction supersedes an earlier, over-cautious draft of this plan that tried to avoid touching `hud.c` at all and planned to re-derive the power-meter animation as parallel Saturn-side logic. That would have been the actual "square peg" — a second implementation of state `hud.c` already computes correctly, every tick, for free. This plan touches `src/game/hud.c` and `src/game/level_update.h`-adjacent headers wherever a plain read-only accessor removes a needless reimplementation, same visibility level `gHudDisplay` already has as `extern`. It does not change what any of those functions compute — only what's externally readable.

1. **Snapshot placement: extend the existing pipeline, don't build a parallel one.** `src/port/saturn/gfx/saturn_render_snapshot.h:41-61` (`sm64_saturn_render_snapshot_t`) is already documented as containing only "scalar copies and generated-bank IDs... no live SM64, graph-node, VDP1, or VRAM pointer" — this is *already* Task 23A's "fixed-width, pointer-free snapshot" requirement, word for word, just not yet extended to HUD fields. Its header comment (`saturn_render_snapshot.h:15-18`) records that this exact bank-handoff design was itself derived from studying SlaveDriver Engine and Sonic Z-Treme's bank-handoff patterns — so extending it for the HUD is the direct continuation of the same prior-art lineage the owner asked to keep using, not a new invention. Confirmed end-to-end by reading the real call sites: `sourceboot_capture_render_snapshot()` (`main.c:347`, called from `main.c:506` immediately after `game_loop_one_iteration()`) already fills this struct once per source tick; the render/transform step (`main.c:1098-1103`) acquires it by generation, copies the fields it needs into `sm64_saturn_vdp1_frame_bank_t` (camera: `main.c:1135-1143`, via `sm64_saturn_vdp1_frame_bank_set_camera_snapshot`), and only *then* retires the snapshot slot (`main.c:1177-1180`) — meaning the render_snapshot itself does **not** survive to presentation time, but a copy of what it carries does, riding in the frame bank. HUD data must follow the exact same two-hop path: `render_snapshot.hud` (tick time) → copied into `frame_bank.hud` (transform time, alongside `camera_snapshot`) → read at presentation time (`sourceboot_present_generation`, where VDP2 composition already happens). This plan does **not** add any new fail-closed generation-coherence gate for the HUD snapshot — `main.c:1101-1103`'s existing `sourceboot_active_render_snapshot->generation != generation` check already provides it, for free, before the HUD fields are ever copied onward.
2. **Power-meter animation is read, not re-simulated.** `sPowerMeterHUD`/`sPowerMeterStoredHealth`/`sPowerMeterVisibleTimer` (`hud.c:42-58`) are already updated correctly, every tick, by the real `handle_power_meter_actions`/`animate_power_meter_*` functions (`hud.c:138-222`), because `render_hud()` genuinely runs (see Q1 finding below) — they just have no external accessor today. Add one. This eliminates an entire task's worth of parallel-logic risk from the first draft of this plan.
3. **Plane: NBG0, character/cell mode, `VDP2_SCRN_CHAR_SIZE_2X2` (16×16 px characters), `VDP2_SCRN_CCC_RGB_32768` (direct RGB1555, no palette), priority 7 (topmost).** Sourceboot currently claims only NBG1 (sky bitmap, all 8 VRAM cycle-pattern timeslots) and NBG3 (dbgio diagnostic text, priority 7) — confirmed by exhaustive grep of `sourceboot/main.c`; NBG0/NBG2/RBG0/RBG1 are unclaimed. **This exact shape — character mode, 1-word/10-bit-class PND, one static `PL_SIZE_1x1` plane, own dedicated VRAM region separate from the world-background plane, and top display priority above sprites — is precisely how Sonic Z-Treme's real, shipped-quality `ztFont2NBG3()` sets up its own VDP2 text/HUD plane** (`work/upstream/sonic-z-treme/Projects/SONIC Z-TREME/ZTE/ZT_VDP2.c:10-26`, pinned `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0): `slCharNbg3(COL_TYPE_256, CHAR_SIZE_1x1); slPageNbg3(...); slPlaneNbg3(PL_SIZE_1x1); slMapNbg3(page,page,page,page);` with `slPriorityNbg3(7)` set above sprites (6) and every other background. Reuse mode: **pattern-only** — SGL's `slCharNbg3`/`slPageNbg3`/etc. are proprietary-SDK declarations with no visible implementation to copy, and this plan calls Yaul's independently-implemented `vdp2_scrn_cell_format_set`/`vdp2_scrn_pnd_set` instead; only the *shape* (dedicated plane, dedicated VRAM region, topmost priority, single static page) is adopted. **Explicitly not validated by SlaveDriver Engine**: that engine's entire HUD/menu text system is VDP1-sprite-based (`work/upstream/slavedriver-engine/PRINT.C:51-137`, `EZ_setChar`/`EZ_normSpr`), and its one NBG0 touch is dead `#if 0` debug code unrelated to text — cite this contrast explicitly in `PROVENANCE.md`, don't imply it endorses the VDP2-NBG choice.
4. **The dirty-cell-only update requirement is solving a real, observed problem, not a hypothetical.** Sonic Z-Treme's own live gameplay HUD counters (lives/rings/timer, `ZT_RENDERING.c:146-152` `draw_stats()`) are reprinted via `slPrintHex`/`slLocate` **unconditionally every single frame**, and its only "clear" primitive is a brute-force 64-row full blank (`ztClearText`, `ZT_VDP2.c:3-8`) called at every scene transition. Neither reference engine implements dirty-cell diffing for VDP2 tile content — this plan's diff/publish design (Task 6) is original engineering for this project, informed by but not copied from either engine. The closest *architectural* precedent for "only write what changed, gated, flushed at a safe point" is SlaveDriver's `SCL_FUNC.C`'s register-shadow-plus-dirty-bit idiom (`SclPriBuffDirty`, lines 681-743) and `SOUND.C`'s `slotDirty[32]` — applied to different register categories (VDP2 priority/color-calc regs, SCSP audio slots), not PND cells, but the same shape. Cite both as pattern-only precedent in `PROVENANCE.md`.
5. **Cannon reticle is a flat gray polygon, not a glyph** (confirmed: `render_hud_cannon_reticle()`, `ingame_menu.c:2080-2104`, uses `gDPSetEnvColor(50,50,50,180)` and `dl_draw_triangle`, no texture lookup at all). The snapshot only needs one boolean: whether `gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON` at capture time. Task 4 draws it as one procedurally-filled solid-gray character, reusing the same atlas upload mechanism, not a separate code path.
6. **Glyph inventory needed** (all confirmed present in `assets.json` with a `"us"` region): digits `0`-`9`, `GLYPH_MULTIPLY`(50, `*`/×), `GLYPH_COIN`(51, `+`), `GLYPH_MARIO_HEAD`(52, `,`), `GLYPH_STAR`(53, `-`), `GLYPH_APOSTROPHE`(56), `GLYPH_DOUBLE_QUOTE`(57) — all from `main_hud_lut[58]` (16×16, `assets.json` prefix `textures/segment2/segment2.*.rgba16.png`). Camera icons: all 6 of `main_hud_camera_lut` (`GLYPH_CAM_CAMERA`, `_MARIO_HEAD`, `_LAKITU_HEAD`, `_FIXED` at 16×16; `_ARROW_UP`, `_ARROW_DOWN` at 8×8). Power meter: `power_meter_health_segments_lut[8]` (32×32, `assets.json` prefix `actors/power_meter/`). `GLYPH_BETA_KEY`(55, `/`) is defined but **not reachable from `render_hud()`** (only from the explicitly-dead `render_hud_keys()`, "unused function... leftover from beta version," `hud.c:312-316`) — do not extract it, do not implement key rendering; that is the source's own dead-code status, not an oversight.
7. **VRAM layout**: character-pattern data at `VDP2_VRAM_ADDR(1, 0x00000)` (bank B0, disjoint from NBG1's `VDP2_VRAM_ADDR(0, 0x00000)` sky bitmap in bank A0 and from NBG3/dbgio's internally-managed region). Pattern-name table at `VDP2_VRAM_ADDR(1, 0x08000)`, same bank, sized for one `VDP2_SCRN_PLANE_SIZE_1X1` page — mirroring Z-Treme's "own dedicated VRAM region, separate from the world-background plane" convention. Exact addresses are a starting proposal verified only by Task 8's target build/link — VRAM collisions are a link/runtime-visible failure, not a silent one.
8. **Verification is a three-stage funnel, not "build it and ask the owner to look."** Per the owner's explicit instruction: host unit/mutation tests (fast, every task) → an **automated** headless Ymir build-boot-peek-assert loop (Task 9, new — no existing Make target in this repo combines a target build with a live Ymir run and an assertion; this plan adds one) → the owner's manual profile-managed desktop-Ymir acceptance (Task 10, still mandatory — the project's standing rule that host tests can't claim target/visual success is unchanged, but nothing before that final gate should require a human to sit and watch).

---

## Task 1: HUD glyph asset extractor

**Files:**
- Create: `tools/saturn/extract_hud_glyphs.py`
- Test: `tools/saturn/test_extract_hud_glyphs.py`

- [ ] **Step 1: Write the failing test**

```python
# tools/saturn/test_extract_hud_glyphs.py
import json
import unittest
from pathlib import Path

from extract_hud_glyphs import GLYPH_MANIFEST, build_header

FIXTURE_ASSETS = {
    "textures/segment2/segment2.00000.rgba16.png": [16, 16, 512, {"us": [1083968, 0]}],
    "textures/segment2/segment2.03200.rgba16.png": [16, 16, 512, {"us": [1083968, 12288]}],
    "actors/power_meter/power_meter_eight_segments.rgba16.png": [
        32, 32, 2048, {"us": [2102288, 0]},
    ],
}


class TestGlyphManifestCoverage(unittest.TestCase):
    def test_manifest_lists_exactly_the_needed_glyphs(self):
        names = {entry.asset_name for entry in GLYPH_MANIFEST}
        self.assertIn("digit_0", names)
        self.assertIn("digit_9", names)
        self.assertIn("glyph_multiply", names)
        self.assertIn("glyph_coin", names)
        self.assertIn("glyph_mario_head", names)
        self.assertIn("glyph_star", names)
        self.assertIn("glyph_apostrophe", names)
        self.assertIn("glyph_double_quote", names)
        self.assertIn("cam_camera", names)
        self.assertIn("cam_arrow_up", names)
        self.assertIn("power_meter_1", names)
        self.assertIn("power_meter_8", names)
        self.assertNotIn("glyph_beta_key", names)

    def test_build_header_rejects_missing_asset_entry(self):
        with self.assertRaises(KeyError):
            build_header({}, rom_sha256="0" * 64)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_extract_hud_glyphs -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'extract_hud_glyphs'`

- [ ] **Step 3: Write the extractor**

```python
#!/usr/bin/env python3
# tools/saturn/extract_hud_glyphs.py
"""Extract the real SM64 gameplay-HUD glyphs into Saturn RGB1555 words.

The input is a user-supplied US SM64 ROM/archive and assets.json's pinned
offset map. Nintendo pixels are emitted only beneath build/, never tracked.
Mirrors the local-ROM-derived, never-committed pattern already established
by extract_title_prompt_font.py and extract_mario_textures.py.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555


@dataclass(frozen=True)
class GlyphAsset:
    asset_name: str
    manifest_key: str


GLYPH_MANIFEST = tuple(
    GlyphAsset(f"digit_{digit}", f"textures/segment2/segment2.{offset:05X}.rgba16.png")
    for digit, offset in enumerate(range(0x0000, 0x0A00, 0x200))
) + (
    GlyphAsset("glyph_multiply", "textures/segment2/segment2.03E00.rgba16.png"),
    GlyphAsset("glyph_coin", "textures/segment2/segment2.04000.rgba16.png"),
    GlyphAsset("glyph_mario_head", "textures/segment2/segment2.04200.rgba16.png"),
    GlyphAsset("glyph_star", "textures/segment2/segment2.04400.rgba16.png"),
    GlyphAsset("glyph_apostrophe", "textures/segment2/segment2.04800.rgba16.png"),
    GlyphAsset("glyph_double_quote", "textures/segment2/segment2.04A00.rgba16.png"),
    GlyphAsset("cam_camera", "textures/segment2/segment2.07B50.rgba16.png"),
    GlyphAsset("cam_mario_head", "textures/segment2/segment2.07D50.rgba16.png"),
    GlyphAsset("cam_lakitu_head", "textures/segment2/segment2.07F50.rgba16.png"),
    GlyphAsset("cam_fixed", "textures/segment2/segment2.08150.rgba16.png"),
    GlyphAsset("cam_arrow_up", "textures/segment2/segment2.081D0.rgba16.png"),
    GlyphAsset("cam_arrow_down", "textures/segment2/segment2.08250.rgba16.png"),
) + tuple(
    GlyphAsset(f"power_meter_{wedges}", f"actors/power_meter/power_meter_{name}_segments.rgba16.png")
    for wedges, name in enumerate(
        ("zero", "one", "two", "three", "four", "five", "six", "seven", "eight"), start=0
    )
    if wedges >= 1
)


def build_header(assets: dict, rom: bytes | None = None, rom_sha256: str | None = None) -> tuple[str, dict]:
    if rom_sha256 is None:
        if rom is None:
            raise ValueError("either rom or rom_sha256 is required")
        rom_sha256 = hashlib.sha256(rom).hexdigest()
    lines = [
        "/* Local ROM-derived output: do not commit. */",
        "#pragma once",
        "#include <stdint.h>",
    ]
    manifest = {"rom_sha256": rom_sha256, "glyphs": {}}
    decoded_bases: dict[int, bytes] = {}
    for glyph in GLYPH_MANIFEST:
        entry = assets[glyph.manifest_key]
        width, height, size, regions = entry
        base, offset = regions["us"]
        if rom is not None:
            image = decoded_bases.setdefault(base, mio0_decode(rom, base))
            data = image[offset : offset + size]
            if len(data) != size:
                raise ValueError(f"{glyph.asset_name}: range outside decoded segment")
            words = [
                saturn_rgb1555(int.from_bytes(data[i : i + 2], "big"))
                for i in range(0, size, 2)
            ]
            manifest["glyphs"][glyph.asset_name] = {
                "width": width,
                "height": height,
                "offset": offset,
                "bytes": size,
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            lines += [
                f"#define SM64_SATURN_HUD_{glyph.asset_name.upper()}_WIDTH {width}U",
                f"#define SM64_SATURN_HUD_{glyph.asset_name.upper()}_HEIGHT {height}U",
                f"static const uint16_t sm64_saturn_hud_{glyph.asset_name}[{len(words)}] = {{",
            ]
            lines += [
                "    " + ", ".join(f"0x{word:04X}" for word in words[i : i + 8]) + ","
                for i in range(0, len(words), 8)
            ]
            lines += ["};"]
        else:
            manifest["glyphs"][glyph.asset_name] = {"width": width, "height": height}
    return "\n".join(lines) + "\n", manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    rom = rom_bytes(args.rom)
    assets = json.loads(args.assets.read_text(encoding="utf-8"))
    header, manifest = build_header(assets, rom=rom)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding="utf-8")
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
```

**Note for the implementer:** the literal hex offsets for `glyph_multiply` through `cam_arrow_down` and the digit range are derived from the JP-canonical `segment2.XXXXX` filename convention, cross-checked against `bin/segment2.c`'s glyph ordering — **before running Step 5**, grep `assets.json` for each `manifest_key` used above to confirm it exists verbatim; if any literal offset is off by one glyph slot, correct it from the real `assets.json` key before proceeding.

- [ ] **Step 4: Run test to verify the manifest-coverage half passes**

Run: `.venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_extract_hud_glyphs.TestGlyphManifestCoverage.test_manifest_lists_exactly_the_needed_glyphs -v`
Expected: PASS

- [ ] **Step 5: Verify every manifest key exists in the real `assets.json`, then run the real extraction against the owner's ROM**

Run:
```bash
python3 -c "
import json
assets = json.loads(open('assets.json', encoding='utf-8').read())
from tools.saturn.extract_hud_glyphs import GLYPH_MANIFEST
missing = [g.manifest_key for g in GLYPH_MANIFEST if g.manifest_key not in assets]
print('missing:', missing)
"
```
Expected: `missing: []`. If not empty, fix the offending `GLYPH_MANIFEST` entries in Step 3 using the real keys before continuing.

Then run:
```bash
.venv-saturn-tools\Scripts\python.exe tools\saturn\extract_hud_glyphs.py --rom baserom.us.z64 --assets assets.json --output build\saturn\sourceboot\generated\saturn_hud_glyphs_generated.h --manifest build\saturn\sourceboot\generated\saturn_hud_glyphs_manifest.json
```
Expected: exits 0, writes both files under `build/` (already gitignored, `.gitignore:48`, `build/*`).

- [ ] **Step 6: Commit**

```bash
git add tools/saturn/extract_hud_glyphs.py tools/saturn/test_extract_hud_glyphs.py
git commit -m "feat(saturn): add source HUD glyph extractor"
```

---

## Task 2: Makefile wiring for the generated glyph header

**Files:**
- Modify: `src/port/saturn/sourceboot/Makefile` (grouped target, mirroring `SOURCEBOOT_MARIO_TEXTURE_HEADER`, lines 742-757)

- [ ] **Step 1: Add the grouped target**

```makefile
SOURCEBOOT_HUD_GLYPH_DIR := $(ROOT)/build/saturn/sourceboot/generated
SOURCEBOOT_HUD_GLYPH_HEADER := $(SOURCEBOOT_HUD_GLYPH_DIR)/saturn_hud_glyphs_generated.h
SOURCEBOOT_HUD_GLYPH_MANIFEST := $(SOURCEBOOT_HUD_GLYPH_DIR)/saturn_hud_glyphs_manifest.json

$(SOURCEBOOT_HUD_GLYPH_HEADER) $(SOURCEBOOT_HUD_GLYPH_MANIFEST) &: \
	$(ROOT)/tools/saturn/extract_hud_glyphs.py \
	$(ROOT)/tools/saturn/extract_mario_textures.py \
	$(ROOT)/assets.json $(ROOT)/baserom.us.z64
	@mkdir -p "$(SOURCEBOOT_HUD_GLYPH_DIR)"
	@$(SOURCEBOOT_PYTHON) "$(ROOT)/tools/saturn/extract_hud_glyphs.py" \
	  --rom "$(ROOT)/baserom.us.z64" --assets "$(ROOT)/assets.json" \
	  --output "$(SOURCEBOOT_HUD_GLYPH_HEADER)" \
	  --manifest "$(SOURCEBOOT_HUD_GLYPH_MANIFEST)"

.PHONY: source-hud-glyphs
source-hud-glyphs: $(SOURCEBOOT_HUD_GLYPH_HEADER) $(SOURCEBOOT_HUD_GLYPH_MANIFEST)
```

Add `source-hud-glyphs` alongside `source-mario-textures` in the main sourceboot build target's prerequisite list.

- [ ] **Step 2: Verify the rule runs standalone**

Run: `make -f src/port/saturn/sourceboot/Makefile source-hud-glyphs`
Expected: exits 0; both generated files exist and are non-empty.

- [ ] **Step 3: Commit**

```bash
git add src/port/saturn/sourceboot/Makefile
git commit -m "build(saturn): wire HUD glyph extraction into sourceboot"
```

---

## Task 3: HUD accessors + snapshot fields on the existing render-snapshot rail

**Files:**
- Modify: `src/game/hud.c` (two read-only accessors, zero behavior change)
- Modify: `src/game/hud.h` (matching declarations)
- Create: `src/port/saturn/gfx/saturn_hud.h` (the `sm64_saturn_hud_snapshot_t` type only — no capture function, no duplicate generation-tuple type)
- Modify: `src/port/saturn/gfx/saturn_render_snapshot.h` (add `sm64_saturn_hud_snapshot_t hud;` field)
- Modify: `src/port/saturn/sourceboot/main.c` (`sourceboot_capture_render_snapshot()`: fill `hud` field)
- Test: `tools/saturn/saturn_hud_snapshot_test.c` (pure struct-shape/host-fixture test — no Yaul dependency)

- [ ] **Step 1: Add two read-only accessors to `hud.c`**

Immediately after `set_hud_camera_status` (`hud.c:369-371`):
```c
void set_hud_camera_status(s16 status) {
    sCameraHUD.status = status;
}

/**
 * Returns the camera HUD status last set by set_hud_camera_status.
 * Pure accessor: gHudDisplay is already externally readable, this brings
 * camera status to the same visibility for platform-layer HUD backends.
 */
s16 get_hud_camera_status(void) {
    return sCameraHUD.status;
}

/**
 * Returns the power meter's current animation phase and Y position, as
 * already computed this tick by render_hud_power_meter(). Pure accessor:
 * no new computation, just visibility for platform-layer HUD backends.
 */
void get_hud_power_meter_state(s8 *out_animation, s16 *out_y) {
    if (out_animation != NULL) *out_animation = sPowerMeterHUD.animation;
    if (out_y != NULL) *out_y = sPowerMeterHUD.y;
}
```

Add matching declarations to `src/game/hud.h`:
```c
void set_hud_camera_status(s16 status);
s16 get_hud_camera_status(void);
void get_hud_power_meter_state(s8 *out_animation, s16 *out_y);
void render_hud(void);
```

- [ ] **Step 2: Write the failing snapshot-shape test**

```c
/* tools/saturn/saturn_hud_snapshot_test.c */
#include <string.h>
#include <stdio.h>

#include "saturn_hud.h"

static int
test_snapshot_is_fixed_width_and_pointer_free(void)
{
    /* Structural proof, not a runtime capture: every field must be a plain
     * scalar. sizeof must be stable and independent of pointer width so the
     * ABI can't silently change between host and SH-2 target builds. */
    if (sizeof(sm64_saturn_hud_snapshot_t) == 0U) {
        fprintf(stderr, "snapshot type is empty\n");
        return 1;
    }
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.lives = 4;
    snapshot.coins = 99;
    snapshot.stars = 15;
    snapshot.wedges = 6;
    snapshot.flags = 0x004F;
    snapshot.timer = 1801;
    snapshot.camera_status = 1;
    snapshot.power_meter_animation = 1;
    snapshot.power_meter_y = 166;
    snapshot.cannon_active = 0;
    if (snapshot.lives != 4 || snapshot.coins != 99 || snapshot.wedges != 6) {
        fprintf(stderr, "snapshot fields did not round-trip\n");
        return 1;
    }
    return 0;
}

int
main(void)
{
    return test_snapshot_is_fixed_width_and_pointer_free();
}
```

- [ ] **Step 3: Add the Make target and run it to verify RED**

```makefile
verify-saturn-hud-snapshot: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/saturn_hud_snapshot_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/saturn-hud-snapshot-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/saturn-hud-snapshot-test$(HOST_EXEEXT)']).returncode)"
```

Run: `make -f Makefile.saturn.mk verify-saturn-hud-snapshot`
Expected: FAIL — `saturn_hud.h` does not exist.

- [ ] **Step 4: Define the snapshot type**

```c
/* src/port/saturn/gfx/saturn_hud.h */
#ifndef SM64_SATURN_HUD_H
#define SM64_SATURN_HUD_H

#include <stdint.h>

/* Everything render_hud() reads or computes that the VDP2 backend needs,
 * gathered into one fixed-width, pointer-free struct -- matching the same
 * "scalar copies only" discipline sm64_saturn_render_snapshot_t already
 * documents (saturn_render_snapshot.h:39-40). This type carries no
 * generation field of its own: it inherits render-snapshot/frame-bank
 * generation coherence for free by riding inside those existing structs
 * (see saturn_render_snapshot.h and saturn_vdp1_frame_bank.h). */
typedef struct sm64_saturn_hud_snapshot {
    int16_t lives;
    int16_t coins;
    int16_t stars;
    int16_t wedges;
    int16_t keys;
    int16_t flags;
    uint16_t timer;
    int16_t camera_status;
    int8_t power_meter_animation;
    int16_t power_meter_y;
    uint8_t cannon_active;
    uint8_t reserved0;
} sm64_saturn_hud_snapshot_t;

#endif /* SM64_SATURN_HUD_H */
```

- [ ] **Step 5: Add the `hud` field to the render snapshot and fill it at the existing capture call site**

In `src/port/saturn/gfx/saturn_render_snapshot.h`, add the include and field:
```c
#include "saturn_hud.h"
```
```c
typedef struct sm64_saturn_render_snapshot {
    sm64_saturn_render_view_t camera;
    sm64_saturn_mario_actor_snapshot_t mario;
    sm64_saturn_mario_pose_selector_t mario_pose;
    sm64_saturn_hud_snapshot_t hud;
    uint32_t generation;
    /* ...existing fields unchanged... */
} sm64_saturn_render_snapshot_t;
```

In `src/port/saturn/sourceboot/main.c`'s `sourceboot_capture_render_snapshot()` (`main.c:347`), after the successful `sm64_saturn_render_snapshot_begin_write` call (`main.c:355-358`) and before the function returns, fill the new field directly from the just-ticked source globals and the two new accessors:
```c
    s8 power_meter_animation = 0;
    s16 power_meter_y = 0;
    get_hud_power_meter_state(&power_meter_animation, &power_meter_y);
    snapshot->hud.lives = gHudDisplay.lives;
    snapshot->hud.coins = gHudDisplay.coins;
    snapshot->hud.stars = gHudDisplay.stars;
    snapshot->hud.wedges = gHudDisplay.wedges;
    snapshot->hud.keys = gHudDisplay.keys;
    snapshot->hud.flags = gHudDisplay.flags;
    snapshot->hud.timer = gHudDisplay.timer;
    snapshot->hud.camera_status = get_hud_camera_status();
    snapshot->hud.power_meter_animation = power_meter_animation;
    snapshot->hud.power_meter_y = power_meter_y;
    snapshot->hud.cannon_active = (gCurrentArea != NULL &&
                                   gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON)
                                       ? 1U : 0U;
    snapshot->hud.reserved0 = 0U;
```
Place this after the existing actor-bank capture logic already in that function (it does not depend on it, but keeping one clear capture block per concern at the end of the function matches the file's existing style — read the current full function body first, since it was only partially quoted during planning, and insert at the point right before whatever the function does to finish/return, not before the `begin_write` guard clause).

- [ ] **Step 6: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-saturn-hud-snapshot`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/game/hud.c src/game/hud.h src/port/saturn/gfx/saturn_hud.h \
        src/port/saturn/gfx/saturn_render_snapshot.h \
        src/port/saturn/sourceboot/main.c tools/saturn/saturn_hud_snapshot_test.c \
        Makefile.saturn.mk
git commit -m "feat(saturn): capture the real HUD state onto the existing render-snapshot rail"
```

---

## Task 4: VDP2 NBG0 glyph atlas — one-time init

**Files:**
- Create: `src/port/saturn/gfx/saturn_hud_atlas.h`
- Create: `src/port/saturn/gfx/saturn_hud_atlas.c` (target-only: real Yaul VDP2 API, not host-testable — verified by Task 9's automated capture and Task 10's target build, not a host fixture)

- [x] **Step 1: Define the atlas layout**

```c
/* src/port/saturn/gfx/saturn_hud_atlas.h */
#ifndef SM64_SATURN_HUD_ATLAS_H
#define SM64_SATURN_HUD_ATLAS_H

#include <stdint.h>

/* One entry per distinct glyph the HUD can display. Order matches the
 * character-pattern slots written into VRAM by
 * sm64_saturn_hud_atlas_init() -- index IS the VDP2 character-pattern
 * number, so this enum's order must never change without also touching
 * the atlas init loop. */
typedef enum sm64_saturn_hud_glyph {
    SM64_SATURN_HUD_GLYPH_DIGIT_0,
    SM64_SATURN_HUD_GLYPH_DIGIT_1,
    SM64_SATURN_HUD_GLYPH_DIGIT_2,
    SM64_SATURN_HUD_GLYPH_DIGIT_3,
    SM64_SATURN_HUD_GLYPH_DIGIT_4,
    SM64_SATURN_HUD_GLYPH_DIGIT_5,
    SM64_SATURN_HUD_GLYPH_DIGIT_6,
    SM64_SATURN_HUD_GLYPH_DIGIT_7,
    SM64_SATURN_HUD_GLYPH_DIGIT_8,
    SM64_SATURN_HUD_GLYPH_DIGIT_9,
    SM64_SATURN_HUD_GLYPH_MULTIPLY,
    SM64_SATURN_HUD_GLYPH_COIN,
    SM64_SATURN_HUD_GLYPH_MARIO_HEAD,
    SM64_SATURN_HUD_GLYPH_STAR,
    SM64_SATURN_HUD_GLYPH_APOSTROPHE,
    SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE,
    SM64_SATURN_HUD_GLYPH_CAM_CAMERA,
    SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD,
    SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD,
    SM64_SATURN_HUD_GLYPH_CAM_FIXED,
    SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP,
    SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN,
    SM64_SATURN_HUD_GLYPH_POWER_METER_1,
    SM64_SATURN_HUD_GLYPH_POWER_METER_2,
    SM64_SATURN_HUD_GLYPH_POWER_METER_3,
    SM64_SATURN_HUD_GLYPH_POWER_METER_4,
    SM64_SATURN_HUD_GLYPH_POWER_METER_5,
    SM64_SATURN_HUD_GLYPH_POWER_METER_6,
    SM64_SATURN_HUD_GLYPH_POWER_METER_7,
    SM64_SATURN_HUD_GLYPH_POWER_METER_8,
    SM64_SATURN_HUD_GLYPH_CANNON_RETICLE,
    SM64_SATURN_HUD_GLYPH_BLANK, /* solid transparent: clears a dirty cell */
    SM64_SATURN_HUD_GLYPH_COUNT
} sm64_saturn_hud_glyph_t;

/* Uploads every character pattern once and configures the NBG0 cell plane.
 * Must run after source_cart_load() (glyph pixels are .cart_rodata-linked,
 * same constraint sourceboot_init_sky_bitmap already documents for NBG1)
 * and before the first sm64_saturn_hud_atlas_write_cell() call. */
void sm64_saturn_hud_atlas_init(void);

/* Writes one 16x16 character cell at tile-grid position (col,row) (0-based,
 * NBG0 plane is 32x28 tiles wide/tall at this screen resolution). No-op and
 * safe if col/row are out of the bounded HUD region. */
void sm64_saturn_hud_atlas_write_cell(uint8_t col, uint8_t row,
                                      sm64_saturn_hud_glyph_t glyph);

#endif /* SM64_SATURN_HUD_ATLAS_H */
```

- [x] **Step 2: Implement the atlas using the verified Yaul cell API** — implemented with two corrections found by re-verifying against the real vendored Yaul source (see implementation note after Step 5 below): (a) `CHAR_SIZE_2X2` character-pattern pixel data is four separately-addressed 8x8 quadrant cells, not one flat 16-wide raster, so the upload helper reorders accordingly instead of copying linearly; (b) the real generated `sm64_saturn_hud_power_meter_*` arrays are `[1024]` (32x32 source), not `[256]`, so those glyphs crop the top-left 16x16 region using the real 32-wide source stride.

```c
/* src/port/saturn/gfx/saturn_hud_atlas.c */
#include "saturn_hud_atlas.h"

#include <yaul.h>

#include "saturn_hud_glyphs_generated.h"

#define HUD_CPD_BASE VDP2_VRAM_ADDR(1, 0x00000)
#define HUD_PND_BASE VDP2_VRAM_ADDR(1, 0x08000)
#define HUD_TILE_COLS 32U
#define HUD_TILE_ROWS 28U
#define HUD_CHAR_BYTES (16U * 16U * 2U) /* 16x16 RGB1555 texels */

static void
hud_atlas_upload_one(sm64_saturn_hud_glyph_t glyph, const uint16_t *pixels,
                     uint32_t pixel_count)
{
    volatile uint16_t *const dest = (volatile uint16_t *)
        (CPU_CACHE_THROUGH | (HUD_CPD_BASE + (uint32_t)glyph * HUD_CHAR_BYTES));
    for (uint32_t index = 0U; index < pixel_count; index++)
        dest[index] = pixels[index];
}

void
sm64_saturn_hud_atlas_init(void)
{
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_0, sm64_saturn_hud_digit_0, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_1, sm64_saturn_hud_digit_1, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_2, sm64_saturn_hud_digit_2, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_3, sm64_saturn_hud_digit_3, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_4, sm64_saturn_hud_digit_4, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_5, sm64_saturn_hud_digit_5, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_6, sm64_saturn_hud_digit_6, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_7, sm64_saturn_hud_digit_7, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_8, sm64_saturn_hud_digit_8, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DIGIT_9, sm64_saturn_hud_digit_9, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_MULTIPLY, sm64_saturn_hud_glyph_multiply, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_COIN, sm64_saturn_hud_glyph_coin, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_MARIO_HEAD, sm64_saturn_hud_glyph_mario_head, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_STAR, sm64_saturn_hud_glyph_star, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_APOSTROPHE, sm64_saturn_hud_glyph_apostrophe, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE, sm64_saturn_hud_glyph_double_quote, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_CAM_CAMERA, sm64_saturn_hud_cam_camera, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD, sm64_saturn_hud_cam_mario_head, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD, sm64_saturn_hud_cam_lakitu_head, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_CAM_FIXED, sm64_saturn_hud_cam_fixed, 256U);
    /* Arrows are native 8x8 source art; upload into the top-left quadrant
     * of their 16x16 slot -- the layout builder (Task 5) places them with
     * an 8px-aware offset, matching hud.c's own render_hud_small_tex_lut
     * distinction between the 16x16 and 8x8 source glyph sizes. */
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP, sm64_saturn_hud_cam_arrow_up, 64U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN, sm64_saturn_hud_cam_arrow_down, 64U);
    /* Power meter source art is 32x32 (4 characters); render as one
     * representative 16x16 cell per wedge count via the top-left quadrant,
     * consistent with the arrow handling above. Only wedges 1-8 exist as
     * source assets -- hud.c's render_hud_power_meter() never renders the
     * meter at 0 wedges (POWER_METER_HIDDEN returns early, hud.c:236-238),
     * and power_meter_health_segments_lut itself is indexed
     * [numHealthWedges - 1] with no 0-wedge slot -- so there is no
     * "power_meter_0" source texture to extract or upload. */
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_1, sm64_saturn_hud_power_meter_1, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_2, sm64_saturn_hud_power_meter_2, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_3, sm64_saturn_hud_power_meter_3, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_4, sm64_saturn_hud_power_meter_4, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_5, sm64_saturn_hud_power_meter_5, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_6, sm64_saturn_hud_power_meter_6, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_7, sm64_saturn_hud_power_meter_7, 256U);
    hud_atlas_upload_one(SM64_SATURN_HUD_GLYPH_POWER_METER_8, sm64_saturn_hud_power_meter_8, 256U);

    /* Cannon reticle and blank are procedural, not extracted -- solid gray
     * (matching ingame_menu.c's gDPSetEnvColor(50,50,50,180)) and solid
     * transparent (alpha bit 0 clear) respectively. */
    {
        volatile uint16_t *const reticle = (volatile uint16_t *)
            (CPU_CACHE_THROUGH | (HUD_CPD_BASE +
             (uint32_t)SM64_SATURN_HUD_GLYPH_CANNON_RETICLE * HUD_CHAR_BYTES));
        volatile uint16_t *const blank = (volatile uint16_t *)
            (CPU_CACHE_THROUGH | (HUD_CPD_BASE +
             (uint32_t)SM64_SATURN_HUD_GLYPH_BLANK * HUD_CHAR_BYTES));
        for (uint32_t index = 0U; index < 256U; index++) {
            reticle[index] = 0x8000U | (6U << 10) | (6U << 5) | 6U; /* opaque gray */
            blank[index] = 0x0000U; /* transparent: alpha bit clear */
        }
    }
    cpu_cache_purge();

    const vdp2_scrn_cell_format_t format = {
        .scroll_screen = VDP2_SCRN_NBG0,
        .ccc = VDP2_SCRN_CCC_RGB_32768,
        .char_size = VDP2_SCRN_CHAR_SIZE_2X2,
        .pnd_size = 1U,
        .aux_mode = VDP2_SCRN_AUX_MODE_1,
        .plane_size = VDP2_SCRN_PLANE_SIZE_1X1,
        .cpd_base = HUD_CPD_BASE,
        .palette_base = 0U,
    };
    const vdp2_scrn_normal_map_t map = {
        .plane_a = HUD_PND_BASE,
        .plane_b = HUD_PND_BASE,
        .plane_c = HUD_PND_BASE,
        .plane_d = HUD_PND_BASE,
    };
    vdp2_scrn_cell_format_set(&format, &map);

    for (uint16_t row = 0U; row < HUD_TILE_ROWS; row++) {
        for (uint16_t col = 0U; col < HUD_TILE_COLS; col++)
            sm64_saturn_hud_atlas_write_cell((uint8_t)col, (uint8_t)row,
                                              SM64_SATURN_HUD_GLYPH_BLANK);
    }
}

void
sm64_saturn_hud_atlas_write_cell(uint8_t col, uint8_t row,
                                 sm64_saturn_hud_glyph_t glyph)
{
    if (col >= HUD_TILE_COLS || row >= HUD_TILE_ROWS)
        return;
    const uint32_t cell_index = (uint32_t)row * HUD_TILE_COLS + col;
    volatile uint16_t *const pnd = (volatile uint16_t *)
        (CPU_CACHE_THROUGH | (HUD_PND_BASE + cell_index * 2U));
    const uint32_t cpd_addr = HUD_CPD_BASE + (uint32_t)glyph * HUD_CHAR_BYTES;
    *pnd = (uint16_t)VDP2_SCRN_PND_CONFIG_1(0, cpd_addr, 0);
}
```

- [x] **Step 3: Record the Z-Treme provenance citation**

Add an entry to `docs/saturn/UPSTREAM_CODE_LEDGER.md` and `docs/saturn/PROVENANCE.md` for this file: reuse mode **pattern-only**, source `Maxime-XL2/SONIC-Z-TREME` pinned `cff75451c1616aac1236fc2b44223902b55c706b` (GPL-3.0), files inspected `ZTE/ZT_VDP2.c:10-26` (`ztFont2NBG3`), pattern adopted: dedicated character-mode plane with its own VRAM region, single static page, topmost display priority — no SGL source copied, Yaul's independently-implemented cell API used instead.

- [x] **Step 4: This module is target-only (no host fixture)** — real Yaul VDP2 register/VRAM access does not compile or mean anything on the host. Correctness is verified by Task 9's automated capture and Task 10's Ymir visual check, not a `make verify-*` step. Note this explicitly in the SDD progress ledger so a reviewer does not mark it "blocked" for lacking a host test.

  **No dedicated SDD progress ledger exists for this plan** (checked `.superpowers/sdd/` — no `2026-08-06-saturn-hud-vdp2/` directory, unlike some other in-flight plans in this repo). This checkbox update in the plan file itself is the progress record for Task 4; a reviewer should not treat the absence of a `make verify-*` target for this file as incomplete or blocked work — it is the expected, designed-for shape of a target-only module per this exact step.

  **Verification actually performed, beyond what Step 4 originally asked for:** no target link was attempted (needs the SH-2 game-tree link from Task 8), but the real `sh-elf-gcc` 14.3.0 cross-compiler at `work/yaul-install/bin/sh-elf-gcc.exe` (found already installed) *was* used to both `-fsyntax-only` check and fully compile-to-object-file (`-c`, real `sh-elf-as` via `-B`) `saturn_hud_atlas.c` against the real vendored Yaul headers and the real Task-2-generated `saturn_hud_glyphs_generated.h`, with `-Wall -Wextra -Wpedantic` and zero diagnostics. The resulting `.o`'s symbol table confirms every `sm64_saturn_hud_*` glyph array resolved, both public functions (`sm64_saturn_hud_atlas_init`, `sm64_saturn_hud_atlas_write_cell`) are correctly exported, the internal helper is correctly local, and the only unresolved externs are the two genuinely-external Yaul calls (`cpu_cache_purge`, `vdp2_scrn_cell_format_set`) plus compiler-generated `memcpy`-class helpers. This is real compiler verification, not merely a read-through — stronger than Step 4 anticipated being possible pre-Task-8.

- [x] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_hud_atlas.h src/port/saturn/gfx/saturn_hud_atlas.c \
        docs/saturn/UPSTREAM_CODE_LEDGER.md docs/saturn/PROVENANCE.md
git commit -m "feat(saturn): initialize the VDP2 NBG0 HUD glyph atlas"
```

**Implementation note (2026-08-07):** re-verifying this task's code against the
real vendored Yaul source (not just this plan's snippet, per the task's own
verification mandate) surfaced two corrections applied before commit:

1. **`CHAR_SIZE_2X2` pixel layout.** VDP2 does not store a 16x16 character
   pattern as one contiguous 16-wide raster; it is four separately-addressed,
   individually-contiguous 8x8 cells (top-left/top-right/bottom-left/
   bottom-right, 64 words each). This is confirmed two ways: (a)
   `vdp2_scrn_pnd_set()`'s aux-mode character-number bit-packing
   (`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_scrn_cell.c:320-356`)
   supplements the pattern-name table's character number with implicit low
   bits that select one of the four sub-cells, which only makes sense if the
   sub-cells are separately addressed; (b) Yaul's own `satconv` texture
   converter's `TILE_16x16` case reads exactly those four 8x8 quadrants, in
   that order, into a contiguous 256-byte buffer
   (`third_party/libyaul/tools/satconv/tile.c:177-208`). The plan's original
   snippet copied source pixels into the character-pattern slot with a flat
   `dest[index] = pixels[index]` loop, which would have interleaved rows
   from different quadrants and produced scrambled glyphs on real hardware
   (and in cycle-accurate emulation) for every glyph wider than 8px. Fixed by
   replacing `hud_atlas_upload_one()` with `hud_atlas_upload_pattern()`,
   which performs the same quadrant reordering as `satconv`'s `TILE_16x16`
   case, generalized with a source-stride parameter.
2. **Power-meter source array size.** The plan's snippet assumed
   `sm64_saturn_hud_power_meter_1..8` were `[256]` (16x16) like the other
   glyphs. The real generated header
   (`build/saturn/sourceboot/generated/saturn_hud_glyphs_generated.h`, from
   Task 2's already-completed extraction run) declares them `[1024]` (32x32
   — `SM64_SATURN_HUD_POWER_METER_*_WIDTH`/`_HEIGHT` are both `32U`),
   matching the plan's own prose ("Power meter source art is 32x32") but not
   its code. `hud_atlas_upload_pattern()`'s source-stride parameter lets the
   power-meter calls crop the real top-left 16x16 region (stride 32, width/
   height 16) instead of reading the first 256 words linearly, which would
   have read the first 8 full 32-wide source rows rather than a 16x16
   square.
3. **Visible tile-grid bounds.** The plan's header doc comment and
   `HUD_TILE_COLS`/`HUD_TILE_ROWS` constants stated "32x28 tiles... at this
   screen resolution." This port's actual VDP2 TV mode is 320x224
   (`VDP2_TVMD_HORZ_NORMAL_A` / `VDP2_TVMD_VERT_224`, set in
   `user_init()`, `src/port/saturn/sourceboot/main.c:1512-1514`), giving
   20x14 visible 16x16-px columns/rows (320/16, 224/16), not 32x28. 32x32 is
   the underlying page's raw hardware capacity for `CHAR_SIZE_2X2`
   (`VDP2_SCRN_PAGE_WIDTH/HEIGHT_CALCULATE` in `scrn_macros.h`, independent
   of screen resolution) — real and usable as a PND address stride, but not
   all visible at this resolution. Fixed by splitting the single constant
   into `HUD_PAGE_STRIDE_COLS` (32, used only for the real PND address
   stride in `sm64_saturn_hud_atlas_write_cell()`) and corrected
   `HUD_TILE_COLS`/`HUD_TILE_ROWS` (20/14, the public visible-region bounds
   check) so a Task 5 layout that stayed within the plan's stated 32x28
   region would not have silently placed HUD elements off-screen.

All three were resolvable by adjusting the code to match verified reality,
per this task's escalation criteria, rather than requiring a BLOCKED/
NEEDS_CONTEXT stop. Full derivation is in the implementation comments in
`saturn_hud_atlas.c`/`.h` and in `PROVENANCE.md`'s 2026-08-07 entry.

**Follow-up (2026-08-07, code-review response):** a stricter-warning review
pass (`-Wconversion -Wsign-conversion -Wshadow -Wcast-align -Wcast-qual
-Wdouble-promotion -Wundef -Wstrict-prototypes`, still zero diagnostics)
found two missing guards, both fixed in a follow-up commit: (1)
`sm64_saturn_hud_atlas_write_cell()` now also rejects `glyph >=
SM64_SATURN_HUD_GLYPH_COUNT`, matching `saturn_demo_render.c:471-472`'s
index-plus-`_COUNT` guard shape; (2) a file-scope `_Static_assert` now
enforces that the atlas's character-pattern data can't grow past
`HUD_PND_BASE`, matching `saturn_pcm_protocol.h:164`'s invariant shape --
verified to actually fire (not just compile) by shrinking `HUD_PND_BASE`
in a scratch copy and confirming `sh-elf-gcc -fsyntax-only` fails with the
written message, then discarding the scratch copy. Also added a
`HUD_CHAR_DIM` named constant for the power-meter crop calls. Left the
reviewer's optional host-testable-extraction suggestion as a note for
later rather than building new test scaffolding that Task 5's own planned
`saturn_hud_layout.c` would likely have to reconcile with. See
`CHANGELOG.md`'s matching entry for full detail.

---

## Task 5: Dirty-cell HUD layout renderer

**Files:**
- Create: `src/port/saturn/gfx/saturn_hud_layout.h`
- Create: `src/port/saturn/gfx/saturn_hud_layout.c`
- Test: `tools/saturn/saturn_hud_layout_test.c`

- [x] **Step 1: Write the failing test** — implemented with one correction: `test_layout_reads_power_meter_from_snapshot_not_recomputed` did not set `snapshot.power_meter_animation`, leaving it `POWER_METER_HIDDEN` (0) from `memset` while still expecting the meter glyph to appear (see implementation note after Step 5 below). Fixed the test, not the layout's gate.

```c
/* tools/saturn/saturn_hud_layout_test.c */
#include <string.h>
#include <stdio.h>

#include "saturn_hud.h"
#include "saturn_hud_atlas.h"
#include "saturn_hud_layout.h"

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

int
main(void)
{
    int failures = 0;
    failures += test_layout_places_lives_digit_and_glyphs();
    failures += test_layout_omits_lives_when_flag_clear();
    failures += test_layout_never_exceeds_capacity();
    failures += test_layout_reads_power_meter_from_snapshot_not_recomputed();
    return failures;
}
```

- [x] **Step 2: Add the Make target and run it to verify RED** — RED confirmed by construction rather than a separate failing-compile run: `saturn_hud_layout.h`/`.c` did not exist anywhere in the repo at the start of this task (confirmed by directory listing before writing anything), so the target could not have passed. Went straight to writing the implementation from there. Running the resulting Make target needed two environment workarounds in this sandbox (see implementation note after Step 5 below).

```makefile
verify-saturn-hud-layout: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/saturn_hud_layout_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_hud_layout.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/saturn-hud-layout-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/saturn-hud-layout-test$(HOST_EXEEXT)']).returncode)"
```

Run: `make -f Makefile.saturn.mk verify-saturn-hud-layout`
Expected: FAIL — `saturn_hud_layout.h`/`.c` do not exist.

- [x] **Step 3: Implement the layout builder** — implemented with one correction found by reading the real, current `saturn_hud_atlas.c` rather than trusting this plan's snippet: nearly every `(col,row)` literal below (lives/coins at `row=26`, stars at `col=24-27`, the timer at `col=20-25`, the camera glyphs at `col=26-27`) falls outside `sm64_saturn_hud_atlas_write_cell()`'s real `col<20`/`row<14` visible-grid bound and would have been silently dropped, permanently invisible on real hardware. Re-derived every placement to fit the real grid (see implementation note after Step 5 below).

```c
/* src/port/saturn/gfx/saturn_hud_layout.h */
#ifndef SM64_SATURN_HUD_LAYOUT_H
#define SM64_SATURN_HUD_LAYOUT_H

#include <stdint.h>

#include "saturn_hud.h"
#include "saturn_hud_atlas.h"

#define SM64_SATURN_HUD_LAYOUT_MAX_CELLS 40U

typedef struct sm64_saturn_hud_cell {
    uint8_t col;
    uint8_t row;
    sm64_saturn_hud_glyph_t glyph;
} sm64_saturn_hud_cell_t;

/* Decides which glyph belongs in which tile cell for one HUD snapshot.
 * Pure function: no VRAM access, fully host-testable. Writes at most
 * capacity cells into out_cells and returns the count actually written
 * (never more than capacity, even for pathological snapshot values). */
uint32_t sm64_saturn_hud_layout_build(const sm64_saturn_hud_snapshot_t *snapshot,
                                      sm64_saturn_hud_cell_t *out_cells,
                                      uint32_t capacity);

#endif /* SM64_SATURN_HUD_LAYOUT_H */
```

```c
/* src/port/saturn/gfx/saturn_hud_layout.c */
#include "saturn_hud_layout.h"

#define HUD_FLAG_LIVES            0x0001
#define HUD_FLAG_COIN_COUNT       0x0002
#define HUD_FLAG_STAR_COUNT       0x0004
#define HUD_FLAG_CAMERA_AND_POWER 0x0008
#define HUD_FLAG_TIMER            0x0040

#define CAM_STATUS_MARIO   (1 << 0)
#define CAM_STATUS_LAKITU  (1 << 1)
#define CAM_STATUS_FIXED   (1 << 2)
#define CAM_STATUS_C_DOWN  (1 << 3)
#define CAM_STATUS_C_UP    (1 << 4)
#define CAM_STATUS_MODE_GROUP   (CAM_STATUS_MARIO | CAM_STATUS_LAKITU | CAM_STATUS_FIXED)
#define CAM_STATUS_C_MODE_GROUP (CAM_STATUS_C_DOWN | CAM_STATUS_C_UP)

static uint32_t
push_cell(sm64_saturn_hud_cell_t *out_cells, uint32_t count, uint32_t capacity,
         uint8_t col, uint8_t row, sm64_saturn_hud_glyph_t glyph)
{
    if (count >= capacity)
        return count;
    out_cells[count].col = col;
    out_cells[count].row = row;
    out_cells[count].glyph = glyph;
    return count + 1U;
}

static sm64_saturn_hud_glyph_t
digit_glyph(int value)
{
    if (value < 0) value = 0;
    if (value > 9) value = 9;
    return (sm64_saturn_hud_glyph_t)(SM64_SATURN_HUD_GLYPH_DIGIT_0 + value);
}

static uint32_t
push_clamped_int(sm64_saturn_hud_cell_t *out_cells, uint32_t count, uint32_t capacity,
                 uint8_t start_col, uint8_t row, int32_t value, uint8_t max_digits)
{
    if (value < 0) value = 0;
    int32_t divisor = 1;
    for (uint8_t place = 1U; place < max_digits; place++)
        divisor *= 10;
    uint8_t written = 0U;
    for (uint8_t place = 0U; place < max_digits; place++) {
        const int32_t digit = (value / divisor) % 10;
        count = push_cell(out_cells, count, capacity,
                          (uint8_t)(start_col + written), row, digit_glyph(digit));
        written++;
        divisor /= 10;
        if (divisor == 0) divisor = 1;
    }
    return count;
}

uint32_t
sm64_saturn_hud_layout_build(const sm64_saturn_hud_snapshot_t *snapshot,
                             sm64_saturn_hud_cell_t *out_cells, uint32_t capacity)
{
    uint32_t count = 0U;
    if (snapshot == NULL || out_cells == NULL || capacity == 0U)
        return 0U;

    const int16_t flags = snapshot->flags;

    if (flags & HUD_FLAG_LIVES) {
        count = push_cell(out_cells, count, capacity, 1U, 26U, SM64_SATURN_HUD_GLYPH_MARIO_HEAD);
        count = push_cell(out_cells, count, capacity, 2U, 26U, SM64_SATURN_HUD_GLYPH_MULTIPLY);
        count = push_clamped_int(out_cells, count, capacity, 3U, 26U, snapshot->lives, 2U);
    }
    if (flags & HUD_FLAG_COIN_COUNT) {
        count = push_cell(out_cells, count, capacity, 10U, 26U, SM64_SATURN_HUD_GLYPH_COIN);
        count = push_cell(out_cells, count, capacity, 11U, 26U, SM64_SATURN_HUD_GLYPH_MULTIPLY);
        count = push_clamped_int(out_cells, count, capacity, 12U, 26U, snapshot->coins, 3U);
    }
    if (flags & HUD_FLAG_STAR_COUNT) {
        const uint8_t star_col = 24U;
        count = push_cell(out_cells, count, capacity, star_col, 26U, SM64_SATURN_HUD_GLYPH_STAR);
        if (snapshot->stars < 100) {
            count = push_cell(out_cells, count, capacity, (uint8_t)(star_col + 1U), 26U,
                              SM64_SATURN_HUD_GLYPH_MULTIPLY);
            count = push_clamped_int(out_cells, count, capacity, (uint8_t)(star_col + 2U), 26U,
                                     snapshot->stars, 2U);
        } else {
            count = push_clamped_int(out_cells, count, capacity, (uint8_t)(star_col + 1U), 26U,
                                     snapshot->stars, 3U);
        }
    }
    if (flags & HUD_FLAG_TIMER) {
        const uint16_t frames = snapshot->timer;
        const uint16_t minutes = (uint16_t)(frames / (30U * 60U));
        const uint16_t seconds = (uint16_t)((frames - minutes * 1800U) / 30U);
        const uint16_t frac = (uint16_t)(((frames - minutes * 1800U - seconds * 30U)) / 3U);
        count = push_clamped_int(out_cells, count, capacity, 20U, 2U, minutes, 1U);
        count = push_cell(out_cells, count, capacity, 21U, 2U, SM64_SATURN_HUD_GLYPH_APOSTROPHE);
        count = push_clamped_int(out_cells, count, capacity, 22U, 2U, seconds, 2U);
        count = push_cell(out_cells, count, capacity, 24U, 2U, SM64_SATURN_HUD_GLYPH_DOUBLE_QUOTE);
        count = push_clamped_int(out_cells, count, capacity, 25U, 2U, frac, 1U);
    }
    if (flags & HUD_FLAG_CAMERA_AND_POWER) {
        count = push_cell(out_cells, count, capacity, 26U, 1U, SM64_SATURN_HUD_GLYPH_CAM_CAMERA);
        switch (snapshot->camera_status & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            count = push_cell(out_cells, count, capacity, 27U, 1U, SM64_SATURN_HUD_GLYPH_CAM_MARIO_HEAD);
            break;
        case CAM_STATUS_LAKITU:
            count = push_cell(out_cells, count, capacity, 27U, 1U, SM64_SATURN_HUD_GLYPH_CAM_LAKITU_HEAD);
            break;
        case CAM_STATUS_FIXED:
            count = push_cell(out_cells, count, capacity, 27U, 1U, SM64_SATURN_HUD_GLYPH_CAM_FIXED);
            break;
        default:
            break;
        }
        switch (snapshot->camera_status & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            count = push_cell(out_cells, count, capacity, 27U, 2U, SM64_SATURN_HUD_GLYPH_CAM_ARROW_DOWN);
            break;
        case CAM_STATUS_C_UP:
            count = push_cell(out_cells, count, capacity, 27U, 0U, SM64_SATURN_HUD_GLYPH_CAM_ARROW_UP);
            break;
        default:
            break;
        }

        /* Power meter Y position (snapshot->power_meter_y) is intentionally
         * not used for tile placement -- Saturn's tile grid is coarser than
         * the source's pixel-accurate slide animation, so this plan places
         * the meter at a fixed cell and represents animation only through
         * which wedge-count glyph is shown. power_meter_animation ==
         * POWER_METER_HIDDEN (0) hides it entirely; every other phase
         * shows the current wedge count. */
        if (snapshot->power_meter_animation != 0) {
            /* Only wedges 1-8 have a source texture (see Task 4's atlas
             * comment) -- clamp to that range, not 0-8. A wedges==0 read
             * while visible would be a source-side edge case already
             * absent from hud.c's own lookup table; this clamp is a
             * defensive VRAM-read bound, not a gameplay behavior change. */
            int wedges = snapshot->wedges;
            if (wedges < 1) wedges = 1;
            if (wedges > 8) wedges = 8;
            count = push_cell(out_cells, count, capacity, 8U, 10U,
                              (sm64_saturn_hud_glyph_t)(SM64_SATURN_HUD_GLYPH_POWER_METER_1 + (wedges - 1)));
        }
    }
    if (snapshot->cannon_active) {
        count = push_cell(out_cells, count, capacity, 15U, 13U, SM64_SATURN_HUD_GLYPH_CANNON_RETICLE);
    }

    return count;
}
```

- [x] **Step 4: Run test to verify it passes** — PASS, confirmed multiple ways: direct `gcc -std=c11 -Wall -Wextra -Werror` compile + execution (exit 0), the actual `make -f Makefile.saturn.mk verify-saturn-hud-layout` target (after two environment workarounds, see implementation note below), and a real mutation-testing pass per project standing policy (flipping the power-meter gate and inverting the lives-flag check were both caught; an off-by-one in the capacity guard survives uncaught, a pre-existing test-coverage gap, see implementation note).

Run: `make -f Makefile.saturn.mk verify-saturn-hud-layout`
Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add src/port/saturn/gfx/saturn_hud_layout.h src/port/saturn/gfx/saturn_hud_layout.c \
        tools/saturn/saturn_hud_layout_test.c Makefile.saturn.mk
git commit -m "feat(saturn): build the source-driven HUD tile layout"
```

**Implementation note (2026-08-07):** resolved the power-meter test/gate
question flagged in Step 1 by reading the real `src/game/hud.h` (`enum
PowerMeterAnimation`: `POWER_METER_HIDDEN` is the first enumerator, value
0) and the real `hud.c` (`render_hud_power_meter()`, `hud.c:229-257`)
rather than assuming. The source's own gate is
`if (sPowerMeterHUD.animation == POWER_METER_HIDDEN) return;` -- it renders
for all four non-hidden phases (EMPHASIZED, DEEMPHASIZING, HIDING,
VISIBLE), not just the resting VISIBLE state. So the plan's
`power_meter_animation != 0` gate was exactly correct and was kept
unchanged; the bug was in the test, which never set
`power_meter_animation` and so left it at `POWER_METER_HIDDEN` while still
expecting the meter to show. Fixed the test (`power_meter_animation = 1`,
`POWER_METER_EMPHASIZED`, matching the numeric-literal convention Task 3's
`saturn_hud_snapshot_test.c` already established for this field), not the
gate.

Also found and fixed a placement bug this plan's Step 3 snippet did not
flag: most of its literal `(col,row)` values are outside
`sm64_saturn_hud_atlas_write_cell()`'s real visible-grid bound (`col<20`,
`row<14`, confirmed in the real, current `saturn_hud_atlas.c`). Left
uncorrected, this would have made lives, coins, stars, the timer, and the
camera status indicator permanently invisible on real hardware and in
emulation -- passing every test in this plan (none check `col`/`row`)
while silently defeating the plan's stated goal, since Task 6's dirty-cell
publisher (already drafted below) forwards this layout's `(col,row)`
straight into `sm64_saturn_hud_atlas_write_cell()` with no remapping.
Re-derived every placement from `hud.c`'s real pixel coordinates, fit to
the real 20x14 grid, and checked for zero cell collisions across every
group that can be simultaneously visible. Full derivation and the CHANGELOG
entry have the details; see `saturn_hud_layout.c`'s own comment for the
final grid assignment.

Hit two host-tooling environment quirks getting a real `make` run in this
sandbox: Cygwin `make` does not see the `OS` environment variable at all
(confirmed with a minimal repro Makefile), so `ifeq ($(OS),Windows_NT)`
silently picked the wrong branch; fixed by passing `OS=Windows_NT` on the
`make` command line. Separately, the venv Python's `subprocess.run()`
test-runner wrapper can't launch an MSYS-style `/d/...` path via native
`CreateProcess`; worked around by running the make-built binary directly.
Both are environment issues, not defects in the Makefile target or the
code.

**Follow-up (2026-08-07, code-review response):** two review passes ran
against this task -- a spec review that brute-force-checked all 1,638,400
possible input combinations (confirmed the placement fix above is
genuinely in-bounds and collision-free) and a code-quality review that
found no functional defects but flagged three things, all addressed in a
follow-up commit:

1. **The placement-derivation comment was factually wrong about screen
   position for 4 of 5 element groups.** It claimed lives/coins/stars/
   camera "all sit at y=205-209" in the source. The reviewer traced the
   actual rendering paths: lives/coins/stars (`HUD_TOP_Y=209`) and the
   timer (`y=185`) all go through `print_text()`/`print_text_fmt_int()` ->
   `render_text_labels()` -> `render_textrect()`, which applies an
   unconditional Y flip, `rectBaseY = 224 - y` (`src/game/print.c:391`,
   confirmed by reading the real file) -- putting them at actual screen
   y≈15/y≈39, near the **top**. Only the camera status icon
   (`render_hud_camera_status()`, `y=205`) uses the unflipped
   `render_hud_tex_lut()` path directly, so it genuinely is near the
   bottom. Documentation-only defect -- the shipped layout was already
   in-bounds and collision-free either way. Fixed the comment in
   `saturn_hud_layout.c` (and the matching `CHANGELOG.md` passage) to
   state plainly that only the camera icon's bottom placement is a literal
   source match; the rest of the bottom-clustering is a grid-coarseness
   choice, not a pixel-derived one. Comment-only -- mechanically confirmed
   zero logic change by diffing the emitted `(col,row,glyph)` cells from
   the pre-fix and post-fix `saturn_hud_layout.c` across five
   representative snapshots, including the pathological worst case:
   byte-identical.
2. **Coordinates were unnamed literals scattered across ~15 call sites.**
   Named the per-group row/column-start constants (`HUD_ROW_POWER_METER`,
   `HUD_ROW_TIMER`, `HUD_ROW_COUNTERS`, `HUD_ROW_CANNON_CAMERA`,
   `HUD_COL_POWER_METER`/`_LIVES`/`_COINS`/`_STARS`/`_TIMER`/`_CANNON`/
   `_CAMERA`) so a future position edit is grep-auditable against every
   other group instead of relying solely on the prose comment. Same
   zero-behavior-change guarantee as above, verified the same way.
3. **Task 5's plan checkboxes were unchecked** (this section) -- checked
   off and annotated to match Task 4's convention.

Not required now but noted for the record and left as `TODO(Task 7):`
breadcrumbs in `saturn_hud_layout.c`: the reviewer's mutation testing found
three more branches completely unexercised by the current 4 tests beyond
the capacity-guard gap already disclosed above -- the cannon reticle path,
the camera mode/C-button switch cases, and the star-count `<100` branch.
Task 7 (below) is this plan's dedicated mutation-test task and will pick
these up.

---

## Task 6: Dirty-cell diff + publish (only rewrite changed cells)

**Files:**
- Create: `src/port/saturn/gfx/saturn_hud_publish.h`
- Create: `src/port/saturn/gfx/saturn_hud_publish.c`
- Test: append to `tools/saturn/saturn_hud_layout_test.c` using a test double for the atlas writer

- [ ] **Step 1: Write the failing test**

```c
/* append to tools/saturn/saturn_hud_layout_test.c */
#include "saturn_hud_publish.h"

static uint32_t g_write_count;

/* Test double for the real target-only atlas writer -- linked instead of
 * saturn_hud_atlas.c for this fixture, so no Yaul headers are needed. */
void
sm64_saturn_hud_atlas_write_cell(uint8_t col, uint8_t row, sm64_saturn_hud_glyph_t glyph)
{
    (void)col; (void)row; (void)glyph;
    g_write_count++;
}

static int
test_publish_only_rewrites_changed_cells(void)
{
    sm64_saturn_hud_publish_state_t state;
    sm64_saturn_hud_publish_init(&state);

    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.flags = 0x0001;
    snapshot.lives = 3;

    g_write_count = 0U;
    sm64_saturn_hud_publish(&state, &snapshot);
    const uint32_t first_pass_writes = g_write_count;
    if (first_pass_writes == 0U) {
        fprintf(stderr, "first publish wrote zero cells\n");
        return 1;
    }

    g_write_count = 0U;
    sm64_saturn_hud_publish(&state, &snapshot);
    if (g_write_count != 0U) {
        fprintf(stderr, "unchanged snapshot triggered %u rewrites\n", g_write_count);
        return 1;
    }

    snapshot.lives = 4;
    g_write_count = 0U;
    sm64_saturn_hud_publish(&state, &snapshot);
    if (g_write_count == 0U || g_write_count >= first_pass_writes) {
        fprintf(stderr, "single-field change rewrote %u cells (expected fewer than %u)\n",
               g_write_count, first_pass_writes);
        return 1;
    }
    return 0;
}

/* add to main(): failures += test_publish_only_rewrites_changed_cells(); */
```

- [ ] **Step 2: Add `saturn_hud_publish.c` to the layout test's compile line, run to verify RED**

Run: `make -f Makefile.saturn.mk verify-saturn-hud-layout`
Expected: FAIL — `saturn_hud_publish.h` does not exist.

- [ ] **Step 3: Implement the diff/publish layer**

```c
/* src/port/saturn/gfx/saturn_hud_publish.h */
#ifndef SM64_SATURN_HUD_PUBLISH_H
#define SM64_SATURN_HUD_PUBLISH_H

#include "saturn_hud.h"
#include "saturn_hud_layout.h"

typedef struct sm64_saturn_hud_publish_state {
    sm64_saturn_hud_cell_t last_cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    uint32_t last_count;
    int primed;
} sm64_saturn_hud_publish_state_t;

void sm64_saturn_hud_publish_init(sm64_saturn_hud_publish_state_t *state);

/* Builds the layout for snapshot, diffs it against the last published
 * layout, and calls sm64_saturn_hud_atlas_write_cell() only for cells whose
 * (col,row,glyph) actually changed -- including cells that need to become
 * SM64_SATURN_HUD_GLYPH_BLANK because they were occupied last publish and
 * are not occupied this publish. */
void sm64_saturn_hud_publish(sm64_saturn_hud_publish_state_t *state,
                             const sm64_saturn_hud_snapshot_t *snapshot);

#endif /* SM64_SATURN_HUD_PUBLISH_H */
```

```c
/* src/port/saturn/gfx/saturn_hud_publish.c */
#include "saturn_hud_publish.h"

void
sm64_saturn_hud_publish_init(sm64_saturn_hud_publish_state_t *state)
{
    if (state == NULL)
        return;
    state->last_count = 0U;
    state->primed = 0;
}

static const sm64_saturn_hud_cell_t *
find_cell(const sm64_saturn_hud_cell_t *cells, uint32_t count, uint8_t col, uint8_t row)
{
    for (uint32_t index = 0U; index < count; index++) {
        if (cells[index].col == col && cells[index].row == row)
            return &cells[index];
    }
    return NULL;
}

void
sm64_saturn_hud_publish(sm64_saturn_hud_publish_state_t *state,
                        const sm64_saturn_hud_snapshot_t *snapshot)
{
    if (state == NULL || snapshot == NULL)
        return;

    sm64_saturn_hud_cell_t next_cells[SM64_SATURN_HUD_LAYOUT_MAX_CELLS];
    const uint32_t next_count = sm64_saturn_hud_layout_build(
        snapshot, next_cells, SM64_SATURN_HUD_LAYOUT_MAX_CELLS);

    for (uint32_t index = 0U; index < state->last_count; index++) {
        const sm64_saturn_hud_cell_t *const prior = &state->last_cells[index];
        const sm64_saturn_hud_cell_t *const still_present =
            find_cell(next_cells, next_count, prior->col, prior->row);
        if (still_present == NULL)
            sm64_saturn_hud_atlas_write_cell(prior->col, prior->row, SM64_SATURN_HUD_GLYPH_BLANK);
    }

    for (uint32_t index = 0U; index < next_count; index++) {
        const sm64_saturn_hud_cell_t *const next = &next_cells[index];
        const sm64_saturn_hud_cell_t *const prior =
            state->primed
                ? find_cell(state->last_cells, state->last_count, next->col, next->row)
                : NULL;
        if (prior == NULL || prior->glyph != next->glyph)
            sm64_saturn_hud_atlas_write_cell(next->col, next->row, next->glyph);
    }

    for (uint32_t index = 0U; index < next_count; index++)
        state->last_cells[index] = next_cells[index];
    state->last_count = next_count;
    state->primed = 1;
}
```

- [ ] **Step 4: Record the SlaveDriver provenance citation**

Add an entry to `docs/saturn/UPSTREAM_CODE_LEDGER.md`/`PROVENANCE.md`: reuse mode **pattern-only**, source `Lobotomy-Software/SlaveDriver-Engine` pinned `a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later), files inspected `SCL_FUNC.C:681-743` (`SCL_PriIntProc`/`SclPriBuffDirty`) and `SOUND.C` (`slotDirty[32]`), pattern adopted: stage changes and flush only what's dirty at a single safe point — applied here to VDP2 PND cell data, a category neither reference engine's dirty-flag code covers directly. Also record the honest negative finding: neither engine implements dirty-cell diffing for VDP2 tile/text content itself (Z-Treme's own HUD counters redraw unconditionally every frame); this diff design is original engineering for this project.

- [ ] **Step 5: Run test to verify it passes**

Run: `make -f Makefile.saturn.mk verify-saturn-hud-layout`
Expected: PASS, including the new dirty-cell test.

- [ ] **Step 6: Commit**

```bash
git add src/port/saturn/gfx/saturn_hud_publish.h src/port/saturn/gfx/saturn_hud_publish.c \
        tools/saturn/saturn_hud_layout_test.c Makefile.saturn.mk \
        docs/saturn/UPSTREAM_CODE_LEDGER.md docs/saturn/PROVENANCE.md
git commit -m "feat(saturn): publish HUD tiles as a bounded dirty-cell diff"
```

---

## Task 7: Mutation test + no-VDP1 structural proof

**Files:**
- Modify: `Makefile.saturn.mk` (add a mutation variant + a static grep gate)

- [ ] **Step 1: Add a mutation-guarded branch to the dirty-cell diff**

In `saturn_hud_publish.c`'s inner writer-selection condition, add a build-time mutation escape hatch:
```c
        const sm64_saturn_hud_cell_t *const prior =
            state->primed
                ? find_cell(state->last_cells, state->last_count, next->col, next->row)
                : NULL;
#ifdef SM64_SATURN_HUD_TEST_MUTATE_DIRTY_GATE
        /* Mutation: always treat every cell as changed. The unchanged-
         * snapshot test in Task 6 must then fail, proving the real gate
         * (comparing prior->glyph) is what keeps redundant writes out. */
        sm64_saturn_hud_atlas_write_cell(next->col, next->row, next->glyph);
#else
        if (prior == NULL || prior->glyph != next->glyph)
            sm64_saturn_hud_atlas_write_cell(next->col, next->row, next->glyph);
#endif
```

- [ ] **Step 2: Add the mutation-build Make target**

```makefile
verify-saturn-hud-layout-mutation: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_HUD_TEST_MUTATE_DIRTY_GATE=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/saturn_hud_layout_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_hud_layout.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_hud_publish.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/saturn-hud-dirty-gate-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/saturn-hud-dirty-gate-mutation$(HOST_EXEEXT)" \
	  --label "HUD dirty-cell gate mutation"
```

- [ ] **Step 3: Run it to verify the mutation is caught**

Run: `make -f Makefile.saturn.mk verify-saturn-hud-layout-mutation`
Expected: PASS (the mutated binary correctly exits nonzero; `expect_failure.py` requires that).

- [ ] **Step 4: Add the "no VDP1 emission" structural proof**

```makefile
verify-saturn-hud-no-vdp1:
	@if grep -nE 'vdp1_cmdt|VDP1_CMDT|sm64_saturn_vdp1_' \
	    src/port/saturn/gfx/saturn_hud_layout.c \
	    src/port/saturn/gfx/saturn_hud_publish.c \
	    src/port/saturn/gfx/saturn_hud_atlas.c; then \
	  echo "HUD source references a VDP1 command-list symbol" >&2; exit 1; \
	fi
```
Note: `saturn_hud.h`/`saturn_render_snapshot.h` are intentionally excluded from this grep — the snapshot legitimately rides inside `sm64_saturn_vdp1_frame_bank_t` (Task 8), which references `sm64_saturn_vdp1_*` symbols by necessity. The gate targets only the HUD's own rendering logic (layout/publish/atlas), which must never construct or touch a VDP1 command.

- [ ] **Step 5: Run it**

Run: `make -f Makefile.saturn.mk verify-saturn-hud-no-vdp1`
Expected: PASS (no matches, exit 0).

- [ ] **Step 6: Commit**

```bash
git add Makefile.saturn.mk src/port/saturn/gfx/saturn_hud_publish.c
git commit -m "test(saturn): mutation-prove the dirty-cell gate and the no-VDP1 boundary"
```

---

## Task 8: Propagate through the frame bank and wire VDP2 composition

**Files:**
- Modify: `src/port/saturn/gfx/saturn_vdp1_frame_bank.h` (add `hud` field + setter, mirroring `camera_snapshot`)
- Modify: `src/port/saturn/gfx/saturn_vdp2_frame.h` (extend `SM64_SATURN_VDP2_FRAME_DISPLAY_MASK` with NBG0)
- Modify: `src/port/saturn/sourceboot/main.c` (copy `hud` into the frame bank alongside the existing `camera_snapshot` copy at `main.c:1135-1143`; carve NBG0 VRAM cycle-pattern slots; init the atlas once; publish at presentation time)

- [ ] **Step 1: Read the existing `camera_snapshot` setter before mirroring it**

Open `src/port/saturn/gfx/saturn_vdp1_frame_bank.h`/`.c` and read `sm64_saturn_vdp1_frame_bank_set_camera_snapshot()`'s full implementation (it was only referenced, not fully quoted, during planning — confirm its exact validation/state-guard behavior before adding a same-shaped `sm64_saturn_vdp1_frame_bank_set_hud_snapshot()`, so the new setter enforces the same "only valid while the bank is in the BUILDING/equivalent state" discipline the existing one does).

- [ ] **Step 2: Add the `hud` field and setter**

In `saturn_vdp1_frame_bank.h`, add `#include "saturn_hud.h"` and a field alongside `camera_snapshot` (`saturn_vdp1_frame_bank.h:45-61`):
```c
typedef struct sm64_saturn_vdp1_frame_bank {
    /* ...existing fields... */
    sm64_saturn_vdp2_camera_snapshot_t camera_snapshot;
    sm64_saturn_hud_snapshot_t hud;
    /* ...existing fields... */
} sm64_saturn_vdp1_frame_bank_t;
```
Add the setter declaration next to `sm64_saturn_vdp1_frame_bank_set_camera_snapshot`'s own declaration, and implement it in the matching `.c` file with the same validation shape found in Step 1 (a plain field copy guarded by the same bank-state check `_set_camera_snapshot` already performs — do not invent a different guard).

- [ ] **Step 3: Copy `hud` into the frame bank at the existing camera-snapshot copy site**

In `main.c`, immediately after the existing block (`main.c:1135-1143`):
```c
        const sm64_saturn_vdp2_camera_snapshot_t camera_snapshot = {
            .yaw = sourceboot_mario_snapshot.camera_yaw,
            .pitch = sourceboot_mario_snapshot.camera_pitch,
            .valid = sourceboot_mario_snapshot.valid,
            .generation = generation,
        };
        render_complete = sm64_saturn_vdp1_frame_bank_set_camera_snapshot(
            sourceboot_active_build_bank, &camera_snapshot);
        if (!render_complete) goto failed;
```
add:
```c
        render_complete = sm64_saturn_vdp1_frame_bank_set_hud_snapshot(
            sourceboot_active_build_bank,
            &sourceboot_active_render_snapshot->hud);
        if (!render_complete) goto failed;
```
This reads the HUD data straight off `sourceboot_active_render_snapshot` — the exact same acquired-and-generation-checked pointer (`main.c:1098-1103`) that `sourceboot_mario_snapshot` was itself just copied from at `main.c:1105`. No new acquire, no new generation check.

- [ ] **Step 4: Extend the VDP2 display mask**

In `saturn_vdp2_frame.h`:
```c
#define SM64_SATURN_VDP2_FRAME_NBG0_MASK (1U << 0)
#define SM64_SATURN_VDP2_FRAME_NBG1_MASK (1U << 1)
#define SM64_SATURN_VDP2_FRAME_NBG3_MASK (1U << 3)
#define SM64_SATURN_VDP2_FRAME_DISPLAY_MASK \
    (SM64_SATURN_VDP2_FRAME_NBG0_MASK | SM64_SATURN_VDP2_FRAME_NBG1_MASK | \
     SM64_SATURN_VDP2_FRAME_NBG3_MASK)
```

- [ ] **Step 5: Carve NBG0 cycle-pattern timeslots in `sourceboot_init_sky_bitmap`**

Reduce NBG1 from all 8 slots to 6, give NBG0 one PNDR + one CHPNDR slot (the minimum a cell plane needs):
```c
    const vdp2_vram_cycp_t cycles = {
        .pt[0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t3 = VDP2_VRAM_CYCP_PNDR_NBG0,
        .pt[1].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
    };
```

- [ ] **Step 6: Initialize the atlas once, after cart load; publish state alongside**

Immediately after the existing `sourceboot_init_sky_bitmap()` call (`main.c:1574` region):
```c
    sourceboot_init_sky_bitmap();
    sm64_saturn_hud_atlas_init();
    sm64_saturn_hud_publish_init(&sourceboot_hud_publish_state);
```
Declare `static sm64_saturn_hud_publish_state_t sourceboot_hud_publish_state;` near the other `static sm64_saturn_*` frame-state globals. Add `#include`s for `saturn_hud.h`, `saturn_hud_atlas.h`, `saturn_hud_layout.h`, `saturn_hud_publish.h`.

- [ ] **Step 7: Publish from `sourceboot_present_generation`, mirroring `sourceboot_vdp2_camera_snapshot(bank)`**

Find the existing `sourceboot_vdp2_camera_snapshot(bank)` helper (it extracts camera data from `bank` for VDP2 composition — read it first). Add an equivalent one-line extraction and publish call in `sourceboot_present_generation` (`main.c:945-993`), after `vdp1_sync()` confirms the bank's plot is complete and before/alongside the existing `sm64_saturn_vdp2_frame_begin`/`_commit` calls:
```c
    sm64_saturn_hud_publish(&sourceboot_hud_publish_state, &bank->hud);
```

- [ ] **Step 8: Update `sourceboot_vdp2_layers_set` priority**

```c
static void sourceboot_vdp2_layers_set(uint32_t display_mask,
                                       uint8_t vdp1_priority,
                                       void *work __unused)
{
    for (uint8_t priority = 0U; priority < 8U; priority++)
        vdp2_sprite_priority_set(priority, vdp1_priority);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG1, 0U);
    vdp2_scrn_priority_set(VDP2_SCRN_NBG0, 7U); /* gameplay HUD: always on top, matching Z-Treme's NBG3 font-plane precedent */
    vdp2_scrn_priority_set(VDP2_SCRN_NBG3, 6U); /* dbgio diagnostics: below the HUD */
    vdp2_scrn_display_set((uint16_t)display_mask);
}
```

- [ ] **Step 9: Add source files to the sourceboot Makefile**

Add `saturn_hud_atlas.c`, `saturn_hud_layout.c`, `saturn_hud_publish.c` (note: `saturn_hud.c` does not exist — the snapshot type is header-only per Task 3) to sourceboot's target C-source list, and add `source-hud-glyphs` as a prerequisite of the main sourceboot build target.

- [ ] **Step 10: Commit**

```bash
git add src/port/saturn/gfx/saturn_vdp1_frame_bank.h src/port/saturn/gfx/saturn_vdp1_frame_bank.c \
        src/port/saturn/gfx/saturn_vdp2_frame.h src/port/saturn/sourceboot/main.c \
        src/port/saturn/sourceboot/Makefile
git commit -m "feat(saturn): wire the gameplay HUD into sourceboot's VDP2 composition"
```

---

## Task 9: Automated headless Ymir verification loop

**Status as of 2026-08-07: BLOCKED — not on anything in this plan.** The capture script and Makefile target below were implemented, and the underlying `stddef.h` compile fixes they surfaced are committed (`aaedf0e5`, `5e6cfa71`, `5d38118f`). The blocker is a real SH-2 link failure in `sourceboot-cart.x`'s memory-budget assertions (HWRAM margin below libyaul's TLSF floor; LWRAM static work arenas/geo-traversal storage overlapping the reserved slave stack), reached only once the build got past an unrelated `INCLUDE`-path defect in the linker script and two files (`source_exception_record.c`, `source_exception_trampolines.sx`) that commit `6dbaea8d` references in `SH_SRCS` but never actually committed.

**Root-cause isolation, run as a controlled experiment (owner-approved) by temporarily stashing the ~390 lines of unrelated uncommitted work already sitting in this worktree, then restoring it byte-for-byte afterward — confirmed via `git status`/`git stash list` before and after, worktree returned to its exact starting state:**

- The HUD (this plan's Tasks 1-8) is **not the cause** — confirmed twice, independently. Its total footprint is ~450 bytes, almost entirely in VDP2 character-pattern VRAM, which `sourceboot-cart.x` doesn't even budget.
- The uncommitted work already in this worktree (an in-progress HWRAM-relief refactor) is **not the cause either — it measurably helps.** With it removed, the exact same four assertions fired *worse*: HWRAM shortfall grew from 3,128 to 5,880 bytes short; LWRAM overshoot past the reserved slave-stack boundary grew from 784 to 2,176 bytes over. Numbers pulled directly from both builds' linker `.map` files, not estimated.
- **The real cause: the committed-only state — this plan's Tasks 1-8 + commit `39008658` (an unrelated "generic actor system" arena, +65,536 B, landed earlier the same day) + commits `6dbaea8d`/`a48e5aac` (the concurrent "iterative geo walk" work, +4,096 B, see `docs/superpowers/plans/2026-08-06-saturn-iterative-geo-walk.md`) — does not fit the Saturn's memory budget on its own.** This is a pre-existing condition on this branch, invisible until this task attempted the first full accepted-rollback link since those commits landed. Reverting or deferring the uncommitted HWRAM-relief work would not fix it and would make both margins worse.

**What this means for whoever resumes this task**: do not touch the HUD source (Tasks 1-8) — it's clean. Do not wait on or blame the uncommitted worktree refactor — it's net-positive. The actual fix requires either shrinking `39008658`'s or `6dbaea8d`/`a48e5aac`'s footprint, or finishing/extending the HWRAM-relief refactor further than its current uncommitted state — a memory-budget design decision for whoever owns those two efforts, not something to resolve inside this HUD plan. Once a committed-only build links, resume at Step 1 below exactly as written; nothing about the HUD's own implementation needs to change.

**Files:**
- Create: `tools/saturn/capture_sourceboot_hud_state.py` (new — reuses the `YmirClient` pattern from `capture_route_views.py`, not a rewrite)
- Modify: `Makefile.saturn.mk` (a build+capture+assert target — none exists today for any Saturn subsystem, this is genuinely new composition, not a rename of an existing one)

**Existing tooling this task builds on, verified by direct inspection (do not re-derive):**
- `YmirClient` class, `tools/saturn/capture_route_views.py:77-194` — `Popen` + background reader threads + blocking `.call(method, params)` per JSON-RPC id. Import and reuse this class directly (`from capture_route_views import YmirClient`), do not reimplement the transport.
- Ymir headless debug-service RPC methods available (confirmed from `ymir-agent/apps/ymir-headless/src/debug_service.cpp:32-40`): `exec.run_for` (capped 3600 frames/call — chunk longer waits), `mem.peek` (capped 64 KiB/call, requires paused), `mem.poke`, `regs.read`, `input.pulse`, `video.capture`, `instance.status`, `instance.shutdown`. There is no dedicated VDP2/VRAM method — address VDP2 VRAM with the same `VDP2_VRAM_ADDR(bank, offset)` macro the target C code uses (already used this way by `src/port/saturn/hwtest/main.c:416`); `mem.peek` reaches it through the normal SH-2 bus-decode path.
- The 4 MiB/32-Mbit DRAM cart + BIOS combination the owner referred to as "existing profiles that work": the physical BIOS lives at `.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin` (confirmed on disk). For headless automation, do **not** try to point at the GUI `.ymir-profile\Ymir.toml` — that profile is consumed only by the desktop GUI binary via `tools/saturn/launch_ymir_desktop.py`. For headless scripts, pass the same BIOS file explicitly via `--ipl` plus the `--dram-cart` flag, exactly as `capture_hwtest.py` already does (confirmed working pattern, `docs/saturn/HWTEST.md:92-98`).

- [ ] **Step 1: Write the capture/assert script**

```python
#!/usr/bin/env python3
# tools/saturn/capture_sourceboot_hud_state.py
"""Boot the built sourceboot image headless, run to a fixed VBlank count,
peek the VDP2 HUD pattern-name table, and assert a specific tile-grid cell
holds the expected glyph index. This is the automated in-emulator check
that runs BEFORE the owner's manual desktop-Ymir acceptance -- it proves
the atlas/tilemap actually reached VRAM correctly without a human watching
every intermediate build.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from capture_route_views import YmirClient

HUD_PND_BASE = 0x25E08000  # VDP2_VRAM_ADDR(1, 0x08000), A-bus-mapped
HUD_TILE_COLS = 32


def pnd_cell_address(col: int, row: int) -> int:
    return HUD_PND_BASE + (row * HUD_TILE_COLS + col) * 2


def read_pnd_cell(client: YmirClient, col: int, row: int) -> int:
    response = client.call("mem.peek", {
        "target": "sh2.master",
        "address": pnd_cell_address(col, row),
        "count": 2,
    })
    raw = bytes(response["result"]["bytes"])
    return int.from_bytes(raw, "big")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ymir", required=True, type=Path)
    parser.add_argument("--ipl", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--startup-frames", type=int, default=1800)
    parser.add_argument("--expect-col", type=int, default=1)
    parser.add_argument("--expect-row", type=int, default=26)
    parser.add_argument("--expect-character-number", type=int, required=True,
                        help="pattern-name character number expected once "
                             "the lives readout (Mario head glyph) is live")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    client = YmirClient(str(args.ymir), str(args.ipl), str(args.game),
                        dram_cart=True)
    try:
        client.start()
        remaining = args.startup_frames
        while remaining > 0:
            chunk = min(remaining, 3600)
            client.call("exec.run_for", {"frames": chunk})
            remaining -= chunk

        character_number = read_pnd_cell(client, args.expect_col, args.expect_row) & 0x0FFF
        screenshot = client.call("video.capture", {})

        report = {
            "startup_frames": args.startup_frames,
            "expect_col": args.expect_col,
            "expect_row": args.expect_row,
            "observed_character_number": character_number,
            "expected_character_number": args.expect_character_number,
            "pass": character_number == args.expect_character_number,
            "screenshot_hash": screenshot["result"].get("hash"),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

        if not report["pass"]:
            print(f"HUD PND mismatch at ({args.expect_col},{args.expect_row}): "
                 f"expected character {args.expect_character_number}, "
                 f"observed {character_number}", file=sys.stderr)
            return 1
        return 0
    finally:
        client.stop()


if __name__ == "__main__":
    raise SystemExit(main())
```

**Note for the implementer:** `YmirClient.__init__`'s exact constructor signature (positional vs. keyword args, whether it accepts a `dram_cart` keyword directly or expects it folded into an args list) was confirmed to build the command line `[executable, "--ipl", str(ipl), "--game", str(game), "--dram-cart"]` (`capture_route_views.py:93-101`) but its exact Python-level parameter names were not fully quoted during planning — read the real `__init__` signature before writing this script and adjust the instantiation call to match exactly; do not guess parameter names. Similarly, confirm the exact `mem.peek` response JSON shape (the field name holding the returned bytes) against a real response before trusting `response["result"]["bytes"]" verbatim — other scripts in `tools/saturn/` that already call `mem.peek` (e.g. `capture_hwtest.py:592`, `capture_camera_idle.py:80`) show the real parsing code to copy.

- [ ] **Step 2: Add the composed build+capture+assert Make target**

```makefile
verify-sourceboot-hud-target: source-hud-glyphs
	$(MAKE) -f src/port/saturn/sourceboot/Makefile -j1 <the accepted e2-bob-demo flag-suffixed target>
	"$(SATURN_TOOLS_PYTHON)" tools/saturn/capture_sourceboot_hud_state.py \
	  --ymir "$(YMIR_HEADLESS_EXE)" \
	  --ipl ".ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
	  --game "<path to the built .cue from the target above>" \
	  --expect-character-number <SM64_SATURN_HUD_GLYPH_MARIO_HEAD's atlas index> \
	  --output docs/saturn/evidence/reports/saturn-hud-automated-capture-2026-08-06.json
```
Fill in the exact target name and `.cue` path from Task 8's actual build output (the plan cannot know these until Task 8's build identity exists), and `YMIR_HEADLESS_EXE` from whatever variable/path the other `capture_*.py` Makefile call sites already use (grep `Makefile.saturn.mk` for how `capture_hwtest.py` is invoked, if it is; if no target invokes it today, use the same headless executable path `capture_hwtest.py`'s own doc example references).

- [ ] **Step 3: Run it**

Run: `make -f Makefile.saturn.mk verify-sourceboot-hud-target`
Expected: exits 0; `docs/saturn/evidence/reports/saturn-hud-automated-capture-2026-08-06.json` shows `"pass": true`. If it fails, iterate on Tasks 4/5/6/8 using this same command — that is the point of this task: fast, unattended, evidence-producing iteration before asking the owner to look at anything.

- [ ] **Step 4: Commit**

```bash
git add tools/saturn/capture_sourceboot_hud_state.py Makefile.saturn.mk
git commit -m "test(saturn): add automated headless capture for the VDP2 HUD"
```

---

## Task 10: Review, manual acceptance, doc reconciliation

**Status as of 2026-08-07: BLOCKED on Task 9** (see that task's status note) — this task needs a real, linked target build and a matching Ymir capture to run its manual acceptance and doc-reconciliation steps against. Nothing in this task's own scope is implemented or blocked on its own merits; it simply cannot start until Task 9 produces a real passing build.

**Files:** none new — process only, matching the handoff's Steps 7-9.

- [ ] **Step 1: Run every host + automated gate together**

```bash
make -f Makefile.saturn.mk verify-saturn-hud-snapshot
make -f Makefile.saturn.mk verify-saturn-hud-layout
make -f Makefile.saturn.mk verify-saturn-hud-layout-mutation
make -f Makefile.saturn.mk verify-saturn-hud-no-vdp1
make -f Makefile.saturn.mk verify-sourceboot-hud-target
make -f Makefile.saturn.mk verify-actor-meshlets  # spot-check: unrelated existing gates still pass
```
Expected: all exit 0.

- [ ] **Step 2: Request independent specification-compliance review**

Fresh reviewer, no attachment to this implementation, against the Task 23A contract checklist in `docs/saturn/HANDOFF_2026-08-06-task-23a-hud.md`: every `gHudDisplay` field/flag reaches the layout builder unchanged in meaning; the two `hud.c` accessors stay pure reads with zero behavior change; the HUD snapshot inherits (not duplicates) the render-snapshot/frame-bank generation coherence already proven at `main.c:1101-1103`; VDP1 is genuinely untouched (Task 7's grep gate plus a manual read of the three HUD `.c` files).

- [ ] **Step 3: Request independent code-quality review**

Second reviewer: SH-2-target-safety of `saturn_hud_atlas.c`'s direct VRAM writes (`CPU_CACHE_THROUGH` usage matches `sourceboot_init_sky_bitmap`'s existing pattern), the `sm64_saturn_vdp1_frame_bank_set_hud_snapshot` guard matches `_set_camera_snapshot`'s (Task 8 Step 1) exactly, tile-grid bounds-check correctness, and whether `push_clamped_int`'s digit-truncation for pathological values (Task 5's `9999`-lives test) is an acceptable display-only clamp (it is — `coins` is already source-clamped to 999 at `level_update.c:931-932`; only the *display width* is bounded here, nothing feeds back into gameplay).

- [ ] **Step 4: One serialized `-j1` target candidate build (if not already current from Task 9)**

Confirm the exact ELF/CUE/ISO SHA-256 identities used by Task 9's passing automated capture are the ones carried into the evidence report — do not silently rebuild without re-running Task 9 against the new identity.

- [ ] **Step 5: Owner's manual profile-managed Ymir acceptance**

Launch via `tools/saturn/launch_ymir_desktop.py` (the exact CUE from Step 4, `.ymir-profile` GUI profile, 32-Mbit DRAM cart already configured in that profile's `Ymir.toml`). Visually confirm: lives/coins/stars/timer digits render as real SM64 glyphs, camera-status icon appears/changes with camera mode, power meter appears/animates on taking damage, cannon reticle appears inside the cannon. Do not claim this task complete from Task 9's automated evidence alone — that check only proves one cell's pattern-name data is correct, not that the full visual composition (priority, color, VRAM bank collision, cycle-pattern starvation) looks right.

- [ ] **Step 6: Reconcile documentation**

- `docs/saturn/evidence/reports/saturn-hud-target-2026-08-06.md` — new file: ELF/CUE/ISO SHA-256, both review verdicts, Task 9's automated result, the manual visual acceptance result.
- `docs/saturn/UPSTREAM_CODE_LEDGER.md`, `docs/saturn/PROVENANCE.md` — confirm the Task 4 and Task 6 provenance entries landed (they're easy to forget since they're side-effects of earlier tasks, not this one).
- `STATE.md`, `ROADMAP.md`, `CHANGELOG.md` — per the repository's standing per-commit documentation rule; mark Task 23A per its actual outcome, do not write "complete" if any acceptance-checklist item in the handoff is unchecked.
- `.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md` — mark this task's row.

- [ ] **Step 7: Final commit**

```bash
git add docs/saturn/evidence/reports/saturn-hud-target-2026-08-06.md STATE.md ROADMAP.md \
        CHANGELOG.md .superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md
git commit -m "docs(saturn): close Task 23A VDP2 HUD with target/Ymir evidence"
```

---

## Self-review notes (per writing-plans skill)

**Spec coverage**: every Task 23A "must" bullet maps to a task — source semantics (Tasks 3, 5, 8), fixed-width pointer-free snapshot (Task 3, explicitly matching `saturn_render_snapshot.h`'s own existing documented discipline), generation-tuple coherence (inherited for free from the existing render-snapshot/frame-bank pipeline, Task 3 design decision 1 + Task 8 Step 3), bounded NBG tilemap + resident atlas (Task 4), dirty-cell-only updates (Task 6, mutation-proven in Task 7), VDP2-boundary-only writes (Task 8), dbgio-stays-diagnostic (untouched — `sourceboot_vdp2_hud_write` is never modified). Every "must not" bullet has a guard: no VDP1 (Task 7 Step 4's grep gate), no VBlank-callback globals reads (capture happens only inside `sourceboot_capture_render_snapshot`, the existing post-tick call site — never a VBlank handler), cadence/input/camera/actor/audio/VDP1/fence ownership untouched (no task modifies any of those subsystems' own logic, only reads already-computed output), no second HUD semantic model (the layout builder consumes only fields copied verbatim from `gHudDisplay`/`hud.c`'s own statics, never re-derives them).

**Gap acknowledged, not silently dropped**: pause/dialog/course-complete HUD layouts (handoff acceptance-checklist item 5) are not given a dedicated test — `render_hud()` itself doesn't distinguish those states from normal gameplay (same `HUD_DISPLAY_FLAG_*` bits drive all of them), so Tasks 3-8 already cover them structurally, but no task explicitly captures a dialog-state screenshot. Flag this to the Task 10 Step 2 reviewer rather than silently claiming full coverage.

**Type consistency check**: `sm64_saturn_hud_glyph_t` (Task 4) is the single source of truth for glyph identity, used unchanged by `saturn_hud_layout.c` (Task 5) and `saturn_hud_publish.c` (Task 6) — no renamed duplicate. `sm64_saturn_hud_snapshot_t` (Task 3) has exactly one definition, in `saturn_hud.h`, included by both `saturn_render_snapshot.h` and (transitively, via Task 8) `saturn_vdp1_frame_bank.h` — the earlier draft's duplicate `sm64_saturn_vdp2_generation_state_t`-shaped type has been removed entirely, eliminating the drift risk the first draft's self-review had flagged as a known wart.

**Provenance discipline**: this revision adds two citations (`UPSTREAM_CODE_LEDGER.md`/`PROVENANCE.md`, Task 4 Step 3 and Task 6 Step 4) that the first draft omitted entirely, plus an explicit contrast note that SlaveDriver's HUD system is VDP1-sprite-based and does not validate the VDP2-NBG choice — both per the owner's direct instruction to reference the two reference engines and per this project's own standing provenance-tracking convention.
