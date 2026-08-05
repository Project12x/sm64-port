#include "saturn_vdp1_frame_bank.h"

#include "saturn_gouraud_bank.h"

#define SM64_SATURN_LWRAM_BASE 0x00200000U
#define SM64_SATURN_LWRAM_TOP  0x00300000U
#define SM64_SATURN_HWRAM_BASE 0x06000000U
#define SM64_SATURN_HWRAM_TOP  0x06100000U
#define SM64_SATURN_ADDRESS_MASK 0x0FFFFFFFU
#define SM64_SATURN_VDP1_COMMAND_BYTES 32U
#define SM64_SATURN_VDP1_SETUP_COMMANDS 3U

static bool range_in_region(const void *source, size_t bytes,
                            uintptr_t region_base, uintptr_t region_top)
{
    const uintptr_t address = (uintptr_t)source & SM64_SATURN_ADDRESS_MASK;
    return source != NULL && bytes > 0U && address >= region_base &&
           address < region_top && bytes <= region_top - address;
}

static uintptr_t normalized_address(const void *source)
{
    return (uintptr_t)source & SM64_SATURN_ADDRESS_MASK;
}

static bool ranges_overlap(const void *left, size_t left_bytes,
                           const void *right, size_t right_bytes)
{
    const uintptr_t left_address = normalized_address(left);
    const uintptr_t right_address = normalized_address(right);
    return left_address < right_address + right_bytes &&
           right_address < left_address + left_bytes;
}

bool sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
    const void *source, size_t bytes)
{
    return range_in_region(source, bytes, SM64_SATURN_LWRAM_BASE,
                           SM64_SATURN_LWRAM_TOP);
}

bool sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
    const void *source, size_t bytes)
{
    return range_in_region(source, bytes, SM64_SATURN_HWRAM_BASE,
                           SM64_SATURN_HWRAM_TOP);
}

static void init_bank(sm64_saturn_vdp1_frame_bank_t *bank,
                      void *command_storage, uint16_t command_capacity,
                      sm64_saturn_gouraud_bank_t *gouraud_bank)
{
    *bank = (sm64_saturn_vdp1_frame_bank_t){
        .command_storage = command_storage,
        .gouraud_storage = gouraud_bank->staging,
        .gouraud_bank = gouraud_bank,
        .command_capacity = command_capacity,
        .state = SM64_SATURN_VDP1_FRAME_BANK_FREE,
    };
}

bool sm64_saturn_vdp1_frame_bank_set_init(
    sm64_saturn_vdp1_frame_bank_set_t *banks,
    void *command_bank_0, void *command_bank_1, uint16_t command_capacity,
    sm64_saturn_gouraud_bank_t *gouraud_bank_0,
    sm64_saturn_gouraud_bank_t *gouraud_bank_1)
{
    if (banks == NULL || gouraud_bank_0 == NULL || gouraud_bank_1 == NULL ||
        normalized_address(gouraud_bank_0) %
            _Alignof(sm64_saturn_gouraud_bank_t) != 0U ||
        normalized_address(gouraud_bank_1) %
            _Alignof(sm64_saturn_gouraud_bank_t) != 0U ||
        ranges_overlap(gouraud_bank_0, sizeof(*gouraud_bank_0),
                       gouraud_bank_1, sizeof(*gouraud_bank_1)) ||
        command_capacity < SM64_SATURN_VDP1_SETUP_COMMANDS)
        return false;
    const size_t command_bytes =
        (size_t)command_capacity * SM64_SATURN_VDP1_COMMAND_BYTES;
    const size_t gouraud_bytes_0 = gouraud_bank_0->capacity > 0U
        ? (size_t)gouraud_bank_0->capacity *
            sizeof(sm64_saturn_gouraud_table_t) : 1U;
    const size_t gouraud_bytes_1 = gouraud_bank_1->capacity > 0U
        ? (size_t)gouraud_bank_1->capacity *
            sizeof(sm64_saturn_gouraud_table_t) : 1U;
    if (!sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
            command_bank_0, command_bytes) ||
        !sm64_saturn_vdp1_frame_bank_command_source_is_lwram(
            command_bank_1, command_bytes) ||
        normalized_address(command_bank_0) % 32U != 0U ||
        normalized_address(command_bank_1) % 32U != 0U ||
        ranges_overlap(command_bank_0, command_bytes,
                       command_bank_1, command_bytes) ||
        !sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
            gouraud_bank_0->staging, gouraud_bytes_0) ||
        !sm64_saturn_vdp1_frame_bank_gouraud_source_is_hwram(
            gouraud_bank_1->staging, gouraud_bytes_1) ||
        normalized_address(gouraud_bank_0->staging) %
            _Alignof(sm64_saturn_gouraud_table_t) != 0U ||
        normalized_address(gouraud_bank_1->staging) %
            _Alignof(sm64_saturn_gouraud_table_t) != 0U ||
        ranges_overlap(gouraud_bank_0->staging, gouraud_bytes_0,
                       gouraud_bank_1->staging, gouraud_bytes_1))
        return false;
    *banks = (sm64_saturn_vdp1_frame_bank_set_t){0};
    init_bank(&banks->banks[0], command_bank_0, command_capacity,
              gouraud_bank_0);
    init_bank(&banks->banks[1], command_bank_1, command_capacity,
              gouraud_bank_1);
    return true;
}

static bool generation_is_newer(const sm64_saturn_vdp1_frame_bank_set_t *banks,
                                uint32_t generation)
{
    if (generation == 0U)
        return false;
    if (!banks->has_build_generation)
        return true;
    return (int32_t)(generation - banks->latest_build_generation) > 0;
}

static bool generation_follows(uint32_t generation, uint32_t prior)
{
    return generation != 0U && (int32_t)(generation - prior) > 0;
}

bool sm64_saturn_vdp1_frame_bank_begin_build(
    sm64_saturn_vdp1_frame_bank_set_t *banks, uint32_t generation,
    sm64_saturn_vdp1_frame_bank_t **out)
{
    if (out != NULL)
        *out = NULL;
    if (banks == NULL || out == NULL || !generation_is_newer(banks, generation))
        return false;
    for (uint32_t index = 0U; index < SM64_SATURN_VDP1_FRAME_BANK_COUNT;
         index++) {
        sm64_saturn_vdp1_frame_bank_t *const bank = &banks->banks[index];
        if (bank->state != SM64_SATURN_VDP1_FRAME_BANK_FREE)
            continue;
        bank->command_count = 0U;
        bank->gouraud_count = 0U;
        bank->snapshot_generation = generation;
        bank->worker_ticket = SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID;
        bank->command_transfer_ticket =
            SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID;
        bank->gouraud_transfer_ticket =
            SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID;
        bank->command_transfer_obligation = SM64_SATURN_VDP1_TRANSFER_INVALID;
        bank->gouraud_transfer_obligation = SM64_SATURN_VDP1_TRANSFER_INVALID;
        bank->state = SM64_SATURN_VDP1_FRAME_BANK_BUILDING;
        banks->latest_build_generation = generation;
        banks->has_build_generation = true;
        *out = bank;
        return true;
    }
    return false;
}

bool sm64_saturn_vdp1_frame_bank_ready(
    sm64_saturn_vdp1_frame_bank_t *bank, uint16_t command_count,
    uint16_t gouraud_count, uint32_t worker_ticket)
{
    if (bank == NULL || bank->gouraud_bank == NULL ||
        bank->state != SM64_SATURN_VDP1_FRAME_BANK_BUILDING ||
        command_count < SM64_SATURN_VDP1_SETUP_COMMANDS ||
        command_count > bank->command_capacity ||
        gouraud_count > bank->gouraud_bank->capacity ||
        worker_ticket == SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID)
        return false;
    bank->command_count = command_count;
    bank->gouraud_count = gouraud_count;
    bank->worker_ticket = worker_ticket;
    bank->state = SM64_SATURN_VDP1_FRAME_BANK_READY;
    return true;
}

bool sm64_saturn_vdp1_frame_bank_begin_transfers(
    sm64_saturn_vdp1_frame_bank_t *bank, uint32_t command_ticket,
    uint32_t gouraud_ticket)
{
    if (bank == NULL || bank->state != SM64_SATURN_VDP1_FRAME_BANK_READY ||
        command_ticket == SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID ||
        (bank->gouraud_count > 0U &&
         gouraud_ticket == SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID) ||
        (bank->gouraud_count == 0U &&
         gouraud_ticket != SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID))
        return false;
    bank->command_transfer_ticket = command_ticket;
    bank->command_transfer_obligation = SM64_SATURN_VDP1_TRANSFER_PENDING;
    bank->gouraud_transfer_ticket = gouraud_ticket;
    bank->gouraud_transfer_obligation = bank->gouraud_count > 0U
        ? SM64_SATURN_VDP1_TRANSFER_PENDING : SM64_SATURN_VDP1_TRANSFER_NOOP;
    bank->state = SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING;
    return true;
}

bool sm64_saturn_vdp1_frame_bank_record_transfers_retired(
    sm64_saturn_vdp1_frame_bank_t *bank, uint32_t command_ticket,
    uint32_t gouraud_ticket)
{
    if (bank == NULL ||
        bank->state != SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING ||
        bank->command_transfer_obligation != SM64_SATURN_VDP1_TRANSFER_PENDING ||
        bank->command_transfer_ticket != command_ticket ||
        (bank->gouraud_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_PENDING &&
         bank->gouraud_transfer_ticket != gouraud_ticket) ||
        (bank->gouraud_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_NOOP &&
         gouraud_ticket != SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID))
        return false;
    bank->command_transfer_obligation = SM64_SATURN_VDP1_TRANSFER_RETIRED;
    if (bank->gouraud_transfer_obligation == SM64_SATURN_VDP1_TRANSFER_PENDING)
        bank->gouraud_transfer_obligation = SM64_SATURN_VDP1_TRANSFER_RETIRED;
    return true;
}

bool sm64_saturn_vdp1_frame_bank_record_synchronous_complete(
    sm64_saturn_vdp1_frame_bank_t *bank)
{
    if (bank == NULL || bank->state != SM64_SATURN_VDP1_FRAME_BANK_READY)
        return false;
    bank->command_transfer_ticket = SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID;
    bank->gouraud_transfer_ticket = SM64_SATURN_VDP1_FRAME_BANK_TICKET_INVALID;
    bank->command_transfer_obligation = SM64_SATURN_VDP1_TRANSFER_RETIRED;
    bank->gouraud_transfer_obligation = bank->gouraud_count > 0U
        ? SM64_SATURN_VDP1_TRANSFER_RETIRED : SM64_SATURN_VDP1_TRANSFER_NOOP;
    bank->state = SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING;
    return true;
}

static bool belongs_to_set(const sm64_saturn_vdp1_frame_bank_set_t *banks,
                           const sm64_saturn_vdp1_frame_bank_t *bank)
{
    return bank == &banks->banks[0] || bank == &banks->banks[1];
}

bool sm64_saturn_vdp1_frame_bank_publish(
    sm64_saturn_vdp1_frame_bank_set_t *banks,
    sm64_saturn_vdp1_frame_bank_t *bank)
{
    if (banks == NULL || bank == NULL || !belongs_to_set(banks, bank) ||
        bank->state != SM64_SATURN_VDP1_FRAME_BANK_TRANSFERRING ||
        bank->command_transfer_obligation != SM64_SATURN_VDP1_TRANSFER_RETIRED ||
        (bank->gouraud_transfer_obligation != SM64_SATURN_VDP1_TRANSFER_RETIRED &&
         bank->gouraud_transfer_obligation != SM64_SATURN_VDP1_TRANSFER_NOOP))
        return false;
    if (banks->published != NULL &&
        !generation_follows(bank->snapshot_generation,
                            banks->published->snapshot_generation)) {
        bank->state = SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED;
        return false;
    }
    bank->state = SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED;
    banks->published = bank;
    return true;
}

bool sm64_saturn_vdp1_frame_bank_retire(
    sm64_saturn_vdp1_frame_bank_set_t *banks, uint32_t generation)
{
    if (banks == NULL || generation == 0U)
        return false;
    for (uint32_t index = 0U; index < SM64_SATURN_VDP1_FRAME_BANK_COUNT;
         index++) {
        sm64_saturn_vdp1_frame_bank_t *const bank = &banks->banks[index];
        if (bank == banks->published ||
            bank->state != SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED ||
            bank->snapshot_generation != generation)
            continue;
        bank->state = SM64_SATURN_VDP1_FRAME_BANK_FREE;
        return true;
    }
    return false;
}

bool sm64_saturn_vdp1_frame_bank_quarantine(
    sm64_saturn_vdp1_frame_bank_t *bank)
{
    if (bank == NULL || bank->state == SM64_SATURN_VDP1_FRAME_BANK_FREE ||
        bank->state == SM64_SATURN_VDP1_FRAME_BANK_PUBLISHED ||
        bank->state == SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED)
        return false;
    bank->state = SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED;
    return true;
}
