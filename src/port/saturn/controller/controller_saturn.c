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
    const uint16_t raw = digital->pressed.raw;
    if (raw & PERIPHERAL_DIGITAL_A) buttons |= A_BUTTON;
    if (raw & PERIPHERAL_DIGITAL_B) buttons |= B_BUTTON;
    if (raw & PERIPHERAL_DIGITAL_C) buttons |= Z_TRIG;
    if (raw & PERIPHERAL_DIGITAL_START) buttons |= START_BUTTON;
    if (raw & PERIPHERAL_DIGITAL_L) buttons |= L_TRIG;
    if (raw & PERIPHERAL_DIGITAL_R) buttons |= R_TRIG;
    if (raw & PERIPHERAL_DIGITAL_X) buttons |= L_CBUTTONS;
    if (raw & PERIPHERAL_DIGITAL_Y) buttons |= D_CBUTTONS;
    if (raw & PERIPHERAL_DIGITAL_Z) buttons |= U_CBUTTONS;
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
    /* Use Yaul's documented active-high raw masks here.  Reading the packed
     * bit-fields directly is compiler-layout dependent on SH-2 and can make
     * a held Saturn direction disappear while the rest of the pad appears
     * connected. */
    if ((digital.pressed.raw & PERIPHERAL_DIGITAL_LEFT) != 0U) pad->stick_x = -80;
    if ((digital.pressed.raw & PERIPHERAL_DIGITAL_RIGHT) != 0U) pad->stick_x = 80;
    if ((digital.pressed.raw & PERIPHERAL_DIGITAL_DOWN) != 0U) pad->stick_y = -80;
    if ((digital.pressed.raw & PERIPHERAL_DIGITAL_UP) != 0U) pad->stick_y = 80;
}

struct ControllerAPI controller_saturn = {
    .init = controller_saturn_init,
    .read = controller_saturn_read,
};
