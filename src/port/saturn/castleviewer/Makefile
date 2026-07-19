ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env)
endif
ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST))))/../../../..)
SH_PROGRAM := sm64-saturn-castle-area1
SH_SRCS := main.c castle_graph_bridge.c \
  $(ROOT)/src/port/saturn/controller/controller_saturn.c \
  $(ROOT)/src/game/game_init.c \
  $(ROOT)/src/game/mario.c \
  $(ROOT)/src/port/saturn/compat/sqrtf.c \
  $(ROOT)/src/engine/geo_layout.c \
  $(ROOT)/src/engine/graph_node.c \
  $(ROOT)/src/engine/graph_node_manager.c \
  $(ROOT)/src/engine/math_util.c
SH_BUILD_DIR := $(ROOT)/build/saturn/castlearea/obj
SH_OUTPUT_DIR := $(ROOT)/build/saturn/castlearea
IMAGE_DIRECTORY := $(ROOT)/build/saturn/castlearea/cd
export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(ROOT)/tools/saturn/xorrisofs-reproducible
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
SH_CFLAGS += -Os -g -DDEBUG \
  -DNON_MATCHING=1 -DAVOID_UB=1 -DVERSION_US=1 -DTARGET_SATURN=1 -DNO_SEGMENTED_MEMORY=1 \
  -ffunction-sections -fdata-sections \
  -I$(ROOT)/src/port/saturn/compat \
  -I$(ROOT) -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT)/src/game -I$(ROOT)/src/engine \
  -I$(ROOT)/build/saturn/castlearea/generated \
  -I$(ROOT)/build/saturn/marioturntable/generated \
  -I$(ROOT)/src/port/saturn/marioturntable \
  -I$(ROOT)/src/port/saturn/controller
ifeq ($(TEXTURE_PROBE),1)
SH_CFLAGS += -DSM64_SATURN_TEXTURE_PROBE_CAMERA=1
endif
ifeq ($(PROJECTION_PROBE),1)
SH_CFLAGS += -DSM64_SATURN_TEXTURE_PROBE_PROJECTION=1
endif
ifeq ($(PAINTER_PROBE),1)
SH_CFLAGS += -DSM64_SATURN_TEXTURE_PROBE_PAINTER=1
endif
IP_VERSION := V0.001
IP_RELEASE_DATE := 20260718
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 SATURN CASTLE M4
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
.PHONY: verify
verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@$(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf -h "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" | grep -Eq 'Entry point address:[[:space:]]+0x6004000'
