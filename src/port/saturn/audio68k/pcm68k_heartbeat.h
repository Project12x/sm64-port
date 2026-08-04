#ifndef SM64_SATURN_PCM68K_HEARTBEAT_H
#define SM64_SATURN_PCM68K_HEARTBEAT_H

#include <stdint.h>

void sm64_saturn_pcm68k_publish_boot(volatile uint8_t *sound_ram);
void sm64_saturn_pcm68k_publish_tick(volatile uint8_t *sound_ram,
                                     uint16_t *heartbeat);

#endif
