#ifndef SM64_SATURN_AUDIO_PACKAGE_H
#define SM64_SATURN_AUDIO_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SM64_SATURN_AUDIO_PACKAGE_MAGIC = 0x53363441U, /* S64A */
    SM64_SATURN_AUDIO_PACKAGE_VERSION = 1U,
    SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE = 96U,
    SM64_SATURN_AUDIO_PACKAGE_CHUNK_SIZE = 52U,
    SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT = 2048U,
    SM64_SATURN_AUDIO_RESIDENT_LIMIT = 480U * 1024U,
};

typedef struct sm64_saturn_audio_package_view {
    uint32_t package_size;
    uint32_t sequence_count;
    uint32_t bank_count;
    uint32_t sample_count;
    uint32_t chunk_count;
    uint8_t source_sha256[32];
    uint8_t package_sha256[32];
} sm64_saturn_audio_package_view_t;

bool sm64_saturn_audio_package_validate_header(
    const void *bytes, uint32_t byte_count,
    sm64_saturn_audio_package_view_t *view);

typedef struct sm64_saturn_audio_residency_plan {
    uint32_t generation;
    uint32_t driver_offset;
    uint32_t driver_size;
    uint32_t mailbox_offset;
    uint32_t mailbox_size;
    uint32_t sample_offset;
    uint32_t sample_size;
    uint32_t total_bytes;
    bool active_generation_retained;
    bool post_boot_clear_rejected;
} sm64_saturn_audio_residency_plan_t;

/* Plans a replacement without touching the committed generation.  The active
 * plan is supplied by value, making it impossible for preparation to erase or
 * mutate a live sound-RAM span. */
bool sm64_saturn_audio_residency_prepare(
    const sm64_saturn_audio_residency_plan_t *active,
    uint32_t replacement_generation, uint32_t driver_size,
    uint32_t mailbox_size, uint32_t sample_size,
    sm64_saturn_audio_residency_plan_t *replacement);

bool sm64_saturn_audio_residency_commit(
    const sm64_saturn_audio_residency_plan_t *active,
    const sm64_saturn_audio_residency_plan_t *replacement,
    uint32_t acknowledged_generation);

bool sm64_saturn_audio_residency_clear_sound_ram(
    const sm64_saturn_audio_residency_plan_t *active, bool post_boot);

#endif
