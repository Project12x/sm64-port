# E2 direct-source 4 MiB cart bank — 2026-07-19

## Result

The direct Bob sourceboot disc now contains two separately linked payloads:

| Payload | Linked location | Purpose | Verified size |
| --- | --- | --- | ---: |
| `A.BIN` | `0x06004000` HWRAM | Yaul startup, source-loop code, platform seam, CDFS/cart loader, frame state | 1,022,564 bytes |
| `SOURCE.DAT` | `0x22400000` DRAM cart | Immutable original SM64 source tables, display lists, textures, animation/model data | 1,694,864 bytes |

`SOURCE.DAT` is below the required 4 MiB / 4,194,304-byte expansion limit. It
is emitted from the final ELF's `.cart_rodata` section and included in the ISO
as a named CDFS file. The bank SHA-256 is
`cd4e0d0b6791f7288b570e2d7cddfdcc1ad95a130577c9c6cd0979361baedefd`.

This is package/link evidence, not a visual-source-frame claim and therefore
has no screenshot entry. The gallery must receive only a captured source loop
once the Fast3D submission is visibly lowered by VDP1.

## Source ownership and residency

The linked source closure retains the direct original Bob script and level
data, its generated asset includes, Mario animation data, shared actor banks,
and the original `thread5_game_loop()` / `game_loop_one_iteration()` path.
Additional direct `leveldata.c` files for menu, Castle grounds, and TTC are
not replacement scenes: their symbols are referenced by retained original
global level scripts/data. The generated collision catalog excludes each
directly linked level (`bob`, `castle_grounds`, and `ttc`) to avoid duplicate
source symbols; all other referenced collision/trajectory fragments remain
source-owned catalog data.

The target-specific N64 RSP output/yield scratch buffers are reduced to one
byte under `TARGET_SATURN`. Saturn directly consumes the source `SPTask`
display-list pointer through `exec_display_list()`, so retaining the N64 RSP
scratch allocation would only consume HWRAM and would not preserve behavior.

## Loader contract

Before `thread5_game_loop()` starts, `source_cart.c`:

1. initializes the DRAM cart and requires Yaul `DRAM_CART_ID_4MIB`;
2. initializes CD block/CDFS and finds literal `SOURCE.DAT` in the root;
3. requires an exact file-size match with the linker's `.cart_rodata` extent;
4. repeatedly reads a 16 KiB CDFS chunk into HWRAM and writes it to the cart
   with 16-bit bus-safe stores; and
5. leaves a debugger-readable work-RAM probe containing stage, status, cart
   identity/capacity, expected bytes, and copied bytes.

For this build the probe symbol is `g_sm64_saturn_source_cart_probe` at
`0x060E7F30`. Its address is linked output, not a permanent ABI; capture tools
must resolve it from the generated `.sym` file for each build.

The exact-size check proves packaging alignment but is not an integrity hash.
Hash/header validation belongs after the cart-enabled target boot proves the
CD transfer lifecycle.

## Reproduction

```text
C:\msys64\usr\bin\bash.exe -lc "cd /c/Users/estee/Documents/Codex/2026-07-16/i-want-to-postulate-a-port/sm64-port/src/port/saturn/sourceboot && source ../../../../.yaul.env && make source-assets -j2 && make -j2 && make verify"

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_tools.py
```

Package checks performed by `make verify` require the expected SH-2 entry
point and `.cart_rodata` VMA. The resulting ISO root contains `A.BIN`,
`SOURCE.DAT`, and the Saturn descriptor text files. The host tool suite has
59 passing tests, including direct-level collision-catalog exclusion.

## Upstream and reuse record

This targets the pinned MIT Yaul 0.3.1 tree at
`6012f79f237773378c8014e70d8998ad95a38d98`. Inspected/useful paths are:

- `sh-elf/lib/ldscripts/yaul.x` — small configuration adaptation in
  `sourceboot-cart.x`;
- `sh-elf/include/yaul/dram-cart.h` — public 4 MiB cart detection and address
  API;
- public CD block and CDFS headers — bounded CDFS source file loading.

No PS1-port, SlaveDriver, Sonic Z-Treme, or other GPL source enters this bank;
the compatibility bank is an original Saturn/Yaul implementation that keeps
the original SM64 source data model intact.

## Next target proof

Use the cart-enabled Ymir SDL profile (`Cartridge.Type = DRAM`, capacity
`32Mbit`) with the supplied USA BIOS, boot this disc, and inspect the dynamic
probe. The current headless debugger can read the probe but does not yet apply
Ymir's nested cartridge configuration, so it cannot substitute for this run.
After the probe reports `READY` and 1,694,864 copied bytes, proceed to the
shared Fast3D-to-VDP1 lowering and capture the first genuine source-loop
frame.
