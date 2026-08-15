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

/*
 * SCSP effect-DSP quiesce -- measured root cause, 2026-08-15.
 *
 * tools/saturn/probe_sound_ram_verify.py read SCSP common register 0x402
 * back as 0x0118 on the R1 candidate: RBP = 24, RBL = 2, which places the
 * effect DSP's ring buffer at [0x30000, 0x40000) in sound RAM, and MPRO
 * held a live BIOS microprogram (377 of 1024 bytes nonzero).  The DSP
 * rewrote that whole window continuously, straight over the staged sample
 * bank: 65,354 bytes of the PCM bank differed from the packager's blob,
 * including 21,320 bytes of the music sample at 0x3ACB8 -- 2.665 s of
 * every 8.146 s loop -- plus 8 SFX samples that were read out of the
 * reverb ring instead of the packaged PCM.  That is the periodic piercing
 * noise.
 *
 * The port never wrote RBP, RBL, MPRO, COEF or MADRS anywhere: they were
 * BIOS leftovers, and they vary by BIOS revision and region.  They are
 * therefore programmed explicitly on every cold boot -- with the sound CPU
 * stopped and before any driver, metadata or PCM byte is copied -- rather
 * than assumed, or dodged by moving our own data out of one observed
 * window.
 *
 * Offsets and encodings verified against Ymir's SCSP core
 * (ymir-core/include/ymir/hw/scsp): the common-register decoder in
 * scsp.hpp places COEF at 0x700-0x77F, MADRS at 0x780-0x7BF and MPRO at
 * 0x800-0xBFF, and WriteReg402 takes RBP from bits 0-6 and RBL from bits
 * 7-8 of register 0x402; scsp_dsp.hpp computes the ring base as
 * RBP << 12 words (RBP * 0x2000 bytes) and its length as
 * (0x2000 << RBL) words.
 */
enum {
    /* SH-2 cache-through view; the 68K sound CPU sees the same register
     * block at 0x00100000. */
    SM64_SATURN_SCSP_REGISTER_BASE = 0x25B00000U,
    SM64_SATURN_SCSP_DSP_COEF_OFFSET = 0x700U,
    SM64_SATURN_SCSP_DSP_COEF_BYTES = 0x080U,
    SM64_SATURN_SCSP_DSP_MADRS_OFFSET = 0x780U,
    SM64_SATURN_SCSP_DSP_MADRS_BYTES = 0x040U,
    SM64_SATURN_SCSP_DSP_MPRO_OFFSET = 0x800U,
    SM64_SATURN_SCSP_DSP_MPRO_BYTES = 0x400U,
    SM64_SATURN_SCSP_DSP_RING_OFFSET = 0x402U,
    /* 0x28 * 0x2000 = 0x50000, above the packaged bank end measured at
     * 0x4AB49 (21,687 bytes of headroom), and 0x20000 clear of the top of
     * 512 KiB sound RAM.  A bank that grows past 0x50000 must move this. */
    SM64_SATURN_SCSP_DSP_RING_RBP = 0x28U,
    SM64_SATURN_SCSP_DSP_RING_RBL = 0x02U,
    SM64_SATURN_SCSP_DSP_RING_BASE =
        SM64_SATURN_SCSP_DSP_RING_RBP * 0x2000U,
    SM64_SATURN_SCSP_DSP_RING_BYTES =
        (0x2000U << SM64_SATURN_SCSP_DSP_RING_RBL) * 2U,
    SM64_SATURN_SCSP_DSP_RING_WORD =
        (SM64_SATURN_SCSP_DSP_RING_RBL << 7) |
        SM64_SATURN_SCSP_DSP_RING_RBP,
};

_Static_assert(SM64_SATURN_SCSP_DSP_RING_WORD == 0x0128U,
               "0x402 must encode RBP in bits 0-6 and RBL in bits 7-8");
_Static_assert((uint32_t)SM64_SATURN_SCSP_DSP_RING_BASE >=
                   (uint32_t)SM64_SATURN_PCM_BANK_OFFSET,
               "effect-DSP ring must sit above the staged PCM bank base");
_Static_assert((uint32_t)SM64_SATURN_SCSP_DSP_RING_BASE +
                       (uint32_t)SM64_SATURN_SCSP_DSP_RING_BYTES <=
                   (uint32_t)SM64_SATURN_PCM_SOUND_RAM_BYTES,
               "effect-DSP ring must fit inside 512 KiB sound RAM");

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

/* Stop the SCSP effect DSP writing sound RAM, then park its ring buffer
 * above everything the cold boot is about to stage.  See the measurement
 * note at the top of this file. */
static void sm64_saturn_sound_cpu_program_effect_dsp(void)
{
    volatile uint16_t *const mpro = (volatile uint16_t *)(uintptr_t)(
        SM64_SATURN_SCSP_REGISTER_BASE + SM64_SATURN_SCSP_DSP_MPRO_OFFSET);
    volatile uint16_t *const coef = (volatile uint16_t *)(uintptr_t)(
        SM64_SATURN_SCSP_REGISTER_BASE + SM64_SATURN_SCSP_DSP_COEF_OFFSET);
    volatile uint16_t *const madrs = (volatile uint16_t *)(uintptr_t)(
        SM64_SATURN_SCSP_REGISTER_BASE + SM64_SATURN_SCSP_DSP_MADRS_OFFSET);
    volatile uint16_t *const ring = (volatile uint16_t *)(uintptr_t)(
        SM64_SATURN_SCSP_REGISTER_BASE + SM64_SATURN_SCSP_DSP_RING_OFFSET);
    uint32_t index;

    /* An all-zero microprogram is all-NOP: no instruction asserts MWT or
     * MRD, so the DSP issues no sound-RAM access at all, wherever the ring
     * happens to point.  Clearing COEF and MADRS removes the leftover
     * coefficients and memory-address registers that program used. */
    for (index = 0U;
         index < SM64_SATURN_SCSP_DSP_MPRO_BYTES / sizeof(uint16_t);
         index++) {
        mpro[index] = 0U;
    }
    for (index = 0U;
         index < SM64_SATURN_SCSP_DSP_COEF_BYTES / sizeof(uint16_t);
         index++) {
        coef[index] = 0U;
    }
    for (index = 0U;
         index < SM64_SATURN_SCSP_DSP_MADRS_BYTES / sizeof(uint16_t);
         index++) {
        madrs[index] = 0U;
    }
    /* Defence in depth: even if some BIOS revision leaves DSP state we did
     * not neutralise, the ring can only reach dead sound RAM. */
    *ring = (uint16_t)SM64_SATURN_SCSP_DSP_RING_WORD;
}

bool sm64_saturn_sound_cpu_yaul_set_512k(void *context __unused)
{
    volatile uint8_t *const scsp_common = (volatile uint8_t *)0x25B00400UL;

    /* The SCSP common control byte is a write-only mode latch.  The pinned
     * PoneSound reference writes it before clearing SCSP RAM and does not
     * read it back (jo_cdda_demo/pcmsys.c:218-224).  A readback is zero on
     * Ymir and falsely aborts sourceboot before the later READY/heartbeat
     * probe can validate the copied driver. */
    *scsp_common = 0x02U;

    /* The sound CPU is stopped and no staged byte has reached sound RAM
     * yet: neutralise the inherited effect-DSP program and repoint its
     * ring before the clear/driver/metadata/PCM copies run. */
    sm64_saturn_sound_cpu_program_effect_dsp();
    return true;
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
