#include <yaul.h>

#include "source_camera_acceptance_route.h"

static const sm64_saturn_input_replay_sample_t sBobDefaultCameraV1[] = {
    { 120U, 0, 0, 0U },
    {   1U, 0, 0, R_TRIG },
    { 1879U, 0, 0, 0U },
};

_Static_assert(120U + 1U + 1879U == 2000U,
               "camera acceptance route length drift");

const sm64_saturn_input_replay_sample_t *
sm64_saturn_sourceboot_bob_default_camera_v1(uint16_t *sample_count)
{
    if (sample_count != NULL)
        *sample_count = (uint16_t)(sizeof(sBobDefaultCameraV1) /
                                  sizeof(sBobDefaultCameraV1[0]));
    return sBobDefaultCameraV1;
}
