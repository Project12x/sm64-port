ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env and see docs/saturn/BUILDING.md)
endif

HELLO_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
REPO_ROOT := $(abspath $(HELLO_ROOT)/../../../..)

SH_PROGRAM := sm64-saturn-hello
SH_SRCS := main.c

SH_BUILD_DIR := $(REPO_ROOT)/build/saturn/hello/obj
SH_OUTPUT_DIR := $(REPO_ROOT)/build/saturn/hello
IMAGE_DIRECTORY := $(REPO_ROOT)/build/saturn/hello/cd
AUDIO_TRACKS_DIRECTORY := $(REPO_ROOT)/build/saturn/hello/audio-tracks

# 2026-07-16 00:00:00 UTC. The xorrisofs wrapper uses this epoch together
# with explicit volume/node dates to keep clean disc builds reproducible.
export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(REPO_ROOT)/tools/saturn/xorrisofs-reproducible

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH_CFLAGS += -Os -g -DDEBUG -I.
SH_LDFLAGS +=

IP_VERSION := V0.001
IP_RELEASE_DATE := 20260716
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 SATURN HELLO
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

.PHONY: verify

verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@header="$$($(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf \
	  -h "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf")"; \
	printf '%s\n' "$$header" | grep -Eq 'Class:[[:space:]]+ELF32' && \
	printf '%s\n' "$$header" | grep -Eq "Data:[[:space:]]+2.s complement, big endian" && \
	printf '%s\n' "$$header" | grep -Eq 'Machine:[[:space:]]+Renesas / SuperH SH' && \
	printf '%s\n' "$$header" | grep -Eq 'Entry point address:[[:space:]]+0x6004000' && \
	printf '%s\n' "$$header" | grep -Eq 'Flags:[[:space:]]+0x2, sh2' || { \
	  printf '%s\n' "$$header" >&2; \
	  printf '%s\n' 'Unexpected Saturn ELF header' >&2; \
	  exit 1; \
	}
	@printf '%s\n' 'Verified ELF32 big-endian SH-2 image at 0x06004000'
