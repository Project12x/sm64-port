# PCM/68K transport provenance

## Reference

| Field | Record |
| --- | --- |
| Upstream | `ponut64/SCSP_poneSound` |
| Pinned revision | `31782e4c61337327f23eb9aa45ecd37fe0944ea0` |
| License | MIT; verbatim text in [PONESOUND_MIT.txt](PONESOUND_MIT.txt) |
| Local inspected checkout | `sm64-port/work/upstream/SCSP_poneSound` |
| Files inspected | `LICENSE`, `README.md`, `documentation.md`, `PROJ/main.c`, `PROJ/linker`, `PROJ/makefile`, `jo_demo/pcmsys.c`, `jo_demo/pcmsys.h` |
| Reuse mode | Pattern-only for the first protocol increment; a later, separately reviewed 68K driver may be a MIT close port of only the vector/linker, SNDOFF/SNDON, slot-word, pitch, and PCM-metadata portions named in the approved audio plan. |

## Current increment

`src/port/saturn/audio/saturn_pcm_protocol.h` is original project code. It
uses fixed byte offsets and explicit big-endian stores because the inspected
reference demonstrates the necessary separate-binary, fixed sound-RAM
communication boundary. It does not copy its `sysComPara`, `_PCM_CTRL`,
driver, wrapper, ADX/CDDA path, or `sdrv.bin`.

The shared ABI intentionally differs from the reference:

- no C structs or C pointers cross between SH-2 and 68K;
- every shared field is a defined big-endian byte sequence;
- the transport is a 32-entry, 16-byte command ring rather than the
  reference's mutable control-array protocol; and
- the first scope supports only PCM `PLAY`, `STOP_ALL`, and `SET_MASTER`.

No Nintendo sample, sequence, or prebuilt driver data is included.
