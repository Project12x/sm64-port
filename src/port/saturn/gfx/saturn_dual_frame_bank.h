/*
 * Dual-SH2 transform-bank publication.
 *
 * This is a project-owned adaptation of the cache-through handoff pattern
 * documented for SlaveDriver Engine WALLS.C:1240-1408, revision
 * a8986591557b6e680550d3c23970284d3b38ff8f (GPL-3.0-or-later).  It uses
 * no upstream code.  Producers retain the normal cached LWRAM pointer while
 * writing their disjoint range.  A consumer reads a peer range only through
 * the 0x2xxxxxxx cache-through alias after the uncached publication fence.
 */
#ifndef SM64_SATURN_DUAL_FRAME_BANK_H
#define SM64_SATURN_DUAL_FRAME_BANK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__sh__)
#include <cpu/cache.h>
#include <yaul/scu/map.h>
#endif

typedef struct sm64_saturn_dual_frame_lane {
    volatile uint32_t sequence;
    volatile uint16_t count;
    /* This is deliberately last: it is the uncached release word. */
    volatile uint16_t ready;
} sm64_saturn_dual_frame_lane_t;

typedef struct sm64_saturn_dual_frame_bank {
    sm64_saturn_dual_frame_lane_t lane[2];
} sm64_saturn_dual_frame_bank_t;

static inline void sm64_saturn_dual_frame_compiler_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static inline void sm64_saturn_dual_frame_reset(
    sm64_saturn_dual_frame_bank_t *bank)
{
    if (bank == NULL) return;
    for (uint8_t lane = 0U; lane < 2U; lane++) {
        bank->lane[lane].ready = 0U;
        bank->lane[lane].count = 0U;
        bank->lane[lane].sequence = 0U;
    }
    sm64_saturn_dual_frame_compiler_fence();
}

/* Bulk range writes precede this call.  The ready word is written last so a
 * peer that observes it has a complete sequence/count header to validate. */
static inline void sm64_saturn_dual_frame_publish(
    sm64_saturn_dual_frame_bank_t *bank, uint8_t lane, uint32_t sequence,
    uint16_t count)
{
    if (bank == NULL || lane >= 2U || sequence == 0U) return;
    sm64_saturn_dual_frame_compiler_fence();
    bank->lane[lane].sequence = sequence;
    bank->lane[lane].count = count;
    sm64_saturn_dual_frame_compiler_fence();
    bank->lane[lane].ready = 1U;
}

/* Read the peer's uncached release record.  The second fence prevents later
 * cache-through bulk reads from moving above the accepted release. */
static inline bool sm64_saturn_dual_frame_peer_ready(
    const sm64_saturn_dual_frame_bank_t *bank, uint8_t lane,
    uint32_t expected_sequence, uint16_t *count)
{
    if (count != NULL) *count = 0U;
    if (bank == NULL || lane >= 2U || expected_sequence == 0U) return false;
    const sm64_saturn_dual_frame_lane_t *const peer =
        &bank->lane[lane ^ 1U];
    if (peer->ready == 0U) return false;
    sm64_saturn_dual_frame_compiler_fence();
    if (peer->sequence != expected_sequence) return false;
    const uint16_t published_count = peer->count;
    sm64_saturn_dual_frame_compiler_fence();
    if (count != NULL) *count = published_count;
    return true;
}

/* This preserves the cached pointer for an owning CPU and selects the
 * physical 0x2xxxxxxx LWRAM mirror for a peer CPU.  On the host both aliases
 * intentionally collapse to the input pointer, making the protocol testable
 * without pretending that the host has SH-2 caches. */
static inline const void *sm64_saturn_dual_frame_cache_through(
    const void *cached)
{
    if (cached == NULL) return NULL;
#if defined(__sh__)
    const uintptr_t physical =
        ((uintptr_t)cached & ~((uintptr_t)CPU_ADDRESS_PARTITION_MASK)) -
        LWRAM(0);
    return (const void *)LWRAM_UNCACHED(physical);
#else
    return cached;
#endif
}

static inline const void *sm64_saturn_dual_frame_read_range(
    uint8_t reader_lane, uint8_t owner_lane, const void *cached)
{
    return reader_lane == owner_lane ? cached :
        sm64_saturn_dual_frame_cache_through(cached);
}

/* Select the owner of a fixed half-open result span.  A recovered all-master
 * span sets slave_begin == count, making every entry cached/master-owned. */
static inline uint8_t sm64_saturn_dual_frame_owner_for_split(
    uint16_t entry, uint16_t slave_begin)
{
#if defined(SM64_SATURN_TEST_MUTATE_SPLIT_OWNER)
    (void)entry;
    (void)slave_begin;
    return 1U;
#else
    return entry < slave_begin ? 0U : 1U;
#endif
}

#endif
