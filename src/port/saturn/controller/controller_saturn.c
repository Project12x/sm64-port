/* Yaul-backed implementation of SM64's existing ControllerAPI boundary.
 *
 * The button/API shape follows this repository's PC controller backends.
 * Saturn hardware access uses the pinned Yaul dependency directly. The
 * malucard/sm64-psx controller was inspected at 3073845 as behavior-only
 * evidence that a console backend should terminate at OSContPad.
 */
#include <yaul.h>
#include <string.h>

#include "controller_saturn.h"

static int8_t axis_to_n64(uint8_t raw, bool invert) {
    int16_t centered = (int16_t)raw - 127;
    if (invert) centered = -centered;
    int16_t value = (centered * 80) / 128;
    if (value < -80) value = -80;
    if (value > 80) value = 80;
    return (int8_t)value;
}

static uint16_t buttons_to_n64(const smpc_peripheral_digital_t *digital) {
    uint16_t buttons = 0;
    if (digital->pressed.button.a) buttons |= A_BUTTON;
    if (digital->pressed.button.b) buttons |= B_BUTTON;
    if (digital->pressed.button.c) buttons |= Z_TRIG;
    if (digital->pressed.button.start) buttons |= START_BUTTON;
    if (digital->pressed.button.l) buttons |= L_TRIG;
    if (digital->pressed.button.r) buttons |= R_TRIG;
    if (digital->pressed.button.x) buttons |= L_CBUTTONS;
    if (digital->pressed.button.y) buttons |= D_CBUTTONS;
    if (digital->pressed.button.z) buttons |= U_CBUTTONS;
    return buttons;
}

static void controller_saturn_init(void) {
    /* The target owns SMPC initialization and the VBlank INTBACK cadence. */
}

static void controller_saturn_read(OSContPad *pad) {
    smpc_peripheral_digital_t digital;
    smpc_peripheral_analog_t analog;
    (void)memset(&digital, 0, sizeof(digital));
    (void)memset(&analog, 0, sizeof(analog));

    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
    if (!digital.connected) {
        pad->button = 0;
        pad->stick_x = 0;
        pad->stick_y = 0;
        pad->errnum = CONT_NO_RESPONSE_ERROR;
        return;
    }

    pad->button = buttons_to_n64(&digital);
    pad->stick_x = 0;
    pad->stick_y = 0;
    pad->errnum = 0;

    smpc_peripheral_analog_port(1, &analog);
    if (analog.connected && analog.size >= 6) {
        pad->stick_x = axis_to_n64(analog.pressed.button.axis.x_axis, false);
        pad->stick_y = axis_to_n64(analog.pressed.button.axis.y_axis, true);
    }

    /* A digital Saturn pad must still drive SM64's analog movement path. */
    if (digital.pressed.button.left) pad->stick_x = -80;
    if (digital.pressed.button.right) pad->stick_x = 80;
    if (digital.pressed.button.down) pad->stick_y = -80;
    if (digital.pressed.button.up) pad->stick_y = 80;
}

struct ControllerAPI controller_saturn = {
    .init = controller_saturn_init,
    .read = controller_saturn_read,
};
