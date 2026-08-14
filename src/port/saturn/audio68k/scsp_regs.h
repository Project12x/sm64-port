#ifndef SM64_SATURN_AUDIO68K_SCSP_REGS_H
#define SM64_SATURN_AUDIO68K_SCSP_REGS_H

/* Reserved for the next PCM-voice increment. The heartbeat image must not
 * touch SCSP slots or mixer state. Addresses are in the 68K sound-CPU view. */
enum {
    SM64_SATURN_SCSP_SLOT_BASE = 0x00100000U,
    SM64_SATURN_SCSP_SLOT_BYTES = 0x20U,
};

#endif
