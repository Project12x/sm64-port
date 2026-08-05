/*
 * Immutable original-source asset bank loader for E2 sourceboot.
 *
 * `SOURCE.DAT` is the linked .cart_rodata section.  Loading that section at
 * its final 0x22400000 VMA preserves all native pointers in generated SM64
 * assets; it is deliberately not an offset-package or a general heap.
 */
#include <stddef.h>
#include <stdint.h>

#include <yaul.h>

#include "source_cart.h"

#ifndef SATURN_SOURCE_CART_STAGE_SECTORS
#define SATURN_SOURCE_CART_STAGE_SECTORS 16U
#endif
#define SOURCE_CART_STAGE_BYTES \
    (SATURN_SOURCE_CART_STAGE_SECTORS * CDFS_SECTOR_SIZE)
_Static_assert(SATURN_SOURCE_CART_STAGE_SECTORS == 4U ||
               SATURN_SOURCE_CART_STAGE_SECTORS == 8U ||
               SATURN_SOURCE_CART_STAGE_SECTORS == 16U,
               "unsupported source-cart staging size");
#define SOURCE_CART_FILELIST_ENTRIES 16

extern const uint8_t __sourceboot_cart_rodata_start[];
extern const uint8_t __sourceboot_cart_rodata_end[];
extern const uint8_t sm64_saturn_sourceboot_scene_package_root[]
    __attribute__((weak));
extern const uint32_t sm64_saturn_sourceboot_scene_package_root_size
    __attribute__((weak));

static const char s_source_cart_name[] __attribute__((section(".bootdata"))) =
    "SOURCE.DAT";
static const char s_source_cart_missing_text[] __attribute__((section(".bootdata"))) =
    "sourceboot: 4 MiB RAM cart required\n";
static const char s_source_cart_cd_text[] __attribute__((section(".bootdata"))) =
    "sourceboot: SOURCE.DAT CD read failed\n";
static const char s_source_cart_image_text[] __attribute__((section(".bootdata"))) =
    "sourceboot: SOURCE.DAT is missing or invalid\n";

static uint8_t s_source_cart_stage[SOURCE_CART_STAGE_BYTES] __aligned(16);
static cdfs_filelist_entry_t
    s_source_cart_file_entries[SOURCE_CART_FILELIST_ENTRIES] __aligned(4);

volatile sm64_saturn_source_cart_probe_t g_sm64_saturn_source_cart_probe;

static sm64_saturn_source_cart_status_t source_cart_finish(
        sm64_saturn_source_cart_status_t status) {
    g_sm64_saturn_source_cart_probe.status = (uint32_t)status;
    g_sm64_saturn_source_cart_probe.stage =
        (status == SM64_SATURN_SOURCE_CART_OK)
            ? SM64_SATURN_SOURCE_CART_STAGE_READY
            : SM64_SATURN_SOURCE_CART_STAGE_FAILED;
    return status;
}

static int source_cart_name_matches(const char *name) {
    size_t index = 0;

    while (s_source_cart_name[index] != '\0') {
        if (name[index] != s_source_cart_name[index])
            return 0;
        index++;
    }
    return name[index] == '\0';
}

static const cdfs_filelist_entry_t *source_cart_file_find(
        const cdfs_filelist_t *filelist) {
    for (uint32_t index = 0; index < filelist->entries_count; index++) {
        const cdfs_filelist_entry_t *entry = &filelist->entries[index];
        if (entry->type == CDFS_ENTRY_TYPE_FILE &&
            source_cart_name_matches(entry->name))
            return entry;
    }
    return NULL;
}

static void source_cart_copy(void *destination, const void *source,
                             size_t byte_count) {
    volatile uint16_t *out = (volatile uint16_t *)destination;
    const uint16_t *in = (const uint16_t *)source;

    /* The linked bank and staging chunks are 16-byte aligned.  Use 16-bit
     * stores for the Saturn DRAM-cart bus rather than assuming 32-bit writes
     * are legal across every supported cart implementation. */
    while (byte_count != 0U) {
        *out++ = *in++;
        byte_count -= sizeof(*in);
    }
}

sm64_saturn_source_cart_status_t sm64_saturn_source_cart_load(void) {
    const size_t expected_size = (size_t)(__sourceboot_cart_rodata_end -
                                          __sourceboot_cart_rodata_start);
    cdfs_filelist_t filelist;
    const cdfs_filelist_entry_t *entry;
    uint8_t *destination;
    size_t remaining;
    fad_t fad;
    dram_cart_id_t cart_id;
    size_t cart_size;

    g_sm64_saturn_source_cart_probe.magic = SM64_SATURN_SOURCE_CART_PROBE_MAGIC;
    g_sm64_saturn_source_cart_probe.stage = SM64_SATURN_SOURCE_CART_STAGE_RESET;
    g_sm64_saturn_source_cart_probe.expected_size = (uint32_t)expected_size;
    g_sm64_saturn_source_cart_probe.copied_size = 0U;
    g_sm64_saturn_source_cart_probe.cart_id = 0U;
    g_sm64_saturn_source_cart_probe.cart_size = 0U;
    g_sm64_saturn_source_cart_probe.status = (uint32_t)SM64_SATURN_SOURCE_CART_OK;

    dram_cart_init();
    cart_id = dram_cart_id_get();
    cart_size = dram_cart_size_get();
    g_sm64_saturn_source_cart_probe.cart_id = (uint32_t)cart_id;
    g_sm64_saturn_source_cart_probe.cart_size = (uint32_t)cart_size;
    if (cart_id != DRAM_CART_ID_4MIB ||
        expected_size == 0U || expected_size > cart_size)
        return source_cart_finish(SM64_SATURN_SOURCE_CART_MISSING_4MIB);
    g_sm64_saturn_source_cart_probe.stage = SM64_SATURN_SOURCE_CART_STAGE_CART_READY;

    if (cd_block_init() != 0)
        return source_cart_finish(SM64_SATURN_SOURCE_CART_CD_INIT_FAILED);
    g_sm64_saturn_source_cart_probe.stage = SM64_SATURN_SOURCE_CART_STAGE_CD_READY;

    cdfs_init();
    cdfs_filelist_init(&filelist, s_source_cart_file_entries,
                       SOURCE_CART_FILELIST_ENTRIES);
    cdfs_filelist_root_read(&filelist);
    entry = source_cart_file_find(&filelist);
    if (entry == NULL)
        return source_cart_finish(SM64_SATURN_SOURCE_CART_IMAGE_NOT_FOUND);
    if (entry->size != expected_size || (entry->size & 1U) != 0U)
        return source_cart_finish(SM64_SATURN_SOURCE_CART_SIZE_MISMATCH);
    g_sm64_saturn_source_cart_probe.stage = SM64_SATURN_SOURCE_CART_STAGE_FILE_READY;

    destination = (uint8_t *)dram_cart_area_get();
    remaining = expected_size;
    fad = entry->starting_fad;
    g_sm64_saturn_source_cart_probe.stage = SM64_SATURN_SOURCE_CART_STAGE_COPYING;
    while (remaining != 0U) {
        const size_t chunk_size =
            (remaining < SOURCE_CART_STAGE_BYTES) ? remaining : SOURCE_CART_STAGE_BYTES;

        if (cd_block_sectors_read(fad, s_source_cart_stage,
                                  (uint32_t)chunk_size) != 0)
            return source_cart_finish(SM64_SATURN_SOURCE_CART_READ_FAILED);
        source_cart_copy(destination, s_source_cart_stage, chunk_size);
        destination += chunk_size;
        remaining -= chunk_size;
        fad += (fad_t)(chunk_size / CDFS_SECTOR_SIZE);
        g_sm64_saturn_source_cart_probe.copied_size += (uint32_t)chunk_size;
    }

    return source_cart_finish(SM64_SATURN_SOURCE_CART_OK);
}

void sm64_saturn_source_cart_report_failure(
        sm64_saturn_source_cart_status_t status) {
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();

    if (status == SM64_SATURN_SOURCE_CART_MISSING_4MIB) {
        dbgio_puts(s_source_cart_missing_text);
    } else if (status == SM64_SATURN_SOURCE_CART_CD_INIT_FAILED ||
               status == SM64_SATURN_SOURCE_CART_READ_FAILED) {
        dbgio_puts(s_source_cart_cd_text);
    } else {
        dbgio_puts(s_source_cart_image_text);
    }
    dbgio_flush();
}

bool sm64_saturn_source_cart_scene_package_validate(
    const void *bytes, uint32_t byte_count,
    sm64_saturn_scene_package_view_t *view) {
    return sm64_saturn_scene_package_validate_target(bytes, byte_count, view);
}

bool sm64_saturn_source_cart_residency_span(
    uint32_t alignment, sm64_saturn_source_cart_residency_span_t *span) {
    const uint32_t source_bytes = (uint32_t)(__sourceboot_cart_rodata_end -
                                             __sourceboot_cart_rodata_start);
    const uint32_t cart_bytes = (uint32_t)dram_cart_size_get();
    uint32_t high_water;
    uint8_t *base;

    if (span != NULL) memset(span, 0, sizeof(*span));
    if (span == NULL || alignment == 0U || alignment > 4096U ||
        (alignment & (alignment - 1U)) != 0U ||
        source_bytes > UINT32_MAX - (alignment - 1U)) return false;
    high_water = (source_bytes + alignment - 1U) & ~(alignment - 1U);
    if (dram_cart_id_get() != DRAM_CART_ID_4MIB || high_water > cart_bytes) return false;
    base = (uint8_t *)dram_cart_area_get();
    if (base == NULL) return false;
    span->base = base + high_water;
    span->source_prefix_bytes = high_water;
    span->byte_count = cart_bytes - high_water;
    return true;
}

sm64_saturn_source_cart_status_t
sm64_saturn_source_cart_boot_scene_package_validate(
    sm64_saturn_scene_package_view_t *view) {
    sm64_saturn_scene_package_view_t candidate;
    if (view != NULL) memset(view, 0, sizeof(*view));
    if (view == NULL) return SM64_SATURN_SOURCE_CART_INVALID_SCENE_ROOT;
    if ((uintptr_t)sm64_saturn_sourceboot_scene_package_root == 0U ||
        (uintptr_t)&sm64_saturn_sourceboot_scene_package_root_size == 0U)
        return SM64_SATURN_SOURCE_CART_OK;
    if (!sm64_saturn_scene_package_validate(
            sm64_saturn_sourceboot_scene_package_root,
            sm64_saturn_sourceboot_scene_package_root_size, &candidate))
        return SM64_SATURN_SOURCE_CART_INVALID_SCENE_ROOT;
    if (sm64_saturn_scene_package_is_provisional(&candidate))
        return SM64_SATURN_SOURCE_CART_PROVISIONAL_SCENE_ROOT;
    *view = candidate;
    return SM64_SATURN_SOURCE_CART_OK;
}
