#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "saturn_pcm_protocol.h"
#include "saturn_sound_cpu.h"

typedef struct fixture {
    uint8_t ram[SM64_SATURN_PCM_SOUND_RAM_BYTES];
    char order[64];
    uint16_t order_count;
    uint16_t stop_polls;
    uint16_t ready_polls;
    bool stage_ok;
    bool mode_ok;
    bool off_ok;
    bool on_ok;
    bool stop_seen;
    bool clear_ok;
    bool copy_ok;
    bool mailbox_ok;
} fixture_t;

static void mark(fixture_t *fixture, char event)
{
    fixture->order[fixture->order_count++] = event;
    fixture->order[fixture->order_count] = '\0';
}

static bool stage_ready(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'G');
    return fixture->stage_ok;
}

static bool set_512k(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'M');
    return fixture->mode_ok;
}

static sm64_saturn_sound_cpu_command_result_t command(
    void *context, uint8_t value)
{
    fixture_t *fixture = context;
    if (value == SM64_SATURN_SOUND_CPU_COMMAND_OFF) {
        mark(fixture, 'O');
        return fixture->off_ok ? SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED
                               : SM64_SATURN_SOUND_CPU_COMMAND_REJECTED;
    }
    assert(value == SM64_SATURN_SOUND_CPU_COMMAND_ON);
    mark(fixture, 'N');
    return fixture->on_ok ? SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED
                          : SM64_SATURN_SOUND_CPU_COMMAND_REJECTED;
}

static bool stopped(void *context)
{
    fixture_t *fixture = context;
    fixture->stop_polls++;
    return fixture->stop_seen && fixture->stop_polls >= 2U;
}

static bool clear_owned(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'C');
    return fixture->clear_ok;
}

static bool copy_staged(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'D');
    return fixture->copy_ok;
}

static bool mailbox_init(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'I');
    return fixture->mailbox_ok;
}

static void barrier(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'B');
}

static void wait_stop_tick(void *context)
{
    mark(context, 'w');
}

static void wait_ready_tick(void *context)
{
    fixture_t *fixture = context;
    mark(fixture, 'W');
    fixture->ready_polls++;
    if (fixture->ready_polls == 2U) {
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                                 SM64_SATURN_PCM_PROTOCOL_MAGIC);
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_VERSION_OFFSET,
                                 SM64_SATURN_PCM_PROTOCOL_VERSION);
        sm64_saturn_pcm_put_be16(fixture->ram, SM64_SATURN_PCM_STATUS_OFFSET,
                                 SM64_SATURN_PCM_STATUS_READY);
        sm64_saturn_pcm_put_be16(fixture->ram,
                                 SM64_SATURN_PCM_HEARTBEAT_OFFSET, 1U);
    }
}

static sm64_saturn_sound_cpu_boot_t boot(fixture_t *fixture)
{
    sm64_saturn_sound_cpu_boot_t value;
    memset(&value, 0, sizeof(value));
    fixture->stage_ok = true;
    fixture->mode_ok = true;
    fixture->off_ok = true;
    fixture->on_ok = true;
    fixture->stop_seen = true;
    fixture->clear_ok = true;
    fixture->copy_ok = true;
    fixture->mailbox_ok = true;
    value.kind = SM64_SATURN_SOUND_CPU_BOOT_COLD;
    value.sound_ram = fixture->ram;
    value.stop_wait_budget = 3U;
    value.ready_wait_budget = 4U;
    value.context = fixture;
    value.stage_ready = stage_ready;
    value.set_512k_mode = set_512k;
    value.generic_smpc_command = command;
    value.sound_cpu_stopped = stopped;
    value.clear_owned_regions = clear_owned;
    value.copy_staged_regions = copy_staged;
    value.initialize_mailbox = mailbox_init;
    value.publish_barrier = barrier;
    value.wait_stop_tick = wait_stop_tick;
    value.wait_ready_tick = wait_ready_tick;
    return value;
}

static void test_cold_boot_orders_staged_bytes_mode_stop_copy_publish_start(void)
{
    fixture_t fixture = {0};
    sm64_saturn_sound_cpu_boot_t config = boot(&fixture);
    sm64_saturn_sound_cpu_state_t state;
    assert(sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwMCDIBNWW") == 0);
    assert(state.phase == SM64_SATURN_SOUND_CPU_PHASE_READY);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_NONE);
    assert(state.boot_attempts == 1U && state.ready_polls == 2U);
}

static void test_preflight_failures_never_stop_or_clear_sound_ram(void)
{
    fixture_t fixture = {0};
    sm64_saturn_sound_cpu_boot_t config = boot(&fixture);
    sm64_saturn_sound_cpu_state_t state;
    fixture.stage_ok = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "G") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_STAGE);

    memset(&fixture, 0, sizeof(fixture));
    config = boot(&fixture);
    fixture.mode_ok = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwM") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_512K_MODE);
}

static void test_stop_and_ready_waits_are_bounded_and_fail_closed(void)
{
    fixture_t fixture = {0};
    sm64_saturn_sound_cpu_boot_t config = boot(&fixture);
    sm64_saturn_sound_cpu_state_t state;
    fixture.stop_seen = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwww") == 0);
    assert(fixture.stop_polls == 3U);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_STOP_TIMEOUT);

    memset(&fixture, 0, sizeof(fixture));
    config = boot(&fixture);
    config.ready_wait_budget = 1U;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwMCDIBNW") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_READY_TIMEOUT);
}

static void test_ready_without_heartbeat_advance_is_not_ready(void)
{
    fixture_t fixture = {0};
    sm64_saturn_sound_cpu_boot_t config = boot(&fixture);
    sm64_saturn_sound_cpu_state_t state;
    config.ready_wait_budget = 1U;
    sm64_saturn_pcm_put_be16(fixture.ram, SM64_SATURN_PCM_MAGIC_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_MAGIC);
    sm64_saturn_pcm_put_be16(fixture.ram, SM64_SATURN_PCM_VERSION_OFFSET,
                             SM64_SATURN_PCM_PROTOCOL_VERSION);
    sm64_saturn_pcm_put_be16(fixture.ram, SM64_SATURN_PCM_STATUS_OFFSET,
                             SM64_SATURN_PCM_STATUS_READY);
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_READY_TIMEOUT);
}

static void test_only_cold_boot_and_explicit_recovery_may_clear(void)
{
    fixture_t fixture = {0};
    sm64_saturn_sound_cpu_boot_t config = boot(&fixture);
    sm64_saturn_sound_cpu_state_t state;
    config.kind = (sm64_saturn_sound_cpu_boot_kind_t)99;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(fixture.order_count == 0U);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_BAD_CONFIG);

    memset(&fixture, 0, sizeof(fixture));
    config = boot(&fixture);
    config.kind = SM64_SATURN_SOUND_CPU_BOOT_RECOVERY;
    assert(sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwMCDIBNWW") == 0);
}

static void test_command_copy_and_mailbox_failures_are_named(void)
{
    fixture_t fixture = {0};
    sm64_saturn_sound_cpu_boot_t config = boot(&fixture);
    sm64_saturn_sound_cpu_state_t state;
    fixture.off_ok = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GO") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_SOUND_OFF);

    memset(&fixture, 0, sizeof(fixture));
    config = boot(&fixture);
    fixture.copy_ok = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwMCD") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_COPY);

    memset(&fixture, 0, sizeof(fixture));
    config = boot(&fixture);
    fixture.mailbox_ok = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwMCDI") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_MAILBOX);

    memset(&fixture, 0, sizeof(fixture));
    config = boot(&fixture);
    fixture.on_ok = false;
    assert(!sm64_saturn_sound_cpu_boot(&config, &state));
    assert(strcmp(fixture.order, "GOwMCDIBN") == 0);
    assert(state.fault == SM64_SATURN_SOUND_CPU_FAULT_SOUND_ON);
}

static void test_generic_completion_retains_raw_oreg31_without_boolean_cast(void)
{
    sm64_saturn_sound_cpu_yaul_result_t diagnostics = {0};
    assert(sm64_saturn_sound_cpu_record_generic_completion(
               &diagnostics, SM64_SATURN_SOUND_CPU_COMMAND_OFF, 0x00U) ==
           SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED);
    assert(diagnostics.completed_count == 1U);
    assert(diagnostics.last_command == SM64_SATURN_SOUND_CPU_COMMAND_OFF);
    assert(diagnostics.last_oreg31 == 0x00U);
    assert(sm64_saturn_sound_cpu_record_generic_completion(
               &diagnostics, SM64_SATURN_SOUND_CPU_COMMAND_ON, 0xffU) ==
           SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED);
    assert(diagnostics.completed_count == 2U);
    assert(diagnostics.last_command == SM64_SATURN_SOUND_CPU_COMMAND_ON);
    assert(diagnostics.last_oreg31 == 0xffU);
    assert(sm64_saturn_sound_cpu_record_generic_completion(
               &diagnostics, 0x55U, 0x00U) ==
           SM64_SATURN_SOUND_CPU_COMMAND_REJECTED);
    assert(diagnostics.completed_count == 2U);
}

int main(void)
{
    test_cold_boot_orders_staged_bytes_mode_stop_copy_publish_start();
    test_preflight_failures_never_stop_or_clear_sound_ram();
    test_stop_and_ready_waits_are_bounded_and_fail_closed();
    test_ready_without_heartbeat_advance_is_not_ready();
    test_only_cold_boot_and_explicit_recovery_may_clear();
    test_command_copy_and_mailbox_failures_are_named();
    test_generic_completion_retains_raw_oreg31_without_boolean_cast();
    return 0;
}
