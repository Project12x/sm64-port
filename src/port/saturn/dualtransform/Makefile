ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env)
endif

ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST))))/../../../..)
SH_PROGRAM := sm64-saturn-dual-transform-smoke
SH_SRCS := main.c $(ROOT)/src/port/saturn/gfx/saturn_ir_transform.c
SH_BUILD_DIR := $(ROOT)/build/saturn/dualtransform/obj
SH_OUTPUT_DIR := $(ROOT)/build/saturn/dualtransform
IMAGE_DIRECTORY := $(ROOT)/build/saturn/dualtransform/cd
AUDIO_TRACKS_DIRECTORY := $(ROOT)/build/saturn/dualtransform/audio-tracks

export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(ROOT)/tools/saturn/xorrisofs-reproducible
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
SH_CFLAGS += -Os -g -DDEBUG -I$(ROOT)/src/port/saturn/gfx
SH_LDFLAGS +=

IP_VERSION := V0.001
IP_RELEASE_DATE := 20260727
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 SATURN DUAL TRANSFORM SMOKE
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

.PHONY: verify
verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@header="$$($(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf -h "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf")"; \
	printf '%s\n' "$$header" | grep -Eq 'Entry point address:[[:space:]]+0x6004000'
