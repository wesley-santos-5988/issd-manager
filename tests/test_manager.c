/* Manager mode league logic. No ROM, no renderer: the module is pure C. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "issd_manager.h"

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static const char *g_dir = ".";

/* A squad shaped like the cartridge's: keepers in slots 0 and 11. `level` is
 * the base rating and `salt` spreads the players around it. */
static void make_squad(IssdMgrPlayer squad[ISSD_MGR_SQUAD], int level, int salt) {
    static const uint8_t outfield[3] = { 2, 4, 6 };
    for (int i = 0; i < ISSD_MGR_SQUAD; ++i) {
        IssdMgrPlayer *p = &squad[i];
        memset(p, 0, sizeof *p);
        bool keeper = i == 0 || i == 11;
        p->shirt_number = (uint8_t)(i + 1);
        snprintf(p->name, sizeof p->name, "P%c%c", 'a' + (salt % 26), 'a' + i);
        p->position = keeper ? 1 : outfield[i % 3];
        p->skin_tone = (uint8_t)(i % 2);
        p->hair_style = keeper ? 0 : (uint8_t)(i % 14);
        for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a) {
            int v = level + ((i * 7 + a * 3 + salt) % 9) - 4;
            p->attr[a] = (uint8_t)(v < 1 ? 1 : v > 99 ? 99 : v);
        }
        p->attr[ISSD_ATTR_GOALKEEPING] = keeper ? (uint8_t)level : 10;
    }
}

static void fill_squads(IssdManager *m, int user_level, int other_level) {
    IssdMgrPlayer squad[ISSD_MGR_SQUAD];
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        make_squad(squad, s == m->user_slot ? user_level : other_level, s);
        CHECK(issd_mgr_set_squad(m, s, squad, ISSD_MGR_SQUAD));
    }
}

static bool new_league(IssdManager *m, uint32_t seed, int user_team) {
    uint8_t ids[ISSD_MGR_TEAMS];
    return issd_mgr_draw_teams(seed, user_team, 36, ids) && issd_mgr_new(m, seed, ids, user_team);
}

/* ------------------------------------------------------------------ draw */

static void test_draw(void) {
    uint8_t a[ISSD_MGR_TEAMS], b[ISSD_MGR_TEAMS], c[ISSD_MGR_TEAMS];
    CHECK(issd_mgr_draw_teams(7, 30, 36, a));
    CHECK(issd_mgr_draw_teams(7, 30, 36, b));
    CHECK(memcmp(a, b, sizeof a) == 0);

    bool has_user = false;
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) {
        CHECK(a[i] < 36);
        if (a[i] == 30) has_user = true;
        if (i) CHECK(a[i - 1] < a[i]); /* ascending, so also distinct */
    }
    CHECK(has_user);

    int different = 0;
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        CHECK(issd_mgr_draw_teams(seed, 5, 36, c));
        if (memcmp(a, c, sizeof a) != 0) ++different;
    }
    CHECK(different >= 18);

    CHECK(!issd_mgr_draw_teams(1, 36, 36, a));   /* user outside the pool */
    CHECK(!issd_mgr_draw_teams(1, -1, 36, a));
    CHECK(!issd_mgr_draw_teams(1, 0, 11, a));    /* pool too small */
    CHECK(!issd_mgr_draw_teams(1, 0, 43, a));    /* pool too large */
    CHECK(!issd_mgr_draw_teams(1, 0, 36, NULL));
    CHECK(issd_mgr_draw_teams(1, 11, 12, a));    /* exactly the pool */
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) CHECK(a[i] == i);
}

static void test_new_validation(void) {
    IssdManager m;
    uint8_t ids[ISSD_MGR_TEAMS];
    CHECK(issd_mgr_draw_teams(3, 4, 36, ids));
    CHECK(issd_mgr_new(&m, 3, ids, 4));
    CHECK(issd_mgr_slot_of_team(&m, 4) == m.user_slot);
    CHECK(issd_mgr_slot_of_team(&m, 99) == -1);
    int absent = 0;
    while (issd_mgr_slot_of_team(&m, absent) >= 0) ++absent;
    CHECK(!issd_mgr_new(&m, 3, ids, absent)); /* user not in the league */
    uint8_t dup[ISSD_MGR_TEAMS];
    memcpy(dup, ids, sizeof dup);
    dup[1] = dup[0];
    CHECK(!issd_mgr_new(&m, 3, dup, ids[0]));
    memcpy(dup, ids, sizeof dup);
    dup[2] = 40;                              /* outside the pool */
    CHECK(!issd_mgr_new(&m, 3, dup, ids[0]));
    CHECK(!issd_mgr_new(NULL, 3, ids, 4));
    CHECK(!issd_mgr_new(&m, 3, NULL, 4));
}

/* -------------------------------------------------------------- schedule */

static void check_schedule(const IssdManager *m) {
    int meetings[ISSD_MGR_TEAMS][ISSD_MGR_TEAMS];
    int home[ISSD_MGR_TEAMS] = {0}, away[ISSD_MGR_TEAMS] = {0};
    memset(meetings, 0, sizeof meetings);
    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r) {
        bool used[ISSD_MGR_TEAMS] = {0};
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
            const IssdMgrFixture *f = &m->fixtures[r][i];
            CHECK(f->home < ISSD_MGR_TEAMS && f->away < ISSD_MGR_TEAMS);
            CHECK(f->home != f->away);
            CHECK(!used[f->home] && !used[f->away]);   /* one match per round */
            used[f->home] = used[f->away] = true;
            CHECK(!f->played);
            meetings[f->home][f->away]++;
            home[f->home]++; away[f->away]++;
        }
    }
    for (int a = 0; a < ISSD_MGR_TEAMS; ++a) {
        CHECK(home[a] == 11 && away[a] == 11);
        for (int b = 0; b < ISSD_MGR_TEAMS; ++b)
            CHECK(meetings[a][b] == (a == b ? 0 : 1));  /* once at each ground */
    }
    /* The return leg must not open with a rematch of the first leg's last round. */
    for (int i = 0; i < ISSD_MGR_FIXTURES; ++i)
        for (int j = 0; j < ISSD_MGR_FIXTURES; ++j) {
            const IssdMgrFixture *x = &m->fixtures[10][i], *y = &m->fixtures[11][j];
            CHECK(!(x->home == y->away && x->away == y->home));
        }
    /* And no rematch in the round straight after the first meeting anywhere. */
    for (int r = 0; r + 1 < ISSD_MGR_ROUNDS; ++r)
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i)
            for (int j = 0; j < ISSD_MGR_FIXTURES; ++j) {
                const IssdMgrFixture *x = &m->fixtures[r][i], *y = &m->fixtures[r + 1][j];
                CHECK(!(x->home == y->away && x->away == y->home));
            }
}

static void test_schedule(void) {
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        IssdManager m;
        CHECK(new_league(&m, seed, (int)(seed % 36)));
        check_schedule(&m);
        CHECK(m.round == 0 && !issd_mgr_finished(&m));
        CHECK(m.budget == ISSD_MGR_START_BUDGET);
    }
    /* Same seed, same fixture list; other seeds differ. */
    IssdManager a, b, c;
    CHECK(new_league(&a, 42, 9));
    CHECK(new_league(&b, 42, 9));
    CHECK(memcmp(a.fixtures, b.fixtures, sizeof a.fixtures) == 0);
    int different = 0;
    for (uint32_t seed = 100; seed < 120; ++seed) {
        uint8_t ids[ISSD_MGR_TEAMS];
        memcpy(ids, a.team_id, sizeof ids);
        CHECK(issd_mgr_new(&c, seed, ids, 9));
        if (memcmp(a.fixtures, c.fixtures, sizeof a.fixtures) != 0) ++different;
    }
    CHECK(different == 20);

    /* The user is in exactly one fixture of every round. */
    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r) {
        int mine = 0;
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i)
            if (a.fixtures[r][i].home == a.user_slot || a.fixtures[r][i].away == a.user_slot) ++mine;
        CHECK(mine == 1);
    }
}

/* ----------------------------------------------------------------- table */

static void check_table(const IssdManager *m, int rounds_played) {
    IssdMgrStanding t[ISSD_MGR_TEAMS];
    issd_mgr_table(m, t);
    int won = 0, lost = 0, drawn = 0, gf = 0, ga = 0;
    bool seen[ISSD_MGR_TEAMS] = {0};
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) {
        CHECK(!seen[t[i].slot]);
        seen[t[i].slot] = true;
        CHECK(t[i].played == rounds_played);
        CHECK(t[i].won + t[i].drawn + t[i].lost == t[i].played);
        CHECK(t[i].points == 3 * t[i].won + t[i].drawn);
        won += t[i].won; lost += t[i].lost; drawn += t[i].drawn;
        gf += t[i].goals_for; ga += t[i].goals_against;
        if (i) {
            int gd0 = (int)t[i - 1].goals_for - (int)t[i - 1].goals_against;
            int gd1 = (int)t[i].goals_for - (int)t[i].goals_against;
            CHECK(t[i - 1].points > t[i].points ||
                  (t[i - 1].points == t[i].points &&
                   (gd0 > gd1 || (gd0 == gd1 && (t[i - 1].goals_for > t[i].goals_for ||
                    (t[i - 1].goals_for == t[i].goals_for && t[i - 1].slot < t[i].slot))))));
        }
    }
    CHECK(won == lost);
    CHECK(drawn % 2 == 0);
    CHECK(gf == ga);
    CHECK(won + lost + drawn == rounds_played * ISSD_MGR_TEAMS);
}

static void test_table_and_rounds(void) {
    IssdManager m;
    CHECK(new_league(&m, 11, 30));
    fill_squads(&m, 60, 60);
    check_table(&m, 0);

    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r) {
        CHECK(!issd_mgr_finished(&m));
        IssdMgrFixture fx;
        uint8_t home_id, away_id;
        CHECK(issd_mgr_next_user_fixture(&m, &fx, &home_id, &away_id));
        CHECK(fx.home == m.user_slot || fx.away == m.user_slot);
        CHECK(home_id == m.team_id[fx.home] && away_id == m.team_id[fx.away]);
        int before = m.budget;
        /* The user wins 2-1 at home or 1-2 away: always a win for the user. */
        bool user_home = fx.home == m.user_slot;
        CHECK(issd_mgr_record_user_result(&m, user_home ? 2 : 1, user_home ? 1 : 2));
        CHECK(m.round == r + 1);
        CHECK(m.budget == before + ISSD_MGR_INCOME_PER_ROUND + ISSD_MGR_WIN_BONUS);
        const IssdMgrFixture *saved = NULL;
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
            const IssdMgrFixture *f = &m.fixtures[r][i];
            CHECK(f->played);
            if (f->home == m.user_slot || f->away == m.user_slot) saved = f;
        }
        CHECK(saved && saved->home_goals == (user_home ? 2 : 1));
        check_table(&m, r + 1);
    }
    CHECK(issd_mgr_finished(&m));
    CHECK(!issd_mgr_next_user_fixture(&m, NULL, NULL, NULL));
    CHECK(!issd_mgr_record_user_result(&m, 1, 0));
    CHECK(!issd_mgr_sim_user_round(&m));
    IssdMgrStanding t[ISSD_MGR_TEAMS];
    issd_mgr_table(&m, t);
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i)
        if (t[i].slot == m.user_slot) CHECK(t[i].won == 22 && t[i].points == 66);
    CHECK(t[0].slot == m.user_slot); /* 22 wins cannot be caught */
}

static void test_bad_results(void) {
    IssdManager m;
    CHECK(new_league(&m, 5, 0));
    CHECK(!issd_mgr_record_user_result(&m, -1, 0));
    CHECK(!issd_mgr_record_user_result(&m, 0, -1));
    CHECK(!issd_mgr_record_user_result(&m, 100, 0));
    CHECK(!issd_mgr_record_user_result(&m, 0, 100));
    CHECK(!issd_mgr_record_user_result(NULL, 0, 0));
    CHECK(m.round == 0);                         /* nothing advanced */
    CHECK(issd_mgr_record_user_result(&m, 0, 0)); /* a draw is allowed */
    CHECK(m.round == 1);
    CHECK(m.budget == ISSD_MGR_START_BUDGET + ISSD_MGR_INCOME_PER_ROUND); /* no bonus */
    CHECK(issd_mgr_record_user_result(&m, 99, 99));
}

/* ------------------------------------------------------------ simulation */

static void test_simulation(void) {
    /* The same league and inputs always give the same season. */
    IssdManager a, b;
    CHECK(new_league(&a, 77, 14)); CHECK(new_league(&b, 77, 14));
    fill_squads(&a, 70, 55); fill_squads(&b, 70, 55);
    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r) {
        CHECK(issd_mgr_sim_user_round(&a)); CHECK(issd_mgr_sim_user_round(&b));
    }
    CHECK(memcmp(a.fixtures, b.fixtures, sizeof a.fixtures) == 0);

    /* Equal sides: average goals per match is football-like. */
    long goals = 0, matches = 0;
    for (uint32_t seed = 1; seed <= 25; ++seed) {
        IssdManager m;
        CHECK(new_league(&m, seed, 3));
        fill_squads(&m, 60, 60);
        while (!issd_mgr_finished(&m)) CHECK(issd_mgr_sim_user_round(&m));
        for (int r = 0; r < ISSD_MGR_ROUNDS; ++r)
            for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
                goals += m.fixtures[r][i].home_goals + m.fixtures[r][i].away_goals;
                ++matches;
            }
    }
    double per_match = (double)goals / (double)matches;
    CHECK(per_match > 2.0 && per_match < 3.2);

    /* Strength matters: a far better squad usually wins the league, and a far
     * worse one usually finishes near the bottom. */
    int titles = 0, bottom_half = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        IssdManager strong, weak;
        CHECK(new_league(&strong, seed, 8)); CHECK(new_league(&weak, seed, 8));
        fill_squads(&strong, 85, 50);
        fill_squads(&weak, 30, 55);
        while (!issd_mgr_finished(&strong)) CHECK(issd_mgr_sim_user_round(&strong));
        while (!issd_mgr_finished(&weak)) CHECK(issd_mgr_sim_user_round(&weak));
        IssdMgrStanding t[ISSD_MGR_TEAMS];
        issd_mgr_table(&strong, t);
        if (t[0].slot == strong.user_slot) ++titles;
        issd_mgr_table(&weak, t);
        for (int i = ISSD_MGR_TEAMS / 2; i < ISSD_MGR_TEAMS; ++i)
            if (t[i].slot == weak.user_slot) ++bottom_half;
    }
    CHECK(titles >= 34);
    CHECK(bottom_half >= 36);
    printf("  sim: %.2f goals/match, strong won %d/40, weak bottom half %d/40\n",
           per_match, titles, bottom_half);

    IssdManager m;
    CHECK(new_league(&m, 1, 0));
    CHECK(issd_mgr_team_strength(&m, 0) == 50);  /* no squad yet */
    fill_squads(&m, 70, 70);
    int s = issd_mgr_team_strength(&m, 0);
    CHECK(s >= 66 && s <= 74);
    CHECK(issd_mgr_team_strength(&m, -1) == 50);
    CHECK(issd_mgr_team_strength(&m, ISSD_MGR_TEAMS) == 50);
}

/* ------------------------------------------------------------- transfers */

static void test_squads(void) {
    IssdManager m;
    CHECK(new_league(&m, 2, 0));
    IssdMgrPlayer squad[ISSD_MGR_SQUAD];
    make_squad(squad, 60, 0);
    CHECK(!issd_mgr_set_squad(&m, 0, squad, 19));
    CHECK(!issd_mgr_set_squad(&m, 0, squad, 21));
    CHECK(!issd_mgr_set_squad(&m, -1, squad, 20));
    CHECK(!issd_mgr_set_squad(&m, ISSD_MGR_TEAMS, squad, 20));
    CHECK(!issd_mgr_set_squad(&m, 0, NULL, 20));
    CHECK(!m.has_squad[0]);

    /* Names, numbers and values are made safe for the cartridge. */
    snprintf(squad[3].name, sizeof squad[3].name, "J0se.X");
    memcpy(squad[4].name, "Muuuuuuuu", 9);            /* nine letters, no terminator in range */
    squad[5].shirt_number = 0;
    squad[6].shirt_number = 200;
    squad[7].position = 99;
    squad[8].skin_tone = 3;
    squad[9].attr[ISSD_ATTR_SPEED] = 250;
    squad[0].hair_style = 9;                           /* a keeper */
    CHECK(issd_mgr_set_squad(&m, 0, squad, 20));
    CHECK(strcmp(m.squad[0][3].name, "J se.X") == 0);
    CHECK(strlen(m.squad[0][4].name) == 8);
    CHECK(m.squad[0][5].shirt_number == 1);
    CHECK(m.squad[0][6].shirt_number == 99);
    CHECK(m.squad[0][7].position == 4);
    CHECK(m.squad[0][8].skin_tone == 1);
    CHECK(m.squad[0][9].attr[ISSD_ATTR_SPEED] == 99);
    CHECK(m.squad[0][0].hair_style == 0);
    CHECK(m.has_squad[0]);
}

static int user_slot_other(const IssdManager *m) { return m->user_slot == 0 ? 1 : 0; }

static void test_swaps(void) {
    IssdManager m;
    CHECK(new_league(&m, 9, 20));
    int user = m.user_slot, other = user_slot_other(&m);

    CHECK(issd_mgr_offer_swap(&m, other, 3, 3, 0) == ISSD_MGR_TR_INVALID); /* no squads yet */
    fill_squads(&m, 60, 60);
    /* Give the other club a clearly better outfield player at index 4. */
    for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a) m.squad[other][4].attr[a] = 90;
    for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a) m.squad[user][5].attr[a] = 55;

    int want = issd_mgr_player_value(&m.squad[other][4]);
    int give = issd_mgr_player_value(&m.squad[user][5]);
    CHECK(want > give);
    int price = issd_mgr_swap_price(&m, other, 4, 5);
    CHECK(price == (want * 115 + 99) / 100 - give);
    CHECK(price > 0);

    CHECK(issd_mgr_offer_swap(&m, user, 4, 5, 999) == ISSD_MGR_TR_INVALID);  /* own club */
    CHECK(issd_mgr_offer_swap(&m, other, 20, 5, 999) == ISSD_MGR_TR_INVALID);
    CHECK(issd_mgr_offer_swap(&m, other, 4, -1, 999) == ISSD_MGR_TR_INVALID);
    CHECK(issd_mgr_offer_swap(&m, ISSD_MGR_TEAMS, 4, 5, 999) == ISSD_MGR_TR_INVALID);
    CHECK(issd_mgr_swap_price(&m, user, 4, 5) == 0x7fffffff);

    CHECK(issd_mgr_offer_swap(&m, other, 4, 5, price - 1) == ISSD_MGR_TR_REFUSED);
    CHECK(issd_mgr_offer_swap(&m, other, 0, 5, 999) == ISSD_MGR_TR_POSITION);   /* keeper for outfield */
    CHECK(issd_mgr_offer_swap(&m, other, 4, 0, 999) == ISSD_MGR_TR_POSITION);   /* outfield for keeper */
    m.budget = price - 1;
    CHECK(issd_mgr_offer_swap(&m, other, 4, 5, price) == ISSD_MGR_TR_FUNDS);
    CHECK(m.transfers_used == 0);                                                 /* failures cost nothing */

    m.budget = 500;
    IssdMgrPlayer buyer = m.squad[user][5], target = m.squad[other][4];
    CHECK(issd_mgr_offer_swap(&m, other, 4, 5, price) == ISSD_MGR_TR_OK);
    CHECK(m.budget == 500 - price);
    CHECK(m.transfers_used == 1);
    /* They traded places and kept their slots' shirt numbers. */
    CHECK(strcmp(m.squad[user][5].name, target.name) == 0);
    CHECK(m.squad[user][5].shirt_number == buyer.shirt_number);
    CHECK(strcmp(m.squad[other][4].name, buyer.name) == 0);
    CHECK(m.squad[other][4].shirt_number == target.shirt_number);
    CHECK(issd_mgr_player_rating(&m.squad[user][5]) > issd_mgr_player_rating(&m.squad[other][4]));

    /* Keepers swap with keepers. */
    int kp = issd_mgr_swap_price(&m, other, 11, 11);
    CHECK(kp < 0x7fffffff);
    CHECK(issd_mgr_offer_swap(&m, other, 11, 11, kp) == ISSD_MGR_TR_OK);
    CHECK(m.transfers_used == 2);
    /* Limit per round, which a played round resets. */
    CHECK(issd_mgr_offer_swap(&m, other, 6, 6, 999) == ISSD_MGR_TR_LIMIT);
    CHECK(issd_mgr_sign_free_agent(&m, 5, 6) == ISSD_MGR_TR_LIMIT);
    CHECK(issd_mgr_record_user_result(&m, 1, 0));
    CHECK(m.transfers_used == 0);
    int price2 = issd_mgr_swap_price(&m, other, 6, 6);
    CHECK(issd_mgr_offer_swap(&m, other, 6, 6, price2 > 0 ? price2 : 0) == ISSD_MGR_TR_OK);

    /* Overpaying is allowed; the other club does not give change. A negative
     * price (the user sells a better man for less) pays the user. */
    for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a) m.squad[user][7].attr[a] = 90;
    for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a) m.squad[other][8].attr[a] = 40;
    int sell = issd_mgr_swap_price(&m, other, 8, 7);
    CHECK(sell < 0);
    int budget = m.budget;
    CHECK(issd_mgr_offer_swap(&m, other, 8, 7, sell) == ISSD_MGR_TR_OK);
    CHECK(m.budget == budget - sell && m.budget > budget);

    /* The season over: no more deals. */
    while (!issd_mgr_finished(&m)) CHECK(issd_mgr_sim_user_round(&m));
    CHECK(issd_mgr_offer_swap(&m, other, 9, 9, 999) == ISSD_MGR_TR_SEASON_OVER);
    CHECK(issd_mgr_sign_free_agent(&m, 0, 9) == ISSD_MGR_TR_SEASON_OVER);
}

static void test_free_agents(void) {
    IssdManager m;
    CHECK(new_league(&m, 31, 12));
    for (int i = 0; i < ISSD_MGR_FREE_AGENTS; ++i) {
        CHECK(m.free_agents[i].position == (i < 2 ? 1 : m.free_agents[i].position));
        CHECK(m.free_agents[i].name[0] != 0);
    }
    /* No two free agents share a name, whatever the seed. */
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        IssdManager n;
        CHECK(new_league(&n, seed, 12));
        for (int i = 0; i < ISSD_MGR_FREE_AGENTS; ++i)
            for (int j = i + 1; j < ISSD_MGR_FREE_AGENTS; ++j)
                CHECK(strcmp(n.free_agents[i].name, n.free_agents[j].name) != 0);
    }
    CHECK(issd_mgr_sign_free_agent(&m, 5, 5) == ISSD_MGR_TR_INVALID); /* no squad */
    fill_squads(&m, 60, 60);
    int user = m.user_slot;
    CHECK(issd_mgr_sign_free_agent(&m, -1, 5) == ISSD_MGR_TR_INVALID);
    CHECK(issd_mgr_sign_free_agent(&m, 12, 5) == ISSD_MGR_TR_INVALID);
    CHECK(issd_mgr_sign_free_agent(&m, 5, 20) == ISSD_MGR_TR_INVALID);
    CHECK(issd_mgr_sign_free_agent(&m, 5, 0) == ISSD_MGR_TR_POSITION);   /* outfield for keeper */
    CHECK(issd_mgr_sign_free_agent(&m, 0, 5) == ISSD_MGR_TR_POSITION);   /* keeper for outfield */
    CHECK(issd_mgr_free_agent_fee(&m, 99) == 0x7fffffff);

    int fee = issd_mgr_free_agent_fee(&m, 5);
    CHECK(fee >= 1);
    m.budget = fee - 1;
    CHECK(issd_mgr_sign_free_agent(&m, 5, 5) == ISSD_MGR_TR_FUNDS);
    m.budget = 1000;
    IssdMgrPlayer signed_player = m.free_agents[5], released = m.squad[user][5];
    CHECK(issd_mgr_sign_free_agent(&m, 5, 5) == ISSD_MGR_TR_OK);
    CHECK(strcmp(m.squad[user][5].name, signed_player.name) == 0);
    CHECK(m.squad[user][5].shirt_number == released.shirt_number);
    CHECK(strcmp(m.free_agents[5].name, released.name) == 0);   /* he joins the list */
    CHECK(m.budget == 1000 - fee);
    CHECK(m.transfers_used == 1);

    /* A keeper for a keeper. */
    int kfee = issd_mgr_free_agent_fee(&m, 0);
    CHECK(issd_mgr_sign_free_agent(&m, 0, 11) == ISSD_MGR_TR_OK);
    CHECK(m.budget == 1000 - fee - kfee);
}

/* ---------------------------------------------------------- save and load */

static unsigned char *read_all(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *buf = (unsigned char *)malloc(16384);
    *size = fread(buf, 1, 16384, f);
    fclose(f);
    return buf;
}

static void write_all(const char *path, const unsigned char *buf, size_t size) {
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) return;
    CHECK(fwrite(buf, 1, size, f) == size);
    fclose(f);
}

static uint32_t crc32_of(const unsigned char *data, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void reseal(unsigned char *buf, size_t size) {
    uint32_t c = crc32_of(buf, size - 4);
    for (int i = 0; i < 4; ++i) buf[size - 4 + i] = (unsigned char)(c >> (8 * i));
}

static void path_in(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/%s", g_dir, name);
}

static void test_save_load(void) {
    char path[1024], again[1024], tmp[1100];
    path_in(path, sizeof path, "career.mgr");
    path_in(again, sizeof again, "career2.mgr");

    IssdManager m;
    CHECK(new_league(&m, 123, 33));
    fill_squads(&m, 65, 55);
    m.budget = 1000;
    int first_price = issd_mgr_swap_price(&m, user_slot_other(&m), 4, 4);
    CHECK(issd_mgr_offer_swap(&m, user_slot_other(&m), 4, 4, first_price > 0 ? first_price : 0) == ISSD_MGR_TR_OK);
    for (int r = 0; r < 9; ++r) CHECK(issd_mgr_record_user_result(&m, r % 4, 1));
    CHECK(issd_mgr_sign_free_agent(&m, 3, 6) == ISSD_MGR_TR_OK);

    CHECK(issd_mgr_save(&m, path));
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *leftover = fopen(tmp, "rb");
    CHECK(leftover == NULL);                      /* the temporary file is gone */
    if (leftover) fclose(leftover);

    IssdManager loaded;
    memset(&loaded, 0xAB, sizeof loaded);
    CHECK(issd_mgr_load(&loaded, path));
    CHECK(loaded.seed == m.seed && loaded.user_slot == m.user_slot);
    CHECK(loaded.round == 9 && loaded.budget == m.budget);
    CHECK(loaded.transfers_used == m.transfers_used);
    CHECK(memcmp(loaded.team_id, m.team_id, sizeof m.team_id) == 0);
    CHECK(memcmp(loaded.fixtures, m.fixtures, sizeof m.fixtures) == 0);
    CHECK(memcmp(loaded.squad, m.squad, sizeof m.squad) == 0);
    CHECK(memcmp(loaded.free_agents, m.free_agents, sizeof m.free_agents) == 0);
    CHECK(memcmp(loaded.has_squad, m.has_squad, sizeof m.has_squad) == 0);

    /* Saving the loaded league is byte-identical to the original file. */
    CHECK(issd_mgr_save(&loaded, again));
    size_t a_size = 0, b_size = 0;
    unsigned char *a = read_all(path, &a_size), *b = read_all(again, &b_size);
    CHECK(a && b && a_size == b_size && memcmp(a, b, a_size) == 0);
    CHECK(a_size > 6000 && a_size < 8192);

    /* The loaded career plays on exactly as the original would have. */
    CHECK(issd_mgr_sim_user_round(&m));
    CHECK(issd_mgr_sim_user_round(&loaded));
    CHECK(memcmp(loaded.fixtures, m.fixtures, sizeof m.fixtures) == 0);

    /* Overwriting an existing file works. */
    CHECK(issd_mgr_save(&m, path));
    CHECK(issd_mgr_load(&loaded, path) && loaded.round == 10);

    /* Corruption: one flipped bit anywhere is refused and leaves *m alone. */
    IssdManager guard = loaded;
    size_t good_size = 0;
    unsigned char *good = read_all(path, &good_size);
    CHECK(good != NULL);
    for (size_t at = 0; good && at < good_size; at += 97) {
        good[at] ^= 0x10;
        write_all(again, good, good_size);
        CHECK(!issd_mgr_load(&loaded, again));
        CHECK(memcmp(&guard, &loaded, sizeof guard) == 0);
        good[at] ^= 0x10;
    }
    /* Truncation, emptiness, a missing file. */
    write_all(again, good, good_size - 1);
    CHECK(!issd_mgr_load(&loaded, again));
    write_all(again, good, 0);
    CHECK(!issd_mgr_load(&loaded, again));
    write_all(again, good, 8);
    CHECK(!issd_mgr_load(&loaded, again));
    path_in(tmp, sizeof tmp, "does-not-exist.mgr");
    CHECK(!issd_mgr_load(&loaded, tmp));
    CHECK(!issd_mgr_load(NULL, path) && !issd_mgr_load(&loaded, NULL));

    /* A file whose checksum is right but whose contents are impossible. */
    struct { size_t offset; unsigned char value; const char *why; } tamper[] = {
        { 24, 12, "user slot out of range" },
        { 25, 23, "round beyond the season" },
        { 26, 9, "more transfers than allowed" },
        { 32, 0, "home equals away" },                 /* fixture 0: away copied from home below */
        { 31 + (21 * 6) * 5 + 3, 4, "goals in an unplayed fixture" },
        { 31 + (21 * 6) * 5 + 2, 1, "an unplayed fixture marked played" },
        { 12, 200, "team id out of range" },
    };
    for (size_t i = 0; good && i < sizeof tamper / sizeof *tamper; ++i) {
        unsigned char *t = (unsigned char *)malloc(good_size);
        memcpy(t, good, good_size);
        if (i == 3) t[32] = t[31];
        else t[tamper[i].offset] = tamper[i].value;
        reseal(t, good_size);
        write_all(again, t, good_size);
        if (issd_mgr_load(&loaded, again)) {
            fprintf(stderr, "FAIL tamper accepted: %s\n", tamper[i].why);
            ++failures;
        }
        CHECK(memcmp(&guard, &loaded, sizeof guard) == 0);
        free(t);
    }
    /* The tampering loop proves rejection only if the pristine file is accepted. */
    write_all(again, good, good_size);
    CHECK(issd_mgr_load(&loaded, again));

    /* An unplayable manager is not written. */
    IssdManager broken = m;
    broken.fixtures[0][0].away = broken.fixtures[0][0].home;
    CHECK(!issd_mgr_save(&broken, again));
    CHECK(!issd_mgr_save(&m, "/nonexistent-directory/career.mgr"));

    free(a); free(b); free(good);
}

/* ----------------------------------------------------------------- pack */

static void test_export_text(void) {
    IssdManager m;
    char path[1024];
    path_in(path, sizeof path, "career.json");
    CHECK(new_league(&m, 4, 1));
    fill_squads(&m, 60, 60);
    m.has_squad[3] = false;                       /* teams without a squad are skipped */
    CHECK(issd_mgr_export_pack(&m, path));
    size_t size = 0;
    unsigned char *text = read_all(path, &size);
    CHECK(text != NULL && size > 1000);
    if (text) {
        text[size < 16384 ? size : 16383] = 0;
        CHECK(strstr((char *)text, "\"ISSD Manager Career\"") != NULL);
        CHECK(strstr((char *)text, "\"position\": \"GK\"") != NULL);
        CHECK(strstr((char *)text, "\"position\": \"DF\"") != NULL);
        CHECK(strstr((char *)text, "\"goalkeeping\"") != NULL);
        free(text);
    }
    CHECK(!issd_mgr_export_pack(&m, "/nonexistent-directory/career.json"));
    CHECK(!issd_mgr_export_pack(NULL, path));
}

int main(int argc, char **argv) {
    if (argc > 1) g_dir = argv[1];
    test_draw();
    test_new_validation();
    test_schedule();
    test_table_and_rounds();
    test_bad_results();
    test_simulation();
    test_squads();
    test_swaps();
    test_free_agents();
    test_save_load();
    test_export_text();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("manager: all checks passed\n");
    return 0;
}
