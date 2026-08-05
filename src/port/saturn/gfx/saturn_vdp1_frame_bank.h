#ifndef SM64_SATURN_VDP1_FRAME_BANK_H
#define SM64_SATURN_VDP1_FRAME_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "saturn_vdp2_camera_snapshot.h"

struct sm64_saturn_gouraud_bank;
typedef struct sm64_saturn_gouraud_bank sm64_saturn_gouraud_bank_t;

#define SM64_SATURN_VDP1_FRAME_BANK_COUNT 2U
#define SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID 0U

typedef enum sm64_saturn_vdp1_frame_bank_state {
    SM64_SATURN_VDP1_FRAME_BANK_FREE = 0,
    SM64_SATURN_VDP1_FRAME_BANK_BUILDING,
    SM64_SATURN_VDP1_FRAME_BANK_READY,
    SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING,
    SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED,
    SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED,
} sm64_saturn_vdp1_frame_bank_state_t;

typedef enum sm64_saturn_vdp1_transfer_obligation {
    SM64_SATURN_VDP1_TRANSFER_INVALID = 0,
    SM64_SATURN_VDP1_TRANSFER_PENDING,
    SM64_SATURN_VDP1_TRANSFER_RETIRED,
    SM64_SATURN_VDP1_TRANSFER_NOOP,
    SM64_SATURN_VDP1_TRANSFER_FAILED,
} sm64_saturn_vdp1_transfer_obligation_t;

typedef struct sm64_saturn_vdp1_transfer_targets {
    void *command_vram;
    void *gouraud_vram;
    size_t command_capacity_bytes;
    size_t gouraud_capacity_bytes;
} sm64_saturn_vdp1_transfer_targets_t;

typedef struct sm64_saturn_vdp1_wait_stats {
    uint32_t command_cpu_dmac_waits;
    uint32_t gouraud_scu_dma_waits;
} sm64_saturn_vdp1_wait_stats_t;

typedef struct sm64_saturn_vdp1_frame_bank {
    void *command_storage;
    void *gouraud_storage;
    sm64_saturn_gouraud_bank_t *gouraud_bank;
    uint16_t command_capacity;
    uint16_t command_count;
    uint16_t gouraud_count;
    uint32_t snapshot_generation;
    sm64_saturn_vdp2_camera_snapshot_t camera_snapshot;
    uint32_t worker_ticket;
    uint32_t command_transfer_ticket;
    uint32_t gouraud_transfer_ticket;
    sm64_saturn_vdp1_transfer_obligation_t command_transfer_obligation;
    sm64_saturn_vdp1_transfer_obligation_t gouraud_transfer_obligation;
    bool resident_list_armed;
    sm64_saturn_vdp1_frame_bank_state_t state;
} sm64_saturn_vdp1_frame_bank_t;

typedef struct sm64_saturn_vdp1_frame_bank_set {
    sm64_saturn_vdp1_frame_bank_t banks[SM64_SATURN_VDP1_FRAME_BANK_COUNT];
    sm64_saturn_vdp1_frame_bank_t *published;
    uint32_t latest_build_generation;
    bool has_build_generation;
} sm64_saturn_vdp1_frame_bank_set_t;

bool sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
    const void *source, size_t bytes);
bool sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
    const void *source, size_t bytes);
bool sm64_saturn_vdp1_frame_bank_set_init(
    sm64_saturn_vdp1_frame_bank_set_t *banks,
    void *command_bank_0, void *command_bank_1, uint16_t command_capacity,
    sm64_saturn_gouraud_bank_t *gouraud_bank_0,
    sm64_saturn_gouraud_bank_t *gouraud_bank_1);
bool sm64_saturn_vdp1_frame_bank_begin_build(
    sm64_saturn_vdp1_frame_bank_set_t *banks, uint32_t generation,
    sm64_saturn_vdp1_frame_bank_t **out);
bool sm64_saturn_vdp1_frame_bank_set_camera_snapshot(
    sm64_saturn_vdp1_frame_bank_t *bank,
    const sm64_saturn_vdp2_camera_snapshot_t *snapshot);
bool sm64_saturn_vdp1_frame_bank_ready(
    sm64_saturn_vdp1_frame_bank_t *bank, uint16_t command_count,
    uint16_t gouraud_count, uint32_t worker_ticket);
bool sm64_saturn_vdp1_frame_bank_begin_transfers(
    sm64_saturn_vdp1_frame_bank_t *bank, uint32_t command_ticket,
    uint32_t gouraud_ticket);
bool sm64_saturn_vdp1_frame_bank_submit_transfers(
    sm64_saturn_vdp1_frame_bank_t *bank,
    const sm64_saturn_vdp1_transfer_targets_t *targets);
bool sm64_saturn_vdp1_frame_bank_poll_transfers(
    sm64_saturn_vdp1_frame_bank_t *bank);
bool sm64_saturn_vdp1_frame_bank_wait_for_publish(
    sm64_saturn_vdp1_frame_bank_t *bank,
    sm64_saturn_vdp1_wait_stats_t *waits);
bool sm64_saturn_vdp1_frame_bank_arm_resident_list(
    sm64_saturn_vdp1_frame_bank_t *bank);
bool sm64_saturn_vdp1_frame_bank_record_transfers_retired(
    sm64_saturn_vdp1_frame_bank_t *bank, uint32_t command_ticket,
    uint32_t gouraud_ticket);
bool sm64_saturn_vdp1_frame_bank_record_synchronous_complete(
    sm64_saturn_vdp1_frame_bank_t *bank);
bool sm64_saturn_vdp1_frame_bank_publish(
    sm64_saturn_vdp1_frame_bank_set_t *banks,
    sm64_saturn_vdp1_frame_bank_t *bank);
bool sm64_saturn_vdp1_frame_bank_retire(
    sm64_saturn_vdp1_frame_bank_set_t *banks, uint32_t generation);
bool sm64_saturn_vdp1_frame_bank_quarantine(
    sm64_saturn_vdp1_frame_bank_t *bank);

#endif
