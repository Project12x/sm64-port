SATURN_REPO_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
LIBYAUL_DIR := $(SATURN_REPO_ROOT)/third_party/libyaul
HELLO_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hello
HWTEST_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hwtest
INTROFACE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/introface
MARIOTURNTABLE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/marioturntable
CASTLEVIEWER_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/castleviewer
SOURCEBOOT_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/sourceboot
VDP2_PROBE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/vdp2probe
PYTHON ?= python3
HOST_CC ?= gcc
ifeq ($(OS),Windows_NT)
SATURN_TOOLS_PYTHON ?= $(SATURN_REPO_ROOT)/.venv-saturn-tools/Scripts/python.exe
HOST_EXEEXT := .exe
else
SATURN_TOOLS_PYTHON ?= $(SATURN_REPO_ROOT)/.venv-saturn-tools/bin/python
HOST_EXEEXT :=
endif
SM64_ROM ?=
CASTLE_TEXTURES ?= inside_09000000 inside_09001000 inside_09003800 inside_09004000 inside_09005000 inside_09008000 inside_09008800 inside_castle_seg7_texture_07000800 inside_castle_seg7_texture_07002000
CASTLE_TILE ?= 16
CASTLE_SOURCE_SCALE ?= 1
CASTLE_TEXTURE_FORMAT ?= clut16
CASTLE_SUBDIVISION ?= 1
CASTLE_SUBDIVISION_THRESHOLD ?= 1024
CASTLE_TILE_BUDGET ?= 6000
CASTLE_ORDERING ?= bsp
MARIO_TEXTURE_TILE ?= 16
MARIO_TEXTURE_SOURCE_SCALE ?= 1
MARIO_TEXTURE_SUBDIVISION ?= 1

# Where compile-quad-map writes. Defaults to the sourceboot build's generated
# directory, the same place src/port/saturn/sourceboot/Makefile puts
# mario_anim_data.c; that Makefile overrides this when it delegates here.
QUAD_MAP_GENERATED ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated
# The models the quad map is compiled for, as --actor GEO MODEL LAYOUT triples.
# Mario enters at mario_geo_body, not mario_geo: the latter's GEO_SHADOW and
# GEO_RENDER_RANGE nodes are unmodelled and poison every triangle below them.
# The two cannon pieces are static geometry and need no extra plumbing --
# same walk, no animation envelope to validate. BOB's terrain is deliberately
# absent: every one of its 1101 triangles is textured, and textured triangles
# are never paired, so its map is empty.
#
# Keep this list in step with SOURCEBOOT_QUAD_MAP_SOURCES in
# src/port/saturn/sourceboot/Makefile, which carries the same files as
# prerequisites so the map is regenerated when the geometry changes.
QUAD_MAP_ACTOR_ARGS := \
  --actor "actors/mario/geo.inc.c" "actors/mario/model.inc.c" "mario_geo_body" \
  --actor "actors/cannon_barrel/geo.inc.c" "actors/cannon_barrel/model.inc.c" "cannon_barrel_geo" \
  --actor "actors/cannon_base/geo.inc.c" "actors/cannon_base/model.inc.c" "cannon_base_geo"

LIBYAUL_VERSION := 0.3.1
LIBYAUL_COMMIT := 6012f79f237773378c8014e70d8998ad95a38d98

.PHONY: all bootstrap bootstrap-host-tools check check-host-tools check-libyaul check-sdk hello verify-hello hwtest verify-hwtest introface verify-introface marioturntable verify-marioturntable castleviewer verify-castleviewer sourceboot verify-sourceboot vdp2probe verify-vdp2probe verify-tools verify-runtime-contracts verify-ir-transform verify-mtxf-lookat-host-diff verify-mtxq-ctors verify-softfp-bitexact classify-source compile-introface-mesh compile-mario-actor compile-mario-textures compile-castle-area1 compile-castle-gameplay-config compile-castle-geo-root compile-castle-textures compile-castle-collision compile-quad-map plan-castle-camera verify-all clean

all: hello

bootstrap:
	@sh "$(SATURN_REPO_ROOT)/tools/saturn/bootstrap-toolchain.sh"

bootstrap-host-tools:
ifeq ($(OS),Windows_NT)
	@powershell -ExecutionPolicy Bypass -File "$(SATURN_REPO_ROOT)/tools/saturn/bootstrap-host-tools.ps1" -Python "$(PYTHON)"
else
	@PYTHON="$(PYTHON)" sh "$(SATURN_REPO_ROOT)/tools/saturn/bootstrap-host-tools.sh"
endif

check: check-libyaul

check-libyaul:
	@if [ ! -e "$(LIBYAUL_DIR)/.git" ]; then \
		printf '%s\n' \
		  'libyaul is not initialized.' \
		  'Run: git submodule update --init third_party/libyaul' >&2; \
		exit 1; \
	fi
	@actual_commit=$$(git -c safe.directory="$(LIBYAUL_DIR)" \
	  -C "$(LIBYAUL_DIR)" rev-parse HEAD); \
	if [ "$$actual_commit" != "$(LIBYAUL_COMMIT)" ]; then \
		printf 'libyaul commit mismatch: expected %s, found %s\n' \
		  '$(LIBYAUL_COMMIT)' "$$actual_commit" >&2; \
		exit 1; \
	fi
	@actual_version=$$(git -c safe.directory="$(LIBYAUL_DIR)" \
	  -C "$(LIBYAUL_DIR)" show HEAD:VERSION); \
	if [ "$$actual_version" != "$(LIBYAUL_VERSION)" ]; then \
		printf 'libyaul version mismatch: expected %s, found %s\n' \
		  '$(LIBYAUL_VERSION)' "$$actual_version" >&2; \
		exit 1; \
	fi
	@printf 'libyaul %s pinned at %s\n' \
	  '$(LIBYAUL_VERSION)' '$(LIBYAUL_COMMIT)'

check-sdk:
	@if [ -z "$(YAUL_INSTALL_ROOT)" ]; then \
		printf '%s\n' \
		  'YAUL_INSTALL_ROOT is not set.' \
		  'Source your .yaul.env, then retry the build.' \
		  'See docs/saturn/BUILDING.md.' >&2; \
		exit 1; \
	fi
	@if [ ! -f "$(YAUL_INSTALL_ROOT)/share/build.pre.mk" ] || \
	   [ ! -f "$(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk" ]; then \
		printf 'No installed libyaul build fragments found under %s/share\n' \
		  '$(YAUL_INSTALL_ROOT)' >&2; \
		printf '%s\n' 'Install the pinned submodule as described in docs/saturn/BUILDING.md.' >&2; \
		exit 1; \
	fi

hello: check-libyaul check-sdk
	$(MAKE) -C "$(HELLO_DIR)"

verify-hello: check-libyaul check-sdk
	$(MAKE) -C "$(HELLO_DIR)" verify

hwtest: check-libyaul check-sdk
	$(MAKE) -C "$(HWTEST_DIR)"

verify-hwtest: check-libyaul check-sdk
	$(MAKE) -C "$(HWTEST_DIR)" verify

introface: check-libyaul check-sdk
	$(MAKE) -C "$(INTROFACE_DIR)"

verify-introface: check-libyaul check-sdk
	$(MAKE) -C "$(INTROFACE_DIR)" verify

marioturntable: compile-mario-actor check-libyaul check-sdk
	$(MAKE) -C "$(MARIOTURNTABLE_DIR)"

verify-marioturntable: marioturntable
	$(MAKE) -C "$(MARIOTURNTABLE_DIR)" verify

castleviewer: compile-castle-area1 compile-castle-gameplay-config compile-castle-geo-root compile-castle-textures compile-castle-collision compile-mario-actor compile-mario-textures check-libyaul check-sdk
	$(MAKE) -C "$(CASTLEVIEWER_DIR)"

verify-castleviewer: castleviewer
	$(MAKE) -C "$(CASTLEVIEWER_DIR)" verify

sourceboot: check-libyaul check-sdk
	$(MAKE) -C "$(SOURCEBOOT_DIR)" source-assets
	$(MAKE) -C "$(SOURCEBOOT_DIR)"

verify-sourceboot: sourceboot
	$(MAKE) -C "$(SOURCEBOOT_DIR)" verify

vdp2probe: check-libyaul check-sdk
	$(MAKE) -C "$(VDP2_PROBE_DIR)"

verify-vdp2probe: check-libyaul check-sdk
	$(MAKE) -C "$(VDP2_PROBE_DIR)" verify

check-host-tools:
	@if [ ! -x "$(SATURN_TOOLS_PYTHON)" ]; then \
		printf '%s\n' 'Saturn host-tool environment is missing.' \
		  'Run: make -f Makefile.saturn.mk bootstrap-host-tools' >&2; \
		exit 1; \
	fi

verify-tools: check-host-tools
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_tools.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_gen_trig_q16.py"

# Depends on compile-quad-map because saturn_fast3d_frontend.c now includes
# the GENERATED saturn_quad_map.h -- the host test compiles that real
# translation unit, so it needs the real generated header, not a hand-written
# stand-in that could drift from the encoding the target actually consumes.
# The generator is ~0.8 s and writes only into build/, so making it a
# prerequisite costs nothing and removes the "works only after a cross-build"
# ordering trap. The test supplies its own sm64_saturn_quad_map_lists[]
# definition (the generated .c cannot link on the host -- it references real
# actor display-list symbols), which is what lets it drive the merge path
# with synthetic maps.
verify-runtime-contracts: compile-quad-map
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  -I"$(QUAD_MAP_GENERATED)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/runtime_contract_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_fast3d_frontend.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"

# Task 1 shared transform contract. This intentionally links the actual
# extracted module, rather than reproducing its math in a test-only helper;
# the same immutable job record is therefore checked independently of either
# Saturn consumer.
verify-ir-transform:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/ir_transform_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_ir_transform.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/ir-transform-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/ir-transform-test$(HOST_EXEEXT)"

# Task 2's fixed-point projection differential gate.  The corpus begins with
# real Bob-omb Battlefield source vertices and is intentionally separate from
# the broad runtime-contract executable so a precision regression reports a
# focused failure.  Target-captured route records extend this corpus; see the
# test header for the current provenance boundary.
verify-fast3d-q16-diff:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/fast3d_q16_diff_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/fast3d-q16-diff-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/fast3d-q16-diff-test$(HOST_EXEEXT)"

# One-off host-vs-target differential diagnostic, not a standing contract
# test: compiles the REAL, unmodified src/engine/math_util.c (mtxf_lookat,
# mtxf_mul) with the host compiler and feeds it the exact real camera
# pos/focus captured live at the corrupted gMatStack[1] frame (see
# docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md),
# swept across the full s16 roll range. Deliberately NOT part of
# `verify-all` -- this tests vanilla SM64 engine math (protected,
# unmodified code per ENGINE_PORT_ARCHITECTURE.md), not this port's own
# platform contracts, and exists to answer one diagnostic question rather
# than gate every build.
verify-mtxf-lookat-host-diff:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/mtxf_lookat_host_diff_test.c" \
	  "$(SATURN_REPO_ROOT)/src/engine/math_util.c" \
	  -lm \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxf-lookat-host-diff-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxf-lookat-host-diff-test$(HOST_EXEEXT)"

# Host differential test for Task 3's Q16.16 render-matrix constructors
# (saturn_matrix_ctors.h) against the REAL, unmodified src/engine/
# math_util.c mtxf_* originals. Same shape as verify-mtxf-lookat-host-diff
# immediately above (and same reasoning for staying a standalone target,
# not folded into verify-all/verify-runtime-contracts: this tests vanilla
# SM64 engine math against this port's fixed-point mirror, not this
# port's platform contracts in isolation). Needs one extra source over
# the lookat-only precedent: saturn_trig_q16.inc.c, so the test's Q16.16
# constructors have gSaturnSineTableQ16 to link against.
verify-mtxq-ctors:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/mtxq_ctor_diff_test.c" \
	  "$(SATURN_REPO_ROOT)/src/engine/math_util.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -lm \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-ctors-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-ctors-test$(HOST_EXEEXT)"

# Bit-exactness gate for the soft-float replacement. sourceboot links
# third_party/gcc-soft-fp (built into libsm64softfp.a) ahead of libgcc, taking
# every f32/f64 operation in the SM64 engine off libgcc's fp-bit.c. The entire
# reason that is allowed to happen with zero engine edits is that soft-fp
# preserves IEEE-754 semantics exactly -- so this compiles the SAME vendored
# sources, with the SAME libgcc/config/sh/sfp-machine.h, for the host, and
# diffs every routine against the host's own FPU bit for bit. See the test's
# header comment for the two places IEEE-754 stops specifying an answer and
# how each is pinned exactly rather than skipped.
#
# Deliberately NOT folded into verify-all, on the same reasoning as
# verify-mtxq-ctors and verify-mtxf-lookat-host-diff above: it is a standalone
# differential diagnostic rather than a per-build platform contract, and at the
# default volume it runs for about twelve minutes. The per-build gate for this
# change is in src/port/saturn/sourceboot/Makefile's `verify`, which asserts on
# the linked ELF that fp-bit is gone.
#
# HOST COMPILER: sfp-machine.h pairs `_FP_W_TYPE_SIZE 32` with
# `_FP_W_TYPE unsigned long`, which is true on sh-elf and on MSYS2's mingw64
# gcc (LLP64) but NOT on its cygwin gcc (LP64, 64-bit long). Building soft-fp
# with mismatched widths would silently test a different configuration from
# the one that ships, so the default prefers mingw64 when it is present and
# the test carries a _Static_assert that refuses the wrong one outright.
# Override SOFTFP_HOST_CC to use a different 32-bit-long compiler.
SOFTFP_HOST_CC ?= $(firstword $(wildcard /mingw64/bin/gcc.exe C:/msys64/mingw64/bin/gcc.exe) $(CC))
SOFTFP_VENDOR := $(SATURN_REPO_ROOT)/third_party/gcc-soft-fp
SOFTFP_HOST_BUILD := $(SATURN_REPO_ROOT)/build/saturn/host-tests/softfp
# Same list as SOFTFP_FUNCS in src/port/saturn/sourceboot/Makefile: the test is
# worth nothing unless it covers exactly what the target links. Keep in step.
SOFTFP_TEST_FUNCS := \
  addsf3 subsf3 mulsf3 divsf3 negsf2 eqsf2 gesf2 lesf2 unordsf2 \
  fixsfsi fixunssfsi floatsisf floatunsisf \
  fixsfdi fixunssfdi floatdisf floatundisf \
  adddf3 subdf3 muldf3 divdf3 negdf2 eqdf2 gedf2 ledf2 unorddf2 \
  fixdfsi fixunsdfsi floatsidf floatunsidf \
  fixdfdi fixunsdfdi floatdidf floatundidf \
  extendsfdf2 truncdfsf2
# Sample volume. The defaults are the full bar from the design note: >=1e7
# seeded random pairs per binary operation, and a stride of 1 -- i.e. all
# 2^32 inputs, exhaustively, for every single-argument routine. That is about
# 12 minutes and ~43 billion bit comparisons. Raise the stride for a quick
# smoke run while iterating; do not lower the bar to make a failure go away.
#   make -f Makefile.saturn.mk verify-softfp-bitexact SOFTFP_TEST_PAIRS=200000 SOFTFP_TEST_STRIDE=512
SOFTFP_TEST_PAIRS ?= 12000000
SOFTFP_TEST_STRIDE ?= 1

verify-softfp-bitexact:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SOFTFP_HOST_BUILD)').mkdir(parents=True, exist_ok=True)"
# A native-Windows mingw64 gcc launched from an MSYS2 shell inherits neither a
# usable TMPDIR (MSYS2 rewrites it on the way across, and the recipe shell's
# TMP/TEMP may be empty) nor a writable fallback -- it lands on C:\WINDOWS and
# dies. Point TMP/TEMP at the build directory instead, in whatever spelling the
# platform wants: `pwd -W` gives the Windows form under MSYS2 and fails
# harmlessly everywhere else, where plain `pwd` is already correct.
	@tmp="$$(cd '$(SOFTFP_HOST_BUILD)' && { pwd -W 2>/dev/null || pwd; })"; \
	export TMP="$$tmp" TEMP="$$tmp" TMPDIR="$$tmp"; \
	for f in $(SOFTFP_TEST_FUNCS); do \
	  "$(SOFTFP_HOST_CC)" -O2 -std=gnu11 -w -c "$(SOFTFP_VENDOR)/soft-fp/$$f.c" \
	    -I"$(SOFTFP_VENDOR)/soft-fp" -I"$(SOFTFP_VENDOR)/config/sh" -I"$(SOFTFP_VENDOR)/include" \
	    -o "$(SOFTFP_HOST_BUILD)/$$f.o" || exit 1; \
	done; \
	"$(SOFTFP_HOST_CC)" -O2 -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
	  "$(SATURN_REPO_ROOT)/tools/saturn/softfp_bitexact_diff_test.c" \
	  $(patsubst %,"$(SOFTFP_HOST_BUILD)/%.o",$(SOFTFP_TEST_FUNCS)) \
	  -lm \
	  -o "$(SOFTFP_HOST_BUILD)/softfp-bitexact-test$(HOST_EXEEXT)"
	"$(SOFTFP_HOST_BUILD)/softfp-bitexact-test$(HOST_EXEEXT)" $(SOFTFP_TEST_PAIRS) $(SOFTFP_TEST_STRIDE)

classify-source: check-host-tools
	@mkdir -p "$(SATURN_REPO_ROOT)/docs/saturn/evidence/reports"
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/asset_classifier.py" \
	  --root . \
	  --primitives "tools/saturn/fixtures/primitives-six-way.json" \
	  --report "docs/saturn/evidence/reports/asset-classifier-sm64.json"

compile-introface-mesh: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/extract_introface_mesh.py" \
	  --input "src/goddard/dynlists/dynlist_mario_face.c" \
	  --eyes-input "src/goddard/dynlists/dynlists_mario_eyes.c" \
	  --features-input "src/goddard/dynlists/dynlists_mario_eyebrows_mustache.c" \
	  --master-input "src/goddard/dynlists/dynlist_mario_master.c" \
	  --animation-input "src/goddard/dynlists/anim_group_2.c" \
	  --output "src/port/saturn/introface/mario_face_mesh.h" \
	  --quad-report "docs/saturn/evidence/reports/introface-quad-pairing.json" \
	  --mesh-ir-output "docs/saturn/evidence/reports/introface-mesh-ir.json"

compile-mario-actor: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/extract_mario_actor.py" \
	  --model "actors/mario/model.inc.c" \
	  --geo "actors/mario/geo.inc.c" \
	  --animation "assets/anims/anim_C5.inc.c" \
	  --walking-animation "assets/anims/anim_48.inc.c" \
	  --animation-frame 0 \
	  --output "src/port/saturn/marioturntable/mario_actor_mesh.h" \
	  --report "docs/saturn/evidence/reports/mario-actor-intake.json" \
	  --mesh-ir-output "docs/saturn/evidence/reports/mario-actor-mesh-ir.json"

compile-mario-textures: compile-mario-actor check-host-tools
	@if [ -z "$(SM64_ROM)" ]; then \
	  printf '%s\n' 'SM64_ROM must name the user-supplied US ROM/archive for local-only Mario texture conversion.' >&2; \
	  exit 1; \
	fi
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/bake_mario_eye_uv.py" \
	  --rom "$(SM64_ROM)" \
	  --assets "assets.json" \
	  --intake "docs/saturn/evidence/reports/mario-actor-intake.json" \
	  --tile "$(MARIO_TEXTURE_TILE)" \
	  --source-scale "$(MARIO_TEXTURE_SOURCE_SCALE)" \
	  --subdivision "$(MARIO_TEXTURE_SUBDIVISION)" \
	  --output "build/saturn/marioturntable/generated/mario_eye_uv_tiles.h" \
	  --report "docs/saturn/evidence/reports/mario-eye-uv-bake.json"

compile-castle-area1: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/compile_castle_area.py" \
	  --area "levels/castle_inside/areas/1" \
	  --output "build/saturn/castlearea/generated/castle_area1.h" \
	  --report "docs/saturn/evidence/reports/castle-area1-source-root-ir-2026-07-18.json"

compile-castle-gameplay-config: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/extract_castle_gameplay_config.py" \
	  --script "levels/castle_inside/script.c" \
	  --collision "levels/castle_inside/areas/1/collision.inc.c" \
	  --camera "src/game/camera.c" \
	  --output "build/saturn/castlearea/generated/castle_gameplay_config.h" \
	  --report "docs/saturn/evidence/reports/castle-area1-gameplay-config-2026-07-18.json"

compile-castle-geo-root: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/extract_castle_geo_root.py" \
	  --input "levels/castle_inside/areas/1/geo.inc.c" \
	  --symbol "castle_geo_000F30" \
	  --output "build/saturn/castlearea/generated/castle_geo_root.h"

plan-castle-camera: compile-castle-area1 check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/plan_castle_camera_coverage.py" \
	  --intake "docs/saturn/evidence/reports/castle-area1-source-root-ir-2026-07-18.json" \
	  --output "docs/saturn/evidence/reports/castle-area1-fixed-camera-coverage-2026-07-18.json"

compile-castle-textures: compile-castle-area1 check-host-tools
	@if [ -z "$(SM64_ROM)" ]; then \
	  printf '%s\n' 'SM64_ROM must name the user-supplied US ROM/archive for local-only Castle texture conversion.' >&2; \
	  exit 1; \
	fi
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/bake_castle_uv.py" \
	  --rom "$(SM64_ROM)" \
	  --assets "assets.json" \
	  --intake "docs/saturn/evidence/reports/castle-area1-source-root-ir-2026-07-18.json" \
	  $(foreach texture,$(CASTLE_TEXTURES),--texture "$(texture)") \
	  --tile "$(CASTLE_TILE)" \
	  --source-scale "$(CASTLE_SOURCE_SCALE)" \
	  --texture-format "$(CASTLE_TEXTURE_FORMAT)" \
	  --subdivision "$(CASTLE_SUBDIVISION)" \
	  --subdivision-threshold "$(CASTLE_SUBDIVISION_THRESHOLD)" \
	  --max-tiles "$(CASTLE_TILE_BUDGET)" \
	  --ordering "$(CASTLE_ORDERING)" \
	  --output "build/saturn/castlearea/generated/castle_uv_tiles.h" \
	  --report "docs/saturn/evidence/reports/castle-area1-all-materials-bake-2026-07-18.json"

# Which triangle pairs VDP1 may draw as one four-corner command. Emits a
# generated header (the encoding) and a generated C table (the data), both
# marked generated, into $(QUAD_MAP_GENERATED). The generated source is linked
# into the sourceboot image; see SH_SRCS in
# src/port/saturn/sourceboot/Makefile, which is also where the file-level rule
# with real prerequisites lives -- it delegates back here so this recipe stays
# the single definition of what gets compiled.
compile-quad-map: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/quad_map.py" \
	  $(QUAD_MAP_ACTOR_ARGS) \
	  --emit-h "$(QUAD_MAP_GENERATED)/saturn_quad_map.h" \
	  --emit-c "$(QUAD_MAP_GENERATED)/saturn_quad_map.c" \
	  --report "$(QUAD_MAP_GENERATED)/saturn_quad_map.json"

compile-castle-collision: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/compile_castle_collision.py" \
	  --source "levels/castle_inside/areas/1/collision.inc.c" \
	  --surface-header "include/surface_terrains.h" \
	  --output "build/saturn/castlearea/generated/castle_collision.h" \
	  --report "docs/saturn/evidence/reports/castle-area1-collision-bank-2026-07-19.json"

verify-all: verify-tools verify-runtime-contracts verify-ir-transform classify-source verify-hello verify-hwtest

clean: check-sdk
	$(MAKE) -C "$(HELLO_DIR)" clean
