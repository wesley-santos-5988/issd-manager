/* Dumps every stock squad read by issd_mgr_read_rom_squad, one player per
 * line, so the Python test can compare it with the Mod Studio reader. */
#include <stdio.h>
#include <stdlib.h>

#include "issd_manager.h"

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    uint8_t *rom = (uint8_t *)malloc(0x400000);
    size_t size = rom ? fread(rom, 1, 0x400000, f) : 0;
    fclose(f);
    for (int team = 0; team < 36; ++team) {
        IssdMgrPlayer squad[ISSD_MGR_SQUAD];
        if (!issd_mgr_read_rom_squad(rom, size, team, squad)) {
            fprintf(stderr, "team %d unreadable\n", team);
            return 1;
        }
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i) {
            const IssdMgrPlayer *p = &squad[i];
            printf("%d|%d|%s|%d|%d|%d|%d,%d,%d,%d,%d,%d,%d,%d,%d\n", team, i, p->name,
                   p->shirt_number, p->position, p->skin_tone * 16 + p->hair_style,
                   p->attr[ISSD_ATTR_ACCELERATION], p->attr[ISSD_ATTR_SPEED],
                   p->attr[ISSD_ATTR_SHOOTING], p->attr[ISSD_ATTR_TECHNIQUE],
                   p->attr[ISSD_ATTR_BALANCE], p->attr[ISSD_ATTR_INTELLIGENCE],
                   p->attr[ISSD_ATTR_DRIBBLING], p->attr[ISSD_ATTR_JUMPING],
                   p->attr[ISSD_ATTR_STAMINA]);
        }
    }
    /* Not a cartridge: refused, never read out of range. */
    IssdMgrPlayer junk[ISSD_MGR_SQUAD];
    if (issd_mgr_read_rom_squad(rom, 1000, 0, junk)) return 4;
    if (issd_mgr_read_rom_squad(rom, size, 36, junk)) return 5;
    if (issd_mgr_read_rom_squad(rom, size, -1, junk)) return 6;
    if (issd_mgr_read_rom_squad(NULL, size, 0, junk)) return 7;
    free(rom);
    return 0;
}
