# Visual milestone evidence

Visual progress is a first-class sprint artifact. A runtime visual milestone
is not complete from counters alone.

For each milestone, save a fresh screenshot and its paired capture report:

```text
docs/saturn/evidence/screenshots/<milestone>-YYYY-MM-DD.png
docs/saturn/evidence/reports/<milestone>-YYYY-MM-DD.json
```

The report must identify the exact ELF and CUE used, their SHA-256 hashes and
mtimes, the capture command/configuration (including probe address and
`--dram-cart` when applicable), route/checkpoint, frame hash, and screenshot
dimensions. Never reuse a screenshot from a different build; resolve moving
symbols from the ELF used for that capture. `capture_hwtest.py`'s stale-CUE
preflight remains enabled unless a diagnostic capture explicitly uses
`--allow-stale` and records why.

The capture is then presented for owner visual confirmation. Only after that
confirmation may the milestone be added to `evidence/TIMELINE.md` and the
repository screenshot gallery, `evidence/index.html`. The gallery is a
curated acceptance surface, not a dump of every capture: failed, diagnostic,
or owner-unconfirmed frames remain available in `screenshots/` and `reports/`,
labelled as such, but are not promoted or silently replaced.

For every gallery entry, keep the image basename and report basename paired,
link the card to the milestone evidence where practical, and record the
owner-confirmation date in the card text or corresponding timeline entry.

## Milestone labels

Use stable labels tied to the plan, for example:

- `task3b-bob-sky`
- `task4-live-mario`
- `task5-textured-bob`
- `task7-fps-gate`

The first visible demo-path gate is `task5-textured-bob`: textured BOB terrain
and animated Mario on the frozen route. Its screenshot, report, and owner
confirmation are separate checkboxes from the renderer and FPS counters.
