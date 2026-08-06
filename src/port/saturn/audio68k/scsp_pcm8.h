#ifndef SM64_SATURN_AUDIO68K_SCSP_PCM8_H
#define SM64_SATURN_AUDIO68K_SCSP_PCM8_H

#include <stdbool.h>
#include <stdint.h>

#include "pcm_voice.h"
#include "slot_shadow.h"

enum {
    SM64_SATURN_SCSP_SLOT_BYTES = 0x20U,
    SM64_SATURN_SCSP_VOICE_COUNT = 4U,
    SM64_SATURN_SCSP_MASTER_OFFSET = 0x400U,
    SM64_SATURN_SCSP_REGISTER_BYTES = 0x402U,
    SM64_SATURN_SCSP_SLOT_KEYS = 0x00U,
    SM64_SATURN_SCSP_SLOT_SA_LOW = 0x02U,
    SM64_SATURN_SCSP_SLOT_LSA = 0x04U,
    SM64_SATURN_SCSP_SLOT_LEA = 0x06U,
    SM64_SATURN_SCSP_SLOT_EG = 0x08U,
    SM64_SATURN_SCSP_SLOT_RELEASE = 0x0AU,
    SM64_SATURN_SCSP_SLOT_ATTENUATION = 0x0CU,
    SM64_SATURN_SCSP_SLOT_PITCH = 0x10U,
    SM64_SATURN_SCSP_SLOT_PAN_SEND = 0x16U,
};

bool sm64_saturn_scsp_pcm8_pitch_word(uint16_t sample_rate,
                                      uint16_t *pitch_word);
bool sm64_saturn_scsp_pcm8_start(volatile uint8_t *registers, uint16_t slot,
                                 const sm64_saturn_pcm_sample_t *sample,
                                 uint16_t volume, int16_t pan);
bool sm64_saturn_scsp_pcm8_stop(volatile uint8_t *registers, uint16_t slot);
bool sm64_saturn_scsp_set_master(volatile uint8_t *registers,
                                 uint16_t volume);
/* Sole raw-MMIO boundary for pointer-free slot-shadow commands. */
bool sm64_saturn_scsp_apply_slot_command(
    volatile uint8_t *registers,
    const sm64_saturn_slot_command_t *command);

#endif
