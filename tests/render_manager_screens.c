/* Draws every Manager Mode screen with the real overlay renderer and writes
 * each as a BMP, three times enlarged, so a change to the page can be looked
 * at without the game. Needs your own cartridge dump for the squads.
 *   render_manager_screens <rom.sfc> <out-dir> */
#include "issd_menu.h"
#include "issd_config.h"
#include "issd_manager.h"
#include "issd_manager_menu.h"
#include "issd_mod.h"
#include "issd_save.h"
#include "issd_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t g_ram[0x20000];
bool issd_save_to_slot(int s, const char *l) { (void)s; (void)l; return true; }
bool issd_load_from_slot(int s) { (void)s; return true; }
bool issd_save_get_info(int s, char *o, size_t n) { (void)s; snprintf(o, n, "Empty Slot"); return false; }
bool issd_save_continue_info(char *o, size_t n) { snprintf(o, n, "none"); return false; }
bool issd_save_continue(void) { return false; }
const char *issd_save_error(void) { return ""; }
bool issd_save_is_legacy(int s) { (void)s; return false; }
bool issd_load_from_slot_confirmed(int s, bool a) { (void)s; (void)a; return true; }
void issd_touch_set_pad_visible(bool v) { (void)v; }
void issd_touch_set_layout(const int *x, const int *y, const int *s) { (void)x; (void)y; (void)s; }
int issd_touch_rects(const void **o) { (void)o; return 0; }
bool issd_touch_center(int c, int *x, int *y) { (void)c; (void)x; (void)y; return false; }
void issd_request_quit(void) {}
void issd_restart_application(void) {}
bool issd_save_match_favorite(const void *p, size_t n, const char *l) { (void)p; (void)n; (void)l; return false; }
bool issd_save_read_match_favorite(void **p, size_t *n) { *p = NULL; *n = 0; return false; }
bool issd_save_match_favorite_info(char *o, size_t n) { snprintf(o, n, "No favorite"); return false; }

static uint8_t *rom;
static size_t rom_size;
static char out_dir[800], work_dir[900];
static uint32_t frame[256 * 224];

static const uint8_t *host_rom(size_t *size) { *size = rom_size; return rom; }
static const char *host_mods(void) { return work_dir; }
static const char *host_team(int t) { return t >= 0 && t < 36 ? issd_mod_rom_team_name(t) : NULL; }
static bool host_career(char *out, size_t cap, bool create) {
    (void)create;
    return snprintf(out, cap, "%s/career.mgr", work_dir) < (int)cap;
}

static int shot_number;
static void shoot(const char *name) {
    for (unsigned i = 0; i < sizeof frame / sizeof frame[0]; ++i) frame[i] = 0xff000000;
    issd_menu_render(frame, 256, 224);
    char path[1000];
    snprintf(path, sizeof path, "%s/%02d_%s.bmp", out_dir, ++shot_number, name);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    const int scale = 3, w = 256 * scale, h = 224 * scale;
    unsigned char header[54] = { 'B', 'M' };
    unsigned size = 54 + w * h * 3;
    memcpy(header + 2, &size, 4);
    header[10] = 54; header[14] = 40;
    memcpy(header + 18, &w, 4); memcpy(header + 22, &h, 4);
    header[26] = 1; header[28] = 24;
    fwrite(header, 1, 54, f);
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            uint32_t p = frame[(y / scale) * 256 + (x / scale)];
            unsigned char px[3] = { (unsigned char)p, (unsigned char)(p >> 8), (unsigned char)(p >> 16) };
            fwrite(px, 1, 3, f);
        }
    fclose(f);
}

static void down(int n) { while (n-- > 0) issd_menu_navigate_down(); }

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    rom = (uint8_t *)malloc(0x400000);
    rom_size = fread(rom, 1, 0x400000, f);
    fclose(f);
    snprintf(out_dir, sizeof out_dir, "%s", argv[2]);
    snprintf(work_dir, sizeof work_dir, "%s/work", argv[2]);
    {   /* Always start with no career, or the hub opens on Continue. */
        char stale[1000];
        snprintf(stale, sizeof stale, "%s/career.mgr", work_dir);
        remove(stale);
    }

    issd_config_init_defaults(&g_issd_config);
    issd_match_init(g_ram, &g_issd_config, NULL, NULL);
    issd_menu_init();
    const IssdManagerHost host = { host_rom, host_rom, host_mods, host_team, host_career };
    issd_manager_menu_set_host(&host);
    issd_manager_menu_set_seed(2026);

    issd_menu_open();
    g_overlay_menu.current_item = 17;            /* Gameplay Tweaks */
    issd_menu_confirm();
    down(4);
    shoot("gameplay_page");
    issd_menu_confirm();                          /* Manager Mode */
    shoot("hub_no_league");
    issd_menu_confirm();                          /* New league -> team picker */
    shoot("pick_team_top");
    down(30);                                     /* Brazil */
    shoot("pick_team_brazil");
    issd_menu_confirm();
    shoot("hub_new_league");

    for (int r = 0; r < 5; ++r) issd_menu_confirm();      /* simulate five rounds */
    shoot("hub_round_6");
    down(1); issd_menu_confirm();                  /* League table */
    shoot("table");
    issd_menu_cancel();
    down(1); issd_menu_confirm();                  /* Calendar */
    shoot("calendar_round");
    issd_menu_confirm();
    shoot("calendar_my_season");
    issd_menu_cancel();
    down(1); issd_menu_confirm();                  /* My squad */
    shoot("squad");
    issd_menu_cancel();
    down(1); issd_menu_confirm();                  /* Swap: clubs */
    shoot("swap_clubs");
    issd_menu_confirm();
    shoot("swap_their_players");
    down(8); issd_menu_confirm();
    shoot("swap_your_player");
    down(5); issd_menu_confirm();
    shoot("swap_offer");
    issd_menu_cancel(); issd_menu_cancel(); issd_menu_cancel(); issd_menu_cancel();
    shoot("hub_after_swap_menu");
    down(1); issd_menu_confirm();                  /* Free agents */
    shoot("free_agents");
    issd_menu_confirm();
    shoot("free_agent_who_leaves");
    printf("wrote %d screens to %s\n", shot_number, out_dir);
    free(rom);
    return 0;
}
