# Task 2 — BOB Mesh IR v2 evidence

The shared BOB adapter walks the six Area 1 GeoLayout display-list roots using
the existing Fast3D parser and emits 1,101 textured source triangles. The v2
conversion bakes static vertices into world space and welds only exact
position/UV/texture/tile-state tuples; it produces 1,625 vertices rather than
unsafe position-only welding.

The compiler retains the existing NetworkX maximum-cardinality matcher and
adds the v2 textured gates: identical texture/tile state, the existing
material/winding/normal/planar-convex geometry checks, exact shared-edge UV
attributes with a non-degenerate boundary cycle, and a bounded raw-UV affine
residual of 256. The deterministic result is 234 quads plus 633 triangle
fallbacks, saving 234 commands and staying inside the expected 220–285
recovery envelope.

The conservative packed-bank estimate is 30,410 bytes against the 576 KiB
LWRAM budget. `compile-bob-area` generates intake, Mesh IR v2, compiled bank,
and report outputs under `build/saturn/sourceboot/generated/`; sourceboot's
`source-assets` target depends on that generator, so geometry changes cannot
silently leave a stale bank.

The Z-Treme promotion lifecycle was close-ported as the isolated
`src/port/saturn/gpl/ztreme_hot_promotion.{c,h}` helper. It is bounded,
alignment-aware, and keeps the frame loop on the returned HWRAM pointer rather
than following a cold LWRAM/cart pointer.
