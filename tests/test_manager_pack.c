/* The career roster pack must be read back by the real mod loader exactly as
 * the manager wrote it: that is how a transfer reaches the game. */
#include <stdio.h>
#include <string.h>

#include "issd_manager.h"
#include "issd_mod.h"

uint8_t g_ram[0x20000];

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void make_squad(IssdMgrPlayer squad[ISSD_MGR_SQUAD], int level, int salt) {
    for (int i = 0; i < ISSD_MGR_SQUAD; ++i) {
        IssdMgrPlayer *p = &squad[i];
        memset(p, 0, sizeof *p);
        bool keeper = i == 0 || i == 11;
        p->shirt_number = (uint8_t)(i + 1);
        snprintf(p->name, sizeof p->name, " %c.%c%c", 'A' + (salt % 26), 'a' + i, 'a' + salt % 26);
        /* Every code the cartridge uses, including the in-between 3 and 5. */
        static const uint8_t codes[] = { 2, 3, 4, 5, 6 };
        p->position = keeper ? 1 : codes[i % 5];
        p->skin_tone = (uint8_t)(i % 2);
        p->hair_style = keeper ? 0 : (uint8_t)(i % 14);
        for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a)
            p->attr[a] = (uint8_t)(level + ((i * 5 + a * 2 + salt) % 11));
        p->attr[ISSD_ATTR_GOALKEEPING] = keeper ? (uint8_t)(level + 10) : 10;
    }
}

static const char *expected_position(uint8_t code, char buf[4]) {
    switch (code) {
    case 1: return "GK";
    case 2: return "DF";
    case 4: return "MF";
    case 6: return "FW";
    default: snprintf(buf, 4, "%u", (unsigned)code); return buf;
    }
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[1024];
    snprintf(path, sizeof path, "%s/career.json", dir);

    uint8_t ids[ISSD_MGR_TEAMS];
    IssdManager m;
    CHECK(issd_mgr_draw_teams(8, 30, 36, ids));
    CHECK(issd_mgr_new(&m, 8, ids, 30));
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        IssdMgrPlayer squad[ISSD_MGR_SQUAD];
        make_squad(squad, 40 + s * 3, s);
        CHECK(issd_mgr_set_squad(&m, s, squad, ISSD_MGR_SQUAD));
    }
    /* Play a deal so the pack carries a changed squad, not just the originals. */
    int other = m.user_slot == 0 ? 1 : 0;
    m.budget = 1000;
    int price = issd_mgr_swap_price(&m, other, 4, 4);
    CHECK(issd_mgr_offer_swap(&m, other, 4, 4, price > 0 ? price : 0) == ISSD_MGR_TR_OK);

    CHECK(issd_mgr_export_pack(&m, path));

    issd_mod_init();
    int index = issd_mod_load_pack(path);
    CHECK(index >= 0);
    IssdModPack *pack = index >= 0 ? issd_mod_get_pack(index) : NULL;
    CHECK(pack != NULL);
    if (!pack) return 1;

    CHECK(strcmp(pack->name, "ISSD Manager Career") == 0);
    CHECK(pack->team_count == ISSD_MGR_TEAMS);
    for (int s = 0; s < ISSD_MGR_TEAMS && s < pack->team_count; ++s) {
        const IssdModTeam *t = &pack->teams[s];
        CHECK(!t->new_team);
        CHECK(t->team_id == m.team_id[s]);
        CHECK(t->player_count == ISSD_MGR_SQUAD);
        for (int i = 0; i < ISSD_MGR_SQUAD && i < t->player_count; ++i) {
            const IssdModPlayer *p = &t->players[i];
            const IssdMgrPlayer *q = &m.squad[s][i];
            char code[4];
            CHECK(p->shirt_number == q->shirt_number);
            CHECK(strcmp(p->name, q->name) == 0);
            CHECK(strcmp(p->position, expected_position(q->position, code)) == 0);
            CHECK(p->skin_tone == q->skin_tone);
            CHECK(p->hair_style == q->hair_style);
            CHECK(p->attributes.acceleration == q->attr[ISSD_ATTR_ACCELERATION]);
            CHECK(p->attributes.speed == q->attr[ISSD_ATTR_SPEED]);
            CHECK(p->attributes.shooting == q->attr[ISSD_ATTR_SHOOTING]);
            CHECK(p->attributes.technique == q->attr[ISSD_ATTR_TECHNIQUE]);
            CHECK(p->attributes.balance == q->attr[ISSD_ATTR_BALANCE]);
            CHECK(p->attributes.intelligence == q->attr[ISSD_ATTR_INTELLIGENCE]);
            CHECK(p->attributes.dribbling == q->attr[ISSD_ATTR_DRIBBLING]);
            CHECK(p->attributes.jumping == q->attr[ISSD_ATTR_JUMPING]);
            CHECK(p->attributes.stamina == q->attr[ISSD_ATTR_STAMINA]);
            CHECK(p->attributes.goalkeeping == q->attr[ISSD_ATTR_GOALKEEPING]);
        }
    }

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("manager pack: all checks passed\n");
    return 0;
}
