#ifndef SM64_SATURN_AUDIO68K_SEQUENCE_VM_H
#define SM64_SATURN_AUDIO68K_SEQUENCE_VM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * This is a bounded, pointer-free control-flow model for the source M64
 * sequence language.  It deliberately stops before bank lookup, sample
 * allocation, or SCSP register writes.  The same scalar layout can therefore
 * be compiled by the freestanding MC68000 image and by the host contract
 * fixture without importing the N64 audio heap.
 */
enum {
    SM64_SATURN_SEQUENCE_VM_STACK_DEPTH = 8U,
    SM64_SATURN_SEQUENCE_VM_MAX_EVENTS = 24U,
    SM64_SATURN_SEQUENCE_VM_INSTRUCTION_BUDGET = 64U,
    SM64_SATURN_SEQUENCE_VM_DEFAULT_TEMPO = 120U * 48U,
    SM64_SATURN_SEQUENCE_VM_DEFAULT_PLAY_PERCENTAGE = 0x80U,
};

typedef enum sm64_saturn_sequence_vm_mode {
    SM64_SATURN_SEQUENCE_VM_SEQUENCE = 0,
    SM64_SATURN_SEQUENCE_VM_LAYER_SMALL = 1,
    SM64_SATURN_SEQUENCE_VM_LAYER_LARGE = 2,
} sm64_saturn_sequence_vm_mode_t;

typedef enum sm64_saturn_sequence_vm_format {
    SM64_SATURN_SEQUENCE_VM_FORMAT_US = 0,
    SM64_SATURN_SEQUENCE_VM_FORMAT_EU_SH = 1,
} sm64_saturn_sequence_vm_format_t;

typedef enum sm64_saturn_sequence_vm_event_type {
    SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL = 1,
    SM64_SATURN_SEQUENCE_VM_EVENT_DELAY = 2,
    SM64_SATURN_SEQUENCE_VM_EVENT_TEMPO = 3,
    SM64_SATURN_SEQUENCE_VM_EVENT_VOLUME = 4,
    SM64_SATURN_SEQUENCE_VM_EVENT_TRANSPOSE = 5,
    SM64_SATURN_SEQUENCE_VM_EVENT_NOTE = 6,
    SM64_SATURN_SEQUENCE_VM_EVENT_CHANNEL_START = 7,
    SM64_SATURN_SEQUENCE_VM_EVENT_END = 8,
} sm64_saturn_sequence_vm_event_type_t;

typedef struct sm64_saturn_sequence_vm_event {
    uint8_t type;
    uint8_t opcode;
    uint16_t arg0;
    uint16_t arg1;
    uint16_t arg2;
    int16_t signed_value;
    uint16_t source_offset;
} sm64_saturn_sequence_vm_event_t;

typedef struct sm64_saturn_sequence_vm {
    /* All execution locations are offsets into the caller-owned byte span. */
    uint16_t data_length;
    uint16_t pc;
    uint16_t delay;
    uint16_t tempo;
    uint16_t tempo_limit;
    uint16_t tempo_accumulator;
    uint16_t default_play_percentage;
    uint16_t play_percentage;
    uint16_t channel_active_mask;
    uint16_t channel_finished_mask;
    uint8_t mode;
    uint8_t format;
    uint8_t enabled;
    uint8_t faulted;
    uint8_t halted;
    uint8_t stack_depth;
    int16_t value;
    uint8_t variation;
    uint8_t volume;
    uint8_t muted;
    uint8_t layer_velocity;
    uint8_t layer_note_duration;
    int16_t transpose;
    uint16_t stack[SM64_SATURN_SEQUENCE_VM_STACK_DEPTH];
    uint8_t frame_kind[SM64_SATURN_SEQUENCE_VM_STACK_DEPTH];
    uint8_t loop_remaining[SM64_SATURN_SEQUENCE_VM_STACK_DEPTH];
    uint32_t tick_count;
    uint32_t instruction_count;
} sm64_saturn_sequence_vm_t;

void sm64_saturn_sequence_vm_init(sm64_saturn_sequence_vm_t *vm,
                                  uint16_t data_length, uint16_t entry_offset,
                                  sm64_saturn_sequence_vm_mode_t mode);

void sm64_saturn_sequence_vm_init_ex(
    sm64_saturn_sequence_vm_t *vm, uint16_t data_length,
    uint16_t entry_offset, sm64_saturn_sequence_vm_mode_t mode,
    sm64_saturn_sequence_vm_format_t format);

bool sm64_saturn_sequence_vm_tick(
    sm64_saturn_sequence_vm_t *vm, const uint8_t *sequence,
    uint16_t sequence_length, sm64_saturn_sequence_vm_event_t *events,
    uint8_t event_capacity, uint8_t *event_count);

#endif
