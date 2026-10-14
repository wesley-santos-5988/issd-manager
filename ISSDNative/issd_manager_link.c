#include "issd_manager_link.h"
#include <stdio.h>
#include <string.h>

/* Pressing A every `PULSE_PERIOD` frames for `PULSE_LENGTH` frames walks the
 * Open Game screens at the speed the original menus can take. */
#define PULSE_PERIOD 90u
#define PULSE_LENGTH 8u
/* Give up on a match nobody started. */
#define GIVE_UP_FRAMES (60u * 60u * 10u)

typedef enum { IDLE, WALKING, SET, LIVE, DONE } Phase;

static struct {
    Phase    phase;
    int      user_team, opp_team;
    uint32_t frames;           /* since the match was armed */
    int      user_goals, opp_goals;
    bool     result_ready;
    bool     seen_menu;        /* the main menu has been reached since arming */
} L;

static unsigned word(const uint8_t *ram, unsigned a) { return ram[a] | (unsigned)ram[a + 1] << 8; }

static bool at_main_menu(const uint8_t *ram) {
    return word(ram, 0x32) == 6 && word(ram, 0x70) == 12 &&
           word(ram, 0x1538) == 0x9d72 && word(ram, 0x153a) == 0xa4;
}
static bool at_setup(const uint8_t *ram) {
    return word(ram, 0x32) == 6 && word(ram, 0x70) == 15 && word(ram, 0x72) == 0 &&
           word(ram, 0x1648) == 0;
}
static bool live(const uint8_t *ram) { return word(ram, 0x32) == 6 && word(ram, 0x70) == 8; }

void issd_link_arm(int user_team, int opp_team) {
    memset(&L, 0, sizeof L);
    L.phase = WALKING;
    L.user_team = user_team;
    L.opp_team = opp_team;
}
void issd_link_cancel(void) { memset(&L, 0, sizeof L); }
bool issd_link_armed(void) { return L.phase != IDLE && L.phase != DONE; }

bool issd_link_take_result(int *user_goals, int *opp_goals) {
    if (!L.result_ready) return false;
    if (user_goals) *user_goals = L.user_goals;
    if (opp_goals) *opp_goals = L.opp_goals;
    issd_link_cancel();
    return true;
}

const char *issd_link_status(void) {
    switch (L.phase) {
    case WALKING: return "Starting the match...";
    case SET:     return "Teams set";
    case LIVE:    return "Match in progress";
    default:      return "";
    }
}

bool issd_link_before_frame(uint8_t *ram, uint16_t pad[4]) {
    if (!ram) return false;

    /* Scenario: confirm (A or Start) with the cursor on its button. */
    if (L.phase == IDLE || L.phase == DONE) {
        if (at_main_menu(ram) && ram[0x4e] == 4 &&
            (pad[0] & (ISSD_LINK_PAD_A | ISSD_LINK_PAD_START))) {
            return true;
        }
        return false;
    }

    L.frames++;
    if (L.frames > GIVE_UP_FRAMES && L.phase == WALKING) { issd_link_cancel(); return false; }
    if (L.phase != WALKING && L.phase != SET) return false;

    /* Walk the menus: to Open Game, then confirm through team and option
     * screens until the pre-match setup. */
    if (word(ram, 0x32) != 6 || live(ram)) return false;
    if (at_setup(ram) && L.phase == WALKING) return false;
    if (at_main_menu(ram)) {
        /* The cursor is read by the menu loop every frame. */
        ram[0x4e] = 0;
        L.seen_menu = true;
    }
    if (L.phase == WALKING && !L.seen_menu) return false;   /* still on the way back to the menu */
    if (L.frames % PULSE_PERIOD < PULSE_LENGTH) pad[0] |= ISSD_LINK_PAD_A;
    return false;
}

void issd_link_after_frame(uint8_t *ram) {
    if (!ram || !issd_link_armed()) return;

    if (L.phase == WALKING && at_setup(ram)) {
        ram[0x0da0] = (uint8_t)(L.user_team * 2);
        ram[0x0ea0] = (uint8_t)(L.opp_team * 2);
        fprintf(stderr, "[Manager] setup: side 1 team %d, side 2 team %d\n", L.user_team, L.opp_team);
        L.phase = SET;
        return;
    }
    if (L.phase == SET && live(ram)) { L.phase = LIVE; return; }

    if (L.phase == LIVE) {
        bool full_time = word(ram, 0x32) == 6 && word(ram, 0x70) == 18 &&
                         ram[0x16d0] == 0 && ram[0x16d1] == 0;
        if (full_time) {
            L.user_goals = ram[0x0da2];
            L.opp_goals = ram[0x0ea2];
            L.result_ready = true;
            L.phase = DONE;
        } else if (word(ram, 0x32) != 6) {
            issd_link_cancel();       /* left to the title or another mode */
        }
    }
}
