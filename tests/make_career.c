/* Writes a career file for a cartridge image, for driving the game headlessly:
 *   make_career <rom> <career.mgr> <team 0-35> [seed]
 * The game's Manager Mode then continues it like any saved career. */
#include "../ISSDNative/issd_manager.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: make_career rom out team [seed]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("rom"); return 1; }
    static uint8_t rom[0x200000 + 0x1000];
    size_t size = fread(rom, 1, sizeof rom, f);
    fclose(f);
    int team = atoi(argv[3]);
    uint32_t seed = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 12345u;
    uint8_t ids[ISSD_MGR_TEAMS];
    static IssdManager m;
    if (!issd_mgr_draw_teams(seed, team, ISSD_MGR_POOL_TEAMS, ids) ||
        !issd_mgr_new(&m, seed, ids, team)) return 1;
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        IssdMgrPlayer squad[ISSD_MGR_SQUAD];
        if (!issd_mgr_read_rom_squad(rom, size, m.team_id[s], squad) ||
            !issd_mgr_set_squad(&m, s, squad, ISSD_MGR_SQUAD)) return 1;
    }
    if (!issd_mgr_save(&m, argv[2])) return 1;
    IssdMgrFixture fx; uint8_t home, away;
    issd_mgr_next_user_fixture(&m, &fx, &home, &away);
    printf("career saved; round 1: team %d vs %d\n", home, away);
    return 0;
}
