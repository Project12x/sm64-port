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
On Windows, use the MSYS2 MinGW 64-bit shell. The official libyaul README at
the pinned commit documents the YAUL package repository and toolchain setup.

Required environment variables are defined by libyaul's `yaul.env.in`:

- `YAUL_INSTALL_ROOT`: absolute toolchain/SDK installation path;
- `YAUL_ARCH_SH_PREFIX`: SH-2 target prefix, normally `sh2eb-elf`;
- `YAUL_PROG_SH_PREFIX`: executable prefix, empty when it matches the target
  prefix;
- `YAUL_ARCH_M68K_PREFIX`: SCSP 68K target prefix, normally `m68keb-elf`;
- `YAUL_BUILD_ROOT`: absolute path to this repository's
  `third_party/libyaul`; and
- `YAUL_BUILD`: libyaul's build-directory name, normally `build`.

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

Install the SH-2/M68K toolchains first using the official libyaul setup. Then
build and install the SDK libraries and disc tools from the pinned submodule,
not from a floating checkout:

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

Clean only this target with:

```sh
make -f Makefile.saturn.mk clean
```

## Verification gate

The hello-disc milestone is complete only after all of the following evidence
is recorded:

1. `make -f Makefile.saturn.mk check` confirms the pinned commit and version.
2. A clean MSYS2 shell builds the CUE/ISO using the documented command.
3. The CUE boots and shows the expected text in two Saturn emulators.
4. The same image boots on retail Saturn hardware.

Emulator success is development evidence; retail hardware remains authoritative.
