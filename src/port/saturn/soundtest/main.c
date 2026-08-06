#include <yaul.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "saturn_pcm_protocol.h"
#include "saturn_pcm_transport.h"
#include "saturn_sound_cpu.h"
#include "soundtest_boot.h"

#define SOUND_RAM ((volatile uint8_t *)0x25A00000UL)

extern const uint8_t sm64_saturn_pcm68k_driver[];
extern const uint8_t sm64_saturn_pcm68k_driver_end[];
extern const uint8_t sm64_saturn_pcm_proof_bank[];
extern const uint8_t sm64_saturn_pcm_proof_bank_end[];

static sm64_saturn_pcm_transport_t transport;
static uint16_t master_volume = 12U;
static bool controls_ready;
static sm64_saturn_sound_cpu_yaul_result_t sound_cpu_diagnostics;

static bool sound_off(void *context __unused)
{
    return sm64_saturn_sound_cpu_yaul_command(
               &sound_cpu_diagnostics, SM64_SATURN_SOUND_CPU_COMMAND_OFF) ==
           SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED;
}

static bool set_512k_mode(void *context __unused)
{
    return sm64_saturn_sound_cpu_yaul_set_512k(NULL);
}

static bool sound_on(void *context __unused)
{
    return sm64_saturn_sound_cpu_yaul_command(
               &sound_cpu_diagnostics, SM64_SATURN_SOUND_CPU_COMMAND_ON) ==
           SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED;
}

static bool copy_region(void *context __unused,
                        volatile uint8_t *destination,
                        const uint8_t *source, uint32_t bytes)
{
    while (bytes-- != 0U) {
        *destination++ = *source++;
    }
    return true;
}

static void wait_vblank(void *context __unused)
{
    vdp2_sync();
    vdp2_sync_wait();
}

static bool enqueue(sm64_saturn_audio_opcode_t opcode, uint16_t word0)
{
    uint16_t words[7] = {0};
    words[0] = word0;
    if (opcode == SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH) {
        words[1] = 12U;
    }
    if (sm64_saturn_audio_opcode_is_control(opcode)) {
        return sm64_saturn_audio_control_enqueue(&transport, opcode, words);
    }
    return sm64_saturn_audio_sfx_enqueue(&transport, opcode, words);
}

void user_init(void)
{
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
        VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
        RGB1555(1, 0, 0, 4));
    vdp2_tvmd_display_set();
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    smpc_peripheral_intback_issue();
}

int main(void)
{
    sm64_saturn_soundtest_boot_t boot = {
        .sound_ram = SOUND_RAM,
        .driver = sm64_saturn_pcm68k_driver,
        .driver_bytes = (uint32_t)(sm64_saturn_pcm68k_driver_end -
                                   sm64_saturn_pcm68k_driver),
        .bank = sm64_saturn_pcm_proof_bank,
        .bank_bytes = (uint32_t)(sm64_saturn_pcm_proof_bank_end -
                                 sm64_saturn_pcm_proof_bank),
        .heartbeat_vblank_budget = 120U,
        .initial_master_volume = master_volume,
        .context = NULL,
        .sound_off = sound_off,
        .set_512k_mode = set_512k_mode,
        .copy_region = copy_region,
        .sound_on = sound_on,
        .wait_vblank = wait_vblank,
    };
    const sm64_saturn_soundtest_boot_result_t result =
        sm64_saturn_soundtest_boot(&boot, &transport);

    for (;;) {
        smpc_peripheral_digital_t digital = {0};
        smpc_peripheral_process();
        smpc_peripheral_digital_port(1, &digital);
        if (result == SM64_SATURN_SOUNDTEST_BOOT_READY) {
            uint16_t edge = 0U;
            if (!digital.connected) {
                controls_ready = false;
            } else if (!controls_ready) {
                /* Libyaul's first populated report can describe every
                 * changed bit as an edge relative to zeroed history. */
                controls_ready = true;
            } else {
                edge = digital.held.raw;
            }
            if ((edge & PERIPHERAL_DIGITAL_A) != 0U)
                (void)enqueue(SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, 0U);
            if ((edge & PERIPHERAL_DIGITAL_B) != 0U)
                (void)enqueue(SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, 1U);
            if ((edge & PERIPHERAL_DIGITAL_C) != 0U)
                (void)enqueue(SM64_SATURN_AUDIO_OPCODE_PLAY_REFRESH, 2U);
            if ((edge & PERIPHERAL_DIGITAL_X) != 0U)
                (void)enqueue(SM64_SATURN_AUDIO_OPCODE_RESET, 0U);
            if ((edge & PERIPHERAL_DIGITAL_L) != 0U && master_volume > 0U) {
                master_volume--;
                (void)enqueue(SM64_SATURN_AUDIO_OPCODE_SET_MASTER,
                              master_volume);
            }
            if ((edge & PERIPHERAL_DIGITAL_R) != 0U && master_volume < 15U) {
                master_volume++;
                (void)enqueue(SM64_SATURN_AUDIO_OPCODE_SET_MASTER,
                              master_volume);
            }
        }
        dbgio_puts("\x1B[H\x1B[2JSM64 SATURN PCM68K SOUNDTEST\n\n");
        dbgio_printf("boot: %s (%u)\n",
            result == SM64_SATURN_SOUNDTEST_BOOT_READY ? "READY" : "FAILED",
            (unsigned int)result);
        dbgio_printf("smpc: count=%u cmd=%02x oreg31=%02x\n",
            (unsigned int)sound_cpu_diagnostics.completed_count,
            (unsigned int)sound_cpu_diagnostics.last_command,
            (unsigned int)sound_cpu_diagnostics.last_oreg31);
        dbgio_printf("heartbeat: %u  consumed: %u\n",
            sm64_saturn_pcm_get_be16(SOUND_RAM,
                SM64_SATURN_PCM_HEARTBEAT_OFFSET),
            sm64_saturn_pcm_get_be16(SOUND_RAM,
                SM64_SATURN_PCM_COMMANDS_CONSUMED_OFFSET));
        dbgio_printf("started: %u  drops: %u  high: %u\n",
            sm64_saturn_pcm_get_be16(SOUND_RAM,
                SM64_SATURN_PCM_VOICES_STARTED_OFFSET),
            (unsigned int)(transport.control_saturated +
                           transport.sfx_saturated),
            (unsigned int)transport.sfx_high_water);
        dbgio_printf("A low  B high  C noise  X stop\nL/R volume: %u\n",
            (unsigned int)master_volume);
        dbgio_flush();
        vdp2_sync();
        vdp2_sync_wait();
        smpc_peripheral_intback_issue();
    }
}
