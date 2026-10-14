#ifndef ISSD_MANAGER_LINK_H
#define ISSD_MANAGER_LINK_H

/* The bridge between Manager Mode and the running cartridge.
 *
 * It does four things, all by watching and writing the guest's work RAM:
 *   - the main menu's Scenario button hands over to Manager Mode,
 *   - "Play my match" walks the original Open Game menus by itself,
 *   - at the pre-match setup boundary it writes the two team codes,
 *   - when the final whistle goes it reads the score.
 *
 * Everything takes the RAM as an argument so it can be tested with a
 * synthetic image. The addresses were measured on the USA cartridge:
 *   $32/$70/$72   game mode, sub-mode, step ($32=6, $70=12 main menu loop;
 *                 $70=15 $72=0 pre-match setup; $70=8 live; $70=18 full time)
 *   $4E           main menu cursor (0 Open Game .. 4 Scenario .. 7 Options)
 *   $0DA0/$0EA0   team code of side 1 / side 2, twice the team id
 *   $0DA2/$0EA2   goals of side 1 / side 2
 *   $16D0/$16D1   clock seconds / minutes
 * Player 1 always controls side 1, so the user's club is written there. */

#include <stdbool.h>
#include <stdint.h>

/* Joypad bits as the runner hands them over (SNES JOY1 layout). */
#define ISSD_LINK_PAD_START 0x0008u
#define ISSD_LINK_PAD_A     0x0100u

/* Ask for a match: the user's club on side 1 against `opp_team`. Both are
 * cartridge team ids 0-35. Replaces anything pending. */
void issd_link_arm(int user_team, int opp_team);
void issd_link_cancel(void);
bool issd_link_armed(void);
/* The last armed match reached the final whistle; hands the score over once. */
bool issd_link_take_result(int *user_goals, int *opp_goals);

/* Called every frame before the guest runs. Returns true when the Scenario
 * button of the main menu was pressed: the caller must then swallow the
 * pads and open Manager Mode. While a match is armed it may press A in
 * pad 1 to walk the menus. */
bool issd_link_before_frame(uint8_t *ram, uint16_t pad[4]);

/* Called every frame after the guest ran, before match checkpoints are
 * taken, so a rematch replays the written teams. */
void issd_link_after_frame(uint8_t *ram);

/* Description of the state for the overlay: "" when nothing is pending. */
const char *issd_link_status(void);

#endif
