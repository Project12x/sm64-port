/* Slot layout/order is a close-port of ponut64/SCSP_poneSound
 * @31782e4c61337327f23eb9aa45ecd37fe0944ea0 (MIT), PROJ/main.c:116-156,
 * 507-577. This module emits pointer-free commands only; MMIO is owned by the
 * explicit scsp_pcm8 target boundary. */
#include "slot_shadow.h"

void sm64_saturn_slot_shadow_init(sm64_saturn_slot_shadow_t *shadow)
{
    if (shadow != 0) *shadow = (sm64_saturn_slot_shadow_t){0};
}

static void emit(sm64_saturn_slot_command_t *commands, uint8_t *count,
                 uint8_t slot, uint8_t field, uint16_t value)
{
    commands[*count].slot = slot;
    commands[*count].field = field;
    commands[*count].value = value;
    (*count)++;
}

static sm64_saturn_slot_state_t state_from_desired(
    const sm64_saturn_desired_voice_t *desired)
{
    sm64_saturn_slot_state_t state = {0};
    state.package_generation = desired->package_generation;
    state.note_id = desired->note_id;
    state.start_address_low = desired->start_address_low;
    state.loop_start = desired->loop_start;
    state.loop_end = desired->loop_end;
    state.envelope_word = desired->envelope_word;
    state.release_word = desired->release_word;
    state.attenuation = desired->attenuation;
    state.pitch_word = desired->pitch_word;
    state.pan_send_word = desired->pan_send_word;
    state.key_word = desired->key_word;
    state.active = desired->active;
    return state;
}

bool sm64_saturn_slot_shadow_diff(
    sm64_saturn_slot_shadow_t *shadow, uint8_t slot,
    const sm64_saturn_desired_voice_t *desired,
    uint32_t active_package_generation,
    sm64_saturn_slot_command_t *commands, uint8_t command_capacity,
    uint8_t *command_count)
{
    sm64_saturn_slot_state_t *current;
    sm64_saturn_slot_state_t next;
    bool reassigned;
    uint8_t needed = 0U;
    if (command_count != 0) *command_count = 0U;
    if (shadow == 0 || desired == 0 || commands == 0 || command_count == 0 ||
        slot >= SM64_SATURN_SCSP_SLOT_COUNT || desired->slot != slot)
        return false;
    current = &shadow->slots[slot];
    if (!desired->active) {
        if (!current->active) return true;
        if (command_capacity < 1U) {
            shadow->capacity_faults++;
            return false;
        }
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_KEY_CONTROL,
             SM64_SATURN_SLOT_KEY_EXECUTE);
        *current = (sm64_saturn_slot_state_t){0};
        shadow->commands_emitted++;
        return true;
    }
    if (active_package_generation == 0U ||
        desired->package_generation != active_package_generation) {
        shadow->stale_generations++;
        if (current->active && command_capacity >= 1U) {
            emit(commands, command_count, slot,
                 SM64_SATURN_SLOT_FIELD_KEY_CONTROL,
                 SM64_SATURN_SLOT_KEY_EXECUTE);
            *current = (sm64_saturn_slot_state_t){0};
            shadow->commands_emitted++;
        }
        return false;
    }
    next = state_from_desired(desired);
    reassigned = !current->active || current->note_id != next.note_id ||
                 current->package_generation != next.package_generation ||
                 current->start_address_low != next.start_address_low;
    if (reassigned) {
        if (command_capacity < SM64_SATURN_SLOT_MAX_COMMANDS) {
            shadow->capacity_faults++;
            return false;
        }
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_KEY_CONTROL,
             SM64_SATURN_SLOT_KEY_EXECUTE);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_START_ADDRESS,
             next.start_address_low);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_LOOP_START,
             next.loop_start);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_LOOP_END,
             next.loop_end);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_ENVELOPE,
             next.envelope_word);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_RELEASE,
             next.release_word);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_ATTENUATION,
             next.attenuation);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_PITCH,
             next.pitch_word);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_PAN_SEND,
             next.pan_send_word);
        emit(commands, command_count, slot, SM64_SATURN_SLOT_FIELD_KEY_CONTROL,
             next.key_word);
        *current = next;
    } else {
#define COUNT_CHANGE(member) do { if (current->member != next.member) needed++; } while (0)
        COUNT_CHANGE(loop_start);
        COUNT_CHANGE(loop_end);
        COUNT_CHANGE(envelope_word);
        COUNT_CHANGE(release_word);
        COUNT_CHANGE(attenuation);
        COUNT_CHANGE(pitch_word);
        COUNT_CHANGE(pan_send_word);
        COUNT_CHANGE(key_word);
#undef COUNT_CHANGE
        if (command_capacity < needed) {
            shadow->capacity_faults++;
            return false;
        }
#define EMIT_CHANGE(member, field_name) do { \
        if (current->member != next.member) \
            emit(commands, command_count, slot, field_name, next.member); \
    } while (0)
        EMIT_CHANGE(loop_start, SM64_SATURN_SLOT_FIELD_LOOP_START);
        EMIT_CHANGE(loop_end, SM64_SATURN_SLOT_FIELD_LOOP_END);
        EMIT_CHANGE(envelope_word, SM64_SATURN_SLOT_FIELD_ENVELOPE);
        EMIT_CHANGE(release_word, SM64_SATURN_SLOT_FIELD_RELEASE);
        EMIT_CHANGE(attenuation, SM64_SATURN_SLOT_FIELD_ATTENUATION);
        EMIT_CHANGE(pitch_word, SM64_SATURN_SLOT_FIELD_PITCH);
        EMIT_CHANGE(pan_send_word, SM64_SATURN_SLOT_FIELD_PAN_SEND);
        EMIT_CHANGE(key_word, SM64_SATURN_SLOT_FIELD_KEY_CONTROL);
#undef EMIT_CHANGE
        *current = next;
    }
    shadow->commands_emitted += *command_count;
    return true;
}
