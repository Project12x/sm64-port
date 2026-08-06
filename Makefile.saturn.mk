SATURN_REPO_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
LIBYAUL_DIR := $(SATURN_REPO_ROOT)/third_party/libyaul
HELLO_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hello
HWTEST_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/hwtest
INTROFACE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/introface
MARIOTURNTABLE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/marioturntable
CASTLEVIEWER_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/castleviewer
SOURCEBOOT_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/sourceboot
VDP2_PROBE_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/vdp2probe
DUAL_TRANSFORM_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/dualtransform
PCM68K_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/audio68k
PONESOUND_ROOT ?= $(firstword $(realpath \
	$(SATURN_REPO_ROOT)/work/upstream/SCSP_poneSound \
	$(SATURN_REPO_ROOT)/../../work/upstream/SCSP_poneSound))
M68K_BINDIR ?= $(PONESOUND_ROOT)/m68k-elf
TASK17_AUDIO68K_BUILD := $(SATURN_REPO_ROOT)/build/saturn/audio68k
TASK17_AUDIO68K_ARTIFACT := $(TASK17_AUDIO68K_BUILD)/task17-audio-modules.o
TASK17_AUDIO68K_OBJECTS := \
	$(TASK17_AUDIO68K_BUILD)/audio_engine.o \
	$(TASK17_AUDIO68K_BUILD)/voice_allocator.o \
	$(TASK17_AUDIO68K_BUILD)/desired_voice.o \
	$(TASK17_AUDIO68K_BUILD)/slot_shadow.o \
	$(TASK17_AUDIO68K_BUILD)/scsp_timer.o \
	$(TASK17_AUDIO68K_BUILD)/audio_freestanding.o
SOUNDTEST_DIR := $(SATURN_REPO_ROOT)/src/port/saturn/soundtest
PCM_PROOF_GENERATED := $(SATURN_REPO_ROOT)/build/saturn/soundtest/generated
AUDIO_GENERATED := $(SATURN_REPO_ROOT)/build/saturn/audio/generated
MARIO_ACTOR_BANK_DIR := $(SATURN_REPO_ROOT)/build/saturn/actors/mario
MARIO_ACTOR_BANK := $(MARIO_ACTOR_BANK_DIR)/mario.s64b
MARIO_ACTOR_BANK_REPORT := $(MARIO_ACTOR_BANK_DIR)/mario-actor-bank.json
ACTOR_FAMILY_BANK_DIR ?= $(SATURN_REPO_ROOT)/build/saturn/packages/$(SCENE_LEVEL)/$(SCENE_AREA)/actors
ACTOR_FAMILY_BANK_REPORT ?= $(ACTOR_FAMILY_BANK_DIR)/actor-families.json
PYTHON ?= python3
HOST_CC ?= gcc
ifeq ($(OS),Windows_NT)
SATURN_TOOLS_PYTHON ?= $(SATURN_REPO_ROOT)/.venv-saturn-tools/Scripts/python.exe
HOST_CC_ENV ?=
HOST_EXEEXT := .exe
else
SATURN_TOOLS_PYTHON ?= $(SATURN_REPO_ROOT)/.venv-saturn-tools/bin/python
HOST_CC_ENV ?= env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS
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
BOB_MESH_GENERATED ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated
BOB_ASSET_ROOT ?= $(SATURN_REPO_ROOT)
BOB_AREA_DIR ?= levels/bob/areas/1
BOB_TILES_GENERATED ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated
BOB_SKY_SOURCE ?= textures/skyboxes/water.png
BOB_SKY_OUTPUT ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated/bob_sky_rgb1555.bin
BOB_SKY_MANIFEST ?= $(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated/bob_sky_manifest.json
SCENE_LEVEL ?= bob
SCENE_AREA ?= 1
SCENE_CLOSURE_OUTPUT ?= $(SATURN_REPO_ROOT)/build/saturn/packages/$(SCENE_LEVEL)/$(SCENE_AREA)/closure.json
SCENE_LEVEL_ID ?= 9
SCENE_PACKAGE_PROVISIONAL_DIR ?= $(SATURN_REPO_ROOT)/build/saturn/packages/$(SCENE_LEVEL)/$(SCENE_AREA)/provisional
SCENE_PACKAGE_PROVISIONAL_ROOT ?= $(SCENE_PACKAGE_PROVISIONAL_DIR)/scene.s64p
SCENE_PACKAGE_PROVISIONAL_REPORT ?= $(SCENE_PACKAGE_PROVISIONAL_DIR)/scene-package-report.json
SCENE_PACKAGE_PROVISIONAL_VALIDATION ?= $(SCENE_PACKAGE_PROVISIONAL_DIR)/scene-package-validation.json
SCENE_PACKAGE_PROVISIONAL_HEADER ?= $(SCENE_PACKAGE_PROVISIONAL_DIR)/scene_package.h
SCENE_PACKAGE_ABI_HEADER ?= $(SATURN_REPO_ROOT)/build/saturn/packages/saturn_scene_package_abi.h
SCENE_PACKAGE_WORLD_STATIC ?= $(BOB_MESH_GENERATED)/bob_area1_compiled.json
SCENE_PACKAGE_COLLISION ?= $(SATURN_REPO_ROOT)/levels/$(SCENE_LEVEL)/areas/$(SCENE_AREA)/collision.inc.c
SCENE_PACKAGE_SKY_BACKGROUND ?= $(BOB_SKY_OUTPUT)
SCENE_PACKAGE_BSP_PORTAL ?= $(BOB_MESH_GENERATED)/bob_area1_bsp_report.json
# The models the quad map is compiled for, as --actor GEO MODEL LAYOUT triples.
# Mario enters at mario_geo_body, not mario_geo: the latter's GEO_SHADOW and
# GEO_RENDER_RANGE nodes are unmodelled and poison every triangle below them.
# The two cannon pieces are static geometry and need no extra plumbing --
# same walk, no animation envelope to validate. BOB terrain is generated by
# compile-bob-area into the shared Mesh IR v2 bank; it is not part of the
# actor quad-map because its static scene has its own source identity.
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

.PHONY: all bootstrap bootstrap-host-tools check check-host-tools check-libyaul check-sdk hello verify-hello hwtest verify-hwtest introface verify-introface marioturntable verify-marioturntable castleviewer verify-castleviewer sourceboot verify-sourceboot verify-sourceboot-feature-identity vdp2probe verify-vdp2probe dual-transform verify-dual-transform pcm68k-image verify-pcm68k-image verify-audio68k-modules compile-pcm-proof-bank soundtest verify-soundtest verify-tools verify-runtime-contracts verify-source-render-policy verify-source-geo-state-diff verify-runtime-camera-contract verify-sourceboot-presentation-boundary verify-sourceboot-boot-trace verify-vdp2-frame verify-pcm-protocol verify-audio-protocol-v2 verify-audio-policy verify-audio-spatial verify-audio-residency compile-saturn-audio verify-pcm-transport verify-pcm68k-model verify-scsp-pcm8 verify-pcm68k-heartbeat-host verify-soundtest-boot verify-sequence-vm verify-audio-voice-allocator verify-audio-slot-shadow verify-audio-scsp-timer verify-terrain-command-template verify-terrain-command-template-target-compile verify-terrain-depth-bins verify-terrain-command-stream verify-terrain-clip verify-ztreme-frustum verify-bob-bsp-header verify-visible-position-set verify-render-clusters verify-scene-admission verify-portal-windows verify-render-snapshot-bank verify-dual-frame-bank verify-frame-pipeline verify-render-overlap-integration verify-demo-render-overlap verify-vdp1-frame-bank verify-vdp1-transfer-pipeline verify-gouraud-transfer verify-actor-pose-bank verify-actor-meshlets verify-actor-family-bank verify-actor-instance-queue verify-actor-batches verify-dma-queue verify-ir-transform verify-render-native-math verify-render-native-math-mutation verify-hot-promotion verify-mtxf-lookat-host-diff verify-mtxq-ctors verify-mtxq-ctors-mutation verify-graph-q16-contract verify-mtxq-conversion-assembly verify-softfp-bitexact verify-render-callback-context verify-scene-package-schema classify-source compile-introface-mesh compile-mario-actor-bank compile-actor-banks compile-mario-actor compile-mario-textures compile-castle-area1 compile-castle-gameplay-config compile-castle-geo-root compile-castle-textures compile-castle-collision compile-quad-map compile-scene-closure compile-provisional-scene-package compile-bob-area compile-bob-bsp compile-bob-bsp-fragments compile-bob-tiles compile-bob-scene compile-bob-sky plan-castle-camera verify-all clean

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

verify-sourceboot-feature-identity:
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_gen_build_identity.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_sourceboot_feature_identity.py"

vdp2probe: check-libyaul check-sdk
	$(MAKE) -C "$(VDP2_PROBE_DIR)"

verify-vdp2probe: check-libyaul check-sdk
	$(MAKE) -C "$(VDP2_PROBE_DIR)" verify

dual-transform: check-libyaul check-sdk
	$(MAKE) -C "$(DUAL_TRANSFORM_DIR)"

verify-dual-transform: check-libyaul check-sdk
	$(MAKE) -C "$(DUAL_TRANSFORM_DIR)" verify

check-host-tools:
	@if [ ! -x "$(SATURN_TOOLS_PYTHON)" ]; then \
		printf '%s\n' 'Saturn host-tool environment is missing.' \
		  'Run: make -f Makefile.saturn.mk bootstrap-host-tools' >&2; \
		exit 1; \
	fi

verify-tools: check-host-tools
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_tools.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_gen_trig_q16.py"

verify-pcm-protocol:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/pcm_protocol_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm-protocol-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm-protocol-test$(HOST_EXEEXT)"

verify-audio-protocol-v2:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/audio_protocol_v2_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-protocol-v2-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-protocol-v2-test$(HOST_EXEEXT)"

verify-audio-policy:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/audio_policy_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/audio/saturn_audio_policy.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/audio/saturn_audio_spatial.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-policy-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-policy-test$(HOST_EXEEXT)'], check=True)"

verify-audio-spatial:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/audio_spatial_diff_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/audio/saturn_audio_spatial.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-spatial-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-spatial-test$(HOST_EXEEXT)'], check=True)"

compile-saturn-audio: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" tools/saturn/compile_saturn_audio.py \
		--root "$(SATURN_REPO_ROOT)" --output-dir "$(AUDIO_GENERATED)"

verify-audio-residency: compile-saturn-audio
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
		-I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
		-I"$(PCM68K_DIR)" \
		"$(SATURN_REPO_ROOT)/tools/saturn/audio_residency_test.c" \
		"$(SATURN_REPO_ROOT)/src/port/saturn/audio/saturn_audio_package.c" \
		"$(PCM68K_DIR)/audio_package.c" \
		-o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-residency-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-residency-test$(HOST_EXEEXT)', r'$(AUDIO_GENERATED)/AUDIO.DAT'], check=True)"
	@"$(SATURN_TOOLS_PYTHON)" -c "import hashlib, pathlib, shutil, subprocess, tempfile; root=pathlib.Path(r'$(SATURN_REPO_ROOT)'); py=r'$(SATURN_TOOLS_PYTHON)'; a=pathlib.Path(tempfile.mkdtemp(prefix='s64a-a-')); b=pathlib.Path(tempfile.mkdtemp(prefix='s64a-b-')); subprocess.run([py, str(root/'tools/saturn/compile_saturn_audio.py'), '--root', str(root), '--output-dir', str(a)], check=True); subprocess.run([py, str(root/'tools/saturn/compile_saturn_audio.py'), '--root', str(root), '--output-dir', str(b)], check=True); names=['AUDIO.DAT','audio_manifest.json','bob_audio_closure.json','wf_audio_closure.json']; assert all(hashlib.sha256((a/n).read_bytes()).digest()==hashlib.sha256((b/n).read_bytes()).digest() for n in names); print('audio deterministic hashes: PASS')"

verify-pcm-transport:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/pcm_transport_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/audio/saturn_pcm_transport.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm-transport-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm-transport-test$(HOST_EXEEXT)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/pcm_publication_order_test.c" \
	  "$(PCM68K_DIR)/scsp_pcm8.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm-publication-order-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm-publication-order-test$(HOST_EXEEXT)"

verify-pcm68k-model:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/pcm68k_model_test.c" \
	  "$(PCM68K_DIR)/pcm_voice.c" \
	  "$(PCM68K_DIR)/scsp_pcm8.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm68k-model-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm68k-model-test$(HOST_EXEEXT)"

verify-scsp-pcm8:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/scsp_pcm8_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/scsp-pcm8-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/scsp-pcm8-test$(HOST_EXEEXT)'], check=True)"

verify-soundtest-boot:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" -I"$(SOUNDTEST_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/soundtest_boot_contract_test.c" \
	  "$(SOUNDTEST_DIR)/soundtest_boot.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/audio/saturn_pcm_transport.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/soundtest-boot-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/soundtest-boot-test$(HOST_EXEEXT)"

verify-pcm68k-heartbeat-host:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio" \
	  -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/pcm68k_heartbeat_test.c" \
	  "$(PCM68K_DIR)/main.c" \
	  "$(PCM68K_DIR)/pcm_voice.c" \
	  "$(PCM68K_DIR)/scsp_pcm8.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm68k-heartbeat-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/pcm68k-heartbeat-test$(HOST_EXEEXT)"

verify-sequence-vm:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/audio68k" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/sequence_vm_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/audio68k/sequence_vm.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/sequence-vm-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/sequence-vm-test$(HOST_EXEEXT)'], check=True)"

verify-audio-voice-allocator:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/audio_voice_allocator_test.c" \
	  "$(PCM68K_DIR)/audio_engine.c" "$(PCM68K_DIR)/voice_allocator.c" \
	  "$(PCM68K_DIR)/desired_voice.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-voice-allocator-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-voice-allocator-test$(HOST_EXEEXT)'], check=True)"

verify-audio-slot-shadow:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/audio_slot_shadow_test.c" \
	  "$(PCM68K_DIR)/desired_voice.c" "$(PCM68K_DIR)/slot_shadow.c" \
	  "$(PCM68K_DIR)/scsp_pcm8.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-slot-shadow-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-slot-shadow-test$(HOST_EXEEXT)'], check=True)"

verify-audio-scsp-timer:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(PCM68K_DIR)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/audio_scsp_timer_test.c" \
	  "$(PCM68K_DIR)/scsp_timer.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-scsp-timer-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/audio-scsp-timer-test$(HOST_EXEEXT)'], check=True)"

pcm68k-image:
	$(MAKE) -C "$(PCM68K_DIR)" all

verify-pcm68k-image:
	$(MAKE) -C "$(PCM68K_DIR)" verify

verify-audio68k-modules:
	@test -n "$(PONESOUND_ROOT)" || { echo "Pinned PoneSound checkout not found" >&2; exit 1; }
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_verify_audio68k_modules.py"
	$(MAKE) -B -C "$(PCM68K_DIR)" \
	  M68K_BINDIR="$(M68K_BINDIR)" PYTHON="$(SATURN_TOOLS_PYTHON)" modules
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/verify_audio68k_modules.py" \
	  --ponesound-root "$(PONESOUND_ROOT)" \
	  --m68k-bindir "$(M68K_BINDIR)" \
	  --artifact "$(TASK17_AUDIO68K_ARTIFACT)" \
	  $(foreach object,$(TASK17_AUDIO68K_OBJECTS),--object "$(object)")

compile-pcm-proof-bank:
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/gen_pcm_proof_bank.py" --output "$(PCM_PROOF_GENERATED)"

soundtest: check-libyaul check-sdk pcm68k-image compile-pcm-proof-bank
	$(MAKE) -C "$(SOUNDTEST_DIR)"

verify-soundtest: verify-pcm-protocol verify-audio-protocol-v2 verify-pcm-transport verify-pcm68k-model verify-scsp-pcm8 verify-pcm68k-heartbeat-host verify-pcm68k-image compile-pcm-proof-bank verify-soundtest-boot soundtest
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_gen_pcm_proof_bank.py"
	$(MAKE) -C "$(SOUNDTEST_DIR)" verify

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
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror $(RUNTIME_CONTRACT_TEST_CFLAGS) \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -DSM64_SATURN_RUNTIME_CONTRACT_TEST=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  -I"$(QUAD_MAP_GENERATED)" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/runtime_contract_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_fast3d_frontend.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_vdp2_frame.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/slavedriver_terrain_clip.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_frustum.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_source_runtime.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/runtime-contract-test$(HOST_EXEEXT)"

verify-runtime-camera-contract: RUNTIME_CONTRACT_TEST_CFLAGS = -DSM64_SATURN_RUNTIME_CONTRACT_ONLY=1 -Wno-unused-function
verify-runtime-camera-contract: verify-runtime-contracts

verify-source-render-policy:
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_source_render_suppression.py"
	$(MAKE) -f "$(SATURN_REPO_ROOT)/Makefile.saturn.mk" verify-runtime-contracts

verify-source-geo-state-diff:
	@printf '%s\n' 'Task 8 static source audit plus illustrative digest model; real graph differential remains open'
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/source_geo_state_diff_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_source_geo_state.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/source-geo-state-contract-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/source-geo-state-contract-test$(HOST_EXEEXT)'], check=True)"

verify-sourceboot-presentation-boundary:
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_sourceboot_presentation_boundary.py"

verify-sourceboot-boot-trace:
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_sourceboot_boot_trace.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_capture_sourceboot_boot_trace.py"

verify-vdp2-frame: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/include" -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/vdp2_frame_contract_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_vdp2_frame.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/vdp2-frame-contract-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/vdp2-frame-contract-test$(HOST_EXEEXT)"

verify-terrain-command-template: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/terrain_command_template_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_terrain_command_template.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-command-template-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-command-template-test$(HOST_EXEEXT)"

verify-terrain-depth-bins: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/terrain_depth_bins_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-depth-bins-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-depth-bins-test$(HOST_EXEEXT)"

verify-terrain-command-stream: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/terrain_command_stream_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-command-stream-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-command-stream-test$(HOST_EXEEXT)"

verify-terrain-command-template-target-compile: check-libyaul check-sdk
	$(MAKE) -C "$(SOURCEBOOT_DIR)" terrain-command-template-target-compile

verify-terrain-clip:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/terrain_clip_smoke.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/slavedriver_terrain_clip.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-clip-smoke$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/terrain-clip-smoke$(HOST_EXEEXT)"

verify-ztreme-frustum:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/ztreme_frustum_smoke.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_frustum.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/ztreme-frustum-smoke$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/ztreme-frustum-smoke$(HOST_EXEEXT)']).returncode)"

verify-bob-bsp-header: compile-bob-scene
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/build/saturn/sourceboot/generated" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/bob_bsp_header_smoke.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/bob-bsp-header-smoke$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/bob-bsp-header-smoke$(HOST_EXEEXT)']).returncode)"

verify-visible-position-set:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/visible_position_set_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/visible-position-set-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/visible-position-set-test$(HOST_EXEEXT)"

verify-render-clusters:
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_render_cluster_generation.py"
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_cluster_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-cluster-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-cluster-test$(HOST_EXEEXT)']).returncode)"

verify-scene-admission: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/scene_admission_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_scene_admission.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_frustum.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/scene-admission-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/scene-admission-test$(HOST_EXEEXT)']).returncode)"

verify-portal-windows: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/portal_window_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_scene_admission.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_frustum.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/portal-window-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/portal-window-test$(HOST_EXEEXT)']).returncode)"

verify-render-snapshot-bank:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_snapshot_bank_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_snapshot.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-snapshot-bank-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-snapshot-bank-test$(HOST_EXEEXT)']).returncode)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_render_snapshot_source.py"

verify-actor-instance-snapshot:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_instance_snapshot_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_instance.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_geo_state_observer.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-instance-snapshot-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-instance-snapshot-test$(HOST_EXEEXT)']).returncode)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_actor_snapshot_source.py"

verify-dual-frame-bank:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/dual_frame_bank_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/dual-frame-bank-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/dual-frame-bank-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/verify_dual_cpu_coherency.py" \
	  --source "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_demo_render.c" \
	  --header "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_dual_frame_bank.h" --self-test

verify-render-job-queue:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_queue_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-queue-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-queue-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/verify_dual_cpu_coherency.py" \
	  --queue-source "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  --queue-header "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.h" --self-test

verify-render-callback-context: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_callback_context_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_callback_context.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-callback-context-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-callback-context-test$(HOST_EXEEXT)"

verify-render-job-bridge:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_bridge_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_bridge.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_output_bank.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-bridge-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-bridge-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_render_job_bridge_source.py"

verify-render-job-live-cutover:
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_live_cutover_source_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-live-cutover-source-test$(HOST_EXEEXT)"
	cd "$(SATURN_REPO_ROOT)" && "build/saturn/host-tests/render-job-live-cutover-source-test$(HOST_EXEEXT)"

verify-render-job-terrain-route:
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_terrain_route_source_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-terrain-route-source-test$(HOST_EXEEXT)"
	cd "$(SATURN_REPO_ROOT)" && "build/saturn/host-tests/render-job-terrain-route-source-test$(HOST_EXEEXT)"

verify-render-job-actor-route:
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_actor_route_source_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-actor-route-source-test$(HOST_EXEEXT)"
	cd "$(SATURN_REPO_ROOT)" && "build/saturn/host-tests/render-job-actor-route-source-test$(HOST_EXEEXT)"

verify-render-job-runtime:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_runtime_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-runtime-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-runtime-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_render_job_runtime_source.py"

verify-render-job-payload-bank:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_payload_bank_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_payload_bank.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_bridge.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_output_bank.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-payload-bank-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-payload-bank-test$(HOST_EXEEXT)"

verify-render-job-graph:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_job_graph_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-graph-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-job-graph-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_render_job_graph_source.py"

verify-dual-actor-worker:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_DUAL_WORKER_TEST_HOOK=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/dual_actor_worker_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/slavedriver_dual_worker.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/dual-actor-worker-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/dual-actor-worker-test$(HOST_EXEEXT)']).returncode)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_DUAL_WORKER_TEST_HOOK=1 \
	  -DSM64_SATURN_TEST_MUTATE_SPLIT_OWNER=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/dual_actor_worker_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/slavedriver_dual_worker.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/dual-actor-worker-owner-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/dual-actor-worker-owner-mutation$(HOST_EXEEXT)" \
	  --label "dual actor worker cached-owner mutation"

verify-actor-meshlets:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_meshlet_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_meshlets.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-meshlet-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-meshlet-test$(HOST_EXEEXT)']).returncode)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -DSM64_SATURN_ACTOR_MESHLET_TEST_INVALID_SPAN=1 \
	  -I"$(SATURN_REPO_ROOT)/include" -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_meshlet_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_meshlets.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-meshlet-span-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-meshlet-span-mutation$(HOST_EXEEXT)" \
	  --label "actor meshlet invalid-span mutation"

verify-actor-instance-queue:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -pedantic -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_instance_queue_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_instance_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-instance-queue-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-instance-queue-test$(HOST_EXEEXT)']).returncode)"

verify-actor-batches:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -pedantic -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_batch_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_batch.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_instance_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-batch-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-batch-test$(HOST_EXEEXT)']).returncode)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_actor_runtime_neutrality.py"

compile-mario-actor-bank: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/compile_actor_bank.py" \
	  --root "$(SATURN_REPO_ROOT)" \
	  --manifest "tools/saturn/manifests/actors/mario.json" \
	  --output "$(MARIO_ACTOR_BANK)" \
	  --report "$(MARIO_ACTOR_BANK_REPORT)"

compile-actor-banks: compile-scene-closure check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/compile_actor_bank.py" \
	  --root "$(SATURN_REPO_ROOT)" \
	  --family-closure "$(SCENE_CLOSURE_OUTPUT)" \
	  --family-output-dir "$(ACTOR_FAMILY_BANK_DIR)" \
	  --output "$(ACTOR_FAMILY_BANK_REPORT)" \
	  --report "$(ACTOR_FAMILY_BANK_REPORT)"

verify-actor-family-bank: compile-actor-banks
	@"$(SATURN_TOOLS_PYTHON)" -c "import json; from pathlib import Path; p=Path(r'$(ACTOR_FAMILY_BANK_REPORT)'); d=json.loads(p.read_text()); assert d['payload_sha256'] and d['family_count'] > 0 and d['unsupported_required_capability_count'] == 13 and not d['complete_closure']; print('actor family report: PASS', d['family_count'], 'families', d['unsupported_required_capability_count'], 'unsupported')"
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_family_bank_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_bank.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-family-bank-test$(HOST_EXEEXT)"
	@"$(SATURN_TOOLS_PYTHON)" -c "import json, subprocess; from pathlib import Path; d=json.loads(Path(r'$(ACTOR_FAMILY_BANK_REPORT)').read_text()); subprocess.check_call([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-family-bank-test$(HOST_EXEEXT)', str(Path(d['payload']))])"

verify-actor-pose-bank: compile-mario-actor-bank
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_pose_bank_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_bank.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_actor_pose.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-pose-bank-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-pose-bank-test$(HOST_EXEEXT)" "$(MARIO_ACTOR_BANK)"

verify-dma-queue:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSATURN_DMA_QUEUE_INITIAL_SEQUENCE=4294967294U \
	  -DSATURN_DMA_QUEUE_HOST_TEST=1 \
	  -I"$(SATURN_REPO_ROOT)/tools/saturn/host_stubs" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/dma_queue_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/slavedriver_dma_queue.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/dma-queue-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/dma-queue-test$(HOST_EXEEXT)"

verify-frame-pipeline:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/frame_pipeline_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-test$(HOST_EXEEXT)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_FRAME_PIPELINE_TEST_FOUR_TICK=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/frame_pipeline_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-four-tick-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-four-tick-mutation$(HOST_EXEEXT)" \
	  --label "frame pipeline four-tick catch-up mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_FRAME_PIPELINE_TEST_READD_CREDIT=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/frame_pipeline_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-readd-credit-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-readd-credit-mutation$(HOST_EXEEXT)" \
	  --label "frame pipeline repeated-observation credit mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_FRAME_PIPELINE_TEST_PUBLISH_INCOMPLETE=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/frame_pipeline_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-incomplete-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/frame-pipeline-incomplete-mutation$(HOST_EXEEXT)" \
	  --label "frame pipeline incomplete-bank publication mutation"

verify-render-overlap-integration:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_a9_overlap_target_coherency.py"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-integration-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" -c "import subprocess; raise SystemExit(subprocess.run([r'$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-integration-test$(HOST_EXEEXT)']).returncode)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_LOD_LIFETIME_TEST_APPLY_DURING_ACTIVE=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-lod-race-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-lod-race-mutation$(HOST_EXEEXT)" \
	  --label "render overlap active-generation LOD reset mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_OVERLAP_PHASE_TEST_OMIT_START=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-start-phase-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-start-phase-mutation$(HOST_EXEEXT)" \
	  --label "render overlap omitted start-construction mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_NOTIFY_MARKER=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-notify-boundary-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-notify-boundary-mutation$(HOST_EXEEXT)" \
	  --label "render overlap late notify-marker timestamp mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_JOB_RUNTIME_TEST_LATE_RETIRE_MARKER=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-retire-boundary-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-retire-boundary-mutation$(HOST_EXEEXT)" \
	  --label "render overlap late retirement-marker timestamp mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_LOD_LIFETIME_TEST_IGNORE_GENERATION=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-generation-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-generation-mutation$(HOST_EXEEXT)" \
	  --label "render overlap ignored LOD generation mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_JOB_RUNTIME_TEST_SKIP_TERMINAL_REFRESH=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_overlap_integration_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_lod_lifetime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_runtime.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_graph.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_job_queue.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_frame_pipeline.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_render_overlap_phase.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-quarantine-refresh-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-overlap-quarantine-refresh-mutation$(HOST_EXEEXT)" \
	  --label "render overlap skipped quarantine refresh mutation"

verify-demo-render-overlap: verify-render-overlap-integration
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/demo_render_overlap_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-test$(HOST_EXEEXT)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_LIFECYCLE_TEST_FINALIZE_BEFORE_RETIREMENT=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/demo_render_overlap_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-early-finalize-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-early-finalize-mutation$(HOST_EXEEXT)" \
	  --label "demo render finalize-before-retirement mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_LIFECYCLE_TEST_LOWER_TWICE=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/demo_render_overlap_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-lower-twice-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-lower-twice-mutation$(HOST_EXEEXT)" \
	  --label "demo render double-lowering mutation"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_RENDER_LIFECYCLE_TEST_REPLAY_ON_FAILURE=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/demo_render_overlap_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_render_lifecycle.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-replay-mutation$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/expect_failure.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/demo-render-overlap-replay-mutation$(HOST_EXEEXT)" \
	  --label "demo render serial replay mutation"

verify-vdp1-frame-bank:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/vdp1_frame_bank_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_vdp1_frame_bank.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/vdp1-frame-bank-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/vdp1-frame-bank-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_vdp1_frame_bank_source.py"

verify-vdp1-transfer-pipeline: verify-dma-queue
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/vdp1_transfer_pipeline_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_vdp1_frame_bank.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/vdp1-transfer-pipeline-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/vdp1-transfer-pipeline-test$(HOST_EXEEXT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_vdp1_transfer_pipeline_source.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_a8_deferred_transfer_runtime_contract.py"

verify-gouraud-transfer:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/gouraud_transfer_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_gouraud_transfer.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/gouraud-transfer-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/gouraud-transfer-test$(HOST_EXEEXT)"

# Task 1 shared transform contract. This intentionally links the actual
# extracted module, rather than reproducing its math in a test-only helper;
# the same immutable job record is therefore checked independently of either
# Saturn consumer.
verify-ir-transform:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/ir_transform_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_ir_transform.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/ir-transform-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/ir-transform-test$(HOST_EXEEXT)"

# Task 1 native-math seam: compile the real public helper header and run its
# float-boundary and signed-Q16-division differential fixture on every normal
# verification pass. The companion target is a deterministic mutation check:
# it only passes when the fixture rejects the test-only broken division mode.
verify-render-native-math:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_native_math_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-native-math-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-native-math-test$(HOST_EXEEXT)"

verify-render-native-math-mutation:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -DSM64_SATURN_TEST_MUTATE_Q16_DIV=1 \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/render_native_math_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-native-math-mutation$(HOST_EXEEXT)"
	@if "$(SATURN_REPO_ROOT)/build/saturn/host-tests/render-native-math-mutation$(HOST_EXEEXT)"; then \
	  printf '%s\\n' 'render-native-math mutation escaped the differential fixture' >&2; \
	  exit 1; \
	else \
	  printf '%s\\n' 'render-native-math mutation caught by differential fixture'; \
	fi

compile-scene-closure: check-host-tools
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/collect_scene_closure.py" \
	  --root "$(SATURN_REPO_ROOT)" --level "$(SCENE_LEVEL)" --area "$(SCENE_AREA)" \
	  --rules "$(SATURN_REPO_ROOT)/tools/saturn/behavior_spawn_rules.json" \
	  --output "$(SCENE_CLOSURE_OUTPUT)"

verify-scene-package-schema: check-host-tools
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_scene_package_schema.py"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_scene_package_determinism.py"

verify-scene-package-runtime: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  -I"$(SATURN_REPO_ROOT)/tools/saturn" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/scene_package_runtime_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_scene_package.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/scene-package-runtime-test$(HOST_EXEEXT)"
	$(SATURN_REPO_ROOT)/build/saturn/host-tests/scene-package-runtime-test$(HOST_EXEEXT)

verify-scene-residency: check-host-tools
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/runtime" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/tools/saturn" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/scene_residency_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_scene_package.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/runtime/saturn_scene_residency.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/scene-residency-test$(HOST_EXEEXT)"
	$(SATURN_REPO_ROOT)/build/saturn/host-tests/scene-residency-test$(HOST_EXEEXT)

# Task 4's BOB artifact is deliberately provisional and cannot satisfy a
# target/final closure gate. Later payload tasks emit content-addressed
# dependencies; Task 22 alone links and reseals the final root.
compile-provisional-scene-package: compile-bob-area compile-bob-bsp compile-bob-sky verify-scene-package-schema
	@mkdir -p "$(SCENE_PACKAGE_PROVISIONAL_DIR)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/compile_scene_package.py" \
	  --level-id "$(SCENE_LEVEL_ID)" --area-id "$(SCENE_AREA)" --provisional \
	  --section "WORLD_STATIC=$(SCENE_PACKAGE_WORLD_STATIC)" \
	  --section "COLLISION=$(SCENE_PACKAGE_COLLISION)" \
	  --section "SKY_BACKGROUND=$(SCENE_PACKAGE_SKY_BACKGROUND)" \
	  --section "BSP_PORTAL=$(SCENE_PACKAGE_BSP_PORTAL)" \
	  --output "$(SCENE_PACKAGE_PROVISIONAL_ROOT)" \
	  --metadata-output "$(SCENE_PACKAGE_PROVISIONAL_REPORT)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/validate_scene_package.py" \
	  --input "$(SCENE_PACKAGE_PROVISIONAL_ROOT)" --allow-provisional \
	  --report "$(SCENE_PACKAGE_PROVISIONAL_VALIDATION)"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/emit_scene_package_header.py" \
	  --input "$(SCENE_PACKAGE_PROVISIONAL_ROOT)" --allow-provisional \
	  --abi-output "$(SCENE_PACKAGE_ABI_HEADER)" \
	  --symbol-prefix "$(SCENE_LEVEL)_area$(SCENE_AREA)" \
	  --output "$(SCENE_PACKAGE_PROVISIONAL_HEADER)"

# Z-Treme-style LWRAM -> HWRAM promotion contract: alignment, bounded
# capacity, copied bytes, and source immutability are all host-verifiable.
verify-hot-promotion:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gpl" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/hot_promotion_test.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/hot-promotion-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/hot-promotion-test$(HOST_EXEEXT)"

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
	  "$(SATURN_REPO_ROOT)/lib/src/guPerspectiveF.c" \
	  "$(SATURN_REPO_ROOT)/lib/src/guOrthoF.c" \
	  "$(SATURN_REPO_ROOT)/lib/src/guMtxF2L.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -lm \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-ctors-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-ctors-test$(HOST_EXEEXT)"

# Mutation gate for Task 5's one-reciprocal normalization. The fixture must
# reject a one-bit reciprocal perturbation; a passing mutant means the
# differential corpus cannot detect corruption in the new shared scale.
verify-mtxq-ctors-mutation:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -DSM64_SATURN_TEST_MUTATE_NORMALIZE_RECIPROCAL=1 \
	  -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/mtxq_ctor_diff_test.c" \
	  "$(SATURN_REPO_ROOT)/src/engine/math_util.c" \
	  "$(SATURN_REPO_ROOT)/lib/src/guPerspectiveF.c" \
	  "$(SATURN_REPO_ROOT)/lib/src/guOrthoF.c" \
	  "$(SATURN_REPO_ROOT)/lib/src/guMtxF2L.c" \
	  "$(SATURN_REPO_ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c" \
	  -lm \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-ctors-mutation$(HOST_EXEEXT)"
	@if "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-ctors-mutation$(HOST_EXEEXT)"; then \
	  printf '%s\n' 'mtxq ctor mutation unexpectedly passed' >&2; exit 1; \
	else \
	  printf '%s\n' 'mtxq ctor mutation rejected as expected'; \
	fi

verify-graph-q16-contract:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -E -P -DTARGET_SATURN=1 -DSATURN_MTX_IS_Q16=1 \
	  -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)" -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/src/game/rendering_graph_node.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/rendering-graph-node-saturn.i"
	$(CC) -E -P -DNON_MATCHING=1 -DAVOID_UB=1 -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
	  -I"$(SATURN_REPO_ROOT)" -I"$(SATURN_REPO_ROOT)/include" \
	  -I"$(SATURN_REPO_ROOT)/src" -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/platform" \
	  "$(SATURN_REPO_ROOT)/src/game/rendering_graph_node.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/rendering-graph-node-source.i"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_graph_q16_contract.py" \
	  --saturn "$(SATURN_REPO_ROOT)/build/saturn/host-tests/rendering-graph-node-saturn.i" \
	  --source "$(SATURN_REPO_ROOT)/build/saturn/host-tests/rendering-graph-node-source.i"

verify-mtxq-conversion-assembly:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(CC) -S -O2 -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/mtxq_conversion_probe.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-conversion-probe.s"
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_mtxq_conversion_assembly.py" \
	  "$(SATURN_REPO_ROOT)/build/saturn/host-tests/mtxq-conversion-probe.s"

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
	  $(HOST_CC_ENV) "$(SOFTFP_HOST_CC)" -O2 -std=gnu11 -w -c "$(SOFTFP_VENDOR)/soft-fp/$$f.c" \
	    -I"$(SOFTFP_VENDOR)/soft-fp" -I"$(SOFTFP_VENDOR)/config/sh" -I"$(SOFTFP_VENDOR)/include" \
	    -o "$(SOFTFP_HOST_BUILD)/$$f.o" || exit 1; \
	done; \
	$(HOST_CC_ENV) "$(SOFTFP_HOST_CC)" -O2 -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
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
	  --output "src/port/saturn/gfx/saturn_mario_actor_mesh.h" \
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

compile-bob-area: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/extract_bob_area.py" \
	  --area "$(BOB_AREA_DIR)" \
	  --intake-output "$(BOB_MESH_GENERATED)/bob_area1_intake.json" \
	  --mesh-ir-output "$(BOB_MESH_GENERATED)/bob_area1_mesh_ir_v2.json"
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/saturn_mesh_ir.py" \
	  --input "$(BOB_MESH_GENERATED)/bob_area1_mesh_ir_v2.json" \
	  --output "$(BOB_MESH_GENERATED)/bob_area1_compiled.json" \
	  --report "$(BOB_MESH_GENERATED)/bob_area1_report.json"

compile-bob-bsp: compile-bob-area
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/compile_bob_bsp.py" \
	  --input "$(BOB_MESH_GENERATED)/bob_area1_compiled.json" \
	  --output "$(BOB_MESH_GENERATED)/bob_area1_bsp_report.json"

compile-bob-bsp-fragments: compile-bob-area
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/bake_bob_bsp_fragments.py" \
	  --input "$(BOB_MESH_GENERATED)/bob_area1_compiled.json" \
	  --asset-root "$(BOB_ASSET_ROOT)" \
	  --bank "$(BOB_MESH_GENERATED)/bob_bsp_fragments_clut16.bin" \
	  --clut "$(BOB_MESH_GENERATED)/bob_bsp_fragments_clut16.pal" \
	  --manifest "$(BOB_MESH_GENERATED)/bob_bsp_fragments_manifest.json" \
	  --scene "$(BOB_MESH_GENERATED)/bob_bsp_fragments_scene.json"

compile-bob-tiles: compile-bob-area
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/bake_bob_tiles.py" \
	  --intake "$(BOB_TILES_GENERATED)/bob_area1_intake.json" \
	  --asset-root "$(SATURN_REPO_ROOT)" \
	  --bank "$(BOB_TILES_GENERATED)/bob_tiles_clut16.bin" \
	  --clut "$(BOB_TILES_GENERATED)/bob_tiles_clut16.pal" \
	  --manifest "$(BOB_TILES_GENERATED)/bob_tiles_manifest.json"

compile-bob-scene: compile-bob-tiles compile-bob-bsp
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/emit_bob_scene.py" \
	  --mesh "$(BOB_MESH_GENERATED)/bob_area1_compiled.json" \
	  --manifest "$(BOB_TILES_GENERATED)/bob_tiles_manifest.json" \
	  --bsp "$(BOB_MESH_GENERATED)/bob_area1_bsp_report.json" \
	  --output "$(BOB_MESH_GENERATED)/bob_scene.h"

compile-bob-sky: check-host-tools
	@cd "$(SATURN_REPO_ROOT)" && "$(SATURN_TOOLS_PYTHON)" "tools/saturn/bake_bob_sky.py" \
	  --input "$(BOB_SKY_SOURCE)" \
	  --output "$(BOB_SKY_OUTPUT)" \
	  --manifest "$(BOB_SKY_MANIFEST)"

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

verify-all: verify-tools verify-runtime-contracts verify-terrain-clip verify-ztreme-frustum verify-bob-bsp-header verify-frame-pipeline verify-vdp1-frame-bank verify-vdp1-transfer-pipeline verify-gouraud-transfer verify-ir-transform verify-render-native-math verify-render-native-math-mutation verify-hot-promotion classify-source verify-hello verify-hwtest

clean: check-sdk
	$(MAKE) -C "$(HELLO_DIR)" clean
