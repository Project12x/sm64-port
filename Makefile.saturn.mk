SATURN_REPO_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
LIBYAUL_DIR := $(SATURN_REPO_ROOT)/third_party/libyaul
HELLO_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hello
HWTEST_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hwtest
INTROFACE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/introface
MARIOTURNTABLE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/marioturntable
CASTLEVIEWER_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/castleviewer
VDP2_PROBE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/vdp2probe
PYTHON ?= python3
ifeq ($(OS),Windows_NT)
SATURN_TOOLS_PYTHON ?= $(SATURN_REPO_ROOT)/.venv-saturn-tools/Scripts/python.exe
else
SATURN_TOOLS_PYTHON ?= $(SATURN_REPO_ROOT)/.venv-saturn-tools/bin/python
endif
SM64_ROM ?=
CASTLE_TEXTURES ?= inside_09000000 inside_09001000 inside_09003800 inside_09004000 inside_09005000 inside_09008000 inside_09008800 inside_castle_seg7_texture_07000800 inside_castle_seg7_texture_07002000
CASTLE_TILE ?= 8
CASTLE_SOURCE_SCALE ?= 2
CASTLE_TEXTURE_FORMAT ?= clut16
CASTLE_SUBDIVISION ?= 1
CASTLE_SUBDIVISION_THRESHOLD ?= 0
CASTLE_TILE_BUDGET ?= 6000
CASTLE_ORDERING ?= bsp
MARIO_TEXTURE_TILE ?= 16
MARIO_TEXTURE_SOURCE_SCALE ?= 1

LIBYAUL_VERSION := 0.3.1
LIBYAUL_COMMIT := 6012f79f237773378c8014e70d8998ad95a38d98

.PHONY: all bootstrap bootstrap-host-tools check check-host-tools check-libyaul check-sdk hello verify-hello hwtest verify-hwtest introface verify-introface marioturntable verify-marioturntable castleviewer verify-castleviewer vdp2probe verify-vdp2probe verify-tools classify-source compile-introface-mesh compile-mario-actor compile-mario-textures compile-castle-area1 compile-castle-gameplay-config compile-castle-geo-root compile-castle-textures plan-castle-camera verify-all clean

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

castleviewer: compile-castle-area1 compile-castle-gameplay-config compile-castle-geo-root compile-castle-textures compile-mario-actor compile-mario-textures check-libyaul check-sdk
	$(MAKE) -C "$(CASTLEVIEWER_DIR)"

verify-castleviewer: castleviewer
	$(MAKE) -C "$(CASTLEVIEWER_DIR)" verify

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

verify-all: verify-tools classify-source verify-hello verify-hwtest

clean: check-sdk
	$(MAKE) -C "$(HELLO_DIR)" clean
