#include "sequence_vm.h"

enum {
    SM64_SATURN_SEQUENCE_VM_FRAME_CALL = 1U,
    SM64_SATURN_SEQUENCE_VM_FRAME_LOOP = 2U,
};

static void vm_fault(sm64_saturn_sequence_vm_t *vm)
{
    vm->faulted = 1U;
    vm->enabled = 0U;
}

static int16_t vm_s8(uint8_t value)
{
    return (value & 0x80U) != 0U ? (int16_t)value - 256 : (int16_t)value;
}

static bool vm_advance_tatum(sm64_saturn_sequence_vm_t *vm)
{
    uint32_t sum = (uint32_t)vm->tempo_accumulator + vm->tempo;
    if (vm->tempo_limit == 0U) return false;
    if (sum < vm->tempo_limit) {
        vm->tempo_accumulator = (uint16_t)sum;
        return false;
    }
    sum -= vm->tempo_limit;
    vm->tempo_accumulator = (uint16_t)sum;
    return true;
}

static bool vm_emit(sm64_saturn_sequence_vm_t *vm,
                    sm64_saturn_sequence_vm_event_t *events,
                    uint8_t event_capacity, uint8_t *event_count,
                    uint8_t type, uint8_t opcode, uint16_t arg0,
                    uint16_t arg1, uint16_t arg2, int16_t signed_value,
                    uint16_t source_offset)
{
    sm64_saturn_sequence_vm_event_t *event;
    if (event_count == 0 || *event_count >= event_capacity || events == 0) {
        vm_fault(vm);
        return false;
    }
    event = &events[*event_count];
    event->type = type;
    event->opcode = opcode;
    event->arg0 = arg0;
    event->arg1 = arg1;
    event->arg2 = arg2;
    event->signed_value = signed_value;
    event->source_offset = source_offset;
    (*event_count)++;
    return true;
}

static bool vm_read_u8(const uint8_t *sequence, uint16_t length,
                       uint16_t *pc, uint8_t *value)
{
    if (sequence == 0 || pc == 0 || value == 0 || *pc >= length) return false;
    *value = sequence[*pc];
    (*pc)++;
    return true;
}

static bool vm_read_be16(const uint8_t *sequence, uint16_t length,
                         uint16_t *pc, uint16_t *value)
{
    uint8_t hi, lo;
    if (!vm_read_u8(sequence, length, pc, &hi) ||
        !vm_read_u8(sequence, length, pc, &lo))
        return false;
    *value = (uint16_t)(((uint16_t)hi << 8) | lo);
    return true;
}

/* Matches m64_read_compressed_u16 and the var macro in seq_macros.inc. */
static bool vm_read_var_u16(const uint8_t *sequence, uint16_t length,
                            uint16_t *pc, uint16_t *value)
{
    uint8_t first;
    if (!vm_read_u8(sequence, length, pc, &first)) return false;
    if ((first & 0x80U) == 0U) {
        *value = first;
        return true;
    }
    if (*pc >= length) return false;
    *value = (uint16_t)(((uint16_t)(first & 0x7fU) << 8) |
                        sequence[*pc]);
    (*pc)++;
    return true;
}

static bool vm_read_target(const uint8_t *sequence, uint16_t length,
                           uint16_t *pc, uint16_t *target)
{
    uint16_t raw;
    int16_t relative;
    if (!vm_read_be16(sequence, length, pc, &raw)) return false;
    relative = (int16_t)raw;
    /* Project12x emits sequence-relative non-negative offsets. */
    if (relative < 0 || (uint16_t)relative >= length) return false;
    *target = (uint16_t)relative;
    return true;
}

static bool vm_push(sm64_saturn_sequence_vm_t *vm, uint16_t return_offset,
                    uint8_t kind, uint8_t loop_count)
{
    const uint8_t depth = vm->stack_depth;
    if (depth >= SM64_SATURN_SEQUENCE_VM_STACK_DEPTH) return false;
    vm->stack[depth] = return_offset;
    vm->frame_kind[depth] = kind;
    vm->loop_remaining[depth] = loop_count;
    vm->stack_depth = (uint8_t)(depth + 1U);
    return true;
}

static bool vm_pop_call(sm64_saturn_sequence_vm_t *vm, uint16_t *return_offset)
{
    uint8_t depth;
    if (vm->stack_depth == 0U) return false;
    depth = (uint8_t)(vm->stack_depth - 1U);
    if (vm->frame_kind[depth] != SM64_SATURN_SEQUENCE_VM_FRAME_CALL)
        return false;
    *return_offset = vm->stack[depth];
    vm->stack_depth = depth;
    return true;
}

static bool vm_flow(sm64_saturn_sequence_vm_t *vm, const uint8_t *sequence,
                    uint16_t length, uint8_t cmd, uint16_t source_offset,
                    sm64_saturn_sequence_vm_event_t *events,
                    uint8_t event_capacity, uint8_t *event_count,
                    bool *stopped)
{
    uint16_t target, return_offset, value16;
    uint8_t count;
    if (cmd == 0xffU) {
        if (vm->stack_depth == 0U) {
            vm->halted = 1U;
            vm->enabled = 0U;
            return vm_emit(vm, events, event_capacity, event_count,
                           SM64_SATURN_SEQUENCE_VM_EVENT_END, cmd, 0U, 0U,
                           0U, 0, source_offset);
        }
        if (!vm_pop_call(vm, &return_offset)) return false;
        vm->pc = return_offset;
        return true;
    }
    if (cmd == 0xfcU || cmd == 0xfbU || cmd == 0xfaU || cmd == 0xf9U ||
        cmd == 0xf5U) {
        if (!vm_read_target(sequence, length, &vm->pc, &target)) return false;
        if (cmd == 0xfcU) {
            if (!vm_push(vm, vm->pc, SM64_SATURN_SEQUENCE_VM_FRAME_CALL, 0U))
                return false;
            vm->pc = target;
        } else if (cmd == 0xfbU ||
                   (cmd == 0xfaU && vm->value == 0U) ||
                   (cmd == 0xf9U && vm->value < 0) ||
                   (cmd == 0xf5U && vm->value >= 0)) {
            vm->pc = target;
        }
        return true;
    }
    if (cmd == 0xf8U) {
        if (!vm_read_u8(sequence, length, &vm->pc, &count) ||
            !vm_push(vm, vm->pc, SM64_SATURN_SEQUENCE_VM_FRAME_LOOP, count))
            return false;
        return true;
    }
    if (cmd == 0xf7U) {
        uint8_t depth;
        if (vm->stack_depth == 0U) return false;
        depth = (uint8_t)(vm->stack_depth - 1U);
        if (vm->frame_kind[depth] != SM64_SATURN_SEQUENCE_VM_FRAME_LOOP)
            return false;
        vm->loop_remaining[depth]--;
        if (vm->loop_remaining[depth] != 0U) {
            vm->pc = vm->stack[depth];
        } else {
            vm->stack_depth = depth;
        }
        return true;
    }
    if (cmd == 0xfdU || cmd == 0xfeU) {
        value16 = 1U;
        if (cmd == 0xfdU &&
            !vm_read_var_u16(sequence, length, &vm->pc, &value16))
            return false;
        vm->delay = value16;
        if (!vm_emit(vm, events, event_capacity, event_count,
                     SM64_SATURN_SEQUENCE_VM_EVENT_DELAY, cmd, value16, 0U,
                     0U, 0, source_offset))
            return false;
        *stopped = true;
        return true;
    }
    if (cmd == 0xf4U || cmd == 0xf3U || cmd == 0xf2U) {
        uint8_t displacement;
        int16_t signed_displacement;
        if (!vm_read_u8(sequence, length, &vm->pc, &displacement)) return false;
        signed_displacement = vm_s8(displacement);
        if ((cmd == 0xf3U && vm->value != 0U) ||
            (cmd == 0xf2U && vm->value < 0))
            return true;
        if (signed_displacement < 0 &&
            (uint16_t)(-signed_displacement) > vm->pc)
            return false;
        if (signed_displacement >= 0 &&
            (uint32_t)vm->pc + (uint16_t)signed_displacement >= length)
            return false;
        vm->pc = signed_displacement < 0
                     ? (uint16_t)(vm->pc - (uint16_t)(-signed_displacement))
                     : (uint16_t)(vm->pc + (uint16_t)signed_displacement);
        return true;
    }
    return false;
}

static bool vm_tick_sequence(sm64_saturn_sequence_vm_t *vm,
                             const uint8_t *sequence, uint16_t length,
                             sm64_saturn_sequence_vm_event_t *events,
                             uint8_t event_capacity, uint8_t *event_count)
{
    uint16_t source_offset, target, value16;
    uint8_t cmd, value8;
    uint16_t steps = 0U;
    bool stopped = false;
    while (steps++ < SM64_SATURN_SEQUENCE_VM_INSTRUCTION_BUDGET) {
        source_offset = vm->pc;
        if (!vm_read_u8(sequence, length, &vm->pc, &cmd)) return false;
        vm->instruction_count++;
        if (cmd >= 0xc0U) {
            if (cmd == 0xccU || cmd == 0xc8U || cmd == 0xc9U || cmd == 0xdbU ||
                cmd == 0xdaU || cmd == 0xddU || cmd == 0xdcU || cmd == 0xdeU ||
                cmd == 0xdfU || cmd == 0xd3U || cmd == 0xd5U || cmd == 0xd0U ||
                cmd == 0xd7U || cmd == 0xd6U || cmd == 0xd2U || cmd == 0xd1U ||
                cmd == 0xd4U || cmd == 0xf1U || cmd == 0xf0U) {
                if (cmd == 0xf1U || cmd == 0xf0U || cmd == 0xd4U) {
                    if (cmd == 0xd4U) vm->muted = 1U;
                    if (!vm_emit(vm, events, event_capacity, event_count,
                                 SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd,
                                 0U, 0U, 0U, 0, source_offset)) return false;
                } else if (cmd == 0xd7U || cmd == 0xd6U || cmd == 0xd2U ||
                           cmd == 0xd1U) {
                    if (!vm_read_be16(sequence, length, &vm->pc, &value16))
                        return false;
                    if (!vm_emit(vm, events, event_capacity, event_count,
                                 SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd,
                                 value16, 0U, 0U, 0, source_offset)) return false;
                } else {
                    if (!vm_read_u8(sequence, length, &vm->pc, &value8))
                        return false;
                    if (cmd == 0xccU) vm->value = value8;
                    else if (cmd == 0xc8U) vm->value = (int16_t)(vm->value - value8);
                    else if (cmd == 0xc9U) vm->value = (int16_t)(vm->value & value8);
                    else if (cmd == 0xddU) vm->tempo = (uint16_t)value8 * 48U;
                    else if (cmd == 0xdcU) vm->tempo = (uint16_t)((int16_t)vm->tempo + (vm_s8(value8) * 48));
                    if (vm->tempo > vm->tempo_limit) vm->tempo = vm->tempo_limit;
                    if (vm->tempo == 0U) vm->tempo = 1U;
                    else if (cmd == 0xdeU) vm->transpose = (int16_t)(vm->transpose + vm_s8(value8));
                    else if (cmd == 0xdfU) vm->transpose = vm_s8(value8);
                    else if (cmd == 0xdbU) vm->volume = value8;
                    else if (cmd == 0xdaU) vm->volume = (uint8_t)(vm->volume + vm_s8(value8));
                    if (cmd == 0xddU || cmd == 0xdcU) {
                        if (!vm_emit(vm, events, event_capacity, event_count,
                                     SM64_SATURN_SEQUENCE_VM_EVENT_TEMPO, cmd,
                                     vm->tempo, 0U, 0U, 0, source_offset)) return false;
                    } else if (cmd == 0xdeU || cmd == 0xdfU) {
                        if (!vm_emit(vm, events, event_capacity, event_count,
                                     SM64_SATURN_SEQUENCE_VM_EVENT_TRANSPOSE, cmd,
                                     0U, 0U, 0U, vm->transpose, source_offset)) return false;
                    } else if (cmd == 0xdbU || cmd == 0xdaU) {
                        if (!vm_emit(vm, events, event_capacity, event_count,
                                     SM64_SATURN_SEQUENCE_VM_EVENT_VOLUME, cmd,
                                     vm->volume, 0U, 0U, 0, source_offset)) return false;
                    } else if (!vm_emit(vm, events, event_capacity, event_count,
                                        SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd,
                                        value8, 0U, 0U, 0, source_offset)) return false;
                }
            } else if (cmd == 0xf8U || cmd == 0xf7U || cmd == 0xfbU ||
                       cmd == 0xfcU || cmd == 0xfaU || cmd == 0xf9U ||
                       cmd == 0xf5U || cmd == 0xfdU || cmd == 0xfeU ||
                       cmd == 0xf4U || cmd == 0xf3U || cmd == 0xf2U ||
                       cmd == 0xffU) {
                if (!vm_flow(vm, sequence, length, cmd, source_offset, events,
                             event_capacity, event_count, &stopped)) return false;
                if (stopped || vm->halted) return true;
            } else {
                return false;
            }
        } else {
            const uint8_t family = cmd & 0xf0U;
            if (family == 0x50U) vm->value = (int16_t)(vm->value - vm->variation);
            else if (family == 0x70U) vm->variation = vm->value;
            else if (family == 0x80U) vm->value = vm->variation;
            else if (family == 0x90U) {
                if (!vm_read_target(sequence, length, &vm->pc, &target)) return false;
                vm->channel_active_mask |= (uint16_t)(1U << (cmd & 0x0fU));
                if (!vm_emit(vm, events, event_capacity, event_count,
                             SM64_SATURN_SEQUENCE_VM_EVENT_CHANNEL_START, cmd,
                             (uint16_t)(cmd & 0x0fU), target, 0U, 0,
                             source_offset)) return false;
            } else if (family != 0x00U && family != 0x10U && family != 0x20U &&
                       family != 0x40U && family != 0x60U && family != 0xa0U) {
                return false;
            }
            if (family != 0x90U &&
                !vm_emit(vm, events, event_capacity, event_count,
                         SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd,
                         (uint16_t)vm->value,
                         0U, 0U, 0, source_offset)) return false;
        }
        if (stopped) return true;
    }
    return false;
}

static bool vm_tick_layer(sm64_saturn_sequence_vm_t *vm, const uint8_t *sequence,
                          uint16_t length, sm64_saturn_sequence_vm_event_t *events,
                          uint8_t event_capacity, uint8_t *event_count)
{
    uint16_t source_offset, play_percentage;
    uint8_t cmd, velocity = 0U, duration = 0U, value8;
    uint16_t value16;
    uint16_t steps = 0U;
    bool stopped = false;
    while (steps++ < SM64_SATURN_SEQUENCE_VM_INSTRUCTION_BUDGET) {
        source_offset = vm->pc;
        if (!vm_read_u8(sequence, length, &vm->pc, &cmd)) return false;
        vm->instruction_count++;
        if (cmd == 0xffU || cmd == 0xf8U || cmd == 0xf7U || cmd == 0xfbU ||
            cmd == 0xfcU) {
            if (!vm_flow(vm, sequence, length, cmd, source_offset, events,
                         event_capacity, event_count, &stopped)) return false;
            if (stopped || vm->halted) return true;
            continue;
        }
        if (cmd == 0xc0U) {
            if (!vm_read_var_u16(sequence, length, &vm->pc, &value16)) return false;
            vm->delay = value16;
            if (!vm_emit(vm, events, event_capacity, event_count,
                         SM64_SATURN_SEQUENCE_VM_EVENT_DELAY, cmd, value16, 0U,
                         0U, 0, source_offset)) return false;
            return true;
        }
        if (cmd >= 0xc1U) {
            if (cmd == 0xc3U) {
                if (!vm_read_var_u16(sequence, length, &vm->pc, &value16)) return false;
                vm->default_play_percentage = value16;
            } else if (cmd == 0xc7U) {
                uint8_t mode, note;
                if (!vm_read_u8(sequence, length, &vm->pc, &mode) ||
                    !vm_read_u8(sequence, length, &vm->pc, &note)) return false;
                if ((mode & 0x80U) != 0U) {
                    if (!vm_read_u8(sequence, length, &vm->pc, &value8)) return false;
                    value16 = value8;
                } else if (!vm_read_var_u16(sequence, length, &vm->pc, &value16)) {
                    return false;
                }
                if (!vm_emit(vm, events, event_capacity, event_count,
                             SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd, mode,
                             note, value16, 0, source_offset)) return false;
                continue;
            } else if (cmd == 0xc1U || cmd == 0xc2U || cmd == 0xc6U ||
                       cmd == 0xc9U || cmd == 0xcaU) {
                if (!vm_read_u8(sequence, length, &vm->pc, &value8)) return false;
                if (cmd == 0xc1U) velocity = value8;
                else if (cmd == 0xc2U) vm->transpose = value8;
                else if (cmd == 0xc9U) duration = value8;
                if (!vm_emit(vm, events, event_capacity, event_count,
                             cmd == 0xc2U ? SM64_SATURN_SEQUENCE_VM_EVENT_TRANSPOSE :
                             SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd, value8,
                             0U, 0U, cmd == 0xc2U ? vm->transpose : 0,
                             source_offset)) return false;
            } else if (cmd == 0xc4U || cmd == 0xc5U || cmd == 0xc8U) {
                if (!vm_emit(vm, events, event_capacity, event_count,
                             SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL, cmd, 0U, 0U,
                             0U, 0, source_offset)) return false;
            } else {
                return false;
            }
            continue;
        }
        play_percentage = vm->play_percentage;
        if ((cmd & 0xc0U) == 0U) {
            if (!vm_read_var_u16(sequence, length, &vm->pc, &play_percentage)) return false;
            vm->play_percentage = play_percentage;
            if (vm->mode == SM64_SATURN_SEQUENCE_VM_LAYER_LARGE) {
                if (!vm_read_u8(sequence, length, &vm->pc, &velocity) ||
                    !vm_read_u8(sequence, length, &vm->pc, &duration)) return false;
            }
        } else if ((cmd & 0xc0U) == 0x40U) {
            play_percentage = vm->default_play_percentage;
            if (vm->mode == SM64_SATURN_SEQUENCE_VM_LAYER_LARGE &&
                !vm_read_var_u16(sequence, length, &vm->pc, &play_percentage)) return false;
            if (vm->mode == SM64_SATURN_SEQUENCE_VM_LAYER_LARGE &&
                !vm_read_u8(sequence, length, &vm->pc, &velocity)) return false;
        } else {
            play_percentage = vm->play_percentage;
            if (vm->mode == SM64_SATURN_SEQUENCE_VM_LAYER_LARGE &&
                (!vm_read_u8(sequence, length, &vm->pc, &velocity) ||
                 !vm_read_u8(sequence, length, &vm->pc, &duration))) return false;
        }
        vm->delay = play_percentage;
        if (!vm_emit(vm, events, event_capacity, event_count,
                     SM64_SATURN_SEQUENCE_VM_EVENT_NOTE, cmd, play_percentage,
                     (uint16_t)(((uint16_t)velocity << 8) | duration), 0U,
                     (int16_t)((cmd & 0x3fU) + vm->transpose), source_offset))
            return false;
        return true;
    }
    return false;
}

void sm64_saturn_sequence_vm_init(sm64_saturn_sequence_vm_t *vm,
                                  uint16_t data_length, uint16_t entry_offset,
                                  sm64_saturn_sequence_vm_mode_t mode)
{
    uint8_t i;
    if (vm == 0) return;
    *vm = (sm64_saturn_sequence_vm_t){0};
    vm->data_length = data_length;
    vm->pc = entry_offset;
    vm->mode = (uint8_t)mode;
    vm->tempo = SM64_SATURN_SEQUENCE_VM_DEFAULT_TEMPO;
    vm->tempo_limit = SM64_SATURN_SEQUENCE_VM_DEFAULT_TEMPO;
    vm->default_play_percentage = SM64_SATURN_SEQUENCE_VM_DEFAULT_PLAY_PERCENTAGE;
    vm->play_percentage = SM64_SATURN_SEQUENCE_VM_DEFAULT_PLAY_PERCENTAGE;
    vm->volume = 127U;
    vm->enabled = (data_length != 0U && entry_offset < data_length &&
                   mode <= SM64_SATURN_SEQUENCE_VM_LAYER_LARGE) ? 1U : 0U;
    for (i = 0U; i < SM64_SATURN_SEQUENCE_VM_STACK_DEPTH; i++) {
        vm->frame_kind[i] = 0U;
        vm->loop_remaining[i] = 0U;
    }
    if (vm->enabled == 0U) vm->faulted = 1U;
}

bool sm64_saturn_sequence_vm_tick(
    sm64_saturn_sequence_vm_t *vm, const uint8_t *sequence,
    uint16_t sequence_length, sm64_saturn_sequence_vm_event_t *events,
    uint8_t event_capacity, uint8_t *event_count)
{
    bool ok;
    if (event_count != 0) *event_count = 0U;
    if (vm == 0 || event_count == 0 || vm->faulted || vm->halted ||
        vm->enabled == 0U || sequence == 0 || sequence_length != vm->data_length ||
        event_capacity > SM64_SATURN_SEQUENCE_VM_MAX_EVENTS)
        return false;
    vm->tick_count++;
    if (!vm_advance_tatum(vm)) return true;
    if (vm->delay > 1U) {
        vm->delay--;
        return true;
    }
    vm->delay = 0U;
    ok = vm->mode == SM64_SATURN_SEQUENCE_VM_SEQUENCE
             ? vm_tick_sequence(vm, sequence, sequence_length, events,
                                event_capacity, event_count)
             : vm_tick_layer(vm, sequence, sequence_length, events,
                             event_capacity, event_count);
    if (!ok) vm_fault(vm);
    return ok;
}
