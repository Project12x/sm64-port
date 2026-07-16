ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env and see docs/saturn/BUILDING.md)
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

HELLO_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
REPO_ROOT := $(abspath $(HELLO_ROOT)/../../../..)

SH_PROGRAM := sm64-saturn-hello
SH_SRCS := main.c

SH_BUILD_DIR := $(REPO_ROOT)/build/saturn/hello/obj
SH_OUTPUT_DIR := $(REPO_ROOT)/build/saturn/hello
IMAGE_DIRECTORY := $(REPO_ROOT)/build/saturn/hello/cd
AUDIO_TRACKS_DIRECTORY := $(REPO_ROOT)/build/saturn/hello/audio-tracks

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
