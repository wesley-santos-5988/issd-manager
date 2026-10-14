#ifndef ISSD_MANAGER_MENU_H
#define ISSD_MANAGER_MENU_H

/* The Manager Mode page of the native overlay: a league hub, a team picker, the
 * table, the calendar, the squad, swaps with other clubs and free agents.
 * The league logic is in issd_manager.h; this file is only screens.
 *
 * The page needs to know where the cartridge image, the mods folder and the
 * career file are. The host hands those over once at start-up, so the page
 * links against no engine code and can be driven by a test. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    /* The cartridge as dumped, with no pack applied: new leagues start from it. */
    const uint8_t *(*base_rom)(size_t *size);
    /* The cartridge the game is running, with the enabled packs applied. */
    const uint8_t *(*applied_rom)(size_t *size);
    /* Folder the roster packs are scanned from at boot. */
    const char *(*mods_dir)(void);
    /* Name of a stock team, 0-35. */
    const char *(*team_name)(int team);
    /* Career file path, as issd_save_career_path. */
    bool (*career_path)(char *out, size_t cap, bool create);
    /* Bring the game to its main menu (or confirm it is there). Optional. */
    bool (*to_main_menu)(void);
} IssdManagerHost;

void issd_manager_menu_set_host(const IssdManagerHost *host);
/* 0 (the default) draws seeds from the clock; a test sets a fixed one. */
void issd_manager_menu_set_seed(uint32_t seed);

void issd_manager_menu_open(void);
void issd_manager_menu_step(int direction);
void issd_manager_menu_adjust(int direction);
void issd_manager_menu_confirm(void);
void issd_manager_menu_cancel(void);
void issd_manager_menu_click(int px, int py, int x, int y);
void issd_manager_menu_render(uint32_t *fb, int width, int height, int x, int y,
    void (*text)(uint32_t *, int, int, int, int, const char *, uint32_t));

/* Called every frame by the host: if the match the page started has reached
 * the final whistle, records the score in the league and saves. Returns true
 * when a result was recorded. */
bool issd_manager_menu_collect_match(void);

/* Diagnostics: the same as choosing "Play my match" in the hub, loading the
 * saved career first when none is open. Returns false when nothing started. */
bool issd_manager_menu_play_now(void);

/* Short state for the main menu row, e.g. "R5/22" or "no career". */
void issd_manager_menu_summary(char *out, size_t cap);

#endif
