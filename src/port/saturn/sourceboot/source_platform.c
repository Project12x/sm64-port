/* Minimal Yaul-hosted implementations for the libultra services reached by
 * the E2 source-game loop.  They are intentionally platform-only: rendering,
 * input and level progression stay in the inherited source files. */
#include <yaul.h>

#include <string.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <ultra64.h>

#include "macros.h"

u64 osClockRate = 26822400ULL;

void osCreateMesgQueue(OSMesgQueue *queue, OSMesg *messages, s32 count) {
    if (queue == NULL) return;
    queue->validCount = 0;
    queue->first = 0;
    queue->msgCount = count;
    queue->msg = messages;
}

void osSetEventMesg(UNUSED OSEvent event, UNUSED OSMesgQueue *queue,
                    UNUSED OSMesg message) {}
s32 osJamMesg(UNUSED OSMesgQueue *queue, UNUSED OSMesg message,
              UNUSED s32 flags) { return 0; }
s32 osSendMesg(UNUSED OSMesgQueue *queue, UNUSED OSMesg message,
               UNUSED s32 flags) { return 0; }
s32 osRecvMesg(UNUSED OSMesgQueue *queue, UNUSED OSMesg *message,
               UNUSED s32 flags) { return 0; }

uintptr_t osVirtualToPhysical(void *address) { return (uintptr_t)address; }
void osWritebackDCache(UNUSED void *address, UNUSED size_t bytes) {}
void osWritebackDCacheAll(void) {}
void osInvalDCache(UNUSED void *address, UNUSED size_t bytes) {}
void osInvalICache(UNUSED void *address, UNUSED size_t bytes) {}

OSTime osGetTime(void) {
    /* Sourceboot's libultra clock is master-only synthetic state. Keep it
     * with the other NOLOAD CPU-only sourceboot accounting rather than
     * spending fixed HWRAM required by VDP1/libyaul. */
    static OSTime ticks __attribute__((section(".lwram_bss"), used));
    return ++ticks;
}
u32 osGetCount(void) { return (u32)cpu_frt_count_get(); }

void osViBlack(UNUSED u8 active) {}
void osViSetSpecialFeatures(UNUSED u32 features) {}
void osViSwapBuffer(UNUSED void *framebuffer) {}
void osViSetEvent(UNUSED OSMesgQueue *queue, UNUSED OSMesg message,
                  UNUSED u32 retraceCount) {}
void osCreateViManager(UNUSED OSPri priority) {}
void osViSetMode(UNUSED OSViMode *mode) {}

s32 osEepromProbe(UNUSED OSMesgQueue *queue) { return 0; }
s32 osEepromRead(UNUSED OSMesgQueue *queue, UNUSED u8 address,
                 UNUSED u8 *buffer) { return -1; }
s32 osEepromWrite(UNUSED OSMesgQueue *queue, UNUSED u8 address,
                  UNUSED u8 *buffer) { return -1; }
s32 osEepromLongRead(UNUSED OSMesgQueue *queue, UNUSED u8 address,
                     UNUSED u8 *buffer, UNUSED int bytes) { return -1; }
s32 osEepromLongWrite(UNUSED OSMesgQueue *queue, UNUSED u8 address,
                      UNUSED u8 *buffer, UNUSED int bytes) { return -1; }
