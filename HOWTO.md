# How to

Use `docs/saturn/BUILDING.md` for Saturn builds and verification.

For the source-only Mario meshlet gate, run `make -f Makefile.saturn.mk
verify-actor-meshlets verify-dual-actor-worker` through
`tools/saturn/with-msys-toolchain.ps1` with the explicit worktree root and
host compiler overrides recorded in the active evidence report.
