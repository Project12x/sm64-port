# Renderer pipeline sprint baseline

This is the complete-probe Task 0 baseline for the pre-pipeline sourceboot
lineage. It uses the standard long Ymir budget (`--timeout 1700`), `--dram-cart`,
fresh ELF/CUE pairing, and a 128-byte probe for both the Fast3D profile and the
SBR2 route block. The old 64-byte run is retained as a diagnostic, but is not
the baseline gate.

## Provenance

- Branch: `saturn/bootstrap`
- Intended sprint baseline: `d58fc37`
- Route: `tools/saturn/routes/bob_parity_v1.json`, 600 simulation ticks
- Degradation: `view_radius=6000`, `poly_tier=0`
- Comparator: `pipeline-task0-route128-compare-2026-07-28.json`
- Comparator result: deterministic; exact checkpoint signature, zero fault
  flags, zero command-capacity rejects, and zero renderer-counter delta

The serial capture used profile `0x060C61E8` and route `0x060C613C`. The dual
capture used profile `0x060CD1E8` and route `0x060CD13C`. These addresses are
valid only for the exact ELF/CUE pairs recorded below and must be resolved
again after every build.

| Build | ELF SHA-256 | CUE SHA-256 | Route probe bytes | Wall seconds | Emulator speed ratio | Render FRT accumulator | Jobs | Master wait | Slave busy |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Serial | `46ff0cf9fa510b64aedd64f8ae752cdcefe13e7a9af4fdce25d9320f003b480c` | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | 128 | 122.016 | 1.2703 | 2,957,536 | 0 | 0 | 0 |
| Dual | `953f3876bf771cc149e5648947adf7b0785f2519ce8eb2fe55e72c2347dd1f3e` | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | 128 | 119.294 | 1.2993 | 4,999,436 | 600 | 868,889 | 14,985,355 |

Both captures report `frame_serial=150`, `replay_ticks=600`,
`triangles_transformed=854580`, and `triangles_vdp1_emitted=110958` at the
checkpoint. The serial/dual render FRT difference is +69.0% for the dual
build, and the dual worker is doing four synchronous jobs per rendered frame;
this is the pre-sprint comparison, not an acceptance result.

## Cadence interpretation

The capture tool now records bounded 60-emulated-frame samples and computes a
median and 1% low from interval deltas. This baseline's untrimmed cadence
series includes BIOS/startup intervals, so it reports guest median `1.0 FPS`
and 1% low `0.0 FPS` for both images. Those values are intentionally marked
diagnostic until a stable post-handoff sampling window is added. Perceived
emulator values would be guest FPS multiplied by the run's speed ratio; they
are not retail-hardware FPS claims.

Screenshots are retained as baseline diagnostics:

- `../screenshots/pipeline-task0-serial-route128-2026-07-28.png`
- `../screenshots/pipeline-task0-dual-route128-2026-07-28.png`
