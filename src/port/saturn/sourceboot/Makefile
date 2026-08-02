ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env)
endif

ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST))))/../../../..)
SH_PROGRAM := sm64-saturn-sourceboot-e2
SOURCEBOOT_GENERATED := $(ROOT)/build/saturn/sourceboot/generated
SOURCEBOOT_PYTHON ?= $(ROOT)/.venv-saturn-tools/Scripts/python.exe
SOURCEBOOT_SH_OBJDUMP := $(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-objdump
SOURCEBOOT_SH_READELF := $(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf
SOURCEBOOT_SH_ADDR2LINE := $(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-addr2line
SOURCEBOOT_NATIVE_MATH_BASELINE := $(ROOT)/tools/saturn/sh2_native_math_baseline_v1.txt
SOURCEBOOT_NATIVE_MATH_ROUTE_ORACLE := $(ROOT)/tools/saturn/sh2_native_math_route_oracle_v1.txt
SOURCEBOOT_NATIVE_MATH_SIM_ROUTE_ORACLE := $(ROOT)/tools/saturn/sh2_native_math_sim_route_oracle_v1.txt
SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT := $(ROOT)/tools/saturn/sh2_native_math_sim_audit_contract_v2.txt
SOURCEBOOT_PC_BUILD := $(ROOT)/build/us_pc
SOURCEBOOT_TEXT_STRINGS := build/us_pc/include/text_strings.h
SOURCEBOOT_COLLISION_CATALOG := $(SOURCEBOOT_GENERATED)/sourceboot_collision_catalog.inc
SOURCEBOOT_COLLISION_INPUTS := $(shell find "$(ROOT)/levels" \( -name collision.inc.c -o -name trajectory.inc.c \) -print)
SOURCEBOOT_COLLISION_EXCLUDE_LEVELS := bob castle_grounds ttc
SOURCEBOOT_WATER_SKYBOX := $(SOURCEBOOT_PC_BUILD)/bin/water_skybox.c
SOURCEBOOT_CART_IMAGE = $(SH_BUILD_DIR)/SOURCE.DAT
SOURCEBOOT_QUAD_MAP_C := $(SOURCEBOOT_GENERATED)/saturn_quad_map.c
SOURCEBOOT_QUAD_MAP_H := $(SOURCEBOOT_GENERATED)/saturn_quad_map.h
SATURN_SOURCEBOOT_ROUTE_REPLAY ?= 0
SATURN_ATAN2_VARIANT ?= 2
ifneq ($(words $(filter $(SATURN_ATAN2_VARIANT),1 2)),1)
  $(error SATURN_ATAN2_VARIANT must be 1 (legacy) or 2 (Q16))
endif
SATURN_CAMERA_VARIANT ?= 1
SATURN_SOURCEBOOT_CAMERA_ROUTE ?= 0
SATURN_CAMERA_IDLE_START_TICK ?= 0
SATURN_CAMERA_IDLE_DISCOVERY ?= 0
SATURN_CAMERA_RANGE_CAPTURE ?= 0
# Camera evidence carries the fixed SCC1 LWRAM reservation and the camera-Q
# transport contract already selected the smallest non-fallback cart stage:
# eight sectors.  Route 0 retains the historical 16-sector boot throughput.
SATURN_SOURCE_CART_STAGE_SECTORS ?= $(if $(filter 1,$(SATURN_SOURCEBOOT_CAMERA_ROUTE)),8,16)
ifneq ($(words $(filter $(SATURN_CAMERA_VARIANT),1 2 3)),1)
  $(error SATURN_CAMERA_VARIANT must be 1 (float), 2 (bypass), or 3 (fixed))
endif
ifneq ($(words $(filter $(SATURN_SOURCEBOOT_CAMERA_ROUTE),0 1)),1)
  $(error SATURN_SOURCEBOOT_CAMERA_ROUTE must be 0 (bob-parity-v1) or 1 (bob-default-camera-v1))
endif
ifeq ($(SATURN_SOURCEBOOT_CAMERA_ROUTE),1)
  ifneq ($(SATURN_SOURCEBOOT_ROUTE_REPLAY),1)
    $(error SATURN_SOURCEBOOT_CAMERA_ROUTE=1 requires SATURN_SOURCEBOOT_ROUTE_REPLAY=1)
  endif
  ifneq ($(words $(filter $(SATURN_SOURCE_CART_STAGE_SECTORS),4 8)),1)
    $(error camera evidence route requires an 8- or 4-sector cart stage)
  endif
endif
ifneq ($(words $(filter $(SATURN_CAMERA_IDLE_DISCOVERY),0 1)),1)
  $(error SATURN_CAMERA_IDLE_DISCOVERY must be 0 or 1)
endif
ifneq ($(words $(filter $(SATURN_CAMERA_RANGE_CAPTURE),0 1)),1)
  $(error SATURN_CAMERA_RANGE_CAPTURE must be 0 or 1)
endif
ifneq ($(words $(filter $(SATURN_SOURCE_CART_STAGE_SECTORS),4 8 16)),1)
  $(error SATURN_SOURCE_CART_STAGE_SECTORS must be 4, 8, or 16)
endif
SATURN_DEMO_PATH ?= 0
SATURN_DEMO_VIEW_RADIUS ?= 6000
SATURN_SLAVE_RENDER ?= 1
SATURN_DEMO_POLY_TIER ?= 0
SATURN_DEMO_HOT_PROMOTION ?= 0
SATURN_DEMO_NEAR_CLIP ?= 0
SATURN_DEMO_BSP_ORDER ?= 1
SATURN_DEMO_BSP_FRAGMENTS ?= 0
SATURN_DEMO_BSP_FRAGMENT_FLAT ?= 0
# Build-lineage tag only. Keep the throughput-recovery image in a distinct
# directory so a manual session running the previous renderer is never
# overwritten underneath Ymir.
SATURN_RENDERER_PIPELINE ?= 2
# The meshes the quad map is compiled from. Listed here so a geometry change
# regenerates the map inside the build that consumes it -- a stale map is
# never reachable. The actor list and the tool arguments live once, in
# Makefile.saturn.mk's compile-quad-map, which the rule below delegates to;
# keep the two in step.
SOURCEBOOT_QUAD_MAP_SOURCES := \
  $(ROOT)/actors/mario/geo.inc.c $(ROOT)/actors/mario/model.inc.c \
  $(ROOT)/actors/cannon_barrel/geo.inc.c \
  $(ROOT)/actors/cannon_barrel/model.inc.c \
  $(ROOT)/actors/cannon_base/geo.inc.c \
  $(ROOT)/actors/cannon_base/model.inc.c
SOURCEBOOT_QUAD_MAP_TOOLS := \
  $(ROOT)/tools/saturn/quad_map.py $(ROOT)/tools/saturn/dl_rigid_groups.py \
  $(ROOT)/tools/saturn/quad_pairing.py \
  $(ROOT)/tools/saturn/extract_mario_actor.py
SOURCEBOOT_BOB_MESH := $(SOURCEBOOT_GENERATED)/bob_area1_mesh_ir_v2.json
SOURCEBOOT_BOB_COMPILED := $(SOURCEBOOT_GENERATED)/bob_area1_compiled.json
SOURCEBOOT_BOB_REPORT := $(SOURCEBOOT_GENERATED)/bob_area1_report.json
SOURCEBOOT_BOB_BSP_REPORT := $(SOURCEBOOT_GENERATED)/bob_area1_bsp_report.json
SOURCEBOOT_BOB_BSP_HEADER := $(SOURCEBOOT_GENERATED)/bob_bsp.h
SOURCEBOOT_BOB_BSP_FRAGMENT_BANK := $(SOURCEBOOT_GENERATED)/bob_bsp_fragments_clut16.bin
SOURCEBOOT_BOB_BSP_FRAGMENT_CLUT := $(SOURCEBOOT_GENERATED)/bob_bsp_fragments_clut16.pal
SOURCEBOOT_BOB_BSP_FRAGMENT_MANIFEST := $(SOURCEBOOT_GENERATED)/bob_bsp_fragments_manifest.json
SOURCEBOOT_BOB_BSP_FRAGMENT_SCENE := $(SOURCEBOOT_GENERATED)/bob_bsp_fragments_scene.json
SOURCEBOOT_BOB_BSP_FRAGMENT_HEADER := $(SOURCEBOOT_GENERATED)/bob_bsp_fragments.h
SOURCEBOOT_BOB_TILES := $(SOURCEBOOT_GENERATED)/bob_tiles_clut16.bin
SOURCEBOOT_BOB_CLUT := $(SOURCEBOOT_GENERATED)/bob_tiles_clut16.pal
SOURCEBOOT_BOB_TILE_MANIFEST := $(SOURCEBOOT_GENERATED)/bob_tiles_manifest.json
SOURCEBOOT_BOB_SCENE := $(SOURCEBOOT_GENERATED)/bob_scene.h
SOURCEBOOT_BOB_SKY := $(SOURCEBOOT_GENERATED)/bob_sky_rgb1555.bin
SOURCEBOOT_BOB_SKY_MANIFEST := $(SOURCEBOOT_GENERATED)/bob_sky_manifest.json
SOURCEBOOT_MARIO_TEXTURE_DIR := $(ROOT)/build/saturn/marioturntable/generated
SOURCEBOOT_MARIO_TEXTURE_HEADER := $(SOURCEBOOT_MARIO_TEXTURE_DIR)/mario_eye_uv_tiles.h
SOURCEBOOT_MARIO_TEXTURE_REPORT := $(SOURCEBOOT_MARIO_TEXTURE_DIR)/mario_eye_uv_tiles.json
SOURCEBOOT_BOB_TOOLS := \
  $(ROOT)/tools/saturn/extract_bob_area.py $(ROOT)/tools/saturn/saturn_mesh_ir.py \
  $(ROOT)/tools/saturn/quad_pairing.py $(ROOT)/tools/saturn/extract_castle_area.py \
  $(ROOT)/tools/saturn/extract_mario_actor.py \
  $(ROOT)/tools/saturn/compile_bob_bsp.py $(ROOT)/tools/saturn/bake_bob_bsp_fragments.py \
  $(ROOT)/tools/saturn/emit_bob_bsp_fragments.py $(ROOT)/tools/saturn/static_bsp.py
SOURCEBOOT_BOB_TILE_TOOLS := $(ROOT)/tools/saturn/bake_bob_tiles.py \
  $(ROOT)/tools/saturn/bake_castle_uv.py $(ROOT)/tools/saturn/vdp1_texture.py
SOURCEBOOT_BOB_SCENE_TOOL := $(ROOT)/tools/saturn/emit_bob_scene.py
SOURCEBOOT_BOB_SOURCES := \
  $(ROOT)/levels/bob/areas/1/geo.inc.c \
  $(wildcard $(ROOT)/levels/bob/areas/1/*/model.inc.c)
SOURCEBOOT_BIN_SRCS := $(wildcard $(ROOT)/bin/*.c)
SOURCEBOOT_ACTOR_SRCS := $(wildcard $(ROOT)/actors/*.c)
# E2 intentionally links one original level, not the full game archive.  The
# unmodified Bob script invokes source global banks 1, 4, and 15 through
# levels/scripts.c; the linker then retains their actual actor sections.
# Future level banks belong in the cart-backed asset package, not in the
# fixed Yaul executable image.
SOURCEBOOT_LEVEL_SRCS := $(ROOT)/levels/bob/script.c \
  $(ROOT)/levels/bob/geo.c $(ROOT)/levels/bob/leveldata.c \
  $(ROOT)/levels/menu/leveldata.c \
  $(ROOT)/levels/castle_grounds/leveldata.c \
  $(ROOT)/levels/ttc/leveldata.c
SOURCEBOOT_GU_SRCS := $(wildcard $(ROOT)/lib/src/gu*.c)
SOURCEBOOT_MATH_SRCS := $(ROOT)/lib/src/math/sinf.c $(ROOT)/lib/src/math/cosf.c
SOURCEBOOT_ASSET_SOURCES := \
  $(patsubst $(ROOT)/%,%,$(SOURCEBOOT_BIN_SRCS)) src/goddard/renderer.c \
  $(patsubst $(ROOT)/%,%,$(SOURCEBOOT_ACTOR_SRCS) $(SOURCEBOOT_LEVEL_SRCS))
SOURCEBOOT_GAME_SRCS := $(filter-out $(ROOT)/src/game/crash_screen.c,$(wildcard $(ROOT)/src/game/*.c))
SOURCEBOOT_ENGINE_SRCS := $(wildcard $(ROOT)/src/engine/*.c)
SOURCEBOOT_MENU_SRCS := $(wildcard $(ROOT)/src/menu/*.c)
SOURCEBOOT_GODDARD_SRCS := $(wildcard $(ROOT)/src/goddard/*.c) $(wildcard $(ROOT)/src/goddard/dynlists/*.c)

# This is intentionally a source closure, not a Castle viewer dependency list.
# The Bob script, Bob level data, shared source actor banks, and generated Mario
# animation table are all consumed through source LevelScript/GeoLayout state.
SH_SRCS := main.c source_entry.c source_demo_data.c source_audio_stub.c source_platform.c source_cart.c source_q16_kernel_probe.c \
  source_collision_catalog.c $(SOURCEBOOT_WATER_SKYBOX) \
  $(ROOT)/src/port/saturn/controller/controller_saturn.c \
  $(ROOT)/src/port/saturn/runtime/saturn_source_runtime.c \
  $(ROOT)/src/port/saturn/runtime/saturn_camera_role.c \
  $(ROOT)/src/port/saturn/runtime/saturn_camera_fixed.c \
  $(ROOT)/src/port/saturn/gfx/saturn_actor_bridge.c \
  $(ROOT)/src/port/saturn/gfx/saturn_demo_render.c \
  $(ROOT)/src/port/saturn/gfx/saturn_terrain_command_template.c \
  $(ROOT)/src/port/saturn/gpl/ztreme_frustum.c \
  $(ROOT)/src/port/saturn/gpl/slavedriver_terrain_clip.c \
  $(ROOT)/src/port/saturn/gpl/slavedriver_terrain_worker.c \
  $(ROOT)/src/port/saturn/gfx/saturn_ir_transform.c \
  $(ROOT)/src/port/saturn/gfx/saturn_ir_texture.c \
  $(ROOT)/src/port/saturn/gpl/ztreme_hot_promotion.c \
  $(ROOT)/src/port/saturn/gfx/saturn_fast3d_frontend.c \
  $(ROOT)/src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c \
  $(ROOT)/src/port/saturn/gfx/saturn_trig_q16.inc.c \
  $(ROOT)/src/port/saturn/gpl/slavedriver_dma_queue.c \
  $(ROOT)/src/port/saturn/gpl/slavedriver_dual_worker.c \
  $(ROOT)/src/port/saturn/gpl/slavedriver_projection.sx \
  $(ROOT)/src/port/saturn/compat/sqrtf.c \
  $(ROOT)/src/buffers/buffers.c $(ROOT)/src/buffers/gfx_output_buffer.c \
  $(SOURCEBOOT_GAME_SRCS) $(SOURCEBOOT_ENGINE_SRCS) $(SOURCEBOOT_MENU_SRCS) \
  $(SOURCEBOOT_GODDARD_SRCS) \
  $(SOURCEBOOT_BIN_SRCS) $(ROOT)/data/behavior_data.c \
  $(SOURCEBOOT_ACTOR_SRCS) $(SOURCEBOOT_LEVEL_SRCS) $(SOURCEBOOT_GU_SRCS) \
  $(SOURCEBOOT_MATH_SRCS) $(ROOT)/levels/scripts.c \
  $(SOURCEBOOT_GENERATED)/mario_anim_data.c \
  $(SOURCEBOOT_QUAD_MAP_C)

ifeq ($(SATURN_SOURCEBOOT_CAMERA_ROUTE),1)
SH_SRCS += source_camera_acceptance_route.c source_camera_idle_probe.c
endif

# Keep the direct-Bob source closure in its own object/output tree. The demo
# profile gets a distinct tree as well: SH_CFLAGS are not part of Yaul's
# object key, so sharing `e2-bob/obj` would silently reuse interpreted
# `main.o` when SATURN_DEMO_PATH changes. This makes the flag variant safe by
# construction and keeps each ELF/CUE pair self-consistent.
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),-demo,)
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_SOURCEBOOT_ROUTE_REPLAY)),$(SOURCEBOOT_OUTPUT_TAG)-replay-camroute$(SATURN_SOURCEBOOT_CAMERA_ROUTE),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_SOURCEBOOT_ROUTE_REPLAY)),$(SOURCEBOOT_OUTPUT_TAG)-atan2v$(SATURN_ATAN2_VARIANT),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(SOURCEBOOT_OUTPUT_TAG)-camv$(SATURN_CAMERA_VARIANT)
ifeq ($(SATURN_SOURCEBOOT_CAMERA_ROUTE),1)
SOURCEBOOT_OUTPUT_TAG := $(SOURCEBOOT_OUTPUT_TAG)-idle$(SATURN_CAMERA_IDLE_START_TICK)-disc$(SATURN_CAMERA_IDLE_DISCOVERY)-range$(SATURN_CAMERA_RANGE_CAPTURE)
endif
SOURCEBOOT_OUTPUT_TAG := $(SOURCEBOOT_OUTPUT_TAG)-stage$(SATURN_SOURCE_CART_STAGE_SECTORS)
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-r$(SATURN_DEMO_VIEW_RADIUS),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-slave$(SATURN_SLAVE_RENDER),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-poly$(SATURN_DEMO_POLY_TIER),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-hot$(SATURN_DEMO_HOT_PROMOTION),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-clip$(SATURN_DEMO_NEAR_CLIP),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-bsp$(SATURN_DEMO_BSP_ORDER),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-frag$(SATURN_DEMO_BSP_FRAGMENTS),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_PATH)),$(SOURCEBOOT_OUTPUT_TAG)-pipe$(SATURN_RENDERER_PIPELINE),$(SOURCEBOOT_OUTPUT_TAG))
SOURCEBOOT_OUTPUT_TAG := $(if $(filter 1,$(SATURN_DEMO_BSP_FRAGMENT_FLAT)),$(SOURCEBOOT_OUTPUT_TAG)-flat1,$(SOURCEBOOT_OUTPUT_TAG))
SH_BUILD_DIR := $(ROOT)/build/saturn/sourceboot/e2-bob$(SOURCEBOOT_OUTPUT_TAG)/obj
SH_OUTPUT_DIR := $(ROOT)/build/saturn/sourceboot/e2-bob$(SOURCEBOOT_OUTPUT_TAG)
IMAGE_DIRECTORY := $(ROOT)/build/saturn/sourceboot/e2-bob$(SOURCEBOOT_OUTPUT_TAG)/cd
SOURCEBOOT_BOB_SKY_ASM := $(SH_BUILD_DIR)/bob_sky_bitmap.sx
SOURCEBOOT_BOB_TEXTURE_ASM := $(SH_BUILD_DIR)/bob_texture_bank.sx
SOURCEBOOT_BOB_BSP_FRAGMENT_TEXTURE_ASM := $(SH_BUILD_DIR)/bob_bsp_fragment_texture_bank.sx
SH_SRCS += $(SOURCEBOOT_BOB_TEXTURE_ASM)
ifeq ($(SATURN_DEMO_BSP_FRAGMENTS),1)
SH_SRCS += $(SOURCEBOOT_BOB_BSP_FRAGMENT_TEXTURE_ASM)
endif
SH_SRCS += $(SOURCEBOOT_BOB_SKY_ASM)
export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(ROOT)/tools/saturn/xorrisofs-reproducible

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

# Keep the default Yaul binary image contiguous in work RAM.  The source data
# bank is exported separately from the final ELF and copied into the cart at
# its already-linked VMA before thread5_game_loop() executes.
SOURCEBOOT_RAW_OBJCOPY := $(SH_OBJCOPY)
SH_OBJCOPY := $(SOURCEBOOT_RAW_OBJCOPY) -R .cart_rodata
SH_SPECS := sourceboot.specs

# Task 2 evidence-only switch.  It appends a bounded 16-record Fast3D
# transform trace to _sourceboot_fast3d; default builds remain byte-for-byte
# free of the trace state and stores.
SATURN_FAST3D_Q16_TRACE ?= 0
ifeq ($(SATURN_FAST3D_Q16_TRACE),1)
SH_CFLAGS += -DSM64_SATURN_FAST3D_Q16_TRACE=1
endif

SH_CFLAGS += -Os -g -DDEBUG -DNON_MATCHING=1 -DAVOID_UB=1 -DVERSION_US=1 -DVERSION_JP_US=1 \
  -D_LANGUAGE_C=1 -DF3DEX_GBI_2E=1 \
  -DTARGET_SATURN=1 -DSATURN_SOURCEBOOT=1 -DNO_SEGMENTED_MEMORY=1 \
  -DSATURN_SOURCEBOOT_ROUTE_REPLAY=$(SATURN_SOURCEBOOT_ROUTE_REPLAY) \
  -DSATURN_ATAN2_VARIANT=$(SATURN_ATAN2_VARIANT) \
  -DSATURN_CAMERA_VARIANT=$(SATURN_CAMERA_VARIANT) \
  -DSATURN_SOURCEBOOT_CAMERA_ROUTE=$(SATURN_SOURCEBOOT_CAMERA_ROUTE) \
  -DSATURN_CAMERA_IDLE_START_TICK=$(SATURN_CAMERA_IDLE_START_TICK) \
  -DSATURN_CAMERA_IDLE_DISCOVERY=$(SATURN_CAMERA_IDLE_DISCOVERY) \
  -DSATURN_CAMERA_RANGE_CAPTURE=$(SATURN_CAMERA_RANGE_CAPTURE) \
  -DSATURN_SOURCE_CART_STAGE_SECTORS=$(SATURN_SOURCE_CART_STAGE_SECTORS) \
  -DSATURN_DEMO_VIEW_RADIUS=$(SATURN_DEMO_VIEW_RADIUS) \
  -DSATURN_SLAVE_RENDER=$(SATURN_SLAVE_RENDER) \
  -DSATURN_DEMO_POLY_TIER=$(SATURN_DEMO_POLY_TIER) \
  -DSATURN_DEMO_HOT_PROMOTION=$(SATURN_DEMO_HOT_PROMOTION) \
  -DSATURN_DEMO_NEAR_CLIP=$(SATURN_DEMO_NEAR_CLIP) \
  -DSATURN_DEMO_BSP_ORDER=$(SATURN_DEMO_BSP_ORDER) \
  -DSATURN_DEMO_BSP_FRAGMENTS=$(SATURN_DEMO_BSP_FRAGMENTS) \
  -DSATURN_DEMO_BSP_FRAGMENT_FLAT=$(SATURN_DEMO_BSP_FRAGMENT_FLAT) \
  -DSATURN_DEMO_PATH=$(SATURN_DEMO_PATH) \
  -DSATURN_MARIO_TEXTURE_BASE_OFFSET=$(if $(filter 1,$(SATURN_DEMO_BSP_FRAGMENTS)),261248U,333696U) \
  -DSM64_SATURN_VDP1_LWRAM_STAGING=1 -DSATURN_MTX_IS_Q16=1 \
  -fno-lto -ffunction-sections -fdata-sections \
  -I$(CURDIR) -I$(ROOT) -I$(ROOT)/include -I$(ROOT)/src \
  -I$(ROOT)/src/game -I$(ROOT)/src/engine -I$(ROOT)/src/menu \
  -I$(ROOT)/src/goddard -I$(ROOT)/src/port/saturn/compat \
  -I$(ROOT)/src/port/saturn/controller -I$(ROOT)/src/port/saturn/runtime \
  -I$(ROOT)/src/port/saturn/gfx -I$(ROOT)/src/port/saturn/platform \
  -I$(SOURCEBOOT_PC_BUILD) -I$(SOURCEBOOT_PC_BUILD)/include \
  -I$(SOURCEBOOT_GENERATED) -I$(SOURCEBOOT_MARIO_TEXTURE_DIR) \
  -DSATURN_DEMO_MARIO_TEXTURES=1
SH_LDFLAGS += -fno-lto -Wl,--gc-sections \
  -Wl,--defsym=sm64_saturn_camera_variant_marker=$(SATURN_CAMERA_VARIANT) \
  -Wl,--defsym=sm64_saturn_camera_route_marker=$(SATURN_SOURCEBOOT_CAMERA_ROUTE)
ifeq ($(SATURN_SOURCEBOOT_CAMERA_ROUTE),1)
SH_LDFLAGS += -Wl,--defsym=__sourceboot_required_hwram_margin=0x1B00
endif

# --- soft-float: GCC's soft-fp in place of libgcc's fp-bit ------------------
#
# WHY: the SH7604 has no FPU, so every f32 operation in the SM64 engine is a
# libgcc call, and the pinned GCC 14.3.0 selects `fp-bit.c` -- the portable C
# reference soft-float, and the slowest correct one there is. A float add
# there is four out-of-line calls (__unpack_f x2, __fpadd_parts, __pack_f)
# through a 56-byte stack frame. GCC 15 switched sh-elf to `soft-fp`, whose
# unpack/operate/pack are macro-expanded inline into a single leaf-ish call
# (upstream commit e95512e2d5a3, 2024-10-10, benchmarked at ~3x on Whetstone).
# See docs/saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md (~82% of SH-2 time is in
# soft-float) and docs/superpowers/specs/2026-07-26-soft-float-replacement-design.md.
#
# HOW: the entire GCC 15 change for sh-elf is (a) an 83-line
# libgcc/config/sh/sfp-machine.h and (b) one line of libgcc/config.host. The
# soft-fp sources themselves are byte-identical between GCC 14.3.0 and 15.2.0
# (verified file by file), so rather than mixing objects from a second
# toolchain -- or spending a day rebuilding libgcc -- this builds the same
# sources with THIS project's own pinned sh-elf-gcc 14.3.0 into a small
# archive, and links it ahead of -lgcc (see the LINK ORDER note below).
# Archive semantics do the rest: the soft-fp members satisfy ___addsf3 and
# friends first, so fp-bit's members are never pulled in at all.
#
# FLAGS: deliberately NOT $(SH_CFLAGS) and deliberately not -specs=sourceboot.specs.
# This is libgcc-class code and wants libgcc-class flags: -O2 (libgcc's own
# level; the engine's -Os would penalise the hottest code in the program) and,
# critically, NO -ffast-math. The specs file adds -ffast-math to every
# translation unit that goes through it, and -ffinite-math-only is precisely
# the wrong assumption to hand code whose entire job is classifying NaNs,
# infinities and subnormals.
SOFTFP_DIR := $(ROOT)/third_party/gcc-soft-fp
SOFTFP_BUILD_DIR := $(SH_BUILD_DIR)/softfp
SOFTFP_LIB := $(SOFTFP_BUILD_DIR)/libsm64softfp.a
# Exactly the file list GCC's libgcc/config/t-softfp-sfdf + t-softfp generate
# for softfp_float_modes="sf df" / softfp_int_modes="si di", minus the _BitInt
# helpers (nothing in this tree uses _BitInt, and fp-bit does not provide them
# either). Anything omitted here simply falls back to 14.3.0's libgcc.
SOFTFP_FUNCS := \
  addsf3 subsf3 mulsf3 divsf3 negsf2 eqsf2 gesf2 lesf2 unordsf2 \
  fixsfsi fixunssfsi floatsisf floatunsisf \
  fixsfdi fixunssfdi floatdisf floatundisf \
  adddf3 subdf3 muldf3 divdf3 negdf2 eqdf2 gedf2 ledf2 unorddf2 \
  fixdfsi fixunsdfsi floatsidf floatunsidf \
  fixdfdi fixunsdfdi floatdidf floatundidf \
  extendsfdf2 truncdfsf2
SOFTFP_OBJS := $(patsubst %,$(SOFTFP_BUILD_DIR)/%.o,$(SOFTFP_FUNCS))
SOFTFP_CFLAGS := -O2 -g -std=gnu11 -m2 -mb -fno-lto -ffreestanding \
  -fomit-frame-pointer -ffunction-sections -fdata-sections \
  -I$(SOFTFP_DIR)/soft-fp -I$(SOFTFP_DIR)/config/sh -I$(SOFTFP_DIR)/include
SOFTFP_HEADERS := $(wildcard $(SOFTFP_DIR)/soft-fp/*.h) \
  $(SOFTFP_DIR)/config/sh/sfp-machine.h $(SOFTFP_DIR)/include/longlong.h

# LINK ORDER, and this is the whole trick -- it is not where you would guess.
# Putting `-lsm64softfp` into sourceboot.specs' `*lib` line does NOT work:
# GCC's link_command expands `%(link_gcc_c_sequence)` as `%G %L %G`, i.e.
# `-lgcc` comes BEFORE the `*lib` spec, so libgcc's fp-bit members are already
# pulled in by the time the archive is reached (observed directly in the .map
# LOAD order: libgcc.a, libgcc-Os-4-200.a, libyaul.a, libsm64softfp.a,
# libgcc.a, ...). SH_LDFLAGS, by contrast, lands in `%o` -- the command line's
# own input-file list, after $(SH_OBJS_UNIQ) and before every spec-supplied
# library -- which is exactly the slot this needs. libyaul is unaffected: it
# has zero undefined soft-float symbols, so nothing of its is stranded behind
# the substitution. The four integer helpers soft-fp imports (___clzsi2,
# ___ashlsi3_r0, ___lshrsi3_r0, ___udiv_qrnnd_16) resolve forward out of the
# later libgcc, which is a normal archive-ordering resolution.
SH_LDFLAGS += -L$(SOFTFP_BUILD_DIR) -lsm64softfp

$(SOFTFP_BUILD_DIR)/%.o: $(SOFTFP_DIR)/soft-fp/%.c $(SOFTFP_HEADERS)
	@mkdir -p "$(@D)"
	$(ECHO)$(SH_CC) $(SOFTFP_CFLAGS) -c "$<" -o "$@"

$(SOFTFP_LIB): $(SOFTFP_OBJS)
	@printf -- "$(V_BEGIN_YELLOW)$(@F)$(V_END)\n"
	$(ECHO)rm -f "$@"
	$(ECHO)$(SH_AR) rcs "$@" $(SOFTFP_OBJS)

IP_VERSION := V0.001
IP_RELEASE_DATE := 20260719
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 SATURN SOURCEBOOT E2
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

$(SOURCEBOOT_GENERATED)/mario_anim_data.c: $(ROOT)/tools/mario_anims_converter.py $(wildcard $(ROOT)/assets/anims/*.inc.c)
	@mkdir -p "$(SOURCEBOOT_GENERATED)"
	@cd "$(ROOT)" && "$(SOURCEBOOT_PYTHON)" tools/mario_anims_converter.py > "$(SOURCEBOOT_GENERATED)/mario_anim_data.c"

# Grouped target (GNU Make >= 4.3): one run of the tool produces both files,
# so `&:` keeps make from scheduling the recipe twice under -j.
$(SOURCEBOOT_QUAD_MAP_C) $(SOURCEBOOT_QUAD_MAP_H) &: \
    $(SOURCEBOOT_QUAD_MAP_TOOLS) $(SOURCEBOOT_QUAD_MAP_SOURCES)
	@mkdir -p "$(SOURCEBOOT_GENERATED)"
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile.saturn.mk" \
	  compile-quad-map \
	  QUAD_MAP_GENERATED="$(SOURCEBOOT_GENERATED)" \
	  SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

.PHONY: source-quad-map
source-quad-map: $(SOURCEBOOT_QUAD_MAP_C)

.PHONY: source-data-catalog
source-data-catalog: $(SOURCEBOOT_COLLISION_CATALOG)

$(SOURCEBOOT_COLLISION_CATALOG): $(ROOT)/tools/saturn/prepare_sourceboot_collision_catalog.py $(SOURCEBOOT_COLLISION_INPUTS)
	@"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/prepare_sourceboot_collision_catalog.py" \
	  --root "$(ROOT)" --output "$@" \
	  $(foreach level,$(SOURCEBOOT_COLLISION_EXCLUDE_LEVELS),--exclude-level $(level))

.PHONY: source-assets
source-assets: source-data-catalog source-bob-mesh source-bob-bsp source-bob-bsp-fragments source-bob-tiles source-bob-scene source-bob-sky source-mario-textures
	@targets="$$($(SOURCEBOOT_PYTHON) $(ROOT)/tools/saturn/prepare_sourceboot_assets.py \
	  --root $(ROOT) --build-prefix build/us_pc \
	  --define VERSION_US --define VERSION_JP_US \
	  $(foreach source,$(SOURCEBOOT_ASSET_SOURCES),--source $(source)))"; \
	if [ -z "$$targets" ]; then \
	  printf '%s\n' 'sourceboot: no generated source assets were found' >&2; exit 1; \
	fi; \
	$(MAKE) -C "$(ROOT)" TARGET_N64=0 VERSION=us \
	  PYTHON="$(SOURCEBOOT_PYTHON)" $$targets $(SOURCEBOOT_TEXT_STRINGS) \
	  $(patsubst $(ROOT)/%,%,$(SOURCEBOOT_WATER_SKYBOX))
	@printf '%s\n' 'sourceboot: local source asset includes are ready'

$(SOURCEBOOT_BOB_MESH) $(SOURCEBOOT_BOB_COMPILED) $(SOURCEBOOT_BOB_REPORT) &: \
    $(SOURCEBOOT_BOB_TOOLS) $(SOURCEBOOT_BOB_SOURCES)
	@mkdir -p "$(SOURCEBOOT_GENERATED)"
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile.saturn.mk" \
	  compile-bob-area BOB_MESH_GENERATED="$(SOURCEBOOT_GENERATED)" \
	  BOB_AREA_DIR="levels/bob/areas/1" SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

.PHONY: source-bob-mesh
source-bob-mesh: $(SOURCEBOOT_BOB_COMPILED)

$(SOURCEBOOT_BOB_BSP_REPORT) $(SOURCEBOOT_BOB_BSP_HEADER): $(SOURCEBOOT_BOB_COMPILED) $(SOURCEBOOT_BOB_TILE_MANIFEST) $(ROOT)/tools/saturn/compile_bob_bsp.py $(ROOT)/tools/saturn/static_bsp.py
	@$(SOURCEBOOT_PYTHON) "$(ROOT)/tools/saturn/compile_bob_bsp.py" \
	  --input "$(SOURCEBOOT_BOB_COMPILED)" \
  --output "$(SOURCEBOOT_BOB_BSP_REPORT)" --header "$(SOURCEBOOT_BOB_BSP_HEADER)" \
  --manifest "$(SOURCEBOOT_BOB_TILE_MANIFEST)"

.PHONY: source-bob-bsp
source-bob-bsp: $(SOURCEBOOT_BOB_BSP_REPORT) $(SOURCEBOOT_BOB_BSP_HEADER)

$(SOURCEBOOT_BOB_BSP_FRAGMENT_BANK) $(SOURCEBOOT_BOB_BSP_FRAGMENT_CLUT) $(SOURCEBOOT_BOB_BSP_FRAGMENT_MANIFEST) $(SOURCEBOOT_BOB_BSP_FRAGMENT_SCENE): $(SOURCEBOOT_BOB_COMPILED) $(ROOT)/tools/saturn/bake_bob_bsp_fragments.py $(ROOT)/tools/saturn/compile_bob_bsp.py $(ROOT)/tools/saturn/static_bsp.py $(ROOT)/tools/saturn/bake_bob_tiles.py $(ROOT)/tools/saturn/bake_castle_uv.py
	@$(MAKE) -f "$(ROOT)/Makefile.saturn.mk" OS=Windows_NT \
	  compile-bob-bsp-fragments BOB_MESH_GENERATED="$(SOURCEBOOT_GENERATED)" \
	  BOB_ASSET_ROOT="$(ROOT)" SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

.PHONY: source-bob-bsp-fragments
source-bob-bsp-fragments: $(SOURCEBOOT_BOB_BSP_FRAGMENT_BANK) $(SOURCEBOOT_BOB_BSP_FRAGMENT_CLUT) $(SOURCEBOOT_BOB_BSP_FRAGMENT_MANIFEST) $(SOURCEBOOT_BOB_BSP_FRAGMENT_SCENE) $(SOURCEBOOT_BOB_BSP_FRAGMENT_HEADER)

$(SOURCEBOOT_BOB_BSP_FRAGMENT_HEADER): $(SOURCEBOOT_BOB_BSP_FRAGMENT_SCENE) $(SOURCEBOOT_BOB_COMPILED) $(ROOT)/tools/saturn/emit_bob_bsp_fragments.py
	@$(SOURCEBOOT_PYTHON) "$(ROOT)/tools/saturn/emit_bob_bsp_fragments.py" \
	  --mesh "$(SOURCEBOOT_BOB_COMPILED)" --scene "$(SOURCEBOOT_BOB_BSP_FRAGMENT_SCENE)" \
	  --output "$@"

$(SOURCEBOOT_BOB_TILES) $(SOURCEBOOT_BOB_CLUT) $(SOURCEBOOT_BOB_TILE_MANIFEST) &: \
	$(SOURCEBOOT_BOB_MESH) $(SOURCEBOOT_BOB_TILE_TOOLS) \
	$(SOURCEBOOT_GENERATED)/bob_area1_intake.json
	@$(MAKE) -f "$(ROOT)/Makefile.saturn.mk" OS=Windows_NT \
	  compile-bob-tiles BOB_TILES_GENERATED="$(SOURCEBOOT_GENERATED)" \
	  BOB_MESH_GENERATED="$(SOURCEBOOT_GENERATED)" \
	  BOB_AREA_DIR="levels/bob/areas/1" SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

.PHONY: source-bob-tiles
source-bob-tiles: $(SOURCEBOOT_BOB_TILES) $(SOURCEBOOT_BOB_CLUT) $(SOURCEBOOT_BOB_TILE_MANIFEST)

$(SOURCEBOOT_BOB_SCENE): $(SOURCEBOOT_BOB_COMPILED) $(SOURCEBOOT_BOB_TILE_MANIFEST) $(SOURCEBOOT_BOB_SCENE_TOOL)
	@$(MAKE) -f "$(ROOT)/Makefile.saturn.mk" OS=Windows_NT \
	  compile-bob-scene BOB_TILES_GENERATED="$(SOURCEBOOT_GENERATED)" \
	  BOB_MESH_GENERATED="$(SOURCEBOOT_GENERATED)" SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

.PHONY: source-bob-scene
source-bob-scene: $(SOURCEBOOT_BOB_SCENE)

$(SOURCEBOOT_BOB_SKY) $(SOURCEBOOT_BOB_SKY_MANIFEST) &: $(ROOT)/tools/saturn/bake_bob_sky.py $(ROOT)/textures/skyboxes/water.png
	@mkdir -p "$(SOURCEBOOT_GENERATED)"
	@$(MAKE) --no-print-directory -f "$(ROOT)/Makefile.saturn.mk" OS=Windows_NT \
	  compile-bob-sky BOB_SKY_SOURCE="textures/skyboxes/water.png" \
	  BOB_SKY_OUTPUT="$(SOURCEBOOT_BOB_SKY)" \
	  BOB_SKY_MANIFEST="$(SOURCEBOOT_BOB_SKY_MANIFEST)" SATURN_TOOLS_PYTHON="$(SOURCEBOOT_PYTHON)"

.PHONY: source-bob-sky
source-bob-sky: $(SOURCEBOOT_BOB_SKY) $(SOURCEBOOT_BOB_SKY_MANIFEST)

$(SOURCEBOOT_MARIO_TEXTURE_HEADER) $(SOURCEBOOT_MARIO_TEXTURE_REPORT) &: \
	$(ROOT)/tools/saturn/bake_mario_eye_uv.py \
	$(ROOT)/tools/saturn/extract_mario_textures.py \
	$(ROOT)/tools/saturn/vdp1_texture.py \
	$(ROOT)/assets.json $(ROOT)/baserom.us.z64 \
	$(ROOT)/docs/saturn/evidence/reports/mario-actor-intake.json
	@mkdir -p "$(SOURCEBOOT_MARIO_TEXTURE_DIR)"
	@$(SOURCEBOOT_PYTHON) "$(ROOT)/tools/saturn/bake_mario_eye_uv.py" \
	  --rom "$(ROOT)/baserom.us.z64" --assets "$(ROOT)/assets.json" \
	  --intake "$(ROOT)/docs/saturn/evidence/reports/mario-actor-intake.json" \
	  --tile 16 --source-scale 1 --subdivision 1 \
	  --output "$(SOURCEBOOT_MARIO_TEXTURE_HEADER)" \
	  --report "$(SOURCEBOOT_MARIO_TEXTURE_REPORT)"

.PHONY: source-mario-textures
source-mario-textures: $(SOURCEBOOT_MARIO_TEXTURE_HEADER) $(SOURCEBOOT_MARIO_TEXTURE_REPORT)

# Keep the large baked bank binary, rather than a generated C initializer, so
# the source cart remains compact and the linker can place it in .cart_rodata
# alongside the other immutable source assets.  The generated assembly is in
# the variant object tree, so demo/interpreted builds cannot cross-contaminate.
$(SOURCEBOOT_BOB_TEXTURE_ASM): $(SOURCEBOOT_BOB_TILES) $(SOURCEBOOT_BOB_CLUT)
	@mkdir -p "$(dir $@)"
	@printf '%s\n' \
	  '.section .rodata' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_texture_bank' \
	  '_sm64_saturn_bob_texture_bank:' \
	  '.incbin "$(subst \\,/,$(SOURCEBOOT_BOB_TILES))"' \
	  '.global _sm64_saturn_bob_texture_bank_end' \
	  '_sm64_saturn_bob_texture_bank_end:' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_clut_bank' \
	  '_sm64_saturn_bob_clut_bank:' \
	  '.incbin "$(subst \\,/,$(SOURCEBOOT_BOB_CLUT))"' \
	  '.global _sm64_saturn_bob_clut_bank_end' \
	  '_sm64_saturn_bob_clut_bank_end:' > "$@"

$(SOURCEBOOT_BOB_BSP_FRAGMENT_TEXTURE_ASM): $(SOURCEBOOT_BOB_BSP_FRAGMENT_BANK) $(SOURCEBOOT_BOB_BSP_FRAGMENT_CLUT)
	@mkdir -p "$(dir $@)"
	@printf '%s\n' \
	  '.section .rodata' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_fragment_texture_bank' \
	  '_sm64_saturn_bob_fragment_texture_bank:' \
	  '.incbin "$(subst \\,/,$(SOURCEBOOT_BOB_BSP_FRAGMENT_BANK))"' \
	  '.global _sm64_saturn_bob_fragment_texture_bank_end' \
	  '_sm64_saturn_bob_fragment_texture_bank_end:' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_fragment_clut_bank' \
	  '_sm64_saturn_bob_fragment_clut_bank:' \
	  '.incbin "$(subst \\,/,$(SOURCEBOOT_BOB_BSP_FRAGMENT_CLUT))"' \
	  '.global _sm64_saturn_bob_fragment_clut_bank_end' \
	  '_sm64_saturn_bob_fragment_clut_bank_end:' > "$@"

$(SOURCEBOOT_BOB_SKY_ASM): $(SOURCEBOOT_BOB_SKY)
	@mkdir -p "$(dir $@)"
	@printf '%s\n' \
	  '.section .rodata' \
	  '.align 4' \
	  '.global _sm64_saturn_bob_sky_bitmap' \
	  '_sm64_saturn_bob_sky_bitmap:' \
	  '.incbin "$(subst \,/,$(SOURCEBOOT_BOB_SKY))"' \
	  '.global _sm64_saturn_bob_sky_bitmap_end' \
	  '_sm64_saturn_bob_sky_bitmap_end:' \
	  '.align 4' > "$@"

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

# Yaul's bundled rule declares the CUE's ISO prerequisite order-only. That
# leaves a stale CUE whenever the ISO/ELF is rebuilt in place, and capture's
# artifact preflight correctly rejects that mismatch. Re-state the
# prerequisite as normal so GNU make regenerates the CUE whenever the exact
# image it describes changes.
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso

# Any translation unit may include the generated quad-map header, so no
# compile may start before it exists; under -j make would otherwise happily
# race a consumer against the generator. Order-only, so a regenerated map does
# not by itself rebuild the world -- the generated .d files carry the real
# header dependency once a build has run.
$(SH_OBJS_UNIQ): | $(SOURCEBOOT_QUAD_MAP_H) $(SOURCEBOOT_MARIO_TEXTURE_HEADER)

# The link resolves -lsm64softfp out of $(SOFTFP_BUILD_DIR) (both the -L and
# the -l are in SH_LDFLAGS above), and Yaul's .elf rule cannot know about it,
# so state the dependency here.
$(SH_BUILD_PATH)/$(SH_PROGRAM).elf: $(SOFTFP_LIB)

$(SOURCEBOOT_CART_IMAGE): $(SH_BUILD_PATH)/$(SH_PROGRAM).elf
	@printf -- "$(V_BEGIN_YELLOW)$(@F)$(V_END)\n"
	$(ECHO)$(SOURCEBOOT_RAW_OBJCOPY) -O binary --only-section=.cart_rodata $< $@
	$(ECHO)test $$(wc -c < $@) -le 4194304

.PHONY: source-cart-image
source-cart-image: $(SOURCEBOOT_CART_IMAGE)

pre-build-iso: $(SOURCEBOOT_CART_IMAGE)
	$(ECHO)mkdir -p $(IMAGE_DIRECTORY)
	$(ECHO)cp $(SOURCEBOOT_CART_IMAGE) $(IMAGE_DIRECTORY)/SOURCE.DAT
# Patch the disc-header Product ID (IP.BIN offset 0x20, 10 bytes). Yaul's
# ip.sx template hardcodes "T-99901G  " for every homebrew, but that code
# belongs to a real title (Real Bout Garou Densetsu Demo) in emulator game
# databases -- Ymir's DB maps it to an 8Mbit DRAM cart, which silently
# overrides the configured 32Mbit/4MiB cart and makes the source-cart
# loader fail with MISSING_4MIB before any display init (observed as a
# black screen at 100% speed). A unique ID keeps auto-cart heuristics from
# ever matching this disc. The submodule template stays untouched per the
# read-only-dependencies rule; this is a post-generation byte patch.
	$(ECHO)printf 'T-SM64SB01' | dd of=$(SH_BUILD_PATH)/IP.BIN bs=1 seek=32 conv=notrunc status=none

# ISO freshness must include the separately emitted cartridge image; the Yaul
# hook copies it into IMAGE_DIRECTORY, but cannot express that dependency by
# itself.
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(SOURCEBOOT_CART_IMAGE)

.PHONY: verify verify-engine-atan2-q16 verify-engine-atan2-q16-mutation
verify-engine-atan2-q16:
	@"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/test_engine_atan2_q16.py"

verify-engine-atan2-q16-mutation:
	@"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_engine_atan2_mutation.py" \
	  --python "$(SOURCEBOOT_PYTHON)" \
	  --test "$(ROOT)/tools/saturn/test_engine_atan2_q16.py"

verify: verify-engine-atan2-q16 verify-engine-atan2-q16-mutation
verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@$(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf -h "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" | grep -Eq 'Entry point address:[[:space:]]+0x6004000'
	@$(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf -S "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" | grep -Eq '[[:space:]]\.cart_rodata[[:space:]]+PROGBITS[[:space:]]+22400000'
	@test $$(wc -c < "$(SOURCEBOOT_CART_IMAGE)") -le 4194304
# The soft-fp substitution is a link-order effect, so it can regress silently
# the moment anything reorders -lgcc or drops the -L. Gate on it: these seven
# symbols exist ONLY inside libgcc's fp-bit.c, so their presence in the linked
# image means fp-bit was pulled in after all, and their absence means every
# soft-float entry point resolved out of libsm64softfp.a. Checked as a set,
# because a partial substitution (say, SF from soft-fp and DF from fp-bit) is
# both possible and worth catching.
	@if $(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-nm "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" \
	    | grep -Eq '[[:space:]](___pack_f|___unpack_f|___pack_d|___unpack_d|__fpadd_parts|___fpcmp_parts_f|___fpcmp_parts_d)$$'; then \
	  printf '%s\n' 'sourceboot: libgcc fp-bit soft-float is linked; expected soft-fp' >&2; \
	  exit 1; \
	fi
# ...and the converse, so an empty/missing archive cannot pass the check above
# by accident: the soft-fp entry points must actually be present.
	@for sym in ___addsf3 ___subsf3 ___mulsf3 ___divsf3 ___floatsisf ___fixsfsi; do \
	  $(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-nm "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" \
	    | grep -Eq "[[:space:]]T[[:space:]]$$sym\$$" || { \
	    printf '%s\n' "sourceboot: $$sym missing from the linked image" >&2; exit 1; }; \
	done
	@"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_q16_sh2_disassembly.py" \
	  "$(SH_BUILD_PATH)/$(SH_PROGRAM).asm"
# Dual-CPU coherency gate: cross-CPU fence flags and result-span headers must
# resolve into the uncached partition (>= 0x20000000). Cached placements were
# the Pipe 5 disappearing-geometry root cause. Demo-path images only -- the
# default profile does not link the demo renderer's shared state.
	@if [ "$(SATURN_DEMO_PATH)" = "1" ]; then \
	  audit_args=""; \
	  if [ "$(SATURN_SOURCEBOOT_ROUTE_REPLAY)" = "1" ]; then \
	    audit_args="--audit-route-oracle $(SOURCEBOOT_NATIVE_MATH_SIM_ROUTE_ORACLE) --audit-contract $(SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT)"; \
	  fi; \
	  "$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_sh2_native_math.py" \
	    "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" "$(SOURCEBOOT_NATIVE_MATH_BASELINE)" \
	    --route-oracle "$(SOURCEBOOT_NATIVE_MATH_ROUTE_ORACLE)" \
	    $$audit_args \
	    --objdump "$(SOURCEBOOT_SH_OBJDUMP)" \
	    --readelf "$(SOURCEBOOT_SH_READELF)" \
	    --addr2line "$(SOURCEBOOT_SH_ADDR2LINE)" || exit $$?; \
	  "$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_dual_cpu_coherency.py" \
	    "$(SH_BUILD_PATH)/$(SH_PROGRAM).sym"; \
	fi

.PHONY: verify-sim-math-route verify-sim-math-route-config
verify-sim-math-route: verify-sim-math-route-config $(SH_BUILD_PATH)/$(SH_PROGRAM).elf
	@"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_sh2_native_math.py" \
	  "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" "$(SOURCEBOOT_NATIVE_MATH_BASELINE)" \
	  --route-oracle "$(SOURCEBOOT_NATIVE_MATH_ROUTE_ORACLE)" \
	  --audit-route-oracle "$(SOURCEBOOT_NATIVE_MATH_SIM_ROUTE_ORACLE)" \
	  --audit-contract "$(SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT)" \
	  --objdump "$(SOURCEBOOT_SH_OBJDUMP)" \
	  --readelf "$(SOURCEBOOT_SH_READELF)" \
	  --addr2line "$(SOURCEBOOT_SH_ADDR2LINE)"

verify-sim-math-route-config:
	@if [ "$(SATURN_DEMO_PATH)" != "1" ] || [ "$(SATURN_SOURCEBOOT_ROUTE_REPLAY)" != "1" ]; then \
	  printf '%s\n' 'verify-sim-math-route requires SATURN_DEMO_PATH=1 and SATURN_SOURCEBOOT_ROUTE_REPLAY=1' >&2; \
	  exit 2; \
	fi
