#include "saturn_audio_package.h"

#include <string.h>

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t align2048(uint32_t value)
{
    return (value + SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT - 1U) &
           ~(SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT - 1U);
}

bool sm64_saturn_audio_package_validate_header(
    const void *bytes, uint32_t byte_count,
    sm64_saturn_audio_package_view_t *view)
{
    const uint8_t *raw = (const uint8_t *)bytes;
    uint32_t i;
    if (raw == 0 || view == 0 || byte_count < SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE ||
        be32(raw) != SM64_SATURN_AUDIO_PACKAGE_MAGIC || be16(raw + 4) != 1U ||
        be16(raw + 6) != SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE) {
        return false;
    }
    memset(view, 0, sizeof(*view));
    view->package_size = be32(raw + 8);
    view->sequence_count = be32(raw + 12);
    view->bank_count = be32(raw + 16);
    view->sample_count = be32(raw + 20);
    view->chunk_count = be32(raw + 24);
    if (view->package_size != byte_count || view->sequence_count != 35U ||
        view->bank_count != 38U || view->sample_count != 219U ||
        view->chunk_count == 0U || view->chunk_count > 1024U ||
        SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE +
            view->chunk_count * SM64_SATURN_AUDIO_PACKAGE_CHUNK_SIZE > byte_count) {
        return false;
    }
    memcpy(view->source_sha256, raw + 32, sizeof(view->source_sha256));
    memcpy(view->package_sha256, raw + 64, sizeof(view->package_sha256));
    for (i = 0U; i < view->chunk_count; ++i) {
        const uint8_t *descriptor = raw + SM64_SATURN_AUDIO_PACKAGE_HEADER_SIZE +
                                    i * SM64_SATURN_AUDIO_PACKAGE_CHUNK_SIZE;
        const uint32_t offset = be32(descriptor + 4);
        const uint32_t size = be32(descriptor + 8);
        const uint32_t alignment = be32(descriptor + 12);
        if (alignment != SM64_SATURN_AUDIO_PACKAGE_ALIGNMENT ||
            offset % alignment != 0U || offset > byte_count || size > byte_count - offset) {
            return false;
        }
    }
    return true;
}

bool sm64_saturn_audio_residency_prepare(
    const sm64_saturn_audio_residency_plan_t *active,
    uint32_t replacement_generation, uint32_t driver_size,
    uint32_t mailbox_size, uint32_t sample_size,
    sm64_saturn_audio_residency_plan_t *replacement)
{
    uint32_t cursor;
    sm64_saturn_audio_residency_plan_t candidate;
    if (active == 0 || replacement == 0 || active->generation == 0U ||
        replacement_generation == 0U || replacement_generation <= active->generation ||
        active->total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT) {
        return false;
    }
    cursor = align2048(active->total_bytes);
    candidate.generation = replacement_generation;
    candidate.driver_offset = cursor;
    candidate.driver_size = driver_size;
    cursor = align2048(cursor + driver_size);
    candidate.mailbox_offset = cursor;
    candidate.mailbox_size = mailbox_size;
    cursor = align2048(cursor + mailbox_size);
    candidate.sample_offset = cursor;
    candidate.sample_size = sample_size;
    candidate.total_bytes = cursor + sample_size;
    candidate.active_generation_retained = true;
    candidate.post_boot_clear_rejected = true;
    /* Both generations must coexist until an MC68000 acknowledgement. */
    if (candidate.total_bytes > SM64_SATURN_AUDIO_RESIDENT_LIMIT) {
        return false;
    }
    *replacement = candidate;
    return true;
}

bool sm64_saturn_audio_residency_commit(
    const sm64_saturn_audio_residency_plan_t *active,
    const sm64_saturn_audio_residency_plan_t *replacement,
    uint32_t acknowledged_generation)
{
    return active != 0 && replacement != 0 &&
           replacement->generation > active->generation &&
           acknowledged_generation == replacement->generation &&
           replacement->active_generation_retained &&
           replacement->post_boot_clear_rejected;
}

bool sm64_saturn_audio_residency_clear_sound_ram(
    const sm64_saturn_audio_residency_plan_t *active, bool post_boot)
{
    /* A whole-RAM clear is only legal before the first committed generation. */
    return !post_boot && (active == 0 || active->generation == 0U);
}
