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
    test_fail_closed_inputs();
    test_pointer_free_bounded_state();
    return 0;
}
