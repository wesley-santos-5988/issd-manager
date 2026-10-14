/* Drives the Manager Mode screens the way the overlay does: key presses in,
 * strings out. Runs on a real cartridge image because a new league reads the
 * 36 stock squads from it; no window, no engine. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "issd_config.h"
#include "issd_manager.h"
#include "issd_manager_link.h"
#include "issd_manager_menu.h"
#include "issd_menu.h"
#include "issd_mod.h"

uint8_t g_ram[0x20000];
IssdConfig g_issd_config;
IssdOverlayMenu g_overlay_menu;

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ------------------------------------------------ what the host provides */

static int returns, restarts, config_saves, closes, menu_requests;
static char last_note[128];
static const uint8_t *g_base, *g_applied;
static size_t g_size;
static char g_dir[900];

void issd_menu_notify(const char *message, int frames) {
    (void)frames;
    snprintf(last_note, sizeof last_note, "%s", message);
}
void issd_menu_return_from_manager(void) { ++returns; }
void issd_menu_close(void) { ++closes; }
void issd_restart_application(void) { ++restarts; }
bool issd_config_save(const IssdConfig *cfg, const char *path) {
    (void)cfg; (void)path; ++config_saves;
    return true;
}

static const uint8_t *host_base(size_t *size) { *size = g_size; return g_base; }
static const uint8_t *host_applied(size_t *size) { *size = g_size; return g_applied; }
static const char *host_mods(void) { return g_dir; }
static const char *host_team(int team) { return team >= 0 && team < 36 ? issd_mod_rom_team_name(team) : NULL; }
static bool host_to_main(void) { ++menu_requests; return true; }
static bool host_career(char *out, size_t cap, bool create) {
    (void)create;
    return snprintf(out, cap, "%s/career.mgr", g_dir) < (int)cap;
}

/* ---------------------------------------------------------- what is drawn */

#define BOX_X 8
#define BOX_Y 2
#define MAX_DRAWN 64
static struct { int x, y; char text[160]; } drawn[MAX_DRAWN];
static int drawn_count;

static void capture(uint32_t *fb, int w, int h, int x, int y, const char *text, uint32_t color) {
    (void)fb; (void)w; (void)h; (void)color;
    if (drawn_count < MAX_DRAWN) {
        drawn[drawn_count].x = x; drawn[drawn_count].y = y;
        snprintf(drawn[drawn_count].text, sizeof drawn[0].text, "%s", text);
        ++drawn_count;
    }
}

static void render(void) {
    drawn_count = 0;
    issd_manager_menu_render(NULL, 256, 224, BOX_X, BOX_Y, capture);
    /* Nothing may run off the 240 pixel box: eight pixels a character. */
    for (int i = 0; i < drawn_count; ++i)
        if (drawn[i].x + (int)strlen(drawn[i].text) * 8 > BOX_X + 240) {
            fprintf(stderr, "too wide at y=%d: '%s'\n", drawn[i].y, drawn[i].text);
            ++failures;
        }
}

static bool on_screen(const char *needle) {
    for (int i = 0; i < drawn_count; ++i)
        if (strstr(drawn[i].text, needle)) return true;
    return false;
}

static const char *title(void) { return drawn_count ? drawn[0].text : ""; }

/* The list row whose text holds `needle`, as the index a cursor would use. */
static int row_of(const char *needle) {
    for (int i = 0; i < drawn_count; ++i)
        if (drawn[i].x == BOX_X + 14 && strstr(drawn[i].text, needle) &&
            drawn[i].y >= BOX_Y + 22)
            return g_overlay_menu.scroll + (drawn[i].y - BOX_Y - 22) / 12;
    return -1;
}

/* ------------------------------------------------------------------ input */

static void down(int n) { for (int i = 0; i < n; ++i) issd_manager_menu_step(1); }
static void ok(void) { issd_manager_menu_confirm(); }
static void back(void) { issd_manager_menu_cancel(); }

/* Move the cursor onto the row holding `needle` and press it. */
static bool press(const char *needle) {
    render();
    int row = row_of(needle);
    if (row < 0) { fprintf(stderr, "no row '%s' on '%s'\n", needle, title()); ++failures; return false; }
    int rows_before = 0;
    while (g_overlay_menu.current_item != row && rows_before++ < 64) issd_manager_menu_step(1);
    ok();
    return true;
}

static void file_into(const char *name, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", g_dir, name);
}

static bool exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    size_t cap = 0x400000;
    uint8_t *rom = (uint8_t *)malloc(cap), *patched = (uint8_t *)malloc(cap);
    g_size = fread(rom, 1, cap, f);
    fclose(f);
    snprintf(g_dir, sizeof g_dir, "%s", argv[2]);
    g_base = g_applied = rom;

    const IssdManagerHost host = { host_base, host_applied, host_mods, host_team, host_career, host_to_main };
    issd_manager_menu_set_host(&host);
    issd_manager_menu_set_seed(4242);
    char note_career[1024], pack_path[1024];
    file_into("career.mgr", note_career, sizeof note_career);
    file_into("manager_career.json", pack_path, sizeof pack_path);
    remove(note_career);
    remove(pack_path);

    /* --- before any league ------------------------------------------- */
    char summary[32];
    issd_manager_menu_summary(summary, sizeof summary);
    CHECK(strcmp(summary, "no career") == 0);
    issd_manager_menu_open();
    CHECK(g_overlay_menu.page == ISSD_MENU_PAGE_MANAGER);
    render();
    CHECK(strcmp(title(), "MANAGER MODE") == 0);
    /* With no league the cursor rests on New league, not on a row that is off. */
    CHECK(g_overlay_menu.current_item == row_of("New league"));
    CHECK(on_screen("No league yet"));
    CHECK(on_screen("Simulate my match (---)"));
    CHECK(on_screen("Continue saved career (---)"));
    CHECK(on_screen("Play my match (---)"));
    press("Play my match");
    CHECK(strstr(last_note, "Start or continue") != NULL);
    CHECK(!issd_link_armed());
    press("Simulate my match");
    CHECK(strstr(last_note, "Start or continue") != NULL);
    press("Continue saved career");
    CHECK(strstr(last_note, "No saved career") != NULL);

    /* --- a new league -------------------------------------------------- */
    press("New league");
    render();
    CHECK(strcmp(title(), "PICK YOUR TEAM") == 0);
    CHECK(on_screen("Italy"));
    down(30);                                   /* Brazil is team 30 */
    render();
    CHECK(row_of("Brazil") == g_overlay_menu.current_item);
    CHECK(on_screen("31/36"));                  /* a long list says where the cursor is */
    ok();
    CHECK(strstr(last_note, "New league drawn") != NULL);
    render();
    CHECK(strcmp(title(), "MANAGER MODE") == 0);
    CHECK(!on_screen("1/11"));                  /* a short list has no counter */
    CHECK(on_screen("R1/22"));
    CHECK(exists(note_career));                 /* a new league is saved at once */
    issd_manager_menu_summary(summary, sizeof summary);
    CHECK(strcmp(summary, "R1/22") == 0);

    /* --- simulate three rounds ---------------------------------------- */
    for (int i = 0; i < 3; ++i) {
        press("Simulate my match");
        CHECK(strchr(last_note, '-') != NULL);  /* "R1: Brazil 2-1 Japan" */
    }
    issd_manager_menu_summary(summary, sizeof summary);
    CHECK(strcmp(summary, "R4/22") == 0);
    {
        IssdManager saved;
        CHECK(issd_mgr_load(&saved, note_career));
        CHECK(saved.round == 3);                /* every round was saved */
        CHECK(saved.team_id[saved.user_slot] == 30);
    }

    /* --- table, calendar and squad ------------------------------------ */
    press("League table");
    render();
    CHECK(strcmp(title(), "LEAGUE TABLE") == 0);
    CHECK(on_screen("Brazil"));
    CHECK(on_screen("#  Team"));
    back();
    render();
    CHECK(strcmp(title(), "MANAGER MODE") == 0);

    press("Match calendar");
    render();
    CHECK(strcmp(title(), "ROUND 4/22") == 0);
    issd_manager_menu_adjust(1);
    render();
    CHECK(strcmp(title(), "ROUND 5/22") == 0);
    issd_manager_menu_adjust(-1); issd_manager_menu_adjust(-1); issd_manager_menu_adjust(-1);
    issd_manager_menu_adjust(-1);               /* round 1 */
    render();
    CHECK(strcmp(title(), "ROUND 1/22") == 0);
    ok();                                       /* my season */
    render();
    CHECK(strcmp(title(), "MY SEASON") == 0);
    CHECK(on_screen("R01"));
    back();

    press("My squad");
    render();
    CHECK(strcmp(title(), "MY SQUAD") == 0);
    CHECK(on_screen("1/20"));
    CHECK(on_screen(" GK "));
    CHECK(on_screen(" DM "));                   /* the cartridge's in-between codes */
    back();

    /* --- a swap with a club, then backing out --------------------------- */
    press("Swap with a club");
    render();
    CHECK(strcmp(title(), "SWAP: PICK A CLUB") == 0);
    ok();                                       /* first club */
    render();
    CHECK(strstr(title(), "PLAYERS") != NULL);
    down(5); ok();                              /* one of their players */
    render();
    CHECK(strcmp(title(), "SWAP: YOUR PLAYER") == 0);
    down(5); ok();                              /* one of mine */
    render();
    CHECK(strcmp(title(), "THE OFFER") == 0);
    CHECK(on_screen("Get:") && on_screen("Give:"));
    CHECK(on_screen("Budget") && on_screen("Deals left 2"));
    CHECK(on_screen("Make the offer"));
    /* Clicking the Back choice, drawn below the offer's text, returns to the
     * player list. It sits five lines down, not at the top of the list. */
    issd_manager_menu_click(BOX_X + 100, BOX_Y + 22 + 6 * 12 + 4, BOX_X, BOX_Y);
    render();
    CHECK(strcmp(title(), "SWAP: YOUR PLAYER") == 0);
    back(); back();                             /* theirs, then the clubs */
    render();
    CHECK(strcmp(title(), "SWAP: PICK A CLUB") == 0);
    back();
    render();
    CHECK(strcmp(title(), "MANAGER MODE") == 0);

    /* --- signing a free agent ------------------------------------------ */
    press("Free agents");
    render();
    CHECK(strcmp(title(), "FREE AGENTS") == 0);
    down(5); ok();                              /* agent 5 is an outfield player */
    render();
    CHECK(strcmp(title(), "WHO LEAVES?") == 0);
    down(5); ok();
    CHECK(strcmp(last_note, "Transfer done") == 0);
    render();
    CHECK(strcmp(title(), "MANAGER MODE") == 0);
    CHECK(g_overlay_menu.current_item == row_of("Apply squads"));
    CHECK(on_screen("Restart to apply transfers"));
    {
        IssdManager saved;
        CHECK(issd_mgr_load(&saved, note_career));
        CHECK(saved.transfers_used == 1);       /* the deal was saved */
    }

    /* --- applying: the pack is written and the app restarts ------------- */
    int saves_before = config_saves;
    ok();
    CHECK(restarts == 1);
    CHECK(exists(pack_path));
    CHECK(strstr(g_issd_config.active_mod_packs, "ISSD Manager Career") != NULL);
    CHECK(config_saves > saves_before);
    /* Applying again does not stack the pack's name twice. */
    {
        size_t before = strlen(g_issd_config.active_mod_packs);
        ok();
        CHECK(strlen(g_issd_config.active_mod_packs) == before);
        CHECK(restarts == 2);
    }

    /* --- after the restart, the squads are in the game ------------------ */
    issd_mod_init();
    int index = issd_mod_load_pack(pack_path);
    CHECK(index >= 0);
    if (index >= 0) issd_mod_set_pack_enabled(index, true);
    memcpy(patched, rom, g_size);
    CHECK(issd_mod_apply_to_rom(patched, g_size) > 0);
    g_applied = patched;
    issd_manager_menu_open();                   /* reopening re-checks the cartridge */
    render();
    CHECK(on_screen("Squads are in the game"));
    CHECK(!on_screen("Restart to apply transfers"));
    press("Squads are in the game");
    CHECK(strstr(last_note, "already in the game") != NULL);
    CHECK(restarts == 2);                       /* nothing left to apply */

    /* --- replacing a career asks twice ---------------------------------- */
    press("New league");
    CHECK(strstr(last_note, "Press A again") != NULL);
    render();
    CHECK(strcmp(title(), "MANAGER MODE") == 0);
    press("New league");
    render();
    CHECK(strcmp(title(), "PICK YOUR TEAM") == 0);
    back();                                     /* cancelling keeps the career */
    render();
    CHECK(on_screen("R4/22"));

    /* --- leaving and coming back --------------------------------------- */
    back();
    CHECK(returns == 1);
    issd_manager_menu_set_host(&host);          /* a fresh start-up */
    issd_manager_menu_summary(summary, sizeof summary);
    CHECK(strcmp(summary, "R4/22") != 0);       /* nothing loaded yet... */
    CHECK(strcmp(summary, "saved") == 0);       /* ...but the file is noticed */
    issd_manager_menu_open();
    press("Continue saved career");
    CHECK(strstr(last_note, "Career loaded") != NULL);
    issd_manager_menu_summary(summary, sizeof summary);
    CHECK(strcmp(summary, "R4/22") == 0);

    /* --- play my match: the hand-off to the game ------------------------ */
    {
        IssdManager before;
        CHECK(issd_mgr_load(&before, note_career));
        IssdMgrFixture fx;
        uint8_t home, away;
        CHECK(issd_mgr_next_user_fixture(&before, &fx, &home, &away));
        const int own = before.team_id[before.user_slot];
        const bool at_home = fx.home == before.user_slot;
        const int other = at_home ? away : home;
        const int round_before = before.round;

        menu_requests = closes = 0;
        press("Play my match");
        CHECK(issd_link_armed());
        CHECK(menu_requests == 1);               /* the host brought the game to its menu */
        CHECK(closes == 1);                      /* the overlay got out of the way */
        CHECK(strstr(last_note, "starting") != NULL);

        /* A synthetic cartridge: main menu, then setup, kick-off, full time. */
        static uint8_t ram[0x20000];
        uint16_t pad[4] = {0, 0, 0, 0};
        memset(ram, 0, sizeof ram);
        ram[0x32] = 6; ram[0x70] = 12; ram[0x1538] = 0x72; ram[0x1539] = 0x9d; ram[0x153a] = 0xa4;
        ram[0x4e] = 4;                           /* the cursor was on Scenario */
        CHECK(!issd_link_before_frame(ram, pad));
        CHECK(ram[0x4e] == 0);                   /* it walks to Open Game */
        ram[0x70] = 15; ram[0x72] = 0; ram[0x1648] = 0;
        issd_link_after_frame(ram);              /* the setup boundary */
        CHECK(ram[0xda0] == own * 2 && ram[0xea0] == other * 2);
        ram[0x70] = 8;
        issd_link_after_frame(ram);
        CHECK(strcmp(issd_link_status(), "Match in progress") == 0);
        ram[0xda2] = 3; ram[0xea2] = 1;
        ram[0x70] = 18; ram[0x16d0] = 0; ram[0x16d1] = 0;
        issd_link_after_frame(ram);
        CHECK(issd_manager_menu_collect_match());
        CHECK(strstr(last_note, "3-1") != NULL);
        CHECK(!issd_link_armed());

        IssdManager after;
        CHECK(issd_mgr_load(&after, note_career));
        CHECK(after.round == round_before + 1);
        const IssdMgrFixture *played = &after.fixtures[round_before][0];
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i)
            if (after.fixtures[round_before][i].home == after.user_slot ||
                after.fixtures[round_before][i].away == after.user_slot)
                played = &after.fixtures[round_before][i];
        CHECK(played->played);
        CHECK((at_home ? played->home_goals : played->away_goals) == 3);
        CHECK((at_home ? played->away_goals : played->home_goals) == 1);
        CHECK(!issd_manager_menu_collect_match()); /* a result is handed over once */
    }

    free(rom); free(patched);
    printf(failures ? "manager menu: FAILED\n" : "manager menu: all checks passed\n");
    return failures ? 1 : 0;
}
