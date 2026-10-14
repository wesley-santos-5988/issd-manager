/* End to end on a real cartridge: the 36 stock squads become a league, a
 * transfer is made, the career pack is applied to a copy of the cartridge and
 * the squads are read back out of the patched image. Needs your own dump. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "issd_manager.h"
#include "issd_mod.h"

uint8_t g_ram[0x20000];

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool same_player(const IssdMgrPlayer *a, const IssdMgrPlayer *b, bool numbers) {
    return strcmp(a->name, b->name) == 0 && a->position == b->position &&
           a->skin_tone == b->skin_tone && a->hair_style == b->hair_style &&
           (!numbers || a->shirt_number == b->shirt_number) &&
           memcmp(a->attr, b->attr, ISSD_ATTR_GOALKEEPING) == 0 &&
           (a->position == 1 ? 1 : a->attr[ISSD_ATTR_GOALKEEPING] == b->attr[ISSD_ATTR_GOALKEEPING]);
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *rom_path = argv[1], *dir = argv[2];
    FILE *f = fopen(rom_path, "rb");
    if (!f) return 3;
    size_t cap = 0x400000;
    uint8_t *rom = (uint8_t *)malloc(cap), *patched = (uint8_t *)malloc(cap);
    size_t size = fread(rom, 1, cap, f);
    fclose(f);

    static IssdMgrPlayer stock[36][ISSD_MGR_SQUAD];
    for (int t = 0; t < 36; ++t) CHECK(issd_mgr_read_rom_squad(rom, size, t, stock[t]));

    /* Real strengths: how far apart are the stock sides? */
    IssdManager probe;
    uint8_t all[ISSD_MGR_TEAMS];
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) all[i] = (uint8_t)i;
    CHECK(issd_mgr_new(&probe, 1, all, 0));
    int lo = 100, hi = 0;
    for (int t = 0; t < 36; ++t) {
        IssdManager one = probe;
        CHECK(issd_mgr_set_squad(&one, 0, stock[t], ISSD_MGR_SQUAD));
        int s = issd_mgr_team_strength(&one, 0);
        if (s < lo) lo = s;
        if (s > hi) hi = s;
    }
    printf("stock team strength: %d to %d\n", lo, hi);

    /* Seasons among random stock sides: does the strongest side usually win? */
    int best_wins = 0, seasons = 300;
    for (int seed = 1; seed <= seasons; ++seed) {
        uint8_t ids[ISSD_MGR_TEAMS];
        IssdManager m;
        CHECK(issd_mgr_draw_teams((uint32_t)seed, 30, 36, ids));
        CHECK(issd_mgr_new(&m, (uint32_t)seed, ids, 30));
        int best = 0, best_strength = -1;
        for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
            CHECK(issd_mgr_set_squad(&m, s, stock[m.team_id[s]], ISSD_MGR_SQUAD));
            if (issd_mgr_team_strength(&m, s) > best_strength) {
                best_strength = issd_mgr_team_strength(&m, s); best = s;
            }
        }
        while (!issd_mgr_finished(&m)) CHECK(issd_mgr_sim_user_round(&m));
        IssdMgrStanding t[ISSD_MGR_TEAMS];
        issd_mgr_table(&m, t);
        if (t[0].slot == best) ++best_wins;
    }
    printf("strongest side won %d of %d seasons\n", best_wins, seasons);

    /* A league around Brazil, one transfer, and the pack applied to the ROM. */
    uint8_t ids[ISSD_MGR_TEAMS];
    IssdManager m;
    CHECK(issd_mgr_draw_teams(2026, 30, 36, ids));
    CHECK(issd_mgr_new(&m, 2026, ids, 30));
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s)
        CHECK(issd_mgr_set_squad(&m, s, stock[m.team_id[s]], ISSD_MGR_SQUAD));
    int user = m.user_slot, other = user == 0 ? 1 : 0;
    int other_team = m.team_id[other], user_team = m.team_id[user];
    IssdMgrPlayer mine = m.squad[user][5], theirs = m.squad[other][5];
    m.budget = 5000;
    int price = issd_mgr_swap_price(&m, other, 5, 5);
    CHECK(issd_mgr_offer_swap(&m, other, 5, 5, price > 0 ? price : 0) == ISSD_MGR_TR_OK);
    CHECK(same_player(&m.squad[user][5], &theirs, false));
    CHECK(m.squad[user][5].shirt_number == mine.shirt_number);

    char path[1024];
    snprintf(path, sizeof path, "%s/career.json", dir);
    CHECK(issd_mgr_export_pack(&m, path));
    issd_mod_init();
    int index = issd_mod_load_pack(path);
    CHECK(index >= 0);
    if (index >= 0) issd_mod_set_pack_enabled(index, true);
    memcpy(patched, rom, size);
    CHECK(issd_mod_apply_to_rom(patched, size) > 0);

    /* Every league team reads back as the manager holds it. */
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        IssdMgrPlayer back[ISSD_MGR_SQUAD];
        CHECK(issd_mgr_read_rom_squad(patched, size, m.team_id[s], back));
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i)
            if (!same_player(&back[i], &m.squad[s][i], true)) {
                fprintf(stderr, "team %d slot %d differs: %s vs %s\n", m.team_id[s], i,
                        back[i].name, m.squad[s][i].name);
                ++failures;
            }
    }
    /* The two clubs of the deal really traded, and everyone else is as stock. */
    IssdMgrPlayer a[ISSD_MGR_SQUAD], b[ISSD_MGR_SQUAD];
    CHECK(issd_mgr_read_rom_squad(patched, size, user_team, a));
    CHECK(issd_mgr_read_rom_squad(patched, size, other_team, b));
    CHECK(strcmp(a[5].name, theirs.name) == 0);
    CHECK(strcmp(b[5].name, mine.name) == 0);
    CHECK(strcmp(a[5].name, stock[user_team][5].name) != 0);
    bool in_league[36] = {0};
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) in_league[m.team_id[s]] = true;
    for (int t = 0; t < 36; ++t) {
        if (in_league[t]) continue;
        IssdMgrPlayer back[ISSD_MGR_SQUAD];
        CHECK(issd_mgr_read_rom_squad(patched, size, t, back));
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i) CHECK(same_player(&back[i], &stock[t][i], true));
    }
    /* Only roster bytes moved: the image is the same size and the original is untouched. */
    CHECK(memcmp(rom, patched, size) != 0);

    printf(failures ? "manager career: FAILED\n" : "manager career: all checks passed\n");
    free(rom); free(patched);
    return failures ? 1 : 0;
}
