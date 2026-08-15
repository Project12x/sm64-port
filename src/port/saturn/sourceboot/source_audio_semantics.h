#ifndef SM64_SATURN_SOURCE_AUDIO_SEMANTICS_H
#define SM64_SATURN_SOURCE_AUDIO_SEMANTICS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The semantic policy is sourceboot game state, not a hardware mailbox.
 * On the SH-2 it therefore lives in an explicitly owned main-pool block:
 * it is allocated before thread5 starts, reset with that pool, and never
 * consumes the fixed HWRAM renderer/transport floor.
 */
size_t sm64_saturn_source_audio_semantic_workspace_bytes(void);
bool sm64_saturn_source_audio_semantic_workspace_bind(void *workspace,
                                                       size_t workspace_bytes);
/* Parks the module unbound (fail-closed no-ops) and zeroes the public ABI
 * globals without trusting current static contents; .lwram_bss is NOLOAD,
 * so this must run before the first bind attempt on the target. */
void sm64_saturn_source_audio_semantics_reset(void);

#endif
