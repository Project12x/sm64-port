# Task 3b — BOB sky bitmap runtime gate

Fresh dual sourceboot capture at `-r2048/-poly0` with `--dram-cart` and fresh
profile/route probes.

| Item | Value |
|---|---|
| Screenshot | `docs/saturn/evidence/screenshots/task3b-bob-sky-fixed-2026-07-28.png` |
| ELF SHA-256 | `794e5340f78294665a56a4985b7f739ddbb786bce43dce42095e26f6051480ec` |
| Profile probe | `0x060D11E0` |
| Route probe | `0x060D1158` |
| Frame serial | `802` |
| Render total | `47,972,047` FRT ticks = `17.8075 ms/frame` |
| Faults / timeouts | `0 / 0` |
| VDP2 | NBG1 bitmap + NBG3 debug text; `262,592` bytes reserved |

The image visibly contains the baked BOB sky bitmap behind the terrain. The
previous black field was a startup-order bug: the bitmap copy ran before the
DRAM cart load and copied zeroed cart-linked data. The upload now runs after
`source_cart_load()`. This is a diagnostic evidence frame until owner visual
confirmation; it is not yet a gallery entry.
