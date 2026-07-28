# Task 6 horizon/fog spike measurement

Date: 2026-07-28

## RBG0 horizon mask

Current VDP2 usage is the checked-in sourceboot layout:

- NBG1 BOB sky: 512×256 RGB1555 bitmap = 262,144 bytes.
- Back-screen gradient: 224 RGB1555 entries = 448 bytes.
- NBG3 debug text remains enabled.
- VDP2 has 512 KiB total VRAM.

A second full-frame RGB1555 RBG0 bitmap would consume another 262,144 bytes
before rotation-parameter storage, map data, and access-cycle reservations. It
would therefore compete directly with the existing NBG1 sky and does not fit
the current layer budget as a drop-in horizon mask. A tilemap RBG0 variant
would fit, but BOB's open, hilly horizon has no measured flat source plane to
serve as a correct seam. The spike is therefore measured and deferred; the
gradient/NBG1 sky remains the valid fallback.

Reference checked: `src/port/saturn/sourceboot/main.c` sky setup and
`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/scrn_rotation.h` rotation
parameter contract.

## Fog-band color calculation

The current renderer emits one VDP1 command stream with no depth buffer and
does not assign far geometry to VDP2 color-calc priority groups. A fog band
would require (1) a stable near/mid/far bucket split, (2) VDP2 sprite color
calculation enable/mode programming, and (3) a seam-safe priority contract
with NBG1/NBG3. No such path exists in the current sourceboot layer setup.
Adding it before the visual geometry gate would change ordering and fill
behavior simultaneously, so the spike is measured and deferred. VDP1
half-transparency remains explicitly banned for this purpose.

Outcome: no RBG0 or fog implementation is promoted by this spike; the
existing gradient horizon and measured view-distance clamp remain the gated
Task 6 degradation path.
