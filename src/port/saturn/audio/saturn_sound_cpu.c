/*
 * Original project wrapper informed by pinned, pattern-only references:
 * - yaul-org/libyaul@6012f79f237773378c8014e70d8998ad95a38d98 (MIT),
 *   smpc/smc.h:93-102 and 199-215.
 * - ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0
 *   (MIT), PROJ/main.c:507-577 and jo_demo/pcmsys.c:218-272.
 * - libyaul-examples@66b648eb059bb8bb7392eac70821605a68205b85
 *   (license unclear), scsp-ponesound-pcm8/ponesound.c:63-92.
 * No upstream source, mutable structure, driver, or asset is copied.
 */
#include "saturn_sound_cpu.h"

#include <string.h>

#include "saturn_pcm_protocol.h"

static bool config_valid(const sm64_saturn_sound_cpu_boot_t *boot)
{
    return boot != NULL &&
           (boot->kind == SM64_SATURN_SOUND_CPU_BOOT_COLD ||
            boot->kind == SM64_SATURN_SOUND_CPU_BOOT_RECOVERY) &&
           boot->sound_ram != NULL && boot->stop_wait_budget != 0U &&
           boot->ready_wait_budget != 0U && boot->stage_ready != NULL &&
           boot->set_512k_mode != NULL &&
           boot->generic_smpc_command != NULL &&
           boot->sound_cpu_stopped != NULL &&
           boot->clear_owned_regions != NULL &&
           boot->copy_staged_regions != NULL &&
           boot->initialize_mailbox != NULL &&
           boot->publish_barrier != NULL && boot->wait_stop_tick != NULL &&
           boot->wait_ready_tick != NULL;
}

static bool fail(sm64_saturn_sound_cpu_state_t *state,
                 sm64_saturn_sound_cpu_fault_t fault)
{
    state->phase = SM64_SATURN_SOUND_CPU_PHASE_FAULT;
    state->fault = (uint8_t)fault;
    state->fault_count++;
    return false;
}

static bool protocol_ready(const volatile uint8_t *ram, uint16_t heartbeat)
{
    return sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_MAGIC_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_MAGIC &&
           sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_VERSION_OFFSET) ==
               SM64_SATURN_PCM_PROTOCOL_VERSION &&
           sm64_saturn_pcm_get_be16(ram, SM64_SATURN_PCM_STATUS_OFFSET) ==
               SM64_SATURN_PCM_STATUS_READY &&
           sm64_saturn_pcm_get_be16(ram,
                                    SM64_SATURN_PCM_HEARTBEAT_OFFSET) !=
               heartbeat;
}

bool sm64_saturn_sound_cpu_boot(
    const sm64_saturn_sound_cpu_boot_t *boot,
    sm64_saturn_sound_cpu_state_t *state)
{
    uint16_t poll;
    if (state == NULL) return false;
    memset(state, 0, sizeof(*state));
    state->phase = SM64_SATURN_SOUND_CPU_PHASE_MUTED;
    if (!config_valid(boot)) return fail(state, SM64_SATURN_SOUND_CPU_FAULT_BAD_CONFIG);
    state->boot_attempts = 1U;
    state->boot_kind = (uint8_t)boot->kind;
    if (!boot->stage_ready(boot->context))
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_STAGE);
    state->phase = SM64_SATURN_SOUND_CPU_PHASE_STAGED;
    state->phase = SM64_SATURN_SOUND_CPU_PHASE_STOPPING;
    if (boot->generic_smpc_command(
            boot->context, SM64_SATURN_SOUND_CPU_COMMAND_OFF) !=
        SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED)
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_SOUND_OFF);
    for (poll = 0U; poll < boot->stop_wait_budget; poll++) {
        state->stop_polls++;
        if (boot->sound_cpu_stopped(boot->context)) break;
        boot->wait_stop_tick(boot->context);
    }
    if (poll == boot->stop_wait_budget)
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_STOP_TIMEOUT);
    state->phase = SM64_SATURN_SOUND_CPU_PHASE_STOPPED;
    if (!boot->set_512k_mode(boot->context))
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_512K_MODE);
    if (!boot->clear_owned_regions(boot->context))
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_CLEAR);
    if (!boot->copy_staged_regions(boot->context))
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_COPY);
    if (!boot->initialize_mailbox(boot->context))
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_MAILBOX);
    boot->publish_barrier(boot->context);
    state->initial_heartbeat = sm64_saturn_pcm_get_be16(
        boot->sound_ram, SM64_SATURN_PCM_HEARTBEAT_OFFSET);
    state->phase = SM64_SATURN_SOUND_CPU_PHASE_STARTING;
    if (boot->generic_smpc_command(
            boot->context, SM64_SATURN_SOUND_CPU_COMMAND_ON) !=
        SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED)
        return fail(state, SM64_SATURN_SOUND_CPU_FAULT_SOUND_ON);
    for (poll = 0U; poll < boot->ready_wait_budget; poll++) {
        boot->wait_ready_tick(boot->context);
        state->ready_polls++;
        if (protocol_ready(boot->sound_ram, state->initial_heartbeat)) {
            state->ready_heartbeat = sm64_saturn_pcm_get_be16(
                boot->sound_ram, SM64_SATURN_PCM_HEARTBEAT_OFFSET);
            state->phase = SM64_SATURN_SOUND_CPU_PHASE_READY;
            return true;
        }
    }
    return fail(state, SM64_SATURN_SOUND_CPU_FAULT_READY_TIMEOUT);
}

#if defined(__sh__)
#include <yaul.h>

#endif

sm64_saturn_sound_cpu_command_result_t
sm64_saturn_sound_cpu_record_generic_completion(
    sm64_saturn_sound_cpu_yaul_result_t *result, uint8_t command,
    uint8_t raw_oreg31)
{
    if (command != SM64_SATURN_SOUND_CPU_COMMAND_OFF &&
        command != SM64_SATURN_SOUND_CPU_COMMAND_ON)
        return SM64_SATURN_SOUND_CPU_COMMAND_REJECTED;
    if (result != NULL) {
        result->last_command = command;
        result->last_oreg31 = raw_oreg31;
        result->completed_count++;
    }
    return SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED;
}

#if defined(__sh__)

sm64_saturn_sound_cpu_command_result_t
sm64_saturn_sound_cpu_yaul_command(void *context, uint8_t command)
{
    sm64_saturn_sound_cpu_yaul_result_t *result = context;
    uint8_t raw;
    if (command != SM64_SATURN_SOUND_CPU_COMMAND_OFF &&
        command != SM64_SATURN_SOUND_CPU_COMMAND_ON)
        return SM64_SATURN_SOUND_CPU_COMMAND_REJECTED;
    /* Yaul's generic call synchronously waits for SMPC command completion;
     * OREG31 has no documented boolean success contract for these commands,
     * so preserve it as telemetry and rely on bounded stopped/READY probes. */
    raw = smpc_smc_call((cpu_smpc_cmd_t)command);
    return sm64_saturn_sound_cpu_record_generic_completion(result, command,
                                                            raw);
}

bool sm64_saturn_sound_cpu_yaul_set_512k(void *context __unused)
{
    volatile uint8_t *const scsp_common = (volatile uint8_t *)0x25B00400UL;
    *scsp_common = 0x02U;
    return *scsp_common == 0x02U;
}
#else
sm64_saturn_sound_cpu_command_result_t
sm64_saturn_sound_cpu_yaul_command(void *context, uint8_t command)
{
    (void)context;
    (void)command;
    return SM64_SATURN_SOUND_CPU_COMMAND_REJECTED;
}

bool sm64_saturn_sound_cpu_yaul_set_512k(void *context)
{
    (void)context;
    return false;
}
#endif
