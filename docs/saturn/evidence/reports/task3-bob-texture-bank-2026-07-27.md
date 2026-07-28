# Task 3 BOB texture-bank evidence

The deterministic BOB intake produces 1,101 textured source triangles. The
milestone-1 partition is 567 16×16 CLUT16 tiles, 510 32×32 CLUT16 tiles, and
24 explicitly listed Gouraud exceptions, for 1,077 textured triangles covered.

The packed texture bank is exactly 333,696 bytes, equal to the specification's
`567×128 + 510×512` ceiling. Its 34,464-byte CLUT bank brings total resident
texture data to 368,160 bytes, below the measured 446,432-byte free VDP1
texture partition. The versioned manifest binds each tile to the source
triangle, display-list identity, texture, tile-state digest, and offsets; an
absent entry is therefore an explicit Gouraud fallback rather than an implicit
textured default.

Verification: `tools/saturn/test_tools.py` passed (164 tests, one skipped), the
`compile-bob-tiles` Makefile dependency completed, and two independent in-process
bakes were byte-identical. The implementation reuses the audited Castle UV
sampler/CLUT quantizer, VDP1 RGB1555 filter, and the repository's exported
RGBA16/IA16 PNG format; it does not introduce a parallel geometry or route IR.
