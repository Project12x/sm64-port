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
    /* Saturn analog reports can dither a few counts around centre.  SM64's
     * original 8-count deadzone is too small after the SMPC conversion and
     * causes an idle Mario to drift.  Keep the source ControllerAPI intact,
     * but quantize only the hardware boundary before it reaches it. */
    if (value > -12 && value < 12) value = 0;
    if (value < -80) value = -80;
    if (value > 80) value = 80;
    return (int8_t)value;
}

static uint16_t buttons_to_n64(const smpc_peripheral_digital_t *digital) {
    uint16_t buttons = 0;
    const uint16_t raw = digital->pressed.raw;
    /* libyaul's SMPC decoder already XORs the raw hardware bytes with 0xFF
     * before publishing `pressed.raw`; its public examples therefore test
     * direction bits as active-high.  Keep this boundary active-high so a
     * neutral report remains zero instead of becoming every N64 button. */
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
    /* Some Ymir/SMPC handoff frames expose a valid raw report one frame
     * before the library flips `connected`. Do not throw that report away;
     * only return no-response when both the connection flag and raw report
     * are empty. */
    if (!digital.connected && digital.pressed.raw == 0U) {
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
    /* A digital pad can still be returned through Yaul's analog-shaped
     * accessor during SMPC handoff. Only ID_ANALOG carries valid axis bytes;
     * accepting any connected record caused the persistent +80,+80 idle drift
     * seen in Ymir. */
    if (analog.connected && analog.type == ID_ANALOG && analog.size >= 6) {
        pad->stick_x = axis_to_n64(analog.pressed.button.axis.x_axis, false);
        pad->stick_y = axis_to_n64(analog.pressed.button.axis.y_axis, true);
    }

    /* A digital Saturn pad must still drive SM64's analog movement path. */
    /* Direction bits use the same active-high public report convention. */
    if (digital.pressed.raw & PERIPHERAL_DIGITAL_LEFT) pad->stick_x = -80;
    if (digital.pressed.raw & PERIPHERAL_DIGITAL_RIGHT) pad->stick_x = 80;
    if (digital.pressed.raw & PERIPHERAL_DIGITAL_DOWN) pad->stick_y = -80;
    if (digital.pressed.raw & PERIPHERAL_DIGITAL_UP) pad->stick_y = 80;
}

struct ControllerAPI controller_saturn = {
    .init = controller_saturn_init,
    .read = controller_saturn_read,
};
