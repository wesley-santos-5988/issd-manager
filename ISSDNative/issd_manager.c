#include "issd_manager.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ RNG */

/* SplitMix64: tiny, fixed-width and identical on every platform, so a seed
 * always draws the same league. The C library's rand() is not. */
typedef struct { uint64_t s; } Rng;

static uint64_t rng_next(Rng *r) {
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static uint32_t rng_below(Rng *r, uint32_t n) {
    return (uint32_t)((rng_next(r) >> 32) % n);
}

static void shuffle(Rng *r, uint8_t *v, int n) {
    for (int i = n - 1; i > 0; --i) {
        int j = (int)rng_below(r, (uint32_t)i + 1);
        uint8_t t = v[i]; v[i] = v[j]; v[j] = t;
    }
}

/* ----------------------------------------------------------- league draw */

bool issd_mgr_draw_teams(uint32_t seed, int user_team, int pool_size,
                         uint8_t out[ISSD_MGR_TEAMS]) {
    if (!out || pool_size < ISSD_MGR_TEAMS || pool_size > ISSD_MGR_POOL_TEAMS ||
        user_team < 0 || user_team >= pool_size)
        return false;
    uint8_t candidates[ISSD_MGR_POOL_TEAMS];
    int count = 0;
    for (int id = 0; id < pool_size; ++id)
        if (id != user_team) candidates[count++] = (uint8_t)id;
    Rng rng = { (uint64_t)seed * 0x2545F4914F6CDD1DULL + 0x1234567ULL };
    for (int i = 0; i < ISSD_MGR_TEAMS - 1; ++i) {
        int j = i + (int)rng_below(&rng, (uint32_t)(count - i));
        uint8_t t = candidates[i]; candidates[i] = candidates[j]; candidates[j] = t;
    }
    for (int i = 0; i < ISSD_MGR_TEAMS - 1; ++i) out[i] = candidates[i];
    out[ISSD_MGR_TEAMS - 1] = (uint8_t)user_team;
    /* Present them in select-screen order. */
    for (int i = 1; i < ISSD_MGR_TEAMS; ++i) {
        uint8_t v = out[i]; int j = i - 1;
        while (j >= 0 && out[j] > v) { out[j + 1] = out[j]; --j; }
        out[j + 1] = v;
    }
    return true;
}

/* ------------------------------------------------------------- schedule */

/* Circle method over a drawn order of the clubs: every club meets every other
 * once in 11 rounds, then the return leg repeats them with home and away
 * swapped, so each club ends with 11 home and 11 away games. The order of the
 * rounds inside each leg is drawn, and the return leg never opens with the
 * fixtures the first leg closed with. */
static void build_schedule(IssdManager *m, Rng *rng) {
    uint8_t perm[ISSD_MGR_TEAMS];
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) perm[i] = (uint8_t)i;
    shuffle(rng, perm, ISSD_MGR_TEAMS);

    const int legs = ISSD_MGR_TEAMS - 1;
    uint8_t leg[ISSD_MGR_TEAMS - 1][ISSD_MGR_FIXTURES][2];
    for (int r = 0; r < legs; ++r) {
        uint8_t fixed = perm[ISSD_MGR_TEAMS - 1], other = perm[r];
        if (r % 2 == 0) { leg[r][0][0] = other; leg[r][0][1] = fixed; }
        else            { leg[r][0][0] = fixed; leg[r][0][1] = other; }
        for (int k = 1; k < ISSD_MGR_TEAMS / 2; ++k) {
            uint8_t x = perm[(r + k) % legs];
            uint8_t y = perm[(r - k + legs) % legs];
            if (k % 2 == 1) { leg[r][k][0] = x; leg[r][k][1] = y; }
            else            { leg[r][k][0] = y; leg[r][k][1] = x; }
        }
    }

    uint8_t first[ISSD_MGR_TEAMS - 1], second[ISSD_MGR_TEAMS - 1];
    for (int i = 0; i < legs; ++i) first[i] = second[i] = (uint8_t)i;
    shuffle(rng, first, legs);
    shuffle(rng, second, legs);
    if (second[0] == first[legs - 1]) {
        uint8_t t = second[0]; second[0] = second[1]; second[1] = t;
    }

    for (int r = 0; r < legs; ++r) {
        for (int f = 0; f < ISSD_MGR_FIXTURES; ++f) {
            IssdMgrFixture *a = &m->fixtures[r][f];
            a->home = leg[first[r]][f][0];
            a->away = leg[first[r]][f][1];
            IssdMgrFixture *b = &m->fixtures[legs + r][f];
            b->home = leg[second[r]][f][1];
            b->away = leg[second[r]][f][0];
        }
    }
}

/* --------------------------------------------------------- free agents */

static const char *const kFreeAgentNames[] = {
    "Silva", "Costa", "Rossi", "Mueller", "Santos", "Garcia", "Novak", "Petrov",
    "Kim", "Okafor", "Diaz", "Moreau", "Lopez", "Haas", "Ito", "Duarte"
};

static uint8_t clamp_u8(int v, int lo, int hi) {
    return (uint8_t)(v < lo ? lo : v > hi ? hi : v);
}

static void make_free_agents(IssdManager *m, Rng *rng) {
    static const uint8_t outfield[3] = { 2, 4, 6 };
    /* Twelve distinct names out of sixteen. */
    uint8_t names[sizeof kFreeAgentNames / sizeof *kFreeAgentNames];
    for (unsigned i = 0; i < sizeof names; ++i) names[i] = (uint8_t)i;
    shuffle(rng, names, (int)sizeof names);
    for (int i = 0; i < ISSD_MGR_FREE_AGENTS; ++i) {
        IssdMgrPlayer *p = &m->free_agents[i];
        memset(p, 0, sizeof *p);
        bool keeper = i < 2;
        int q = 42 + (int)rng_below(rng, 24);
        snprintf(p->name, sizeof p->name, "%.8s", kFreeAgentNames[names[i]]);
        p->shirt_number = (uint8_t)(20 + i);
        p->position = keeper ? 1 : outfield[rng_below(rng, 3)];
        p->skin_tone = (uint8_t)rng_below(rng, 2);
        p->hair_style = keeper ? 0 : (uint8_t)rng_below(rng, 14);
        for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a)
            p->attr[a] = clamp_u8(q + (int)rng_below(rng, 21) - 10, 1, 99);
        p->attr[ISSD_ATTR_GOALKEEPING] = keeper ? clamp_u8(q + 20, 1, 99) : 10;
    }
}

/* ------------------------------------------------------------- creating */

int issd_mgr_slot_of_team(const IssdManager *m, int team_id) {
    if (!m) return -1;
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i)
        if (m->team_id[i] == team_id) return i;
    return -1;
}

bool issd_mgr_new(IssdManager *m, uint32_t seed,
                  const uint8_t team_ids[ISSD_MGR_TEAMS], int user_team) {
    if (!m || !team_ids) return false;
    bool seen[ISSD_MGR_POOL_TEAMS] = {0};
    int user_slot = -1;
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) {
        if (team_ids[i] >= ISSD_MGR_POOL_TEAMS || seen[team_ids[i]]) return false;
        seen[team_ids[i]] = true;
        if (team_ids[i] == user_team) user_slot = i;
    }
    if (user_slot < 0) return false;
    memset(m, 0, sizeof *m);
    m->seed = seed;
    memcpy(m->team_id, team_ids, ISSD_MGR_TEAMS);
    m->user_slot = (uint8_t)user_slot;
    m->budget = ISSD_MGR_START_BUDGET;
    Rng rng = { (uint64_t)seed * 0x9E3779B97F4A7C15ULL + 0xABCDEFULL };
    build_schedule(m, &rng);
    make_free_agents(m, &rng);
    return true;
}

/* A name the cartridge can print: A-Z a-z, space and a full stop. */
static void sanitise_name(char out[ISSD_MGR_NAME_LEN + 1], const char *in) {
    memset(out, 0, ISSD_MGR_NAME_LEN + 1);
    for (int i = 0; i < ISSD_MGR_NAME_LEN && in[i]; ++i) {
        char c = in[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '.';
        out[i] = ok ? c : ' ';
    }
}

static void sanitise_player(IssdMgrPlayer *out, const IssdMgrPlayer *in) {
    memset(out, 0, sizeof *out);
    out->shirt_number = clamp_u8(in->shirt_number, 1, 99);
    sanitise_name(out->name, in->name);
    out->position = in->position > 15 ? 4 : in->position;
    out->skin_tone = in->skin_tone > 1 ? 1 : in->skin_tone;
    /* Keepers use the cartridge's zero hairstyle convention. */
    out->hair_style = out->position == 1 ? 0 : (uint8_t)(in->hair_style & 0x0F);
    for (int a = 0; a < ISSD_ATTR_COUNT; ++a)
        out->attr[a] = in->attr[a] > 99 ? 99 : in->attr[a];
}

bool issd_mgr_set_squad(IssdManager *m, int slot, const IssdMgrPlayer *players,
                        int count) {
    if (!m || !players || slot < 0 || slot >= ISSD_MGR_TEAMS ||
        count != ISSD_MGR_SQUAD)
        return false;
    for (int i = 0; i < ISSD_MGR_SQUAD; ++i)
        sanitise_player(&m->squad[slot][i], &players[i]);
    m->has_squad[slot] = true;
    return true;
}

/* ------------------------------------------------------------- cartridge */

/* Layout of the USA cartridge's roster tables; see ISSDNative/issd_mod_rom.c
 * and tools/mod_studio/cartridge.py, which read the same bytes. */
#define ROM_ROSTER_PTRS   0x038138u
#define ROM_NAME_BASE     0x03818Eu   /* the pointer table's own first entry */
#define ROM_ATTR_BASE     0x050000u
#define ROM_NAME_BYTES    8u
#define ROM_ATTR_BYTES    7u
#define ROM_STOCK_TEAMS   36
#define NIBBLE_MIN        2u
#define NIBBLE_MAX        9u

/* The cartridge's own font. 0x00 is a space and doubles as padding. */
static char decode_char(uint8_t code) {
    if (code == 0x54) return '.';
    if (code >= 0x68 && code <= 0x81) return (char)('A' + (code - 0x68));
    if (code >= 0x82 && code <= 0x9B) return (char)('a' + (code - 0x82));
    return ' ';
}

/* The middle of the 0-99 band that quantises to this nibble, the inverse of
 * the loader's rating_to_nibble: 2 + (rating * 7 + 49) / 99, capped at 9. */
static uint8_t nibble_rating(unsigned nibble) {
    if (nibble < NIBBLE_MIN) nibble = NIBBLE_MIN;
    if (nibble > NIBBLE_MAX) nibble = NIBBLE_MAX;
    int lo = -1, hi = -1;
    for (int r = 0; r < 100; ++r) {
        unsigned n = NIBBLE_MIN + ((unsigned)r * (NIBBLE_MAX - NIBBLE_MIN) + 49u) / 99u;
        if (n > NIBBLE_MAX) n = NIBBLE_MAX;
        if (n == nibble) { if (lo < 0) lo = r; hi = r; }
    }
    return (uint8_t)((lo + hi) / 2);
}

bool issd_mgr_read_rom_squad(const uint8_t *rom, size_t size, int team,
                             IssdMgrPlayer out[ISSD_MGR_SQUAD]) {
    if (!rom || !out || team < 0 || team >= ROM_STOCK_TEAMS) return false;
    if (size < ROM_ATTR_BASE + (size_t)ROM_STOCK_TEAMS * ISSD_MGR_SQUAD * ROM_ATTR_BYTES)
        return false;
    /* The pointer table is followed, as the editor does, because a cartridge
     * a pack has been applied to keeps some squads elsewhere. */
    size_t names = ROM_NAME_BASE + (size_t)team * ISSD_MGR_SQUAD * ROM_NAME_BYTES;
    size_t ptr = ROM_ROSTER_PTRS + (size_t)team * 2;
    unsigned addr = rom[ptr] | ((unsigned)rom[ptr + 1] << 8);
    if (addr >= 0x8000 && 0x38000u + (addr - 0x8000u) + ISSD_MGR_SQUAD * ROM_NAME_BYTES <= size)
        names = 0x38000u + (addr - 0x8000u);
    /* Position 0 must read as a name, or this is another cartridge. */
    if (decode_char(rom[names]) == ' ' && rom[names] != 0x00) return false;
    {
        char first = decode_char(rom[ROM_NAME_BASE]);
        if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) return false;
    }
    for (int i = 0; i < ISSD_MGR_SQUAD; ++i) {
        IssdMgrPlayer *p = &out[i];
        memset(p, 0, sizeof *p);
        const uint8_t *raw = rom + names + (size_t)i * ROM_NAME_BYTES;
        int end = 0;
        for (int k = 0; k < (int)ROM_NAME_BYTES; ++k) {
            p->name[k] = decode_char(raw[k]);
            if (p->name[k] != ' ') end = k + 1;
        }
        p->name[end] = 0;            /* trailing padding goes, leading stays */
        const uint8_t *b = rom + ROM_ATTR_BASE +
                           ((size_t)team * ISSD_MGR_SQUAD + (size_t)i) * ROM_ATTR_BYTES;
        p->shirt_number = (uint8_t)(i + 1);
        p->position = b[4] >> 4;
        p->skin_tone = b[6] >> 4;
        p->hair_style = b[6] & 0x0F;
        p->attr[ISSD_ATTR_ACCELERATION] = nibble_rating(b[0] >> 4);
        p->attr[ISSD_ATTR_SPEED]        = nibble_rating(b[0] & 0x0F);
        p->attr[ISSD_ATTR_SHOOTING]     = nibble_rating(b[1] >> 4);
        p->attr[ISSD_ATTR_TECHNIQUE]    = nibble_rating(b[1] & 0x0F);
        p->attr[ISSD_ATTR_BALANCE]      = nibble_rating(b[2] >> 4);
        p->attr[ISSD_ATTR_INTELLIGENCE] = nibble_rating(b[2] & 0x0F);
        p->attr[ISSD_ATTR_DRIBBLING]    = nibble_rating(b[3] >> 4);
        p->attr[ISSD_ATTR_JUMPING]      = nibble_rating(b[3] & 0x0F);
        p->attr[ISSD_ATTR_STAMINA]      = nibble_rating(b[4] & 0x0F);
        p->attr[ISSD_ATTR_GOALKEEPING]  = p->position == 1
            ? (uint8_t)((p->attr[ISSD_ATTR_JUMPING] + p->attr[ISSD_ATTR_BALANCE] +
                         p->attr[ISSD_ATTR_INTELLIGENCE]) / 3)
            : 10;
        if (p->position > 15 || p->skin_tone > 1) return false;
    }
    return true;
}

/* The nibble the loader would store for a 0-99 rating. */
static unsigned rating_nibble(unsigned rating) {
    if (rating > 99) rating = 99;
    unsigned n = NIBBLE_MIN + (rating * (NIBBLE_MAX - NIBBLE_MIN) + 49u) / 99u;
    return n > NIBBLE_MAX ? NIBBLE_MAX : n;
}

bool issd_mgr_squads_match_rom(const IssdManager *m, const uint8_t *rom, size_t size) {
    if (!m || !rom) return false;
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        if (!m->has_squad[s]) continue;
        IssdMgrPlayer back[ISSD_MGR_SQUAD];
        if (!issd_mgr_read_rom_squad(rom, size, m->team_id[s], back)) return false;
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i) {
            const IssdMgrPlayer *a = &m->squad[s][i], *b = &back[i];
            if (strcmp(a->name, b->name) != 0 || a->position != b->position ||
                a->skin_tone != b->skin_tone || a->hair_style != b->hair_style ||
                a->shirt_number != b->shirt_number)
                return false;
            for (int k = 0; k < ISSD_ATTR_GOALKEEPING; ++k)
                if (rating_nibble(a->attr[k]) != rating_nibble(b->attr[k])) return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------- strength */

/* The cartridge stores nine attributes and no goalkeeping rating, so a keeper
 * is rated like everyone else on what is there. */
int issd_mgr_player_rating(const IssdMgrPlayer *p) {
    if (!p) return 0;
    int sum = 0;
    for (int a = 0; a < ISSD_ATTR_GOALKEEPING; ++a) sum += p->attr[a];
    return sum / ISSD_ATTR_GOALKEEPING;
}

int issd_mgr_team_strength(const IssdManager *m, int slot) {
    if (!m || slot < 0 || slot >= ISSD_MGR_TEAMS || !m->has_squad[slot]) return 50;
    const IssdMgrPlayer *s = m->squad[slot];
    int starters = 2 * issd_mgr_player_rating(&s[0]);
    for (int i = 1; i <= 10; ++i) starters += issd_mgr_player_rating(&s[i]);
    int bench = 0;
    for (int i = 12; i < ISSD_MGR_SQUAD; ++i) bench += issd_mgr_player_rating(&s[i]);
    /* Starting eleven weighs 85%, the eight outfield substitutes 15%. */
    return (starters * 85 / 12 + bench * 15 / 8 + 50) / 100;
}

/* ----------------------------------------------------------- simulation */

/* Each rating point of difference scales a side's scoring by 2%. Measured
 * over 200 seasons with the whole squad shifted: +5 points wins the league
 * about a third of the time, +10 about half, +20 nine times in ten, against
 * one in seven at equal strength. */
#define GROWTH_PERCENT 102

/* 1000 * 1.02^d without floating point, so results never depend on the
 * platform's maths library. */
static int growth_milli(int d) {
    int v = 1000;
    if (d >= 0) for (int i = 0; i < d; ++i) v = v * GROWTH_PERCENT / 100;
    else        for (int i = 0; i < -d; ++i) v = v * 100 / GROWTH_PERCENT;
    return v;
}

/* Ten chances, each converting with the same probability: goals average
 * lam_milli / 1000 and the spread resembles a short match. */
static int goals_for(Rng *r, int lam_milli) {
    int p = lam_milli / 10;
    if (p > 900) p = 900;
    int goals = 0;
    for (int i = 0; i < 10; ++i)
        if ((int)rng_below(r, 1000) < p) ++goals;
    return goals;
}

static void simulate_fixture(const IssdManager *m, int round, int index,
                             uint8_t *home_goals, uint8_t *away_goals) {
    const IssdMgrFixture *f = &m->fixtures[round][index];
    int diff = issd_mgr_team_strength(m, f->home) - issd_mgr_team_strength(m, f->away) + 3;
    if (diff > 40) diff = 40;
    if (diff < -40) diff = -40;
    Rng rng = { (uint64_t)m->seed ^ ((uint64_t)round << 40) ^ ((uint64_t)index << 32) ^ 0xA5A5ULL };
    rng_next(&rng);
    *home_goals = (uint8_t)goals_for(&rng, 1300 * growth_milli(diff) / 1000);
    *away_goals = (uint8_t)goals_for(&rng, 1300 * growth_milli(-diff) / 1000);
}

/* ---------------------------------------------------------------- rounds */

bool issd_mgr_finished(const IssdManager *m) {
    return !m || m->round >= ISSD_MGR_ROUNDS;
}

static int user_fixture_index(const IssdManager *m, int round) {
    for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
        const IssdMgrFixture *f = &m->fixtures[round][i];
        if (f->home == m->user_slot || f->away == m->user_slot) return i;
    }
    return -1;
}

bool issd_mgr_next_user_fixture(const IssdManager *m, IssdMgrFixture *fixture,
                                uint8_t *home_team_id, uint8_t *away_team_id) {
    if (issd_mgr_finished(m)) return false;
    int i = user_fixture_index(m, m->round);
    if (i < 0) return false;
    const IssdMgrFixture *f = &m->fixtures[m->round][i];
    if (fixture) *fixture = *f;
    if (home_team_id) *home_team_id = m->team_id[f->home];
    if (away_team_id) *away_team_id = m->team_id[f->away];
    return true;
}

/* Settle every fixture of the current round and move on. When
 * use_user_result is false the user's match is simulated like the rest. */
static bool finish_round(IssdManager *m, bool use_user_result,
                         int user_home_goals, int user_away_goals) {
    if (issd_mgr_finished(m)) return false;
    int ui = user_fixture_index(m, m->round);
    if (ui < 0) return false;
    for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
        IssdMgrFixture *f = &m->fixtures[m->round][i];
        if (i == ui && use_user_result) {
            f->home_goals = (uint8_t)user_home_goals;
            f->away_goals = (uint8_t)user_away_goals;
        } else {
            simulate_fixture(m, m->round, i, &f->home_goals, &f->away_goals);
        }
        f->played = true;
    }
    const IssdMgrFixture *u = &m->fixtures[m->round][ui];
    bool user_home = u->home == m->user_slot;
    int mine = user_home ? u->home_goals : u->away_goals;
    int theirs = user_home ? u->away_goals : u->home_goals;
    m->budget += ISSD_MGR_INCOME_PER_ROUND + (mine > theirs ? ISSD_MGR_WIN_BONUS : 0);
    m->round++;
    m->transfers_used = 0;
    return true;
}

bool issd_mgr_record_user_result(IssdManager *m, int home_goals, int away_goals) {
    if (!m || home_goals < 0 || away_goals < 0 ||
        home_goals > ISSD_MGR_MAX_GOALS || away_goals > ISSD_MGR_MAX_GOALS)
        return false;
    return finish_round(m, true, home_goals, away_goals);
}

bool issd_mgr_sim_user_round(IssdManager *m) {
    return m && finish_round(m, false, 0, 0);
}

/* ---------------------------------------------------------------- table */

static bool ranks_before(const IssdMgrStanding *a, const IssdMgrStanding *b) {
    if (a->points != b->points) return a->points > b->points;
    int gda = (int)a->goals_for - (int)a->goals_against;
    int gdb = (int)b->goals_for - (int)b->goals_against;
    if (gda != gdb) return gda > gdb;
    if (a->goals_for != b->goals_for) return a->goals_for > b->goals_for;
    return a->slot < b->slot;
}

void issd_mgr_table(const IssdManager *m, IssdMgrStanding out[ISSD_MGR_TEAMS]) {
    if (!m || !out) return;
    memset(out, 0, sizeof(IssdMgrStanding) * ISSD_MGR_TEAMS);
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) out[i].slot = (uint8_t)i;
    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r) {
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
            const IssdMgrFixture *f = &m->fixtures[r][i];
            if (!f->played) continue;
            IssdMgrStanding *h = &out[f->home], *a = &out[f->away];
            h->played++; a->played++;
            h->goals_for += f->home_goals; h->goals_against += f->away_goals;
            a->goals_for += f->away_goals; a->goals_against += f->home_goals;
            if (f->home_goals > f->away_goals) { h->won++; a->lost++; h->points += 3; }
            else if (f->home_goals < f->away_goals) { a->won++; h->lost++; a->points += 3; }
            else { h->drawn++; a->drawn++; h->points++; a->points++; }
        }
    }
    for (int i = 1; i < ISSD_MGR_TEAMS; ++i) {
        IssdMgrStanding v = out[i]; int j = i - 1;
        while (j >= 0 && ranks_before(&v, &out[j])) { out[j + 1] = out[j]; --j; }
        out[j + 1] = v;
    }
}

/* ------------------------------------------------------------ transfers */

int issd_mgr_player_value(const IssdMgrPlayer *p) {
    int r = issd_mgr_player_rating(p);
    int v = r > 35 ? ((r - 35) * (r - 35)) / 8 : 5;
    return v < 5 ? 5 : v;
}

static bool transfer_ready(const IssdManager *m, int target_slot) {
    return m && target_slot >= 0 && target_slot < ISSD_MGR_TEAMS &&
           target_slot != m->user_slot && m->has_squad[m->user_slot] &&
           m->has_squad[target_slot];
}

int issd_mgr_swap_price(const IssdManager *m, int target_slot, int target_index,
                        int own_index) {
    if (!transfer_ready(m, target_slot) || target_index < 0 || target_index >= ISSD_MGR_SQUAD ||
        own_index < 0 || own_index >= ISSD_MGR_SQUAD)
        return INT_MAX;
    int want = issd_mgr_player_value(&m->squad[target_slot][target_index]);
    int give = issd_mgr_player_value(&m->squad[m->user_slot][own_index]);
    /* The other club asks for a 15% margin over its man's value. */
    return (want * 115 + 99) / 100 - give;
}

static bool is_keeper(const IssdMgrPlayer *p) { return p->position == 1; }

IssdMgrTransferResult issd_mgr_offer_swap(IssdManager *m, int target_slot,
                                          int target_index, int own_index,
                                          int cash) {
    if (!m) return ISSD_MGR_TR_INVALID;
    if (issd_mgr_finished(m)) return ISSD_MGR_TR_SEASON_OVER;
    if (!transfer_ready(m, target_slot) || target_index < 0 || target_index >= ISSD_MGR_SQUAD ||
        own_index < 0 || own_index >= ISSD_MGR_SQUAD)
        return ISSD_MGR_TR_INVALID;
    if (m->transfers_used >= ISSD_MGR_TRANSFERS_PER_ROUND) return ISSD_MGR_TR_LIMIT;
    IssdMgrPlayer *own = &m->squad[m->user_slot][own_index];
    IssdMgrPlayer *target = &m->squad[target_slot][target_index];
    if (is_keeper(own) != is_keeper(target)) return ISSD_MGR_TR_POSITION;
    if (cash < issd_mgr_swap_price(m, target_slot, target_index, own_index))
        return ISSD_MGR_TR_REFUSED;
    if (cash > m->budget) return ISSD_MGR_TR_FUNDS;
    IssdMgrPlayer coming = *target, going = *own;
    coming.shirt_number = own->shirt_number;
    going.shirt_number = target->shirt_number;
    *own = coming;
    *target = going;
    m->budget -= cash;
    m->transfers_used++;
    return ISSD_MGR_TR_OK;
}

int issd_mgr_free_agent_fee(const IssdManager *m, int agent_index) {
    if (!m || agent_index < 0 || agent_index >= ISSD_MGR_FREE_AGENTS) return INT_MAX;
    int fee = issd_mgr_player_value(&m->free_agents[agent_index]) / 2;
    return fee < 1 ? 1 : fee;
}

IssdMgrTransferResult issd_mgr_sign_free_agent(IssdManager *m, int agent_index,
                                               int own_index) {
    if (!m) return ISSD_MGR_TR_INVALID;
    if (issd_mgr_finished(m)) return ISSD_MGR_TR_SEASON_OVER;
    if (agent_index < 0 || agent_index >= ISSD_MGR_FREE_AGENTS || own_index < 0 ||
        own_index >= ISSD_MGR_SQUAD || !m->has_squad[m->user_slot])
        return ISSD_MGR_TR_INVALID;
    if (m->transfers_used >= ISSD_MGR_TRANSFERS_PER_ROUND) return ISSD_MGR_TR_LIMIT;
    IssdMgrPlayer *own = &m->squad[m->user_slot][own_index];
    IssdMgrPlayer *agent = &m->free_agents[agent_index];
    if (is_keeper(own) != is_keeper(agent)) return ISSD_MGR_TR_POSITION;
    int fee = issd_mgr_free_agent_fee(m, agent_index);
    if (fee > m->budget) return ISSD_MGR_TR_FUNDS;
    IssdMgrPlayer signed_player = *agent, released = *own;
    signed_player.shirt_number = own->shirt_number;
    *own = signed_player;
    *agent = released;
    m->budget -= fee;
    m->transfers_used++;
    return ISSD_MGR_TR_OK;
}

/* ----------------------------------------------------------- validation */

static bool valid_player(const IssdMgrPlayer *p) {
    if (p->shirt_number < 1 || p->shirt_number > 99 || p->position > 15 ||
        p->skin_tone > 1 || p->hair_style > 15 || p->name[ISSD_MGR_NAME_LEN] != 0)
        return false;
    for (int i = 0; i < ISSD_MGR_NAME_LEN; ++i) {
        char c = p->name[i];
        if (c == 0) {
            for (int j = i; j < ISSD_MGR_NAME_LEN; ++j) if (p->name[j]) return false;
            break;
        }
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '.';
        if (!ok) return false;
    }
    for (int a = 0; a < ISSD_ATTR_COUNT; ++a) if (p->attr[a] > 99) return false;
    return true;
}

static bool valid_manager(const IssdManager *m) {
    if (m->user_slot >= ISSD_MGR_TEAMS || m->round > ISSD_MGR_ROUNDS ||
        m->transfers_used > ISSD_MGR_TRANSFERS_PER_ROUND ||
        m->budget < 0 || m->budget > 100000000)
        return false;
    bool seen_team[ISSD_MGR_POOL_TEAMS] = {0};
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) {
        if (m->team_id[i] >= ISSD_MGR_POOL_TEAMS || seen_team[m->team_id[i]]) return false;
        seen_team[m->team_id[i]] = true;
    }
    int meetings[ISSD_MGR_TEAMS][ISSD_MGR_TEAMS];
    memset(meetings, 0, sizeof meetings);
    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r) {
        bool used[ISSD_MGR_TEAMS] = {0};
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
            const IssdMgrFixture *f = &m->fixtures[r][i];
            if (f->home >= ISSD_MGR_TEAMS || f->away >= ISSD_MGR_TEAMS || f->home == f->away ||
                used[f->home] || used[f->away])
                return false;
            used[f->home] = used[f->away] = true;
            if (f->played != (r < m->round)) return false;
            if (f->home_goals > ISSD_MGR_MAX_GOALS || f->away_goals > ISSD_MGR_MAX_GOALS) return false;
            if (!f->played && (f->home_goals || f->away_goals)) return false;
            meetings[f->home][f->away]++;
        }
    }
    for (int a = 0; a < ISSD_MGR_TEAMS; ++a)
        for (int b = 0; b < ISSD_MGR_TEAMS; ++b)
            if (meetings[a][b] != (a == b ? 0 : 1)) return false;
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        if (!m->has_squad[s]) continue;
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i)
            if (!valid_player(&m->squad[s][i])) return false;
    }
    for (int i = 0; i < ISSD_MGR_FREE_AGENTS; ++i)
        if (!valid_player(&m->free_agents[i])) return false;
    return true;
}

/* ----------------------------------------------------------- save / load */

#define MGR_MAGIC   0x474D5349u /* "ISMG" little endian */
#define MGR_VERSION 1u
#define MGR_BUFFER  8192

typedef struct { uint8_t *p; size_t n, cap; bool ok; } Writer;
typedef struct { const uint8_t *p; size_t n, pos; bool ok; } Reader;

static void w8(Writer *w, unsigned v) {
    if (w->n >= w->cap) { w->ok = false; return; }
    w->p[w->n++] = (uint8_t)v;
}
static void w16(Writer *w, unsigned v) { w8(w, v & 0xFF); w8(w, (v >> 8) & 0xFF); }
static void w32(Writer *w, uint32_t v) { w16(w, v & 0xFFFF); w16(w, v >> 16); }

static unsigned r8(Reader *r) {
    if (r->pos >= r->n) { r->ok = false; return 0; }
    return r->p[r->pos++];
}
static unsigned r16(Reader *r) { unsigned lo = r8(r); return lo | (r8(r) << 8); }
static uint32_t r32(Reader *r) { uint32_t lo = r16(r); return lo | ((uint32_t)r16(r) << 16); }

static uint32_t crc32(const uint8_t *data, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static void write_player(Writer *w, const IssdMgrPlayer *p) {
    w8(w, p->shirt_number);
    for (int i = 0; i <= ISSD_MGR_NAME_LEN; ++i) w8(w, (uint8_t)p->name[i]);
    w8(w, p->position); w8(w, p->skin_tone); w8(w, p->hair_style);
    for (int a = 0; a < ISSD_ATTR_COUNT; ++a) w8(w, p->attr[a]);
}

static void read_player(Reader *r, IssdMgrPlayer *p) {
    p->shirt_number = (uint8_t)r8(r);
    for (int i = 0; i <= ISSD_MGR_NAME_LEN; ++i) p->name[i] = (char)r8(r);
    p->position = (uint8_t)r8(r); p->skin_tone = (uint8_t)r8(r); p->hair_style = (uint8_t)r8(r);
    for (int a = 0; a < ISSD_ATTR_COUNT; ++a) p->attr[a] = (uint8_t)r8(r);
}

/* Replace `path` with the finished temporary file. */
static bool commit_file(const char *tmp, const char *path) {
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

static bool temp_name(char *out, size_t cap, const char *path) {
    int n = snprintf(out, cap, "%s.tmp", path);
    return n > 0 && (size_t)n < cap;
}

bool issd_mgr_save(const IssdManager *m, const char *path) {
    if (!m || !path || !valid_manager(m)) return false;
    uint8_t *buffer = (uint8_t *)malloc(MGR_BUFFER);
    if (!buffer) return false;
    Writer w = { buffer, 0, MGR_BUFFER - 4, true };
    w32(&w, MGR_MAGIC); w32(&w, MGR_VERSION); w32(&w, m->seed);
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) w8(&w, m->team_id[i]);
    w8(&w, m->user_slot); w8(&w, m->round); w8(&w, m->transfers_used);
    w32(&w, (uint32_t)m->budget);
    for (int r = 0; r < ISSD_MGR_ROUNDS; ++r)
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
            const IssdMgrFixture *f = &m->fixtures[r][i];
            w8(&w, f->home); w8(&w, f->away); w8(&w, f->played ? 1 : 0);
            w8(&w, f->home_goals); w8(&w, f->away_goals);
        }
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) w8(&w, m->has_squad[s] ? 1 : 0);
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s)
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i) write_player(&w, &m->squad[s][i]);
    for (int i = 0; i < ISSD_MGR_FREE_AGENTS; ++i) write_player(&w, &m->free_agents[i]);
    bool ok = w.ok;
    if (ok) {
        uint32_t crc = crc32(buffer, w.n);
        Writer tail = { buffer + w.n, 0, 4, true };
        w32(&tail, crc);
    }
    char tmp[1024];
    if (ok) ok = temp_name(tmp, sizeof tmp, path);
    if (ok) {
        FILE *f = fopen(tmp, "wb");
        ok = f != NULL;
        if (f) {
            ok = fwrite(buffer, 1, w.n + 4, f) == w.n + 4;
            if (fclose(f) != 0) ok = false;
            if (!ok) remove(tmp);
        }
    }
    free(buffer);
    return ok && commit_file(tmp, path);
}

bool issd_mgr_load(IssdManager *m, const char *path) {
    if (!m || !path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t *buffer = (uint8_t *)malloc(MGR_BUFFER + 1);
    if (!buffer) { fclose(f); return false; }
    size_t n = fread(buffer, 1, MGR_BUFFER + 1, f);
    fclose(f);
    bool ok = n > 8 && n <= MGR_BUFFER;
    if (ok) {
        Reader tail = { buffer + n - 4, 4, 0, true };
        ok = r32(&tail) == crc32(buffer, n - 4);
    }
    IssdManager loaded;
    if (ok) {
        memset(&loaded, 0, sizeof loaded);
        Reader r = { buffer, n - 4, 0, true };
        ok = r32(&r) == MGR_MAGIC && r32(&r) == MGR_VERSION;
        loaded.seed = r32(&r);
        for (int i = 0; i < ISSD_MGR_TEAMS; ++i) loaded.team_id[i] = (uint8_t)r8(&r);
        loaded.user_slot = (uint8_t)r8(&r); loaded.round = (uint8_t)r8(&r);
        loaded.transfers_used = (uint8_t)r8(&r);
        loaded.budget = (int32_t)r32(&r);
        for (int rd = 0; rd < ISSD_MGR_ROUNDS; ++rd)
            for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
                IssdMgrFixture *fx = &loaded.fixtures[rd][i];
                fx->home = (uint8_t)r8(&r); fx->away = (uint8_t)r8(&r);
                unsigned played = r8(&r);
                if (played > 1) ok = false;
                fx->played = played == 1;
                fx->home_goals = (uint8_t)r8(&r); fx->away_goals = (uint8_t)r8(&r);
            }
        for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
            unsigned has = r8(&r);
            if (has > 1) ok = false;
            loaded.has_squad[s] = has == 1;
        }
        for (int s = 0; s < ISSD_MGR_TEAMS; ++s)
            for (int i = 0; i < ISSD_MGR_SQUAD; ++i) read_player(&r, &loaded.squad[s][i]);
        for (int i = 0; i < ISSD_MGR_FREE_AGENTS; ++i) read_player(&r, &loaded.free_agents[i]);
        ok = ok && r.ok && r.pos == r.n && valid_manager(&loaded);
    }
    free(buffer);
    if (!ok) return false;
    *m = loaded;
    return true;
}

/* ----------------------------------------------------------------- pack */

/* The four names the pack schema offers; the in-between codes (3, 5 and the
 * rest) are written as their number so a squad read from the cartridge and
 * written back is unchanged. Always a string, which is what the loader reads. */
static const char *position_text(uint8_t code, char buf[4]) {
    switch (code) {
    case 1: return "GK";
    case 2: return "DF";
    case 4: return "MF";
    case 6: return "FW";
    default: snprintf(buf, 4, "%u", (unsigned)code); return buf;
    }
}

static const char *const kAttrKeys[ISSD_ATTR_COUNT] = {
    "acceleration", "speed", "shooting", "technique", "balance",
    "intelligence", "dribbling", "jumping", "stamina", "goalkeeping"
};

bool issd_mgr_export_pack(const IssdManager *m, const char *path) {
    if (!m || !path) return false;
    char tmp[1024];
    if (!temp_name(tmp, sizeof tmp, path)) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    fprintf(f, "{\n  \"name\": \"ISSD Manager Career\",\n  \"author\": \"ISSD Manager\",\n"
               "  \"version\": \"1\",\n"
               "  \"description\": \"Squads of the current manager career. Regenerated by the game.\",\n"
               "  \"teams\": [\n");
    bool first_team = true;
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        if (!m->has_squad[s]) continue;
        fprintf(f, "%s    {\n      \"team_id\": %d,\n      \"players\": [\n",
                first_team ? "" : ",\n", m->team_id[s]);
        first_team = false;
        for (int i = 0; i < ISSD_MGR_SQUAD; ++i) {
            const IssdMgrPlayer *p = &m->squad[s][i];
            char code[4];
            fprintf(f, "        {\"shirt_number\": %d, \"name\": \"%s\", \"position\": \"%s\", "
                       "\"skin_tone\": %d, \"hair_style\": %d",
                    p->shirt_number, p->name, position_text(p->position, code),
                    p->skin_tone, p->hair_style);
            for (int a = 0; a < ISSD_ATTR_COUNT; ++a)
                fprintf(f, ", \"%s\": %d", kAttrKeys[a], p->attr[a]);
            fprintf(f, "}%s\n", i + 1 < ISSD_MGR_SQUAD ? "," : "");
        }
        fprintf(f, "      ]\n    }");
    }
    fprintf(f, "\n  ]\n}\n");
    bool ok = ferror(f) == 0;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    return commit_file(tmp, path);
}
