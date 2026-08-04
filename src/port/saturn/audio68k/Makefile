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
PYTHON ?= python3

ROOT := ../../../..
BUILD := $(ROOT)/build/saturn/audio68k
ELF := $(BUILD)/pcm68k-heartbeat.elf
BIN := $(BUILD)/pcm68k-heartbeat.bin
MAP := $(BUILD)/pcm68k-heartbeat.map
OBJECTS := $(BUILD)/start.o $(BUILD)/main.o $(BUILD)/pcm_voice.o

CPPFLAGS := -I. -I../audio
CFLAGS := -mc68000 -Os -std=c11 -ffreestanding -fno-builtin -fno-common \
	-mno-align-int -mno-strict-align -Wall -Wextra -Werror
ASFLAGS := -mc68000
LDFLAGS := -mc68000 -nostdlib -Wl,--no-undefined,-Map,$(MAP) -T linker.ld

.PHONY: all verify clean

all: $(ELF) $(BIN)

$(BUILD):
	mkdir -p $@

$(BUILD)/start.o: start.S | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(ASFLAGS) -c $< -o $@

$(BUILD)/main.o: main.c pcm68k_heartbeat.h pcm_voice.h ../audio/saturn_pcm_protocol.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/pcm_voice.o: pcm_voice.c pcm_voice.h ../audio/saturn_pcm_protocol.h | $(BUILD)
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJECTS) linker.ld
	$(CC) $(M68K_EXEC_PREFIX_FLAG) $(LDFLAGS) $(OBJECTS) -o $@

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

verify: all
	$(PYTHON) $(ROOT)/tools/saturn/verify_pcm68k_image.py \
		--elf $(ELF) --map $(MAP) --nm $(NM)

clean:
	rm -f $(OBJECTS) $(ELF) $(BIN) $(MAP)
