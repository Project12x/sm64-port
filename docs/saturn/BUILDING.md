# Building the Saturn Bring-up Target

The Saturn build is intentionally separate from the existing N64 and PC
Makefile. It currently produces a small 320x224 VDP2 hello-screen disc and does
not compile SM64 game code.

## Pinned dependency

The repository pins libyaul 0.3.1 as a Git submodule:

- path: `third_party/libyaul`
- repository: <https://github.com/yaul-org/libyaul>
- commit: `6012f79f237773378c8014e70d8998ad95a38d98`
- license: MIT, recorded in `THIRD_PARTY_LICENSES.md`

Clone with submodules or initialize it after cloning:

```sh
git clone --recurse-submodules https://github.com/Project12x/sm64-port.git
cd sm64-port
```

```sh
git submodule update --init third_party/libyaul
```

Verify the exact revision without requiring a cross toolchain:

```sh
make -f Makefile.saturn.mk check
```

## Hermetic sourceboot pipeline

The outer `sourceboot` target now has one fail-closed sequence:

1. `assets` materializes the selected BOB content and stable generated source
   inputs without reading or selecting a build identity.
2. `discover` freshly scans every real C, C++, and `.sx` source even when a
   prior depfile exists. C uses SH GCC, `SH_CFLAGS`, and `SH_SPECS`; C++ uses
   SH G++, `SH_CXXFLAGS`, `SH_SPECS`, and `SH_CXX_SPECS`; `.sx` uses SH GCC
   and `SH_CFLAGS` without specs, exactly like Yaul's assembly rule. Discovery
   removes only `-save-temps=obj` from the real preprocessing flags, applies
   all three repository prefix maps to C and C++, then writes the source
   closure, absolute-path diagnostic external-dependency handoff, and portable
   toolchain attestation.
3. `seal` resolves the selected profile/package manifests and composes identity
   v2 from their exact bytes plus the closure and attestation. Only this stage
   may read the identity label or select an identity-tagged output directory.
4. `build` compiles and links into that sealed directory.
5. `verify-sealed-inputs` compares the real C/C++ depfiles and freshly rescanned
   `.sx` dependencies with discovery, rehashes the closure, enforces release
   cleanliness when requested, and remeasures the live toolchain before a later
   release-manifest stage may publish artifacts.
6. `seal-release` verifies those inputs again, then writes
   `saturn-release-manifest-v1.json` beside the CUE. The canonical manifest
   binds the resolved profile, identity-v2 effective configuration, source
   closure, package set, toolchain attestation, ELF, `SOURCE.DAT`, ISO, and CUE
   without timestamps or absolute paths. `verify-release` independently
   rehashes every output, extracts the exact identity symbol from the ELF, and
   requires the CUE's single `FILE` directive to resolve to the hashed ISO.

The default profile is
`tools/saturn/profiles/sourceboot-bob-demo-v1.json`. Override it with
`SOURCEBOOT_TARGET_PROFILE=/absolute/or/repository/path.json`. The default
`SOURCEBOOT_RELEASE_MODE=development` seals actual selected inputs but permits
dirty checked-in closure files for local iteration. Set
`SOURCEBOOT_RELEASE_MODE=release` for a release-enabled profile and clean,
tracked closure inputs; any mismatch stops before release sealing. Both values
are passed explicitly through every recursive Make stage.

Discovery and seal outputs live under `build/saturn/sourceboot/generated/`:

- `saturn-source-closure-v2.json` — canonical repository input closure;
- `saturn-external-dependencies-v1.json` — diagnostic absolute SDK paths,
  deliberately excluded from identity;
- `saturn-toolchain-attestation-v1.json` — canonical component-relative SDK
  measurements;
- `saturn-target-profile-v1.json` and `saturn-package-set-v1.json` — resolved
  selected-content manifests; and
- `saturn_build_identity_spec.json` plus the generated identity include/blob/
  JSON/label.

The host Make-contract suites validate ordering and command expansion only.
They do not build SH-2 code or close the real-target, reproducibility, audit,
smoke, visual, or manual-play gates.

Discovery and post-link verification pass their large path inventories through
generated `sm64-saturn-path-list-v1` files rather than repeated command-line
arguments. These LF-only, unique, byte-sorted lists are strict transport
metadata: their semantic paths derive the canonical closure, but the list
files themselves are not identity inputs. This keeps the same closure contract
below Windows/MSYS command-line limits even as the full-game source set grows;
Windows Python converts only canonical `/d/...`-style MSYS drive paths from
the transport back to native drive paths before reading them.

Do not copy or launch a release by selecting artifacts manually. Stage only a
verified manifest into a missing or empty destination:

```sh
python tools/saturn/stage_saturn_release.py \
  --manifest build/saturn/sourceboot/e2-bob-*/saturn-release-manifest-v1.json \
  --destination /path/to/new/staged-release
```

Verification copies the manifest-bound bytes into a private snapshot before
returning. The staging tool consumes only that snapshot, copies the four
outputs into a private sibling tree, writes the captured manifest bytes last,
requires exact inventory, atomically publishes without replacement, and
verifies the exact published tree again. No failure path uses `unlink`,
`rmdir`, or recursive deletion after a namespace race. Ambiguous partial,
published, or foreign content is retained beside the requested destination as
`.sm64-saturn-quarantine-<destination>-<unique-id>` and its full path is added
to the diagnostic; inspect ownership before removing a quarantine. A failed
missing destination remains missing, a failed preexisting-empty destination is
restored empty, and either can be retried.

Atomic publication support is deliberately explicit:

- Windows uses an exclusive rename while retained directory handles pin the
  active namespace. A proven preexisting-empty backup is deleted only through
  its identity-checked opened handle.
- Linux requires libc `renameat2` with `RENAME_NOREPLACE` and a filesystem that
  implements that flag.
- macOS and BSD-family hosts are accepted only when libc exports the
  directory-relative `renameatx_np` API with `RENAME_EXCL`. Path-only
  `renamex_np` is not a fallback because it would abandon the guarded parent
  descriptor. BSD variants without `renameatx_np`, and all other POSIX hosts,
  fail capability preflight before any staging namespace is created.

Platforms without identity-conditional opened-object directory deletion retain
the proven empty preexisting-destination backup under the same sibling
quarantine prefix after successful publication and emit a stable
`RuntimeWarning` containing its path. The retained directory is empty; the
published destination still has exact manifest inventory, and a destination
that was initially missing never creates this backup. The host tests execute
the Windows adapter and inject the documented POSIX libc contracts; they do not
claim execution on Linux, macOS, or BSD.

The throughput, object-pool, automated HUD, and desktop-Ymir entry points
require `--release-manifest`; a separately supplied `--game`, `--elf`, or
desktop `--cue` must resolve to the original manifest paths, while emulator and
SH-tool reads use the private verified snapshot. If desktop Ymir remains alive
after the bounded monitor window, a cleanup watcher retains the CUE/ISO
snapshot until that child exits and records the snapshot root, PID, and cleanup
state in the launch report. Identity-v2 object-pool captures read
`object_pool_capacity` from the manifest's hash-bound effective configuration.
`--identity-spec` is retained only for explicit historical identity-v1
occupancy; the release writer also accepts a real v1 ELF/identity JSON only
when the resolved profile derives the same canonical effective-config hash.

Release schemas use forward-slash, Unicode-normalized, case-folded path
uniqueness independent of the host filesystem and reject Windows-reserved,
escaping, symlink, and filesystem-alias outputs. `release_manifest.py compare`
compares canonical identity inputs and each output's size/SHA-256, ignoring
manifest location, output relative layout, Git provenance, and other
non-identity metadata.

## Host environment

Libyaul 0.3.1 expects a Unix-like shell and rejects paths containing spaces.
The 2026-07-16 proof build used the MSYS2 MSYS shell, not the MinGW shell,
because the cross compiler and disc tools are Unix-hosted programs. Required
host packages were `base-devel`, `gcc`, `texinfo`, `wget`, and `xorriso`.

Do not currently follow the Windows package-feed stanza in libyaul's README.
During the proof build, its HTTP MinGW feed returned HTTP 403, the HTTPS host
did not have a matching certificate, and its historical toolchain release
asset was no longer available. Do not disable TLS verification or install an
unsigned replacement.

The verified fallback was a workspace-local, source-built SH compiler using
the permissively licensed `andwn/sh-gcc-toolchain` recipe pinned through
Marsdev:

- Marsdev commit: `3318d3f39823154b24ce48bfa5d8fe6e3f6cde3f` (MIT);
- `sh-gcc-toolchain` commit:
  `e5d330c1528758da70bb9e41a3927c648d28a77b` (zlib license in its README);
- GCC 14.3.0 and binutils 2.44, with the recipe's SHA-256 checks;
- target `sh-elf`, configured for big-endian SH-2 (`--with-endian=big
  --with-cpu=m2`); and
- freestanding C/C++ compiler only; newlib and hosted C++ are not needed by
  this target.

Current MSYS2 headers required a host-only generated-config workaround while
building GCC: the generated `gcc/auto-host.h` incorrectly recorded
`fgets_unlocked` and `fputs_unlocked` as declared even though MSYS2 exposes
them only under `__GNU_VISIBLE`. The proof build set both generated
`HAVE_DECL_*` values to `0`, invoking GCC's own fallback declarations. This
was a temporary patch to the GPL build tool in an ignored work directory; no
GCC source enters this repository. The one-command toolchain bootstrap is now
present; its container execution still needs to be exercised on a
Docker-capable host. On Linux or a Docker-capable host, the official
MIT-licensed `yaul-org/libyaul-docker` image is the preferred reproducibility
path.

Required environment variables are defined by libyaul's `yaul.env.in`:

- `YAUL_INSTALL_ROOT`: absolute toolchain/SDK installation path;
- `YAUL_ARCH_SH_PREFIX`: compiler target prefix (`sh-elf` in the proof build);
- `YAUL_PROG_SH_PREFIX`: executable prefix (`sh-elf` in the proof build);
- `YAUL_ARCH_M68K_PREFIX`: SCSP 68K target prefix, normally `m68keb-elf`;
- `YAUL_BUILD_ROOT`: absolute path under which libyaul may place build output;
  this need not be the source checkout; and
- `YAUL_BUILD`: libyaul's build-directory name.

## Portable bootstrap command

On a Docker-capable host, the repository provides one command that installs the
pinned libyaul submodule, builds/verifies both discs in the official Yaul
container layout, then runs the host-side classifier/telemetry regression
suite:

```sh
make -f Makefile.saturn.mk bootstrap
```

The same command also regenerates the checked-out source inventory and
six-way geometry/UV report at
`docs/saturn/evidence/reports/asset-classifier-sm64.json`.

The wrapper is also directly runnable as
`tools/saturn/bootstrap-toolchain.sh`, or from Windows PowerShell as
`pwsh -File tools/saturn/bootstrap-toolchain.ps1`. Both follow the MIT-licensed
`yaul-org/libyaul-docker` layout at commit
`e0b4c2d63f1a39f213a67c6ca31e6bc582976de6`, using the published
`ijacquez/yaul:1.0.15` image by default. This tag corresponds to the pinned
Docker commit. Set `YAUL_DOCKER_IMAGE` to an
immutable digest in CI or in a lab notebook when byte-for-byte toolchain
provenance is required. The image is only the host environment; the script
still installs and checks this repository's libyaul commit `6012f79` before
building both targets. The host-side Python regression runs after the
container exits because the pinned Yaul image does not include Python. Set
`SATURN_HOST_PYTHON` when the host Python executable is not discoverable.

Docker is optional. The existing MSYS2/source-built workflow below remains the
fallback when Docker is unavailable or when the host needs a locally inspected
compiler build.

The Saturn mesh compiler uses hash-pinned NetworkX 3.6.1 for exact blossom
matching. Create the workspace-local host-tool environment once:

```sh
make -f Makefile.saturn.mk bootstrap-host-tools
```

PowerShell and POSIX entry points are also available as
`tools/saturn/bootstrap-host-tools.ps1` and
`tools/saturn/bootstrap-host-tools.sh`. The virtual environment is ignored;
`tools/saturn/requirements.txt` is the committed dependency lock. Run the
classifier, matching, controller, and telemetry regression suite with:

```sh
make -f Makefile.saturn.mk verify-tools
```

To regenerate the checked-out SM64 geometry/UV inventory and six-way fixture
report in one command:

```sh
make -f Makefile.saturn.mk classify-source
```

To run the source-derived intro face through the reusable Saturn mesh IR and
regenerate its C header plus both machine-readable reports:

```sh
make -f Makefile.saturn.mk compile-introface-mesh
```

The versioned source/compiled contracts and direct CLI usage are documented in
[`SATURN_MESH_IR.md`](SATURN_MESH_IR.md).

## Regenerating local Mario texture tiles

The Mario turntable's source texture tiles are derived from a user-supplied US
SM64 ROM/archive and intentionally remain under ignored `build/` output. Before
building the turntable after changing the UV baker, regenerate that header
explicitly with a native path appropriate for the host Python runtime:

```sh
make -f Makefile.saturn.mk compile-mario-textures \
  SM64_ROM='E:/ROM and ISO/n64/Super_Mario_64_(U)_[!].zip'
make -f Makefile.saturn.mk verify-marioturntable
```

`compile-mario-textures` also refreshes the source actor intake first. It does
not commit Nintendo-derived pixels; it only updates the ignored generated
header and the tracked conversion report.

### Texture scaling profiles

Both source-asset bakers retain the original ROM data as their canonical
input. They can box-filter RGB1555 source images by 1×, 2×, or 4× and emit
8×8, 16×16, or (for Mario experiments) 32×32 VDP1 tiles. Source scaling lowers
sampling resolution; tile size is the control that lowers final VDP1 texture
residency. For example:

```sh
make -f Makefile.saturn.mk compile-castle-textures \
  SM64_ROM='E:/ROM and ISO/n64/Super_Mario_64_(U)_[!].zip' \
  CASTLE_TILE=8 CASTLE_SOURCE_SCALE=2

make -f Makefile.saturn.mk compile-mario-textures \
  SM64_ROM='E:/ROM and ISO/n64/Super_Mario_64_(U)_[!].zip' \
  MARIO_TEXTURE_TILE=16 MARIO_TEXTURE_SOURCE_SCALE=1 \
  MARIO_TEXTURE_SUBDIVISION=1
```

The accepted Castle default is 8×8 with a 2× RGB1555 box filter. Mario stays
at 16×16/1× by default. Its performance profile uses one affine tile per
source triangle; `MARIO_TEXTURE_SUBDIVISION=4` retains the former close-camera
quality tier at four times the texture bytes and up to four times the commands.
Each conversion report records source bytes,
resampled-source bytes, emitted VDP1 bytes, filter, tile size, and scale.
The filter is an original small host implementation because the required
five-bit direct-color and binary-alpha contract is specific to this offline
VDP1 bake; importing a general image runtime would add maintenance and runtime
cost without improving the deterministic conversion.

With a locally installed Yaul SDK, the complete fallback verification gate is:

```sh
make -f Makefile.saturn.mk verify-all
```

On Windows, the repository's `tools/saturn/with-msys-toolchain.ps1` wrapper
preflights the MSYS2 runtime closure and prepends both `C:\msys64\usr\bin`
and `C:\msys64\mingw64\bin` (or `MSYS2_ROOT`) before launching Make, GCC,
objdump, or their helper processes. The verified user PATH should contain the
same two directories for desktop tools launched outside Make; restart existing
terminals/apps after changing PATH because Windows does not refresh an already
running process. The wrapper treats the public `mingw32-make` spelling as the
MSYS2 `usr\bin\make.exe` compatibility alias and requires GNU Make 4.3 or
newer, because sourceboot uses grouped targets; it never falls through to an
older Qt make from the inherited desktop PATH. Do not copy DLLs beside
individual executables or launch `sh-elf-*` tools from a bare PowerShell
environment.

The command uses `python3` by default. If the MSYS2 shell does not expose a
Python executable on `PATH`, set the repository's host Python explicitly, for
example:

```sh
PYTHON=/c/Users/estee/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe \
  make -f Makefile.saturn.mk verify-all
```

It runs the host tests, regenerates the classifier report, and verifies both
the hello and hardware-test ELF headers.

`verify-all` now also builds and runs `verify-runtime-contracts` (native host
tests for the Fast3D-to-VDP1 matrix/frontend pipeline) in the same `make`
invocation as the Yaul-cross-compiled targets. If `.yaul.env` is sourced in
that shell, its `COMPILER_PATH` is set to two colon-separated directories --
`$YAUL_INSTALL_ROOT/bin` and `$YAUL_INSTALL_ROOT/libexec/gcc/sh-elf/14.3.0`,
the latter being exactly where the SH-2 cross-compiler's own `cc1` lives --
and GCC's driver searches `COMPILER_PATH` ahead of its own exec-prefix, so
the native host compile step can pick up `cc1` (from that second directory)
and/or `as`/`ld` (from the first) belonging to the SH-2 cross toolchain
instead of the host's own — producing garbage compile/assembler errors that
have nothing to do with the actual C code. Run `unset COMPILER_PATH` after sourcing
`.yaul.env` and before invoking `make -f Makefile.saturn.mk verify-all` in the
same shell session; this does not affect the Yaul-cross-compiled targets,
which resolve their toolchain via `$(AS)`/`$(AR)`/`$(RANLIB)` and the
cross-compiler's own baked-in exec-prefix, not `COMPILER_PATH`.

Copy the pinned template rather than inventing a different environment layout:

```sh
cp third_party/libyaul/yaul.env.in .yaul.env
```

Edit `.yaul.env` with absolute MSYS paths. A checkout at
`C:\src\sm64-port`, for example, uses `/c/src/sm64-port/third_party/libyaul`
for `YAUL_BUILD_ROOT`. Do not commit `.yaul.env`; it is machine-specific.

Source it in each build shell:

```sh
source .yaul.env
```

### AI-agent shell sandboxes: two build gotchas

Two environment quirks were hit and confirmed while independently
reproducing a build from an AI-agent (Claude Code) Bash-tool sandbox on
Windows; both are sandbox/tooling artifacts, not codebase bugs, and are
recorded here so future agent-run review/build rounds don't re-diagnose
them from scratch.

1. **`export`-ed variables set mid-session do not reach child processes
   launched from a later tool call.** Each Bash-tool invocation runs in a
   fresh shell; only variables already present at shell start (`PATH`,
   `HOME`, etc.) are visible to a `make.exe`/`sh-elf-gcc.exe` launched from a
   *different* tool call than the one that exported them -- confirmed with a
   minimal `export FOO=bar` + child-process repro. Modifying an
   already-present variable (e.g. `export PATH="$YAUL_INSTALL_ROOT/bin:$PATH"`)
   *does* propagate, because `PATH` already existed; a brand-new variable
   name (`YAUL_INSTALL_ROOT`, `COMPILER_PATH`, `AS`, `AR`, `RANLIB`, ...)
   does not. Workaround: pass every new variable `.yaul.env` would otherwise
   export as an explicit `make VAR=value` command-line argument, in the same
   tool call that runs `make` -- not as a preceding `export`. Also keep the
   `.yaul.env` PATH ordering intact (yaul-install `bin/` before
   `mingw64/bin`): passing `COMPILER_PATH` with the wrong path-list
   delimiter, or reordering `PATH` incorrectly, both reproduce the same
   symptom as a missing cross-toolchain -- `as.exe: unrecognized option
   '-big'` -- because GCC's driver falls back to a bare, unprefixed `as`
   found via `PATH` search when its own search dirs don't resolve one, and
   picks up the host's native `as.exe` instead of the SH-2 cross
   assembler's.
2. **Very long recipe lines can fail through the recursive
   `make -C .../sourceboot verify` chain with a nonsensical error.**
   Sourceboot's final `.elf` link recipe passes ~230 object file paths on
   one command line; invoked through `make -f Makefile.saturn.mk
   verify-sourceboot`'s recursive `$(MAKE) -C sourceboot` chain under MSYS,
   this was observed to intermittently fail with `sh-elf-gcc: error: -E or
   -x required when input is from standard input` -- a message that
   normally means GCC received no input files and fell back to reading
   stdin. `make -n` confirms the recipe's *expanded* command line is
   well-formed (all ~230 `.o` paths present, ending in a normal `-o
   .../*.elf`); extracting that exact line with `make -n ... | grep
   '\.elf'` and running it directly (with `SOURCEBOOT_GENERATED_LDDIR` --
   normally `export`-ed by the sourceboot Makefile itself -- set explicitly,
   since a manual replay outside `make` does not inherit it) links cleanly.
   This points to a shell-handoff/argv-marshaling artifact in the recursive
   `make -C` chain on this MSYS setup, not a real command defect. Workaround
   when this specific failure is hit: `make -n` the failing target, extract
   its one expanded recipe line, and run it directly.

## Installing the pinned libyaul build

Install an SH-2 compiler first, then build and install the SDK libraries and
disc tools from the pinned submodule, not from a floating checkout. The hello
target does not use the M68K compiler; later SCSP sound-driver work will.

```sh
make -C third_party/libyaul install-release
make -C third_party/libyaul install-tools
```

Re-run those commands whenever the pinned libyaul revision or build mode
changes. The Saturn target consumes the installed files below
`YAUL_INSTALL_ROOT`; the submodule check prevents silent source-revision drift,
but cannot identify an unrelated SDK previously installed at the same prefix.

## Producing the hello disc

From the repository root:

```sh
make -f Makefile.saturn.mk hello
make -f Makefile.saturn.mk verify-hello
```

Expected outputs:

```text
build/saturn/hello/sm64-saturn-hello.cue
build/saturn/hello/sm64-saturn-hello.iso
build/saturn/hello/obj/sm64-saturn-hello.map
```

The disc should display:

```text
SM64 SATURN PORT

libyaul 0.3.1 / 6012f79
Phase 0: hello-disc bring-up
```

The target exports `SOURCE_DATE_EPOCH=1784160000` (2026-07-16 00:00:00 UTC)
and routes libyaul's ISO call through `tools/saturn/xorrisofs-reproducible`.
That small wrapper pins both the volume timestamp and every ISO node timestamp;
`SOURCE_DATE_EPOCH` alone leaves copied-file modification times intact.
Together they make otherwise identical clean ISO builds byte-for-byte stable.
Set `SATURN_XORRISOFS_REAL` if `xorrisofs` is not available on `PATH`.

Clean only this target with:

```sh
make -f Makefile.saturn.mk clean
```

## Verification gate

The hello-disc milestone is complete only after all of the following evidence
is recorded:

1. `make -f Makefile.saturn.mk check` confirms the pinned commit and version.
2. A clean MSYS2 shell builds the CUE/ISO using the documented command.
3. `make -f Makefile.saturn.mk verify-hello` confirms an ELF32, big-endian,
   SH-2 executable with entry point `0x06004000`.
4. The CUE boots and shows the expected text in two Saturn emulators.
5. The same image boots on retail Saturn hardware.

Steps 1-3 and one Yabause HLE emulator run passed on 2026-07-16; see
[`evidence/hello-disc-2026-07-16.md`](evidence/hello-disc-2026-07-16.md) and
[`evidence/yabause-hle-2026-07-16.md`](evidence/yabause-hle-2026-07-16.md).
The second-emulator and retail-hardware runs remain open. Emulator success is
development evidence; retail hardware remains authoritative.
