# Ymir capture path fix — 2026-07-17

The Ymir runner previously changed its working directory to the cue directory
but passed a relative `--game` path unchanged. That caused Ymir to resolve the
disc as `cue-directory/cue-directory/game` and report a misleading file/CD
failure.

`tools/saturn/capture_hwtest.py` now resolves the Ymir executable, BIOS, disc,
and output paths before launching the subprocess. A 3,600-frame BIOS-backed
smoke run with a relative `--game` argument now reaches Ymir's frame limit and
returns a structured `mem.peek` response instead of failing disc loading.

The response still contains zeroed telemetry (`SAT0` is absent), so this fixes
the runner path bug but does not change Ymir's separate BIOS/CD execution
limitation. The raw run was intentionally not committed because it contains
machine-local absolute paths and invalid telemetry.
