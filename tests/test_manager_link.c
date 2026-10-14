/* The bridge between Manager Mode and the cartridge, on a synthetic RAM. */
#include <stdio.h>
#include <string.h>

#include "issd_manager_link.h"

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static uint8_t ram[0x20000];

static void at_main_menu(int cursor) {
    memset(ram, 0, sizeof ram);
    ram[0x32] = 6; ram[0x70] = 12;
    ram[0x1538] = 0x72; ram[0x1539] = 0x9d; ram[0x153a] = 0xa4;
    ram[0x4e] = (uint8_t)cursor;
}
static void state(int mode, int sub, int step) { ram[0x32] = mode; ram[0x70] = sub; ram[0x72] = step; }

/* How many of `frames` frames pressed A. */
static int a_presses(int frames) {
    int n = 0;
    for (int i = 0; i < frames; ++i) {
        uint16_t pad[4] = {0, 0, 0, 0};
        issd_link_before_frame(ram, pad);
        if (pad[0] & ISSD_LINK_PAD_A) ++n;
    }
    return n;
}

int main(void) {
    uint16_t pad[4];

    /* --- the Scenario button ------------------------------------------- */
    at_main_menu(4);
    pad[0] = ISSD_LINK_PAD_A; pad[1] = pad[2] = pad[3] = 0;
    CHECK(issd_link_before_frame(ram, pad));
    pad[0] = ISSD_LINK_PAD_START;
    CHECK(issd_link_before_frame(ram, pad));
    pad[0] = 0x0001;                              /* another button */
    CHECK(!issd_link_before_frame(ram, pad));
    pad[0] = 0;
    CHECK(!issd_link_before_frame(ram, pad));     /* no press, no hand-over */
    for (int cursor = 0; cursor < 8; ++cursor) {
        if (cursor == 4) continue;
        at_main_menu(cursor);
        pad[0] = ISSD_LINK_PAD_A;
        CHECK(!issd_link_before_frame(ram, pad));
    }
    at_main_menu(4);
    ram[0x70] = 8;                                /* a live match, cursor RAM is scratch */
    pad[0] = ISSD_LINK_PAD_A;
    CHECK(!issd_link_before_frame(ram, pad));
    CHECK(!issd_link_before_frame(NULL, pad));

    /* --- nothing armed: no input of its own ----------------------------- */
    at_main_menu(0);
    CHECK(a_presses(300) == 0);
    CHECK(strcmp(issd_link_status(), "") == 0);

    /* --- armed: waits for the main menu, then walks it ----------------- */
    issd_link_arm(30, 2);
    CHECK(issd_link_armed());
    state(6, 5, 0);                                /* somewhere else first */
    CHECK(a_presses(300) == 0);
    at_main_menu(4);                               /* now at the menu, cursor on Scenario */
    pad[0] = ISSD_LINK_PAD_A;
    CHECK(!issd_link_before_frame(ram, pad));      /* armed: Scenario is not taken */
    CHECK(ram[0x4e] == 0);                         /* walks to Open Game */
    state(6, 3, 0);                                /* in the team screens */
    int walked = a_presses(300);
    CHECK(walked > 10 && walked < 100);            /* pulses, never held */
    state(6, 8, 0);                                /* never during live play */
    CHECK(a_presses(300) == 0);

    /* --- the setup boundary -------------------------------------------- */
    state(6, 15, 0);
    ram[0x1648] = 0x80;                            /* a campaign, not an exhibition */
    issd_link_after_frame(ram);
    CHECK(ram[0xda0] == 0 && ram[0xea0] == 0);
    ram[0x1648] = 0;
    state(6, 15, 3);                               /* mid-construction: too late */
    issd_link_after_frame(ram);
    CHECK(ram[0xda0] == 0 && ram[0xea0] == 0);
    state(6, 15, 0);
    issd_link_after_frame(ram);
    CHECK(ram[0xda0] == 60 && ram[0xea0] == 4);    /* twice the team ids: side 1 is the user */
    CHECK(strcmp(issd_link_status(), "Teams set") == 0);
    state(6, 1, 0);                                /* intro screens still need A */
    CHECK(a_presses(300) > 10);
    ram[0xda0] = 0; ram[0xea0] = 0;
    state(6, 15, 0);
    issd_link_after_frame(ram);                    /* written once only */
    CHECK(ram[0xda0] == 0);

    /* --- the match ------------------------------------------------------ */
    state(6, 8, 0);
    issd_link_after_frame(ram);
    CHECK(strcmp(issd_link_status(), "Match in progress") == 0);
    CHECK(a_presses(300) == 0);
    int user, other;
    CHECK(!issd_link_take_result(&user, &other));
    ram[0xda2] = 1; ram[0xea2] = 0;
    state(6, 9, 0);                                /* a goal, clock still running */
    ram[0x16d0] = 10; ram[0x16d1] = 4;
    issd_link_after_frame(ram);
    CHECK(!issd_link_take_result(&user, &other));
    state(6, 18, 0);                               /* the results screen mid-game: clock > 0 */
    issd_link_after_frame(ram);
    CHECK(!issd_link_take_result(&user, &other));
    ram[0xda2] = 2; ram[0xea2] = 5;
    ram[0x16d0] = 0; ram[0x16d1] = 0;
    issd_link_after_frame(ram);                    /* full time */
    CHECK(!issd_link_armed());
    CHECK(issd_link_take_result(&user, &other));
    CHECK(user == 2 && other == 5);
    CHECK(!issd_link_take_result(&user, &other));  /* only once */

    /* --- leaving the match, and starting over --------------------------- */
    issd_link_arm(1, 2);
    state(6, 15, 0);
    issd_link_after_frame(ram);
    state(6, 8, 0);
    issd_link_after_frame(ram);
    state(1, 0, 0);                                /* back to the title */
    issd_link_after_frame(ram);
    CHECK(!issd_link_armed());
    CHECK(!issd_link_take_result(&user, &other));

    issd_link_arm(1, 2);
    issd_link_arm(7, 9);                           /* the second request wins */
    state(6, 15, 0);
    ram[0x4e] = 0;
    at_main_menu(0);
    a_presses(1);
    state(6, 15, 0);
    issd_link_after_frame(ram);
    CHECK(ram[0xda0] == 14 && ram[0xea0] == 18);
    issd_link_cancel();
    CHECK(!issd_link_armed());

    /* --- a match nobody starts is given up ------------------------------ */
    issd_link_arm(1, 2);
    at_main_menu(0);
    for (int i = 0; i < 60 * 60 * 10 + 10; ++i) {
        uint16_t p[4] = {0, 0, 0, 0};
        issd_link_before_frame(ram, p);
        ram[0x32] = 9;                             /* never reaches the menu */
    }
    CHECK(!issd_link_armed());

    printf(failures ? "manager link: FAILED\n" : "manager link: all checks passed\n");
    return failures ? 1 : 0;
}
