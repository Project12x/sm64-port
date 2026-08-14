# A5.8.1 graph-runtime review — NO-GO

Reviewed commit: `92ba442e646f8daa9b505dcbaf35c2ec03a062ba`

## Verdict

**NO-GO** — one P1/P2 coherency defect blocks the eventual SH-2 activation.

## Blocking finding

`sm64_saturn_render_job_runtime_poll_slave()` and
`sm64_saturn_render_job_runtime_drain_master()` claim through the graph (whose
queue operations correctly convert the queue address to `CPU_CACHE_THROUGH`),
then dereference the original cacheable `s_runtime.queue` directly:

```c
const sm64_saturn_render_job_t *const job =
    &s_runtime.queue->jobs[index];
```

On SH-2 the master publishes these descriptors and the slave consumes them.
The new direct dereference can read a stale P1 cache line even though the
release state was observed safely through P2.  The earlier generic queue drain
explicitly cache-through-converts `queue` before its equivalent `jobs[index]`
read.  A claimed job therefore needs to be read through a queue P2 accessor
before callback dispatch (or the runtime must store a P2 queue pointer);
completion/failure must use the same coherent queue reference.

This is source-only today — `92ba442e` does not edit the renderer or bind its
fixed terrain/Mario arrays to CPU-DUAL — but the defect must be fixed before
any live activation is accepted.

## Verified behavior

- The deprecated raw activation fails closed and cannot install a CPU-DUAL
  callback.
- Both new runtime drains use `sm64_saturn_render_job_graph_claim_*`; they do
  not call the raw ready-scan drains.
- Graph scheduling correctly withholds a consumer published before its
  producer.  The focused runtime fixture passes.
- Existing graph fixture covers producer failure, reverse-chain quarantine,
  and independent work remaining eligible; it passes.
- The commit does not change `saturn_demo_render.c`; there is no accepted
  renderer-side CPU-DUAL binding in this commit.  The working tree has a
  pre-existing unrelated renderer modification and was not changed by review.
- Commit docs accurately describe this as a source-only prerequisite and make
  no CUE/Ymir/FPS claim.

## Evidence run

```text
C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
  -I src/port/saturn/gfx tools/saturn/render_job_runtime_test.c \
  src/port/saturn/gfx/saturn_render_job_runtime.c \
  src/port/saturn/gfx/saturn_render_job_graph.c \
  src/port/saturn/gfx/saturn_render_job_queue.c
render job runtime fixture: PASS

C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
  -I src/port/saturn/gfx tools/saturn/render_job_graph_test.c \
  src/port/saturn/gfx/saturn_render_job_graph.c \
  src/port/saturn/gfx/saturn_render_job_queue.c
render job graph fixture: PASS
```

`git diff --check 92ba442e^ 92ba442e` is clean.  The Python static guards were
not run because this session's WindowsApps `python.exe` launcher could not be
started (`file cannot be accessed by the system`); this does not replace the
target/P2 gate.

## Required repair

Cache-through-convert the queue before dereferencing a claimed descriptor in
both graph runtime drains, and use that coherent pointer for terminal
transitions.  Add a static/source contract that prevents raw
`s_runtime.queue->jobs[...]` access in runtime dispatch so a later refactor
cannot restore the P1 read.  Re-run the two C11/Werror fixtures and obtain a
fresh independent review.
