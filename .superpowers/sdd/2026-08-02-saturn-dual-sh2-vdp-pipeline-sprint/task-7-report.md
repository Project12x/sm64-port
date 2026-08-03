# Task 7 report — compact BSP work spans

## Result

The BOB BSP generator now emits deterministic packed work spans:
`leaf_first_ref[]`, `leaf_ref_count[]`, and `primitive_refs[]`.  Each span is
the unique, source-stable local reference sequence for one BSP node. Exact BSP
splits may conservatively retain one primitive in multiple node spans, but no
span contains the same primitive twice.

The accepted non-fragment BSP path appends only the admitted spans directly to
the fixed `s_render_work_order` list. It retains predecessor first-reference
wins behavior with a 28-word bitset, preserves near-child/local/far-child
source order, validates generated ranges and primitive IDs, fails closed on a
malformed generated span, and retains the fixed work-list capacity. The former
867-byte admission state, copied admission order, and post-traversal primitive
scan are removed. Master ownership of traversal, worker scheduling, final
order, and VDP1 allocation is unchanged; the slave still receives only the
immutable resulting work range.

`emit_bob_scene.py` consumes the BSP report and stamps the scene with the
span/ref cardinalities, so stale scene/BSP generated artifacts are visible at
review. The sourceboot dependency now produces the BSP report before the scene
header.

## TDD and verification

- RED: added the focused generator contract before implementation; it failed
  with `KeyError: 'leaf_spans'`, proving the required symbols/data were absent.
- GREEN: `.venv-saturn-tools\Scripts\python.exe tools\saturn\test_tools.py
  BobMeshIRTests.test_bob_scene_emitter_preserves_ir_counts_and_manifest_offsets
  BobMeshIRTests.test_bob_bsp_leaf_spans_are_deterministic_complete_and_unique
  BobMeshIRTests.test_bob_bsp_report_is_exact_and_deterministic
  BobMeshIRTests.test_bob_bsp_header_emits_conservative_bounds_and_work_weights`
  passed (4 tests). The new test checks repeated byte-equivalent generation,
  span/range bounds, in-range IDs, no within-span duplicate, complete coverage,
  and exact predecessor first-reference-wins set/order equality over three
  deterministic admitted-node masks.
- GREEN: regenerated `bob_area1_bsp_report.json`, `bob_bsp.h`, and
  `bob_scene.h` with the local venv Python only.
- GREEN: native Qt MinGW host compilation and execution of
  `tools/saturn/bob_bsp_header_smoke.c` passed. The smoke fixture validates all
  emitted spans, their in-range references, uniqueness within each span, and
  complete primitive coverage.
- GREEN: `git diff --check` passed.

No MSYS, `bash`, `sh-elf-*`, target build, or Ymir process was invoked under
the active missing-DLL containment constraint.

## Reference-code-first record

- **Project generator:** `tools/saturn/compile_bob_bsp.py`, its exact-rational
  `static_bsp.py` dependency, and `tools/saturn/bake_bob_bsp_fragments.py`
  were inspected. The packed-span lowering is new project code, because the
  existing generated BOB node-local references already precisely fit this
  renderer's source-ID and bounded-work-list contract.
- **Sonic Z-Treme**, `Maxime-XL2/SONIC-Z-TREME`
  `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0; recorded inspected
  paths `ZT_FRUSTUM.c:126-161`, `ZT_RENDERING.c:406-505,718-786`,
  `ZT_LOADING.c:118-176,299-355`, `workarea.c:14-20`, and `ZTE_DEF.H`.
  Reuse mode: **pattern-only**. Its bounded hierarchy/leaf work model informed
  the fixed span/list shape; no source was copied.
- **SlaveDriver Engine**, `Lobotomy-Software/SlaveDriver-Engine`
  `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later; recorded
  inspected paths `WALLS.C:288-500,1240-1408,1803-1950,2062-2285`, `DMA.C`,
  `DMA.H`, and `V_BLANK.C:94-145`. Reuse mode: **pattern-only** for bounded
  first-wins result handling. Its portal/sector renderer is an architecture
  mismatch for BOB's generated BSP and was not copied.

The pinned sources are documented in `docs/saturn/UPSTREAM_CODE_LEDGER.md` and
`docs/saturn/RENDERER_PRIOR_ART.md`; no third-party notices or licenses change
because this task adds no copied or close-ported code.
