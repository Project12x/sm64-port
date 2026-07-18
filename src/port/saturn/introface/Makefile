ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env)
endif
ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST))))/../../../..)
SH_PROGRAM := sm64-saturn-introface
SH_SRCS := main.c
SH_BUILD_DIR := $(ROOT)/build/saturn/introface/obj
SH_OUTPUT_DIR := $(ROOT)/build/saturn/introface
IMAGE_DIRECTORY := $(ROOT)/build/saturn/introface/cd
export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(ROOT)/tools/saturn/xorrisofs-reproducible
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
SH_CFLAGS += -Os -g -DDEBUG
TITLE_FONT_DIR := $(ROOT)/build/saturn/introface/generated
ifneq ($(wildcard $(TITLE_FONT_DIR)/mario_title_font_generated.h),)
SH_CFLAGS += -I$(TITLE_FONT_DIR) -DSM64_TITLE_FONT_GENERATED
endif
IP_VERSION := V0.001
IP_RELEASE_DATE := 20260717
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 SATURN INTRO FACE
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
.PHONY: verify
verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@$(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf -h "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" | grep -Eq 'Entry point address:[[:space:]]+0x6004000'
