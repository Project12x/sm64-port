/* Project-owned, bounded Saturn sound-CPU cold-boot/recovery contract. */
#ifndef SM64_SATURN_SOUND_CPU_H
#define SM64_SATURN_SOUND_CPU_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SM64_SATURN_SOUND_CPU_COMMAND_ON = 0x06U,
    SM64_SATURN_SOUND_CPU_COMMAND_OFF = 0x07U,
};

typedef enum sm64_saturn_sound_cpu_boot_kind {
    SM64_SATURN_SOUND_CPU_BOOT_COLD = 1,
    SM64_SATURN_SOUND_CPU_BOOT_RECOVERY = 2,
} sm64_saturn_sound_cpu_boot_kind_t;

typedef enum sm64_saturn_sound_cpu_phase {
    SM64_SATURN_SOUND_CPU_PHASE_MUTED = 0,
    SM64_SATURN_SOUND_CPU_PHASE_STAGED,
    SM64_SATURN_SOUND_CPU_PHASE_STOPPING,
    SM64_SATURN_SOUND_CPU_PHASE_STOPPED,
    SM64_SATURN_SOUND_CPU_PHASE_STARTING,
    SM64_SATURN_SOUND_CPU_PHASE_READY,
    SM64_SATURN_SOUND_CPU_PHASE_FAULT,
} sm64_saturn_sound_cpu_phase_t;

typedef enum sm64_saturn_sound_cpu_fault {
    SM64_SATURN_SOUND_CPU_FAULT_NONE = 0,
    SM64_SATURN_SOUND_CPU_FAULT_BAD_CONFIG,
    SM64_SATURN_SOUND_CPU_FAULT_STAGE,
    SM64_SATURN_SOUND_CPU_FAULT_512K_MODE,
    SM64_SATURN_SOUND_CPU_FAULT_SOUND_OFF,
    SM64_SATURN_SOUND_CPU_FAULT_STOP_TIMEOUT,
    SM64_SATURN_SOUND_CPU_FAULT_CLEAR,
    SM64_SATURN_SOUND_CPU_FAULT_COPY,
    SM64_SATURN_SOUND_CPU_FAULT_MAILBOX,
    SM64_SATURN_SOUND_CPU_FAULT_SOUND_ON,
    SM64_SATURN_SOUND_CPU_FAULT_READY_TIMEOUT,
} sm64_saturn_sound_cpu_fault_t;

typedef enum sm64_saturn_sound_cpu_command_result {
    /* The generic Yaul call returned after its synchronous SF wait. OREG31 is
     * telemetry, not interpreted as a boolean success byte. */
    SM64_SATURN_SOUND_CPU_COMMAND_COMPLETED = 0,
    SM64_SATURN_SOUND_CPU_COMMAND_REJECTED = 1,
} sm64_saturn_sound_cpu_command_result_t;

typedef struct sm64_saturn_sound_cpu_yaul_result {
    uint16_t completed_count;
    uint8_t last_command;
    uint8_t last_oreg31;
} sm64_saturn_sound_cpu_yaul_result_t;

typedef struct sm64_saturn_sound_cpu_state {
    uint32_t boot_attempts;
    uint32_t fault_count;
    uint16_t stop_polls;
    uint16_t ready_polls;
    uint16_t initial_heartbeat;
    uint16_t ready_heartbeat;
    uint8_t phase;
    uint8_t fault;
    uint8_t boot_kind;
    uint8_t reserved;
} sm64_saturn_sound_cpu_state_t;

typedef struct sm64_saturn_sound_cpu_boot {
    sm64_saturn_sound_cpu_boot_kind_t kind;
    volatile uint8_t *sound_ram;
    uint16_t stop_wait_budget;
    uint16_t ready_wait_budget;
    void *context;
    bool (*stage_ready)(void *context);
    bool (*set_512k_mode)(void *context);
    sm64_saturn_sound_cpu_command_result_t (*generic_smpc_command)(
        void *context, uint8_t command);
    bool (*sound_cpu_stopped)(void *context);
    bool (*clear_owned_regions)(void *context);
    bool (*copy_staged_regions)(void *context);
    bool (*initialize_mailbox)(void *context);
    void (*publish_barrier)(void *context);
    void (*wait_stop_tick)(void *context);
    void (*wait_ready_tick)(void *context);
} sm64_saturn_sound_cpu_boot_t;

bool sm64_saturn_sound_cpu_boot(
    const sm64_saturn_sound_cpu_boot_t *boot,
    sm64_saturn_sound_cpu_state_t *state);

/* Target adapters remain inside this module so no production caller reaches
 * Yaul's warned SNDON/SNDOFF convenience functions. */
sm64_saturn_sound_cpu_command_result_t
sm64_saturn_sound_cpu_yaul_command(void *context, uint8_t command);
bool sm64_saturn_sound_cpu_yaul_set_512k(void *context);

#endif
