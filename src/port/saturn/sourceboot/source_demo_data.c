/* The direct-Bob E2 bootstrap never selects a title-screen demo.  Preserve
 * the source DmaHandlerList ABI with an empty table until the normal menu
 * package is part of the Saturn level-script closure. */
#include "game/memory.h"
#include <PR/os_cont.h>

#include "saturn_input_replay.h"

struct SaturnSourceDemoInputs {
    u32 numEntries;
    const void *addrPlaceholder;
    struct OffsetSizePair entries[1];
};

const struct SaturnSourceDemoInputs gDemoInputs = {
    0U,
    NULL,
    { { 0U, 0U } },
};

/* Test-only BOB parity route.  This feeds OSContPad samples through the
 * existing Saturn runtime seam; it does not alter source game state.  Keep it
 * in lockstep with tools/saturn/routes/bob_parity_v1.json. */
static const sm64_saturn_input_replay_sample_t sBobParityV1[] = {
    { 120U,   0,   0,          0U }, /* neutral startup */
    { 120U,  64,   0,          0U }, /* move */
    {  24U,  64,   0,    A_BUTTON }, /* running jump */
    {  96U,  64,  20,          0U }, /* arc */
    {  72U,   0,   0, L_CBUTTONS }, /* normal camera input */
    {  72U, -48,  48,          0U }, /* return leg */
    {  48U, -48,  48,    A_BUTTON }, /* second jump */
    {  48U,   0, -64, R_CBUTTONS }, /* final camera variation */
    { 200U,   0,   0,          0U }, /* settle before extended sweep */
    { 200U,  48,  48,          0U }, /* forward-right traverse */
    {  24U,  48,  48,    A_BUTTON }, /* traverse jump */
    { 176U,  48,  48,          0U }, /* settle traverse */
    {  96U,   0,   0, R_CBUTTONS }, /* camera-right sweep */
    { 200U, -64,   0,          0U }, /* return traverse */
    {  24U, -64,   0,    A_BUTTON }, /* return jump */
    { 180U, -64, -32,          0U }, /* return arc */
    { 100U,   0,   0, L_CBUTTONS }, /* camera-left sweep */
    { 200U,   0,  64,          0U }, /* final forward traverse */
};

const sm64_saturn_input_replay_sample_t *
sm64_saturn_sourceboot_bob_parity_v1(uint16_t *sample_count)
{
    if (sample_count != NULL)
        *sample_count = (uint16_t)(sizeof(sBobParityV1) / sizeof(sBobParityV1[0]));
    return sBobParityV1;
}
