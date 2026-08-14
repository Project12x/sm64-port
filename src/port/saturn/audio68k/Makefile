M68K_PREFIX ?= m68k-elf
M68K_BINDIR ?=
ifneq ($(strip $(M68K_BINDIR)),)
M68K_TOOL_PREFIX := $(M68K_BINDIR)/$(M68K_PREFIX)
M68K_EXEC_PREFIX_FLAG := -B$(M68K_BINDIR)/
else
M68K_TOOL_PREFIX := $(M68K_PREFIX)
M68K_EXEC_PREFIX_FLAG :=
endif
CC := $(M68K_TOOL_PREFIX)-gcc
OBJCOPY := $(M68K_TOOL_PREFIX)-objcopy
NM := $(M68K_TOOL_PREFIX)-nm
OBJDUMP := $(M68K_TOOL_PREFIX)-objdump
LD := $(M68K_TOOL_PREFIX)-ld
PYTHON ?= python3

ROOT := ../../../..
BUILD := $(ROOT)/build/saturn/audio68k
ELF := $(BUILD)/pcm68k-heartbeat.elf
BIN := $(BUILD)/pcm68k-heartbeat.bin
MAP := $(BUILD)/pcm68k-heartbeat.map
# Task 4 driver diet: the sequence VM / software voice engine objects
# (sequence_vm, audio_engine, voice_allocator, desired_voice) stay in the
# tree but are no longer linked into the image.  audio_freestanding remains:
# GCC emits memset for struct zero-initialization even with -fno-builtin.
OBJECTS := $(BUILD)/start.o $(BUILD)/main.o $(BUILD)/pcm_voice.o $(BUILD)/scsp_pcm8.o \
	$(BUILD)/audio_freestanding.o
TASK17_OBJECTS := $(BUILD)/audio_engine.o $(BUILD)/voice_allocator.o \
	$(BUILD)/desired_voice.o $(BUILD)/slot_shadow.o $(BUILD)/scsp_timer.o \
	$(BUILD)/audio_freestanding.o
TASK17_RELOC := $(BUILD)/task17-audio-modules.o
PROTOCOL_HEADER := ../audio/saturn_pcm_protocol.h

CPPFLAGS := -I. -I../audio
CFLAGS := -mc68000 -Os -std=c11 -ffreestanding -fno-builtin -fno-common \
	-fno-delete-null-pointer-checks -mno-align-int -mno-strict-align \
	-Wall -Wextra -Werror -DSM64_SATURN_PCM_MAPPED_ZERO=1
ASFLAGS := -mc68000
LDFLAGS := -mc68000 -nostdlib -Wl,--no-undefined,-Map,$(MAP) -T linker.ld

.PHONY: all modules verify clean

all: $(ELF) $(BIN)

modules: $(TASK17_RELOC)

$(BUILD):
	mkdir -p $@

$(BUILD)/start.o: start.S | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(ASFLAGS) -c $< -o $@

$(BUILD)/main.o: main.c pcm68k_heartbeat.h pcm_voice.h $(PROTOCOL_HEADER) | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/pcm_voice.o: pcm_voice.c pcm_voice.h $(PROTOCOL_HEADER) | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/sequence_vm.o: sequence_vm.c sequence_vm.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/scsp_pcm8.o: scsp_pcm8.c scsp_pcm8.h pcm_voice.h slot_shadow.h desired_voice.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/audio_engine.o: audio_engine.c audio_engine.h voice_allocator.h desired_voice.h sequence_vm.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/voice_allocator.o: voice_allocator.c voice_allocator.h desired_voice.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/desired_voice.o: desired_voice.c desired_voice.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/slot_shadow.o: slot_shadow.c slot_shadow.h desired_voice.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/scsp_timer.o: scsp_timer.c scsp_timer.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/audio_freestanding.o: audio_freestanding.c | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TASK17_RELOC): $(TASK17_OBJECTS)
	$(LD) -r $(TASK17_OBJECTS) -o $@
	$(PYTHON) -c "import subprocess; out=subprocess.check_output([r'$(NM)', '-u', r'$(TASK17_RELOC)'], text=True); assert not out.strip(), out"

$(ELF): $(OBJECTS) linker.ld
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(LDFLAGS) $(OBJECTS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

verify: all
	$(PYTHON) $(ROOT)/tools/saturn/verify_pcm68k_image.py \
		--elf $(ELF) --map $(MAP) --nm $(NM)
	$(PYTHON) $(ROOT)/tools/saturn/verify_pcm68k_zero_map.py \
		--elf $(ELF) --objdump $(OBJDUMP)

clean:
	rm -f $(OBJECTS) $(TASK17_OBJECTS) $(TASK17_RELOC) $(BUILD)/sequence_vm.o \
		$(ELF) $(BIN) $(MAP)
