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

#endif
