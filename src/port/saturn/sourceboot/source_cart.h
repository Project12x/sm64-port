#ifndef SM64_SATURN_SOURCEBOOT_SOURCE_CART_H
#define SM64_SATURN_SOURCEBOOT_SOURCE_CART_H

#include <stdint.h>

typedef enum sm64_saturn_source_cart_status {
    SM64_SATURN_SOURCE_CART_OK = 0,
    SM64_SATURN_SOURCE_CART_MISSING_4MIB,
    SM64_SATURN_SOURCE_CART_CD_INIT_FAILED,
    SM64_SATURN_SOURCE_CART_IMAGE_NOT_FOUND,
    SM64_SATURN_SOURCE_CART_SIZE_MISMATCH,
    SM64_SATURN_SOURCE_CART_READ_FAILED,
} sm64_saturn_source_cart_status_t;

/* Kept in work RAM so Ymir's debugger can prove early CD -> cart progress
 * without dereferencing the source asset bank itself. */
typedef struct sm64_saturn_source_cart_probe {
    uint32_t magic;
    uint32_t stage;
    uint32_t expected_size;
    uint32_t copied_size;
    uint32_t cart_id;
    uint32_t cart_size;
    uint32_t status;
} sm64_saturn_source_cart_probe_t;

enum {
    SM64_SATURN_SOURCE_CART_PROBE_MAGIC = 0x53434152U, /* "SCAR" */
    SM64_SATURN_SOURCE_CART_STAGE_RESET = 0,
    SM64_SATURN_SOURCE_CART_STAGE_CART_READY,
    SM64_SATURN_SOURCE_CART_STAGE_CD_READY,
    SM64_SATURN_SOURCE_CART_STAGE_FILE_READY,
    SM64_SATURN_SOURCE_CART_STAGE_COPYING,
    SM64_SATURN_SOURCE_CART_STAGE_READY,
    SM64_SATURN_SOURCE_CART_STAGE_FAILED,
};

extern volatile sm64_saturn_source_cart_probe_t g_sm64_saturn_source_cart_probe;

/* Must run before any original source data is dereferenced. */
sm64_saturn_source_cart_status_t sm64_saturn_source_cart_load(void);

/* This reports through boot-resident data, so it is safe after a failed load. */
void sm64_saturn_source_cart_report_failure(sm64_saturn_source_cart_status_t status);

#endif
