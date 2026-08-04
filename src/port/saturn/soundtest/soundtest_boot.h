#ifndef SM64_SATURN_SOUNDTEST_BOOT_H
#define SM64_SATURN_SOUNDTEST_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "saturn_pcm_transport.h"

typedef enum sm64_saturn_soundtest_boot_result {
    SM64_SATURN_SOUNDTEST_BOOT_READY = 0,
    SM64_SATURN_SOUNDTEST_BOOT_BAD_CONFIG,
    SM64_SATURN_SOUNDTEST_BOOT_BAD_ASSETS,
    SM64_SATURN_SOUNDTEST_BOOT_SOUND_OFF_FAILED,
    SM64_SATURN_SOUNDTEST_BOOT_COPY_FAILED,
    SM64_SATURN_SOUNDTEST_BOOT_SOUND_ON_FAILED,
    SM64_SATURN_SOUNDTEST_BOOT_HEARTBEAT_TIMEOUT,
    SM64_SATURN_SOUNDTEST_BOOT_ENQUEUE_FAILED,
} sm64_saturn_soundtest_boot_result_t;

typedef struct sm64_saturn_soundtest_boot {
    volatile uint8_t *sound_ram;
    const uint8_t *driver;
    uint32_t driver_bytes;
    const uint8_t *bank;
    uint32_t bank_bytes;
    uint16_t heartbeat_vblank_budget;
    uint16_t initial_master_volume;
    void *context;
    bool (*sound_off)(void *context);
    bool (*copy_region)(void *context, volatile uint8_t *destination,
                        const uint8_t *source, uint32_t bytes);
    bool (*sound_on)(void *context);
    void (*wait_vblank)(void *context);
} sm64_saturn_soundtest_boot_t;

sm64_saturn_soundtest_boot_result_t sm64_saturn_soundtest_boot(
    const sm64_saturn_soundtest_boot_t *boot,
    sm64_saturn_pcm_transport_t *transport);

#endif
