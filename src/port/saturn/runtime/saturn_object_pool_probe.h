#ifndef SM64_SATURN_OBJECT_POOL_PROBE_H
#define SM64_SATURN_OBJECT_POOL_PROBE_H

#include <stdint.h>

/*
 * Target-visible occupancy probe for gObjectPool (src/game/
 * object_list_processor.c, OBJECT_POOL_CAPACITY slots at 608 B/slot. The
 * portable 240-slot fallback consumes 145,920 B of always-resident HWRAM
 * .bss; a sealed sourceboot override may select a smaller pool.
 *
 * The port's snapshot domain already attests at most 64 live RENDERED
 * actors (SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE), but pool occupancy is not
 * the same number: the pool also holds invisible logic objects (spawners,
 * triggers, Mario/camera helpers) and transient particles. This probe
 * measures the real occupancy so a capacity cut (memory-residency
 * campaign Task 4) is sized from a measured peak, never a borrowed one.
 *
 * Always compiled under TARGET_SATURN -- 20 bytes, not worth a feature
 * flag. Counters are updated at the real allocate/free sites in
 * src/game/spawn_object.c (try_allocate_object() / deallocate_object() /
 * allocate_object()'s true pool-exhaustion path), and frames_sampled is
 * bumped once per game-loop tick from src/port/saturn/sourceboot/main.c's
 * sourceboot_run_source_tick(), the same per-tick site
 * sourceboot_boot_trace publishes its own SOURCE_TICK boundary from.
 *
 * Deliberately non-static/volatile: headless Ymir resolves this symbol
 * from the ELF (sh-elf-nm) and reads it out of live target RAM, the same
 * pattern as g_sm64_saturn_source_cart_probe (source_cart.h) and
 * sourceboot_boot_trace (sourceboot/main.c).
 */
#define SM64_SATURN_OBJECT_POOL_PROBE_MAGIC 0x4F504F4Cu /* 'OPOL' */

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t current_allocated;
    volatile uint32_t peak_allocated;
    volatile uint32_t alloc_failures;
    volatile uint32_t frames_sampled;
} sm64_saturn_object_pool_probe_t;

extern volatile sm64_saturn_object_pool_probe_t g_sm64_saturn_object_pool_probe;

#endif
