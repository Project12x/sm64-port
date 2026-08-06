#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "desired_voice.h"
#include "scsp_pcm8.h"
#include "slot_shadow.h"

_Static_assert(sizeof(sm64_saturn_slot_command_t) == 4U,
               "slot commands must remain pointer-free scalars");

static sm64_saturn_desired_voice_t desired(uint32_t generation, uint32_t note_id)
{
    sm64_saturn_voice_request_t request = {0};
    sm64_saturn_desired_voice_t desired;
    request.package_generation = generation;
    request.note_id = note_id;
    request.sound_ram_offset = 0x12340U;
    request.sample_count = 0x120U;
    request.loop_start = 0x20U;
    request.sample_rate = 22050U;
    request.tuning_q12 = 4096U;
    request.root_note = 60;
    request.note = 60;
    request.velocity = 100U;
    request.pan = -16;
    request.priority = 8U;
    request.source_class = SM64_SATURN_VOICE_CLASS_MUSIC;
    request.loop = 1U;
    request.attack_ticks = 2U;
    request.decay_ticks = 3U;
    request.sustain_q15 = 0x5000U;
    request.release_ticks = 4U;
    assert(sm64_saturn_desired_voice_begin(&desired, &request, 5U, 1U));
    assert(desired.pitch_word == 0x7800U);
    assert(desired.loop_start == 0x20U && desired.loop_end == 0x11fU);
    assert(desired.pan_send_word == 0xd400U);
    assert(desired.envelope_word == 31U);
    assert(desired.release_word == 31U);
    assert((desired.key_word & SM64_SATURN_SLOT_LOOP_NORMAL) != 0U);
    return desired;
}

static void test_initial_program_is_keyoff_first_keyexecute_last(void)
{
    sm64_saturn_slot_shadow_t shadow;
    sm64_saturn_slot_command_t commands[SM64_SATURN_SLOT_MAX_COMMANDS];
    sm64_saturn_desired_voice_t voice = desired(7U, 1U);
    uint8_t count = 0U;
    sm64_saturn_slot_shadow_init(&shadow);
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &voice, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(count == SM64_SATURN_SLOT_MAX_COMMANDS);
    assert(commands[0].field == SM64_SATURN_SLOT_FIELD_KEY_CONTROL);
    assert(commands[0].value == SM64_SATURN_SLOT_KEY_EXECUTE);
    assert(commands[count - 1U].field == SM64_SATURN_SLOT_FIELD_KEY_CONTROL);
    assert((commands[count - 1U].value & SM64_SATURN_SLOT_KEY_ON) != 0U);
    assert((commands[count - 1U].value & SM64_SATURN_SLOT_KEY_EXECUTE) != 0U);
    assert(commands[count - 1U].slot == 5U);
}

static void test_unchanged_is_zero_and_one_field_is_minimal(void)
{
    sm64_saturn_slot_shadow_t shadow;
    sm64_saturn_slot_command_t commands[SM64_SATURN_SLOT_MAX_COMMANDS];
    sm64_saturn_desired_voice_t voice = desired(7U, 1U);
    uint8_t count;
    sm64_saturn_slot_shadow_init(&shadow);
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &voice, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &voice, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(count == 0U);
    voice.pitch_word++;
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &voice, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(count == 1U);
    assert(commands[0].field == SM64_SATURN_SLOT_FIELD_PITCH);
}

static void test_reassignment_and_stale_generation_keyoff_is_applied(void)
{
    sm64_saturn_slot_shadow_t shadow;
    sm64_saturn_slot_command_t commands[SM64_SATURN_SLOT_MAX_COMMANDS];
    sm64_saturn_desired_voice_t first = desired(7U, 1U);
    sm64_saturn_desired_voice_t second = desired(7U, 2U);
    uint8_t count;
    sm64_saturn_slot_shadow_init(&shadow);
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &first, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &second, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(count == SM64_SATURN_SLOT_MAX_COMMANDS);
    assert(commands[0].value == SM64_SATURN_SLOT_KEY_EXECUTE);
    assert((commands[count - 1U].value & SM64_SATURN_SLOT_KEY_ON) != 0U);

    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &second, 8U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(count == 1U);
    assert(commands[0].field == SM64_SATURN_SLOT_FIELD_KEY_CONTROL);
    assert(commands[0].value == SM64_SATURN_SLOT_KEY_EXECUTE);
    assert(!shadow.slots[5].active);
}

static bool caller_diff_and_apply(
    sm64_saturn_slot_shadow_t *shadow, uint8_t slot,
    const sm64_saturn_desired_voice_t *voice, uint32_t generation,
    volatile uint8_t *registers, uint8_t command_capacity)
{
    sm64_saturn_slot_command_t commands[SM64_SATURN_SLOT_MAX_COMMANDS];
    uint8_t count, index;
    if (!sm64_saturn_slot_shadow_diff(shadow, slot, voice, generation,
                                      commands, command_capacity, &count))
        return false;
    for (index = 0U; index < count; ++index)
        assert(sm64_saturn_scsp_apply_slot_command(registers, &commands[index]));
    return true;
}

static void test_stale_keyoff_is_transactional_for_normal_caller(void)
{
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    volatile uint8_t *registers = (volatile uint8_t *)register_words;
    sm64_saturn_slot_shadow_t shadow;
    sm64_saturn_desired_voice_t voice = desired(7U, 91U);
    uint16_t key_offset = (uint16_t)(5U * SM64_SATURN_SCSP_SLOT_BYTES +
                                     SM64_SATURN_SCSP_SLOT_KEYS);
    sm64_saturn_slot_shadow_init(&shadow);
    assert(caller_diff_and_apply(&shadow, 5U, &voice, 7U, registers,
                                 SM64_SATURN_SLOT_MAX_COMMANDS));
    assert((register_words[key_offset / 2U] & SM64_SATURN_SLOT_KEY_ON) != 0U);

    assert(caller_diff_and_apply(&shadow, 5U, &voice, 8U, registers,
                                 SM64_SATURN_SLOT_MAX_COMMANDS));
    assert(register_words[key_offset / 2U] == SM64_SATURN_SLOT_KEY_EXECUTE);
    assert(!shadow.slots[5].active);
}

static void test_stale_zero_capacity_retains_shadow_for_retry(void)
{
    uint16_t register_words[SM64_SATURN_SCSP_REGISTER_BYTES / 2U] = {0};
    volatile uint8_t *registers = (volatile uint8_t *)register_words;
    sm64_saturn_slot_shadow_t shadow;
    sm64_saturn_desired_voice_t voice = desired(7U, 92U);
    uint16_t key_offset = (uint16_t)(5U * SM64_SATURN_SCSP_SLOT_BYTES +
                                     SM64_SATURN_SCSP_SLOT_KEYS);
    sm64_saturn_slot_shadow_init(&shadow);
    assert(caller_diff_and_apply(&shadow, 5U, &voice, 7U, registers,
                                 SM64_SATURN_SLOT_MAX_COMMANDS));
    assert(!caller_diff_and_apply(&shadow, 5U, &voice, 8U, registers, 0U));
    assert((register_words[key_offset / 2U] & SM64_SATURN_SLOT_KEY_ON) != 0U);
    assert(shadow.slots[5].active);
    assert(shadow.slots[5].note_id == 92U);
    assert(shadow.capacity_faults == 1U);

    assert(caller_diff_and_apply(&shadow, 5U, &voice, 8U, registers, 1U));
    assert(register_words[key_offset / 2U] == SM64_SATURN_SLOT_KEY_EXECUTE);
    assert(!shadow.slots[5].active);
}

static void test_normal_release_emits_one_keyoff(void)
{
    sm64_saturn_slot_shadow_t shadow;
    sm64_saturn_slot_command_t commands[SM64_SATURN_SLOT_MAX_COMMANDS];
    sm64_saturn_desired_voice_t voice = desired(7U, 3U);
    uint8_t count;
    sm64_saturn_slot_shadow_init(&shadow);
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &voice, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    voice.active = false;
    voice.key_word = SM64_SATURN_SLOT_KEY_EXECUTE;
    assert(sm64_saturn_slot_shadow_diff(&shadow, 5U, &voice, 7U, commands,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &count));
    assert(count == 1U);
    assert(commands[0].field == SM64_SATURN_SLOT_FIELD_KEY_CONTROL);
    assert(commands[0].value == SM64_SATURN_SLOT_KEY_EXECUTE);
}

static void test_capacity_failure_does_not_mutate_and_trace_is_deterministic(void)
{
    sm64_saturn_slot_shadow_t a, b, before;
    sm64_saturn_slot_command_t ca[SM64_SATURN_SLOT_MAX_COMMANDS];
    sm64_saturn_slot_command_t cb[SM64_SATURN_SLOT_MAX_COMMANDS];
    sm64_saturn_desired_voice_t voice = desired(7U, 1U);
    uint8_t ac, bc;
    sm64_saturn_slot_shadow_init(&a);
    before = a;
    assert(!sm64_saturn_slot_shadow_diff(&a, 5U, &voice, 7U, ca, 1U, &ac));
    assert(ac == 0U);
    assert(memcmp(a.slots, before.slots, sizeof(a.slots)) == 0);
    assert(a.capacity_faults == 1U);

    sm64_saturn_slot_shadow_init(&a);
    sm64_saturn_slot_shadow_init(&b);
    assert(sm64_saturn_slot_shadow_diff(&a, 5U, &voice, 7U, ca,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &ac));
    assert(sm64_saturn_slot_shadow_diff(&b, 5U, &voice, 7U, cb,
                                        SM64_SATURN_SLOT_MAX_COMMANDS, &bc));
    assert(ac == bc && memcmp(ca, cb, ac * sizeof(*ca)) == 0);
    assert(!sm64_saturn_slot_shadow_diff(&a, SM64_SATURN_SCSP_SLOT_COUNT,
                                         &voice, 7U, ca,
                                         SM64_SATURN_SLOT_MAX_COMMANDS, &ac));
}

int main(void)
{
    test_initial_program_is_keyoff_first_keyexecute_last();
    test_unchanged_is_zero_and_one_field_is_minimal();
    test_reassignment_and_stale_generation_keyoff_is_applied();
    test_stale_keyoff_is_transactional_for_normal_caller();
    test_stale_zero_capacity_retains_shadow_for_retry();
    test_normal_release_emits_one_keyoff();
    test_capacity_failure_does_not_mutate_and_trace_is_deterministic();
    return 0;
}
