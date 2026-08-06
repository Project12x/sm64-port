# Task 16 bounded lifecycle-handoff report

## Scope and implementation

Implemented the host/source-neutral lifecycle seam only in
`saturn_actor_runtime_handoff.{h,c}`. It owns the sequence
`EMPTY -> ACQUIRED -> QUEUED -> TERMINAL -> BATCHED -> CONSUMED -> RETIRED` for
one already-published Task 14 bank generation. It does not add geometry,
materials, renderer output, sourceboot wiring, frame-graph wiring, or generic
meshlet preparation.

The exact reused interfaces are:

- Task 14 bank lifecycle: `sm64_saturn_actor_instance_bank_acquire` (lines
  273-290), `..._complete` (292-304), `..._retire` (306-321), and
  `..._quarantine` (323-341) in `saturn_actor_instance.c`.
- Task 16 identity factory and queue lifecycle:
  `sm64_saturn_actor_instance_descriptor_from_snapshot` (72-109),
  `..._queue_publish` (159-202), `..._queue_all_terminal` (397-413), and
  `..._queue_reset_retired` (415-441) in `saturn_actor_instance_queue.c`.
- Task 16 stable painter-order batching:
  `sm64_saturn_actor_batches_build` (18-61) in `saturn_actor_batch.c`.

The handoff regenerates snapshot-derived descriptor identity with that existing
factory and compares the complete ABI record, deliberately retaining the
caller-supplied material ID, output span, and output class arguments used to
construct the record. Pre-acquire argument/bank refusal leaves both systems
unchanged. A live queue is rejected by its existing P2-safe publication path
after acquisition, then the acquired bank is quarantined rather than
raw-reading shared queue metadata. A post-acquire mismatch or
queue-publication failure calls
the existing bank quarantine helper, so no failed handoff leaves a bank in
`RENDERING`. Finalize will not complete a bank until all published descriptors
are DONE or QUARANTINED and batches are built. Retire requires explicit
consumer acknowledgement and resets the queue before releasing the completed
bank.

## Host coverage

`actor_runtime_handoff_test.c` covers the zero-count lifecycle, exact
descriptors, source-derived identity mutations, caller-owned
material/output-span/class preservation, count and output-capacity failures,
stale/wrong bank generation, non-ready bank, live queue, terminal/finalize
refusal, per-descriptor claimant quarantine with stable batches excluding it,
acknowledgement/double-operation refusal, pre-acquire immutability,
post-acquire cleanup, and `UINT32_MAX` lifecycle generation handling.

The new `verify-actor-runtime-handoff` Make target executes its test binary
directly in MSYS instead of passing a POSIX executable path to native Python.
That runner correction is local to the new target. The four inherited targets
still compile successfully but their existing native-Python launch recipes
cannot open their `/d/...` executable arguments; their compiled binaries were
therefore run directly through the same MSYS/DLL-preflight environment.

## Evidence

- PASS: `with-msys-toolchain.ps1 make -f Makefile.saturn.mk
  verify-actor-runtime-handoff -j1`.
- PASS: compiled inherited snapshot/queue/batch/render-snapshot host binaries
  executed directly under the same DLL-preflighted MSYS environment.
- PASS: `tools/saturn/test_actor_runtime_neutrality.py` (2 tests), extended to
  cover the new handoff files.
- Inherited Make runner caveat: `verify-actor-instance-snapshot`,
  `verify-actor-instance-queue`, `verify-actor-batches`, and
  `verify-render-snapshot-bank` each reach successful compilation then fail
  only when native Python receives their POSIX `/d/...exe` path. No assertion
  or compiler failure occurred.

## Remaining boundaries

Task 14 registry/typed-field defects, generic meshlet preparation, production
queue drain, target retirement race, sourceboot integration, target/Ymir/manual
evidence, and FPS claims remain open.
