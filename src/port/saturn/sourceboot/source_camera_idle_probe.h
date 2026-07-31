#ifndef SM64_SATURN_SOURCEBOOT_CAMERA_IDLE_PROBE_H
#define SM64_SATURN_SOURCEBOOT_CAMERA_IDLE_PROBE_H

#include <stdint.h>

#include "saturn_camera_probe.h"
#include "saturn_source_runtime.h"

#define SOURCEBOOT_SCC1_HEADER_WORDS 24U
#define SOURCEBOOT_SCC1_SAMPLE_WORDS 81U
#define SOURCEBOOT_SCC1_SAMPLE_COUNT 600U
#define SOURCEBOOT_SCC1_TOTAL_WORDS \
    (SOURCEBOOT_SCC1_HEADER_WORDS + \
     SOURCEBOOT_SCC1_SAMPLE_WORDS * SOURCEBOOT_SCC1_SAMPLE_COUNT)

typedef struct sourceboot_camera_idle_capture {
    uint32_t words[SOURCEBOOT_SCC1_TOTAL_WORDS];
} sourceboot_camera_idle_capture_t;

_Static_assert(sizeof(sourceboot_camera_idle_capture_t) == 194496U,
               "SCC1 raw size drift");

extern sourceboot_camera_idle_capture_t sourceboot_camera_idle_capture;

void sm64_saturn_sourceboot_camera_idle_probe_reset(void);
void sm64_saturn_sourceboot_camera_idle_probe_record(
    const sm64_saturn_source_runtime_state_t *runtime, uint32_t source_tick);

#endif
