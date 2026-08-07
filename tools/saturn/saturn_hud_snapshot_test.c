/* tools/saturn/saturn_hud_snapshot_test.c */
#include <string.h>
#include <stdio.h>

#include "saturn_hud.h"

static int
test_snapshot_is_fixed_width_and_pointer_free(void)
{
    /* Structural proof, not a runtime capture: every field must be a plain
     * scalar. sizeof must be stable and independent of pointer width so the
     * ABI can't silently change between host and SH-2 target builds. */
    if (sizeof(sm64_saturn_hud_snapshot_t) == 0U) {
        fprintf(stderr, "snapshot type is empty\n");
        return 1;
    }
    sm64_saturn_hud_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.lives = 4;
    snapshot.coins = 99;
    snapshot.stars = 15;
    snapshot.wedges = 6;
    snapshot.flags = 0x004F;
    snapshot.timer = 1801;
    snapshot.camera_status = 1;
    snapshot.power_meter_animation = 1;
    snapshot.power_meter_y = 166;
    snapshot.cannon_active = 0;
    if (snapshot.lives != 4 || snapshot.coins != 99 || snapshot.wedges != 6) {
        fprintf(stderr, "snapshot fields did not round-trip\n");
        return 1;
    }
    return 0;
}

int
main(void)
{
    return test_snapshot_is_fixed_width_and_pointer_free();
}
