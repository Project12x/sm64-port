# VDP2 CLUT Texture-Depth Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the two remaining VDP2 RGB1555 surfaces in this port (BOB's NBG1 sky bitmap and the NBG0 HUD glyph atlas) to 4bpp CLUT (`VDP2_SCRN_CCC_PALETTE_16`) storage, recovering VRAM/cart footprint with no visible fidelity loss on either surface (both are flat/unlit -- no Gouraud interaction, unlike VDP1's Mario/actor geometry, which stays RGB1555 on purpose and is untouched by this plan).

**Architecture:** One small new shared module (`saturn_vdp2_palette`) owns CRAM (color RAM) uploads for both surfaces. Each surface's own init function gains a palette upload call plus a `.ccc`/`.palette_base` format change; pixel data itself is rebaked host-side by extending the existing Python baking tools with the same quantize/pack primitives `bake_castle_uv.py` already uses for VDP1 CLUT16 textures (this project's own established, working pattern for the *same* algorithm, applied to a VDP2 target for the first time).

**Tech Stack:** C (Yaul SDK: `vdp2/cram.h`, `vdp2/scrn_bitmap.h`, `vdp2/scrn_cell.h`), Python (host-side asset baking, this project's `tools/saturn/*.py` conventions), GNU Make (`Makefile.saturn.mk`, `src/port/saturn/sourceboot/Makefile`).

**Why this is safe to do now:** VDP1 CLUT16 is already load-bearing in this codebase (97.8% of BOB terrain triangles render through `saturn_ir_texture.c`'s `bind_clut16`) -- this plan is not introducing an unproven technique, it's applying an already-proven-in-this-repo technique (median-cut quantization -> 4bpp packed indices -> palette lookup) to two VDP2 surfaces that have never had palette code before. Zero VDP2 CRAM/palette code exists anywhere in `src/` today (confirmed via repo-wide grep) -- this is genuinely new infrastructure, not a tweak to something fragile.

---

## Background reading (do this before Task 1, all real citations, no placeholders)

- `third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/cram.h` -- the real CRAM API: `vdp2_cram_mode_get/set(mode)`, `vdp2_cram_offset_set(scroll_screen, cram)`, and `VDP2_CRAM_ADDR(word_index)` (`0x25F00000UL + (word_index << 1)`). CRAM access is word/longword only, never byte (the header's own comment says so).
- `third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_cram.c` -- the real (if terse) implementation: `__vdp2_cram_init()` zeroes all of CRAM via `cpu_dmac_memset`, sets mode 1 (RGB555, 2048 colors) at boot. `vdp2_cram_offset_set()` writes `CRAOFA`/`CRAOFB` shadow registers -- this sets which CRAM *bank* a scroll screen's palette reads start from; NBG0's field is the low nibble, NBG1's is bits 4-7 of `craofa`.
- `third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_scrn_bitmap.c`, function `vdp2_scrn_bitmap_ccc_set` -- shows `.palette_base` (a *different*, smaller field than the CRAM offset above -- a "supplementary palette number", shifted `>>10` or `>>9` of the CRAM mode) feeds a per-surface register. For this plan's scope (one palette bank, bank 0), both `.palette_base = 0` and the default CRAM offset (0, i.e. do not call `vdp2_cram_offset_set` at all) are sufficient -- **the implementer must verify this against the real current header/implementation before writing code**, since getting a CRAM bank/offset register wrong is a real hardware-timing/correctness class of bug this project treats with extra review rigor (see project memory on subagent review rigor).
- `third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/scrn_shared.h` -- `VDP2_SCRN_CCC_PALETTE_16 = 0`, `..._PALETTE_256 = 1`, `..._RGB_32768 = 3` (current value both surfaces use).
- `tools/saturn/bake_castle_uv.py`, functions `quantize_clut16` (median-cut quantizer producing a 16-entry RGB1555 palette: index 0 always transparent, indices 1-15 populated by histogram-weighted box splitting) and `pack_clut16` (packs two 4-bit indices per byte, high nibble first). These are the *exact* primitives to reuse -- do not reimplement quantization.
- `tools/saturn/bake_bob_sky.py` -- current sky baker: reads a PNG, replicates edges into a fixed 512x256 canvas, emits raw big-endian RGB1555 words. `bake()` is already a pure, host-testable function separate from `main()`'s CLI/file-I/O -- follow the same shape when adding the CLUT path.
- `src/port/saturn/sourceboot/main.c`, function `sourceboot_init_sky_bitmap` (around line 895) -- current RGB1555 init: raw VRAM word copy, `vdp2_scrn_bitmap_format_t` with `.ccc = VDP2_SCRN_CCC_RGB_32768`, and a `vdp2_vram_cycp_t` VRAM-cycle-pattern config whose own comment states "CHPNDR slot count scales with color depth" -- the implementer must re-derive the correct slot count for `PALETTE_16` from real Yaul header documentation/examples before changing `.ccc`, not assume the existing RGB_32768 slot count still applies.
- `src/port/saturn/gfx/saturn_hud_atlas.c`, function `sm64_saturn_hud_atlas_init` (around line 223) -- current HUD atlas init: `vdp2_scrn_cell_format_t` with `.ccc = VDP2_SCRN_CCC_RGB_32768`, `.palette_base = 0U` (already present, currently inert).
- `src/port/saturn/sourceboot/Makefile` lines 249-252 (`SOURCEBOOT_BOB_SKY`/`_MANIFEST` vars), 738-746 (`compile-bob-sky` invocation + `source-bob-sky` phony), 827-837 (`SOURCEBOOT_BOB_SKY_ASM` -- the `.incbin` assembly stub that gets the raw baked bytes onto the cart) -- the exact pattern Task 3 below extends for the new palette data.
- `Makefile.saturn.mk` lines 75-77 (`BOB_SKY_SOURCE`/`_OUTPUT`/`_MANIFEST` vars) and 1617-1621 (`compile-bob-sky` target, the real CLI invocation of `bake_bob_sky.py`).
- **Cross-reference, low risk but worth knowing:** `Makefile.saturn.mk:90`, `SCENE_PACKAGE_SKY_BACKGROUND ?= $(BOB_SKY_OUTPUT)` -- the (currently unwired, per separate research this session) scene-package system references the sky bitmap's output path. Changing its byte format doesn't break anything today (nothing consumes that reference yet), but note it in the commit message so whoever eventually wires up scene residency (plan `2026-08-05-saturn-full-game-completeness-parallel-optimization.md` Task 22) knows the format changed.

---

### Task 1: Add a CLUT16 baking mode to `bake_bob_sky.py`

**Files:**
- Modify: `tools/saturn/bake_bob_sky.py`
- Create: `tools/saturn/test_bake_bob_sky.py` (no test file exists for this tool today -- confirmed via glob)

- [ ] **Step 1: Write the failing tests**

```python
# tools/saturn/test_bake_bob_sky.py
import struct
import unittest

from bake_bob_sky import bake, bake_clut16


def _solid_rgba8_png(width: int, height: int, rgba: tuple[int, int, int, int]) -> bytes:
    import zlib
    row = bytes(rgba) * width
    raw = b"".join(b"\x00" + row for _ in range(height))
    compressed = zlib.compress(raw, 9)

    def chunk(kind: bytes, payload: bytes) -> bytes:
        data = kind + payload
        return struct.pack(">I", len(payload)) + data + struct.pack(">I", zlib.crc32(data))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")


class TestBakeClut16(unittest.TestCase):
    def test_palette_has_sixteen_entries_index_zero_transparent(self):
        from pathlib import Path
        import tempfile
        png = _solid_rgba8_png(4, 4, (255, 0, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            indices, palette, manifest = bake_clut16(path, output_width=8, output_height=8)
            self.assertEqual(len(palette), 16)
            self.assertEqual(palette[0], 0x0000)
            self.assertEqual(manifest["format"], "CLUT16")
            self.assertEqual(manifest["palette_entries"], 16)

    def test_packed_indices_are_nibble_packed_two_per_byte(self):
        from pathlib import Path
        import tempfile
        png = _solid_rgba8_png(4, 4, (0, 255, 0, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            indices, palette, manifest = bake_clut16(path, output_width=8, output_height=8)
            self.assertEqual(len(indices), (8 * 8) // 2)
            self.assertEqual(manifest["bytes"], (8 * 8) // 2)

    def test_rgb1555_path_unchanged_by_new_option(self):
        from pathlib import Path
        import tempfile
        png = _solid_rgba8_png(4, 4, (10, 20, 30, 255))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sky.png"
            path.write_bytes(png)
            pixels, manifest = bake(path, output_width=8, output_height=8)
            self.assertEqual(manifest["format"], "RGB1555")
            self.assertEqual(len(pixels), 8 * 8 * 2)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools/saturn && python -m unittest test_bake_bob_sky -v`
Expected: FAIL with `ImportError: cannot import name 'bake_clut16'`

- [ ] **Step 3: Implement `bake_clut16` in `bake_bob_sky.py`**

Add near the top of the file, alongside the existing `bake()`:

```python
import sys
from pathlib import Path as _Path
sys.path.insert(0, str(_Path(__file__).resolve().parent))
from bake_castle_uv import pack_clut16, quantize_clut16  # noqa: E402


def bake_clut16(source: Path, output_width: int = 512, output_height: int = 256) -> tuple[list[int], list[int], dict[str, object]]:
    """Same edge-replicated canvas as bake(), quantized to a 16-color CLUT.

    Returns (packed_nibble_indices, palette_16_rgb1555_words, manifest).
    Reuses bake()'s exact PNG decode + edge-replication so the two paths only
    diverge at the final per-pixel quantization step -- keeps them impossible
    to accidentally desync on canvas geometry.
    """
    width, height, rows = _png_rows(source)
    if width > output_width or height > output_height:
        raise ValueError("sky source exceeds VDP2 bitmap dimensions")
    x_offset = (output_width - width) // 2
    y_offset = (output_height - height) // 2
    raw_rgb1555: list[int] = []
    for y in range(output_height):
        source_y = min(max(y - y_offset, 0), height - 1)
        row = rows[source_y]
        for x in range(output_width):
            source_x = min(max(x - x_offset, 0), width - 1)
            r, g, b = row[source_x * 4:source_x * 4 + 3]
            value = 0x8000 | ((r * 31 // 255) << 10) | ((g * 31 // 255) << 5) | (b * 31 // 255)
            raw_rgb1555.append(value)
    palette, index_by_color = quantize_clut16(raw_rgb1555)
    indices = [index_by_color.get(value, 0) if (value & 0x8000) else 0 for value in raw_rgb1555]
    packed = pack_clut16(indices)
    manifest = {
        "schema": "sm64-saturn-vdp2-sky",
        "source": source.as_posix(),
        "source_dimensions": [width, height],
        "bitmap_dimensions": [output_width, output_height],
        "format": "CLUT16",
        "palette_entries": len(palette),
        "bytes": len(packed),
        "sha256": __import__("hashlib").sha256(bytes(packed)).hexdigest(),
        "edge_replication": True,
    }
    return packed, palette, manifest
```

Read `quantize_clut16`'s real current return type in `bake_castle_uv.py` before wiring this in (the plan's citation above is a best-effort read; confirm the exact tuple shape, e.g. whether it returns a dict or list for the index mapping, and adjust `index_by_color.get(...)` accordingly).

- [ ] **Step 4: Wire `--texture-format` into `main()`**

```python
def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--texture-format", choices=["rgb1555", "clut16"], default="rgb1555")
    parser.add_argument("--palette-output", type=Path, help="required when --texture-format=clut16")
    args = parser.parse_args(argv)
    if args.texture_format == "clut16":
        if args.palette_output is None:
            parser.error("--palette-output is required with --texture-format clut16")
        packed, palette, manifest = bake_clut16(args.input)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(bytes(packed))
        args.palette_output.parent.mkdir(parents=True, exist_ok=True)
        args.palette_output.write_bytes(b"".join(struct.pack(">H", c) for c in palette))
    else:
        pixels, manifest = bake(args.input)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(pixels)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd tools/saturn && python -m unittest test_bake_bob_sky -v`
Expected: PASS, 3/3

- [ ] **Step 6: Commit**

```bash
git add tools/saturn/bake_bob_sky.py tools/saturn/test_bake_bob_sky.py
git commit -m "feat(saturn): add CLUT16 baking mode to the BOB sky tool"
```

---

### Task 2: New `saturn_vdp2_palette` CRAM upload helper

**Files:**
- Create: `src/port/saturn/gfx/saturn_vdp2_palette.h`
- Create: `src/port/saturn/gfx/saturn_vdp2_palette.c`
- Test: `tools/saturn/saturn_vdp2_palette_test.c` (host-compilable address-math test, matching this project's existing pattern of small standalone host C test files for Saturn gfx code -- see `tools/saturn/saturn_hud_layout_test.c` for the house style)

- [ ] **Step 1: Write the failing host test**

```c
/* tools/saturn/saturn_vdp2_palette_test.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "saturn_vdp2_palette.h"

static uint16_t fake_cram[SM64_SATURN_VDP2_PALETTE_CRAM_TEST_WORDS];

int main(void)
{
    const uint16_t colors[16] = {
        0x0000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007,
        0x8008, 0x8009, 0x800A, 0x800B, 0x800C, 0x800D, 0x800E, 0x800F,
    };
    memset(fake_cram, 0xFF, sizeof(fake_cram));
    sm64_saturn_vdp2_palette_upload_to(fake_cram, 0U, colors, 16U);
    for (unsigned i = 0; i < 16U; i++)
        assert(fake_cram[i] == colors[i]);
    assert(fake_cram[16] == 0xFFFFU); /* untouched past the written range */

    memset(fake_cram, 0xFF, sizeof(fake_cram));
    sm64_saturn_vdp2_palette_upload_to(fake_cram, 16U, colors, 16U);
    for (unsigned i = 0; i < 16U; i++)
        assert(fake_cram[16U + i] == colors[i]);
    assert(fake_cram[0] == 0xFFFFU); /* start-of-bank offset respected */

    printf("saturn_vdp2_palette_test: OK\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run (from repo root, matching this project's host-native compile convention used by `saturn_hud_layout_test.c`):
```bash
gcc -std=c11 -Wall -Wextra -Werror -I src/port/saturn/gfx -DSM64_SATURN_VDP2_PALETTE_HOST_TEST tools/saturn/saturn_vdp2_palette_test.c src/port/saturn/gfx/saturn_vdp2_palette.c -o /tmp/palette_test
```
Expected: FAIL (files don't exist yet)

- [ ] **Step 3: Implement the header**

```c
/* src/port/saturn/gfx/saturn_vdp2_palette.h */
#ifndef SM64_SATURN_VDP2_PALETTE_H
#define SM64_SATURN_VDP2_PALETTE_H

#include <stdint.h>

/* Small enough for host address-math testing without pulling in real VDP2
 * headers; the real target path writes through VDP2_CRAM_ADDR() directly
 * (see saturn_vdp2_palette.c), this constant only bounds the host fixture. */
#define SM64_SATURN_VDP2_PALETTE_CRAM_TEST_WORDS 64U

/* Writes `count` RGB1555 words into a CRAM-shaped uint16_t array starting at
 * `bank_offset` words in. On real target hardware, `cram` is always the
 * fixed VDP2_CRAM_ADDR(0)-based pointer; the indirection exists purely so
 * this address math is host-testable without touching real VDP2 registers.
 * `count` is expected to be 16 for every caller in this codebase today (one
 * CLUT16 bank) -- deliberately not hardcoded to 16 so a future 256-color
 * (VDP2_SCRN_CCC_PALETTE_256) surface can reuse this same helper. */
void sm64_saturn_vdp2_palette_upload_to(uint16_t *cram, uint16_t bank_offset,
                                        const uint16_t *colors, uint16_t count);

/* Uploads to the real VDP2_CRAM_ADDR(0)-based hardware address. Not
 * host-testable (writes through a fixed physical address); the pure
 * addressing logic above is what Task 2's test actually exercises. */
void sm64_saturn_vdp2_palette_upload(uint16_t bank_offset,
                                     const uint16_t *colors, uint16_t count);

#endif /* SM64_SATURN_VDP2_PALETTE_H */
```

- [ ] **Step 4: Implement the source file**

```c
/* src/port/saturn/gfx/saturn_vdp2_palette.c */
#include "saturn_vdp2_palette.h"

void
sm64_saturn_vdp2_palette_upload_to(uint16_t *cram, uint16_t bank_offset,
                                   const uint16_t *colors, uint16_t count)
{
    for (uint16_t i = 0U; i < count; i++)
        cram[bank_offset + i] = colors[i];
}

#ifndef SM64_SATURN_VDP2_PALETTE_HOST_TEST
#include <vdp2/cram.h>

void
sm64_saturn_vdp2_palette_upload(uint16_t bank_offset,
                                const uint16_t *colors, uint16_t count)
{
    volatile uint16_t *const cram = (volatile uint16_t *)VDP2_CRAM_ADDR(0);
    for (uint16_t i = 0U; i < count; i++)
        cram[bank_offset + i] = colors[i];
}
#endif
```

Before finalizing this step, verify `VDP2_CRAM_ADDR`'s exact signature and whether it needs a `CPU_CACHE_THROUGH` OR-in (matching the pattern `saturn_hud_atlas.c` already uses for VRAM writes, e.g. `(volatile uint16_t *)(CPU_CACHE_THROUGH | (HUD_CPD_BASE + ...))`) against the real current `third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/cram.h` and any Yaul CRAM examples -- the plan's background-reading section above is a best-effort read, not a substitute for checking the exact current header at implementation time.

- [ ] **Step 5: Run test to verify it passes**

Run the same command as Step 2.
Expected: PASS, prints `saturn_vdp2_palette_test: OK`

- [ ] **Step 6: Commit**

```bash
git add src/port/saturn/gfx/saturn_vdp2_palette.h src/port/saturn/gfx/saturn_vdp2_palette.c tools/saturn/saturn_vdp2_palette_test.c
git commit -m "feat(saturn): add VDP2 CRAM palette upload helper"
```

---

### Task 3: Convert the BOB sky bitmap to CLUT16

**Files:**
- Modify: `src/port/saturn/sourceboot/main.c` (`sourceboot_init_sky_bitmap`, and the `SOURCEBOOT_SKY_BITMAP_*`/`SOURCEBOOT_VDP2_VRAM_BYTES` macros just above it)
- Modify: `src/port/saturn/sourceboot/Makefile` (sky bake invocation + a new palette `.incbin` stub, mirroring `SOURCEBOOT_BOB_SKY_ASM`)
- Modify: `Makefile.saturn.mk` (`compile-bob-sky` target, `BOB_SKY_*` vars)
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add palette output variables and switch the bake invocation, `Makefile.saturn.mk`**

```makefile
BOB_SKY_SOURCE ?= textures/skyboxes/water.png
BOB_SKY_OUTPUT ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated/bob_sky_clut16.bin
BOB_SKY_PALETTE ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated/bob_sky_palette.bin
BOB_SKY_MANIFEST ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated/bob_sky_manifest.json
```

```makefile
compile-bob-sky: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/bake_bob_sky.py" \
	  --input "$(BOB_SKY_SOURCE)" \
	  --output "$(BOB_SKY_OUTPUT)" \
	  --palette-output "$(BOB_SKY_PALETTE)" \
	  --texture-format clut16 \
	  --manifest "$(BOB_SKY_MANIFEST)"
```

Grep `Makefile.saturn.mk` for every other reference to `BOB_SKY_OUTPUT` before finalizing this step (`SCENE_PACKAGE_SKY_BACKGROUND` at line 90 is already known; confirm there are no others) and confirm none of them assume the old RGB1555 byte size/format.

- [ ] **Step 2: Update the sourceboot Makefile's asm-stub generation to also emit the palette**

Rename the existing `SOURCEBOOT_BOB_SKY_ASM` variable's output var (`SOURCEBOOT_BOB_SKY` -> keep, points at the new CLUT16-format output now) and add a second `.incbin` block for the palette, mirroring lines 251-252 and 827-837 exactly:

```makefile
SOURCEBOOT_BOB_SKY := $(SOURCEBOOT_GENERATED)/bob_sky_clut16.bin
SOURCEBOOT_BOB_SKY_PALETTE := $(SOURCEBOOT_GENERATED)/bob_sky_palette.bin
SOURCEBOOT_BOB_SKY_MANIFEST := $(SOURCEBOOT_GENERATED)/bob_sky_manifest.json
```

```makefile
$(SOURCEBOOT_BOB_SKY) $(SOURCEBOOT_BOB_SKY_PALETTE) $(SOURCEBOOT_BOB_SKY_MANIFEST) &: $(ROOT)/tools/saturn/bake_bob_sky.py $(ROOT)/textures/skyboxes/water.png
	@mkdir -p "$(SOURCEBOOT_GENERATED)"
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile.saturn.mk" OS=Windows_NT \
	  compile-bob-sky BOB_SKY_SOURCE="textures/skyboxes/water.png" \
	  BOB_SKY_OUTPUT="$(SOURCEBOOT_BOB_SKY)" \
	  BOB_SKY_PALETTE="$(SOURCEBOOT_BOB_SKY_PALETTE)" \
	  BOB_SKY_MANIFEST="$(SOURCEBOOT_BOB_SKY_MANIFEST)" SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

$(SOURCEBOOT_BOB_SKY_ASM): $(SOURCEBOOT_BOB_SKY) $(SOURCEBOOT_BOB_SKY_PALETTE)
	@mkdir -p "$(dir $@)"
	@printf '%s\n' \
	  '.section .rodata' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_sky_bitmap' \
	  '_sm64_saturn_bob_sky_bitmap:' \
	  '.incbin "$(subst \,/,$(SOURCEBOOT_BOB_SKY))"' \
	  '.global _sm64_saturn_bob_sky_bitmap_end' \
	  '_sm64_saturn_bob_sky_bitmap_end:' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_sky_palette' \
	  '_sm64_saturn_bob_sky_palette:' \
	  '.incbin "$(subst \,/,$(SOURCEBOOT_BOB_SKY_PALETTE))"' \
	  '.global _sm64_saturn_bob_sky_palette_end' \
	  '_sm64_saturn_bob_sky_palette_end:' \
	  '.align 4' > "$@"
```

- [ ] **Step 3: Update `main.c`'s sky init**

Read the real current `SOURCEBOOT_SKY_BITMAP_WORDS`/`SOURCEBOOT_VDP2_VRAM_BYTES` macros (just above `sourceboot_init_sky_bitmap`) first -- `SOURCEBOOT_VDP2_VRAM_BYTES` currently assumes 2 bytes/pixel for the sky and must be corrected to the new packed size (0.5 bytes/pixel) or the HWRAM/VRAM budget comment becomes actively wrong.

```c
extern const uint16_t sm64_saturn_bob_sky_bitmap[]; /* now CLUT16-packed, 4 bpp */
extern const uint16_t sm64_saturn_bob_sky_palette[16];

static void sourceboot_init_sky_bitmap(void)
{
    volatile uint16_t * const vram = (volatile uint16_t *)
        (CPU_CACHE_THROUGH | VDP2_VRAM_ADDR(0, 0x00000));
    const uint32_t packed_words = SOURCEBOOT_SKY_BITMAP_WORDS / 4U; /* 4 texels/word at 4bpp */
    for (uint32_t index = 0; index < packed_words; index++) {
        vram[index] = sm64_saturn_bob_sky_bitmap[index];
    }
    sm64_saturn_vdp2_palette_upload(0U, sm64_saturn_bob_sky_palette, 16U);
    const vdp2_scrn_bitmap_format_t format = {
        .scroll_screen = VDP2_SCRN_NBG1,
        .ccc = VDP2_SCRN_CCC_PALETTE_16,
        .bitmap_size = VDP2_SCRN_BITMAP_SIZE_512X256,
        .palette_base = 0,
        .bitmap_base = VDP2_VRAM_ADDR(0, 0x00000),
    };
    /* CHPNDR slot requirement at 4bpp: re-derive from real Yaul VRAM-cycle-
     * pattern documentation before trusting this unchanged from the
     * RGB_32768 config below -- the original comment on this exact site
     * states slot count scales with color depth, and this plan's authoring
     * pass did not independently confirm the exact new slot count. */
    const vdp2_vram_cycp_t cycles = {
        .pt[0].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[0].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t0 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[1].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG1,
        .pt[2].t0 = VDP2_VRAM_CYCP_PNDR_NBG0,
        .pt[2].t1 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t2 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t3 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
        .pt[2].t4 = VDP2_VRAM_CYCP_CHPNDR_NBG0,
    };
    vdp2_vram_cycp_set(&cycles);
    vdp2_scrn_bitmap_format_set(&format);
    vdp2_scrn_display_set(VDP2_SCRN_DISPLAY_NBG1, true);
}
```

The `.pt[0]`/`.pt[1]` reduction from 4 slots each to 2 each above is this plan's *proposed* halving (4bpp needs fewer VRAM-access cycles per pixel than RGB_32768) -- treat it as a starting hypothesis to verify against real Yaul documentation/examples, not a confirmed-correct value; get it wrong and the symptom is VRAM-bandwidth-contention corruption on real hardware, not a compile error, so this needs real verification (Ymir capture comparing before/after sky bitmap pixels, not just "it compiled").

- [ ] **Step 4: Rebuild the generated header/asset dependency chain and verify host-side**

Run: `cd tools/saturn && python -m unittest test_bake_bob_sky -v` (from Task 1, still green)
Run the generator directly and inspect output:
```bash
cd ../.. && .venv-saturn-tools/Scripts/python.exe tools/saturn/bake_bob_sky.py --input textures/skyboxes/water.png --output /tmp/sky.bin --palette-output /tmp/sky_pal.bin --texture-format clut16 --manifest /tmp/sky_manifest.json
```
Expected: exits 0; `/tmp/sky.bin` is exactly `512*256/2 = 65536` bytes; `/tmp/sky_pal.bin` is exactly `32` bytes (16 RGB1555 words).

- [ ] **Step 5: Get real `sh-elf-gcc` verification if available**

The cross toolchain may be reachable one directory above this worktree at `sm64-port/work/yaul-install/bin/` (confirmed present there, not inside this specific worktree, during this plan's research). If reachable, `-fsyntax-only` check `main.c` and fully compile `saturn_vdp2_palette.c` against the real vendored Yaul headers with `-Wall -Wextra -Wpedantic`. If not reachable, say so honestly in the commit/report rather than skip the check silently -- matching this project's established standard for prior HUD-plan tasks.

- [ ] **Step 6: Update CHANGELOG.md**

Add an entry under `### Changed` (or `### Fixed` if the section exists) describing the byte recovery (262,144 -> ~65,536+32 bytes for the sky surface, a ~196 KB reduction), citing this plan file, and noting the `SCENE_PACKAGE_SKY_BACKGROUND` format-change cross-reference from the background-reading section.

- [ ] **Step 7: Commit**

```bash
git add src/port/saturn/sourceboot/main.c src/port/saturn/sourceboot/Makefile Makefile.saturn.mk CHANGELOG.md
git commit -m "feat(saturn): convert BOB sky bitmap to VDP2 CLUT16"
```

---

### Task 4: Convert the HUD glyph atlas to CLUT16

**Files:**
- Modify: `src/port/saturn/gfx/saturn_hud_atlas.c`
- Modify: `tools/saturn/extract_hud_glyphs.py` (glyph pixels currently extracted as raw RGB1555 `uint16_t` words; add the same quantize/pack step)
- Modify: `tools/saturn/test_extract_hud_glyphs.py`
- Modify: `CHANGELOG.md`

**Context you need first:** `extract_hud_glyphs.py`'s real current `build_header(assets, rom=None, rom_sha256=None)` (already read during this plan's research) loops over the 46-entry `GLYPH_MANIFEST`, decodes each glyph's MIO0-compressed source region, converts every pixel via `extract_mario_textures.saturn_rgb1555`, and emits one `static const uint16_t sm64_saturn_hud_<name>[N]` array per glyph -- each glyph's own raw RGB1555 words, no shared palette today. This step adds a shared 16-color palette computed across every glyph's pixels combined (all 46 glyphs share one VDP2 cell format / one palette bank in `saturn_hud_atlas.c`, so they must share one palette), and repacks each glyph's array as 4bpp nibble-packed indices into that shared palette instead of raw words.

- [ ] **Step 1: Extend the existing test fixture, `test_extract_hud_glyphs.py`**

Add to the existing `TestGlyphManifestCoverage` class (the file already has `FIXTURE_ASSETS`/`_FIXTURE_MIO0_ROM` fixtures from the existing RGB1555 tests -- reuse them, do not duplicate):

```python
class TestSharedPaletteOutput(unittest.TestCase):
    def test_shared_palette_has_sixteen_entries_index_zero_transparent(self):
        header, manifest = build_header(FIXTURE_ASSETS, rom=_FIXTURE_MIO0_ROM)
        self.assertIn("shared_palette", manifest)
        self.assertEqual(len(manifest["shared_palette"]), 16)
        self.assertEqual(manifest["shared_palette"][0], 0)

    def test_glyph_arrays_are_nibble_packed_not_raw_words(self):
        header, manifest = build_header(FIXTURE_ASSETS, rom=_FIXTURE_MIO0_ROM)
        # digit_0 fixture is 2 texels (see _FIXTURE_MIO0_ROM docstring above);
        # packed 4bpp is 1 byte for 2 texels, versus 2 raw uint16_t words today.
        self.assertIn(
            "static const uint8_t sm64_saturn_hud_digit_0[1] = {",
            header,
        )
        self.assertNotIn(
            "static const uint16_t sm64_saturn_hud_digit_0[2] = {\n    0x99F5, 0x0000,",
            header,
        )

    def test_build_header_still_rejects_missing_asset_entry(self):
        # Existing RGB1555-path regression must survive this change unchanged.
        with self.assertRaises(KeyError):
            build_header({}, rom_sha256="0" * 64)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tools/saturn && python -m unittest test_extract_hud_glyphs -v`
Expected: FAIL on both new `TestSharedPaletteOutput` cases (`manifest["shared_palette"]` KeyError; `uint8_t` array text absent).

- [ ] **Step 3: Implement the shared-palette pass in `build_header`**

Import the same quantizer Task 1 used (`quantize_clut16`, `pack_clut16` from `bake_castle_uv.py`) and restructure `build_header` into two passes: first collect every glyph's decoded pixel list (the function already computes this per-glyph inside its existing loop -- change it to accumulate into one `all_pixels: list[int]` across every glyph before emitting any header text), quantize once, then re-loop to emit each glyph as packed indices against the shared palette:

```python
from bake_castle_uv import pack_clut16, quantize_clut16  # add to existing imports

def build_header(assets: dict, rom: bytes | None = None, rom_sha256: str | None = None) -> tuple[str, dict]:
    if rom_sha256 is None:
        if rom is None:
            raise ValueError("either rom or rom_sha256 is required")
        rom_sha256 = hashlib.sha256(rom).hexdigest()
    manifest = {"rom_sha256": rom_sha256, "glyphs": {}}
    decoded_bases: dict[int, bytes] = {}
    glyph_pixels: dict[str, list[int]] = {}
    all_pixels: list[int] = []
    for glyph in GLYPH_MANIFEST:
        entry = assets[glyph.manifest_key]
        width, height, size, regions = entry
        base, offset = regions["us"]
        if rom is None:
            manifest["glyphs"][glyph.asset_name] = {"width": width, "height": height}
            continue
        if base not in decoded_bases:
            decoded_bases[base] = mio0_decode(rom, base)
        image = decoded_bases[base]
        data = image[offset : offset + size]
        if len(data) != size:
            raise ValueError(f"{glyph.asset_name}: range outside decoded segment")
        words = [
            saturn_rgb1555(int.from_bytes(data[i : i + 2], "big"))
            for i in range(0, size, 2)
        ]
        glyph_pixels[glyph.asset_name] = words
        all_pixels.extend(words)
        manifest["glyphs"][glyph.asset_name] = {
            "width": width, "height": height, "offset": offset,
            "bytes": size, "sha256": hashlib.sha256(data).hexdigest(),
        }
    if rom is None:
        return "\n".join(["#pragma once", "#include <stdint.h>"]) + "\n", manifest

    palette, index_by_color = quantize_clut16(all_pixels)
    manifest["shared_palette"] = palette
    lines = [
        "/* Local ROM-derived output: do not commit. */",
        "#pragma once",
        "#include <stdint.h>",
        "static const uint16_t sm64_saturn_hud_glyph_palette[16] = {",
        "    " + ", ".join(f"0x{c:04X}" for c in palette),
        "};",
    ]
    for glyph in GLYPH_MANIFEST:
        words = glyph_pixels[glyph.asset_name]
        indices = [index_by_color.get(w, 0) if (w & 0x8000) else 0 for w in words]
        packed = pack_clut16(indices)
        info = manifest["glyphs"][glyph.asset_name]
        lines += [
            f"#define SM64_SATURN_HUD_{glyph.asset_name.upper()}_WIDTH {info['width']}U",
            f"#define SM64_SATURN_HUD_{glyph.asset_name.upper()}_HEIGHT {info['height']}U",
            f"static const uint8_t sm64_saturn_hud_{glyph.asset_name}[{len(packed)}] = {{",
        ]
        lines += [
            "    " + ", ".join(f"0x{b:02X}" for b in packed[i : i + 8]) + ","
            for i in range(0, len(packed), 8)
        ]
        lines += ["};"]
    return "\n".join(lines) + "\n", manifest
```

Verify `pack_clut16`'s real current signature requires an *even* texel count per call (confirmed in this plan's research: it raises `ValueError` on odd length) -- every glyph in `GLYPH_MANIFEST` must have an even `width*height`. Check this against the real current manifest before implementing; if any glyph is odd, pad with one transparent (index 0) texel and record the pad in the manifest so the C reader knows to ignore the last nibble.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tools/saturn && python -m unittest test_extract_hud_glyphs -v`
Expected: PASS, full suite green (existing tests plus the 3 new ones).

- [ ] **Step 3: Update `saturn_hud_atlas.c`'s init to CLUT16, reusing Task 2's helper**

```c
extern const uint16_t sm64_saturn_hud_glyph_palette[16];
```

```c
    sm64_saturn_vdp2_palette_upload(16U, sm64_saturn_hud_glyph_palette, 16U);
    /* bank_offset=16 keeps the HUD's palette bank disjoint from the sky's
     * (bank 0, Task 3) -- both surfaces are visible simultaneously (VDP2
     * NBG0=HUD, NBG1=sky), so they cannot share CRAM bank 0. */
    const vdp2_scrn_cell_format_t format = {
        .scroll_screen = VDP2_SCRN_NBG0,
        .ccc = VDP2_SCRN_CCC_PALETTE_16,
        .char_size = VDP2_SCRN_CHAR_SIZE_2X2,
        .pnd_size = 1U,
        .aux_mode = VDP2_SCRN_AUX_MODE_1,
        .plane_size = VDP2_SCRN_PLANE_SIZE_1X1,
        .cpd_base = HUD_CPD_BASE,
        .palette_base = 16U,
    };
```

Every `hud_atlas_upload_pattern`/procedural-fill call above this format-set call in the real current function writes raw RGB1555 words directly into character pattern VRAM -- all of those call sites need to switch to writing packed 4bpp nibble pairs instead. Re-read the real current function in full (`saturn_hud_atlas.c`, already read in this plan's research through line ~260) before changing it; this plan's authoring pass did not fully re-derive every upload call site's new byte layout.

- [ ] **Step 4: Verify and commit**

Same shape as Task 3 Steps 4-7 (host Python test green, `sh-elf-gcc` check if reachable, CHANGELOG entry, atomic commit).

```bash
git add src/port/saturn/gfx/saturn_hud_atlas.c tools/saturn/extract_hud_glyphs.py tools/saturn/test_extract_hud_glyphs.py CHANGELOG.md
git commit -m "feat(saturn): convert HUD glyph atlas to VDP2 CLUT16"
```

---

### Task 5: Structural proof + real-hardware-adjacent verification

**Files:**
- Create: `tools/saturn/test_vdp2_clut_no_gouraud_boundary.py` (or `.c`, whichever matches this project's existing structural-boundary-test convention -- see Task 7 of the HUD plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`, "no-VDP1 structural proof", for the precedent this mirrors)
- Modify: `docs/saturn/evidence/reports/` (new capture report, if a real Ymir capture is obtained)

- [ ] **Step 1: Write a structural test proving VDP1 Gouraud command sites never reference CLUT16-format data**

Grep every `CC_GOURAUD`-flagged VDP1 command emission site in `saturn_fast3d_vdp1_emit.c` and assert (via a source-text/AST-level check, matching whatever mechanism Task 7 of the HUD plan used for its "no-VDP1" proof) that none of them reference `sm64_saturn_bob_sky_bitmap`, `sm64_saturn_bob_sky_palette`, `sm64_saturn_hud_*`, or `saturn_vdp2_palette` symbols -- this is the automated version of "VDP2 CLUT surfaces and VDP1 Gouraud-lit surfaces are structurally disjoint," matching this plan's stated safety argument in the header above.

- [ ] **Step 2: Get a real Ymir capture comparing before/after the sky and HUD conversions**

Use this project's existing `tools/saturn/capture_sourceboot_throughput.py` (or the equivalent capture tool already used for prior HUD-plan tasks) to grab a screenshot of BOB's sky and HUD at a fixed frame, both before (stash this plan's changes) and after. This is the only real way to catch a wrong CHPNDR slot count or a wrong palette bank offset -- both are silent-corruption-class bugs that compile and link cleanly. Do not mark Task 3/4 as visually verified without this.

- [ ] **Step 3: Run the full existing host test suite to confirm no regression**

Run whatever this project's aggregate host-test entry point is (`tools/saturn/test_tools.py` and/or the `Makefile.saturn.mk` `verify-*` targets touched by Tasks 1-4) and confirm everything is still green.

- [ ] **Step 4: Commit**

```bash
git add tools/saturn/test_vdp2_clut_no_gouraud_boundary.py docs/saturn/evidence/reports/
git commit -m "test(saturn): structural proof + visual verification for VDP2 CLUT conversion"
```
