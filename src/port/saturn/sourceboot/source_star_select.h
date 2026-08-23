#ifndef SM64_SATURN_SOURCE_STAR_SELECT_H
#define SM64_SATURN_SOURCE_STAR_SELECT_H

#include <stdbool.h>
#include <stdint.h>

/* Runs the boot-time act picker and returns the chosen act, 1..6.
 * Falls back to act 1 if nothing is pressed before the timeout, matching the
 * default a fresh save file's star select offers. */
/* Paces itself off the caller's VBlank-out counter -- the selector runs before
 * the game loop exists, so it has no frame clock of its own. */
int sm64_saturn_star_select_run(const volatile uint32_t *vblank_counter);

/* True when every act label can be drawn by the US HUD alphabet and fits the
 * visible grid. Exists so a reworded label cannot silently render holes --
 * the font has no J, Q, V, X or Z. */
bool sm64_saturn_star_select_labels_are_drawable(void);

#endif /* SM64_SATURN_SOURCE_STAR_SELECT_H */
