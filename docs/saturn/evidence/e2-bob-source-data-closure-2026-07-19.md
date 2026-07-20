# E2 Bob source-data closure — 2026-07-19

## Outcome

The direct Bob-omb Battlefield source target now has a deterministic closure
for the original collision/trajectory tables and the actual Bob ocean-sky
asset. Its original static data exceeds Yaul's first-read HWRAM image, so the
target now packages that data as a separately linked 4 MiB DRAM-cart bank. It
does not replace the source data with a bespoke scene or an offset-only IR.

The source path remains:

```text
thread5_game_loop -> game_loop_one_iteration -> level_script_execute
  -> direct original level_bob_entry -> original Mario/object/camera/geo state
  -> exec_display_list -> Saturn Fast3D front end
```

`source_entry.c` is only the bounded level selection. The Bob level script,
terrain, macro objects, behaviors, collision, and geometry are original source
data.

## Reproducible inputs and checks

```text
.venv-saturn-tools\Scripts\python.exe -m py_compile \
  tools\saturn\prepare_sourceboot_collision_catalog.py
.venv-saturn-tools\Scripts\python.exe \
  tools\saturn\prepare_sourceboot_collision_catalog.py \
  --root . --output build\saturn\sourceboot\generated\sourceboot_collision_catalog.inc
```

The generated catalog contains 206 original `collision.inc.c` and
`trajectory.inc.c` fragments outside `levels/bob`; Bob's own fragments remain
in `levels/bob/leveldata.c`, preventing duplicate symbols. The catalog exists
because the original static behavior tables retain collision/trajectory
pointers beyond the current area. It contains no rewritten surfaces or
generated game logic.

The narrow build was invoked through the pinned local Yaul 0.3.1 toolchain:

```text
make -C src/port/saturn/sourceboot source-assets -j2
make -C src/port/saturn/sourceboot -j4
```

It reaches the final SH-2 link and proves the former missing collision class is
resolved. The linker reports the remaining real budget boundary:

```text
section `.rodata' will not fit in region `ram'
region `ram' overflowed by 1748848 bytes
```

The generated Yaul link map reports a `0x19975C`-byte `.rodata` output section
(1,677,148 bytes). Its principal original-data contributors are:

| Source data owner | Retained bytes | Why it is live |
| --- | ---: | --- |
| `mario_anim_data.c` | 580,632 | Original Mario animation table/data |
| `actors/common0.c` | 156,480 | Common original actor/model bank |
| `actors/group14.c` | 149,648 | Bob global-model bank dependency |
| `water_skybox.c` | 131,392 | Bob's `BACKGROUND_OCEAN_SKY` source asset |
| `levels/bob/leveldata.c` | 85,666 | Bob terrain, macro objects, display lists, textures |
| `bin/segment2.c` | 84,128 | Original shared text/HUD source segment |
| `actors/group12.c` | 82,846 | Original model-bank dependency |

This is a data-residency result, not a failure of the original engine loop.
The direct target intentionally uses an isolated `e2-bob` object tree because
Yaul's standard fragment links its declared object list; it avoids stale
all-level experiment objects contaminating the map.

## Implemented consequence: source-data bank before first source visual boot

Yaul's standard `objcopy -O binary` build is one contiguous first-read binary
loaded at `0x06004000`; it cannot directly populate the 4 MiB DRAM cartridge.
The sourceboot target therefore now emits `SOURCE.DAT` from a dedicated
`.cart_rodata` section linked at `0x22400000`. Its CDFS loader runs before any
ordinary source-data access and performs:

```text
CD SOURCE.DAT -> 16 KiB HWRAM staging block -> 0x22400000 DRAM cart
  -> original thread5_game_loop() -> game_loop_one_iteration()
```

The loader requires Yaul's `DRAM_CART_ID_4MIB` (`0x5C`), verifies the file's
exact linked byte count, and uses 16-bit cart writes. The cart is immutable
native-pointer source data; code, frame pools, CD staging, and the loader
remain in internal RAM. `source_cart.c` also publishes a small work-RAM probe
(`g_sm64_saturn_source_cart_probe`) with cart ID/size, source bytes, copied
bytes, state, and status so a debugger can distinguish missing cart, CD, file,
size, and transfer failures without dereferencing the cart bank.

The pinned Yaul headers expose the required public pieces:

- `dram_cart_init()`, `dram_cart_id_get()`, `dram_cart_size_get()`, and
  `dram_cart_area_get()`; a valid 4 MiB cart is `DRAM_CART_ID_4MIB` (`0x5C`);
- `cd_block_init()`, CDFS root-file enumeration, and
  `cd_block_sectors_read()` for source-bank file reads.

The concrete linker/configuration adaptation is
`src/port/saturn/sourceboot/sourceboot-cart.x`, based on pinned MIT Yaul
`sh-elf/lib/ldscripts/yaul.x`; it is recorded in the upstream ledger. The
sourceboot Makefile emits and includes `SOURCE.DAT` as a fresh ISO prerequisite.
The current package check is exact-size validation, not yet a content hash;
hash validation is an explicit remaining integrity improvement.

Because original SM64 data contains native addresses (behavior callbacks,
display-list pointers, and animation/model references), the early source-data
bank must be linked/relocated for its cart address. It is not interchangeable
with the later offset-only Saturn IR package format. The latter remains the
portable generated-renderer format; this bank is the compatibility bridge that
lets inherited source code keep its original data model while the port comes
up.

## Open work

1. Run this exact disc under a 32 Mbit/4 MiB cart-enabled Ymir profile and
   read the linked probe after boot. The existing headless JSON-RPC runner does
   not yet apply Ymir's nested cartridge TOML settings, so it must not be used
   as a no-cart negative result for this target.
2. Add a cart-bank content hash/header check after the first cart-enabled boot
   proves the CDFS transfer path.
3. Restore the original main-script Mario/shared-model setup before claiming a
   Mario visual. The current direct entry proves level selection, while the
   Fast3D backend still needs to lower the submitted source task into VDP1
   work.
4. Capture and add a gallery entry only after this path produces a genuine
   source-loop frame. No placeholder or Castle-viewer image is evidence for
   this gate.
