ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env)
endif
ROOT := $(abspath $(dir $(realpath $(firstword $(MAKEFILE_LIST))))/../../../..)
SH_PROGRAM := sm64-saturn-mario-turntable
SH_SRCS := main.c \
  $(ROOT)/src/port/saturn/controller/controller_saturn.c \
  $(ROOT)/src/port/saturn/compat/sqrtf.c \
  $(ROOT)/src/game/game_init.c \
  $(ROOT)/src/game/mario.c \
  $(ROOT)/src/engine/math_util.c
SH_BUILD_DIR := $(ROOT)/build/saturn/marioturntable/obj
SH_OUTPUT_DIR := $(ROOT)/build/saturn/marioturntable
IMAGE_DIRECTORY := $(ROOT)/build/saturn/marioturntable/cd
export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(ROOT)/tools/saturn/xorrisofs-reproducible
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
SH_CFLAGS += -Os -g -DDEBUG -DNON_MATCHING=1 -DAVOID_UB=1 -DVERSION_US=1 -DTARGET_SATURN=1 \
  -I$(ROOT) \
  -I$(ROOT)/src/port/saturn/compat \
  -I$(ROOT)/include \
  -I$(ROOT)/src \
  -I$(ROOT)/src/game \
  -I$(ROOT)/src/engine \
  -I$(ROOT)/src/port/saturn/controller \
  -I$(ROOT)/build/saturn/marioturntable/generated
IP_VERSION := V0.001
IP_RELEASE_DATE := 20260718
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 SATURN MARIO M2
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
.PHONY: verify
verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@$(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf -h "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" | grep -Eq 'Entry point address:[[:space:]]+0x6004000'
