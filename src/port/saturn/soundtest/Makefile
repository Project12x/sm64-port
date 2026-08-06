ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
  $(error Undefined YAUL_INSTALL_ROOT; source .yaul.env and see docs/saturn/BUILDING.md)
endif

SOUNDTEST_ROOT := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
REPO_ROOT := $(abspath $(SOUNDTEST_ROOT)/../../../..)
GENERATED := $(REPO_ROOT)/build/saturn/soundtest/generated
ASSETS_SX := $(GENERATED)/soundtest_assets.sx
PCM68K_BIN := $(REPO_ROOT)/build/saturn/audio68k/pcm68k-heartbeat.bin
PCM_BANK := $(GENERATED)/pcm_proof_bank.bin

SH_PROGRAM := sm64-saturn-soundtest
SH_SRCS := main.c soundtest_boot.c \
  $(REPO_ROOT)/src/port/saturn/audio/saturn_sound_cpu.c \
  $(REPO_ROOT)/src/port/saturn/audio/saturn_pcm_transport.c \
  $(ASSETS_SX)
SH_BUILD_DIR := $(REPO_ROOT)/build/saturn/soundtest/obj
SH_OUTPUT_DIR := $(REPO_ROOT)/build/saturn/soundtest
IMAGE_DIRECTORY := $(REPO_ROOT)/build/saturn/soundtest/cd
AUDIO_TRACKS_DIRECTORY := $(REPO_ROOT)/build/saturn/soundtest/audio-tracks

export SOURCE_DATE_EPOCH ?= 1784160000
export MAKE_ISO_XORRISOFS := $(REPO_ROOT)/tools/saturn/xorrisofs-reproducible

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH_CFLAGS += -Os -g -DDEBUG -I. -I$(REPO_ROOT)/src/port/saturn/audio

IP_VERSION := V0.001
IP_RELEASE_DATE := 20260804
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := JAMKST
IP_TITLE := SM64 PCM68K SOUNDTEST
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

$(ASSETS_SX): $(PCM68K_BIN) $(PCM_BANK)
	@mkdir -p "$(dir $@)"
	@printf '%s\n' \
	  '.section .rodata' \
	  '.align 4' \
	  '.global _sm64_saturn_pcm68k_driver' \
	  '_sm64_saturn_pcm68k_driver:' \
	  '.incbin "$(subst \,/,$(PCM68K_BIN))"' \
	  '.global _sm64_saturn_pcm68k_driver_end' \
	  '_sm64_saturn_pcm68k_driver_end:' \
	  '.align 4' \
	  '.global _sm64_saturn_pcm_proof_bank' \
	  '_sm64_saturn_pcm_proof_bank:' \
	  '.incbin "$(subst \,/,$(PCM_BANK))"' \
	  '.global _sm64_saturn_pcm_proof_bank_end' \
	  '_sm64_saturn_pcm_proof_bank_end:' > "$@"

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

.PHONY: verify
verify: $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	@test -s "$(PCM68K_BIN)"
	@test -s "$(PCM_BANK)"
	@test -s "$(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue"
	@printf '%s\n' 'Verified standalone PCM68K soundtest CUE inputs'
