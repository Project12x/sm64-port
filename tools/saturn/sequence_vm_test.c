#include <assert.h>
#include <stdint.h>

#include "sequence_vm.h"

static void test_sequence_control_and_timing(void)
{
    static const uint8_t script[] = {0xdd, 120, 0xfc, 0, 6, 0xff, 0xfd, 2,
                                     0xff};
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[8];
    uint8_t count;

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(script), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(sm64_saturn_sequence_vm_tick(&vm, script, sizeof(script), events,
                                        8U, &count));
    assert(count == 2U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_TEMPO &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_DELAY);
    assert(sm64_saturn_sequence_vm_tick(&vm, script, sizeof(script), events,
                                        8U, &count));
    assert(count == 0U);
    assert(sm64_saturn_sequence_vm_tick(&vm, script, sizeof(script), events,
                                        8U, &count));
    assert(count == 1U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);
}

static void test_loop_and_note_events(void)
{
    static const uint8_t loop_script[] = {0xf8, 2, 0xdd, 60, 0xf7, 0xff};
    static const uint8_t note_script[] = {0x41, 0xc0, 1, 0xff};
    static const uint8_t portamento_script[] = {0xc7, 0x80, 40, 5, 0xff};
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[8];
    uint8_t count;

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(loop_script), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(sm64_saturn_sequence_vm_tick(&vm, loop_script,
                                        sizeof(loop_script), events, 8U,
                                        &count));
    assert(count == 3U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_TEMPO &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_TEMPO &&
           events[2].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);
    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(note_script), 0U,
                                 SM64_SATURN_SEQUENCE_VM_LAYER_SMALL);
    assert(sm64_saturn_sequence_vm_tick(&vm, note_script, sizeof(note_script),
                                        events, 8U, &count));
    assert(count == 1U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_NOTE);
    assert(events[0].signed_value == 1);
    sm64_saturn_sequence_vm_init(
        &vm, (uint16_t)sizeof(portamento_script), 0U,
        SM64_SATURN_SEQUENCE_VM_LAYER_SMALL);
    assert(sm64_saturn_sequence_vm_tick(&vm, portamento_script,
                                        sizeof(portamento_script), events, 8U,
                                        &count));
    assert(count == 2U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL &&
           events[0].arg0 == 0x80U && events[0].arg1 == 40U &&
           events[0].arg2 == 5U &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);
}

static void test_layer_call_loop_and_persistent_short_note_state(void)
{
    static const uint8_t call_loop_script[] = {
        0xfc, 0, 4, 0xff, 0xf8, 2, 0xc1, 0x7f, 0xf7, 0xff,
    };
    static const uint8_t persistent_script[] = {
        0xc1, 0x7f, 0xc9, 0x40, 0xc3, 1, 0x41, 0x42, 0xff,
    };
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[8];
    uint8_t count;

    sm64_saturn_sequence_vm_init(
        &vm, (uint16_t)sizeof(call_loop_script), 0U,
        SM64_SATURN_SEQUENCE_VM_LAYER_SMALL);
    assert(sm64_saturn_sequence_vm_tick(&vm, call_loop_script,
                                        sizeof(call_loop_script), events, 8U,
                                        &count));
    assert(count == 3U &&
           events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_CONTROL &&
           events[2].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);
    assert(events[0].opcode == 0xc1U && events[1].opcode == 0xc1U);

    sm64_saturn_sequence_vm_init(
        &vm, (uint16_t)sizeof(persistent_script), 0U,
        SM64_SATURN_SEQUENCE_VM_LAYER_SMALL);
    assert(vm.layer_note_duration == 0x80U);
    assert(sm64_saturn_sequence_vm_tick(&vm, persistent_script,
                                        sizeof(persistent_script), events, 8U,
                                        &count));
    assert(count == 3U && events[2].type == SM64_SATURN_SEQUENCE_VM_EVENT_NOTE &&
           events[2].arg1 == 0x7f40U);
    assert(sm64_saturn_sequence_vm_tick(&vm, persistent_script,
                                        sizeof(persistent_script), events, 8U,
                                        &count));
    assert(count == 1U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_NOTE &&
           events[0].arg1 == 0x7f40U);
    assert(sm64_saturn_sequence_vm_tick(&vm, persistent_script,
                                        sizeof(persistent_script), events, 8U,
                                        &count));
    assert(count == 1U && events[0].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);
}

static void test_bounded_flow_and_output_guards(void)
{
    static const uint8_t stack_overflow[] = {0xfc, 0, 0};
    static const uint8_t stack_underflow[] = {0xf7};
    static const uint8_t bad_target[] = {0xfc, 0, 5};
    static const uint8_t output_overflow[] = {0xdd, 60, 0xff};
    static const uint8_t zero_loop[] = {0xf8, 0, 0xfd, 1, 0xf7, 0xff};
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[8];
    uint8_t count;
    uint16_t ticks;

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(stack_overflow), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, stack_overflow,
                                         sizeof(stack_overflow), events, 8U,
                                         &count));
    assert(vm.faulted != 0U && vm.stack_depth ==
           SM64_SATURN_SEQUENCE_VM_STACK_DEPTH);

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(stack_underflow), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, stack_underflow,
                                         sizeof(stack_underflow), events, 8U,
                                         &count));
    assert(vm.faulted != 0U);

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(bad_target), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, bad_target, sizeof(bad_target),
                                         events, 8U, &count));
    assert(vm.faulted != 0U);

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(output_overflow), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, output_overflow,
                                         sizeof(output_overflow), events, 1U,
                                         &count));
    assert(vm.faulted != 0U && count == 1U);

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(zero_loop), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    for (ticks = 0U; ticks < 600U && vm.halted == 0U; ticks++) {
        assert(sm64_saturn_sequence_vm_tick(&vm, zero_loop, sizeof(zero_loop),
                                            events, 8U, &count));
    }
    /* One setup tick plus one tick per loop iteration: zero means 256. */
    assert(vm.halted != 0U && ticks == 257U && vm.faulted == 0U);
}

static void test_branch_polarity_and_channel_state(void)
{
    static const uint8_t negative_branch[] = {
        0xc8, 1, 0xf2, 2, 0xcc, 7, 0xff,
    };
    static const uint8_t nonnegative_branch[] = {
        0xcc, 1, 0xf2, 2, 0xcc, 7, 0xff,
    };
    static const uint8_t channel_state[] = {
        0xd7, 0, 1, 0x00, 0xd6, 0, 1, 0x00, 0xff,
    };
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[8];
    uint8_t count;

    sm64_saturn_sequence_vm_init_ex(
        &vm, (uint16_t)sizeof(negative_branch), 0U,
        SM64_SATURN_SEQUENCE_VM_SEQUENCE,
        SM64_SATURN_SEQUENCE_VM_FORMAT_EU_SH);
    assert(sm64_saturn_sequence_vm_tick(&vm, negative_branch,
                                        sizeof(negative_branch), events, 8U,
                                        &count));
    assert(count == 2U && events[0].opcode == 0xc8U &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);

    sm64_saturn_sequence_vm_init_ex(
        &vm, (uint16_t)sizeof(nonnegative_branch), 0U,
        SM64_SATURN_SEQUENCE_VM_SEQUENCE,
        SM64_SATURN_SEQUENCE_VM_FORMAT_EU_SH);
    assert(sm64_saturn_sequence_vm_tick(&vm, nonnegative_branch,
                                        sizeof(nonnegative_branch), events, 8U,
                                        &count));
    assert(count == 3U && events[1].opcode == 0xccU &&
           events[1].arg0 == 7U &&
           events[2].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(channel_state), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(sm64_saturn_sequence_vm_tick(&vm, channel_state,
                                        sizeof(channel_state), events, 8U,
                                        &count));
    assert(count == 5U && events[1].arg0 == 0U && events[3].arg0 == 1U &&
           vm.channel_active_mask == 0U && vm.channel_finished_mask == 0xffffU);
}

static void test_sequence_format_reserve_opcodes(void)
{
    static const uint8_t us_reserve[] = {0xf2, 5, 0xff};
    static const uint8_t eu_reserve[] = {0xf1, 5, 0xff};
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[4];
    uint8_t count;

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(us_reserve), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(sm64_saturn_sequence_vm_tick(&vm, us_reserve, sizeof(us_reserve),
                                        events, 4U, &count));
    assert(count == 2U && events[0].opcode == 0xf2U &&
           events[0].arg0 == 5U &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);

    sm64_saturn_sequence_vm_init_ex(
        &vm, (uint16_t)sizeof(eu_reserve), 0U,
        SM64_SATURN_SEQUENCE_VM_SEQUENCE,
        SM64_SATURN_SEQUENCE_VM_FORMAT_EU_SH);
    assert(sm64_saturn_sequence_vm_tick(&vm, eu_reserve, sizeof(eu_reserve),
                                        events, 4U, &count));
    assert(count == 2U && events[0].opcode == 0xf1U &&
           events[0].arg0 == 5U &&
           events[1].type == SM64_SATURN_SEQUENCE_VM_EVENT_END);
}

static void test_fail_closed_inputs(void)
{
    static const uint8_t truncated[] = {0xfc, 0};
    static const uint8_t unknown[] = {0xee};
    static const uint8_t non_progress[] = {0xfb, 0, 0};
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[8];
    uint8_t count;

    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(truncated), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, truncated, sizeof(truncated),
                                         events, 8U, &count));
    assert(vm.faulted != 0U);
    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(unknown), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, unknown, sizeof(unknown), events,
                                         8U, &count));
    assert(vm.faulted != 0U);
    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(non_progress), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, non_progress,
                                         sizeof(non_progress), events, 8U,
                                         &count));
    assert(vm.faulted != 0U &&
           vm.instruction_count == SM64_SATURN_SEQUENCE_VM_INSTRUCTION_BUDGET);
}

static void test_pointer_free_bounded_state(void)
{
    static const uint8_t script[] = {0xff};
    sm64_saturn_sequence_vm_t vm;
    sm64_saturn_sequence_vm_event_t events[1];
    uint8_t count;

    sm64_saturn_sequence_vm_init(&vm, 0U, 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(!sm64_saturn_sequence_vm_tick(&vm, script, sizeof(script), events,
                                         1U, &count));
    sm64_saturn_sequence_vm_init(&vm, (uint16_t)sizeof(script), 0U,
                                 SM64_SATURN_SEQUENCE_VM_SEQUENCE);
    assert(sm64_saturn_sequence_vm_tick(&vm, script, sizeof(script), events,
                                        1U, &count));
    assert(count == 1U);
}

int main(void)
{
    test_sequence_control_and_timing();
    test_loop_and_note_events();
    test_layer_call_loop_and_persistent_short_note_state();
    test_bounded_flow_and_output_guards();
    test_branch_polarity_and_channel_state();
    test_sequence_format_reserve_opcodes();
    test_fail_closed_inputs();
    test_pointer_free_bounded_state();
    return 0;
}
