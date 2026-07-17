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
GCC source enters this repository. A one-command toolchain bootstrap remains
open work. On Linux or a Docker-capable host, the official MIT-licensed
`yaul-org/libyaul-docker` image is the preferred next reproducibility check.

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
pinned libyaul submodule and builds/verifies the hello disc inside the official
Yaul container layout:

```sh
make -f Makefile.saturn.mk bootstrap
```

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
building.

Docker is optional. The existing MSYS2/source-built workflow below remains the
fallback when Docker is unavailable or when the host needs a locally inspected
compiler build.

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
