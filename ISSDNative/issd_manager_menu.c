#include "issd_manager_menu.h"
#include "issd_manager_link.h"

#include "issd_config.h"
#include "issd_manager.h"
#include "issd_menu.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define VISIBLE     13
#define ROW_HEIGHT  12
#define LIST_TOP    22
#define LINE_CHARS  28
#define OFFER_FIRST_ROW 5
#define PACK_NAME   "ISSD Manager Career"
#define PACK_FILE   "manager_career.json"

typedef enum {
    S_HUB, S_TEAMS, S_TABLE, S_CALENDAR, S_SQUAD,
    S_CLUBS, S_THEIRS, S_OWN, S_OFFER, S_AGENTS, S_AGENT_OWN
} Screen;

enum {
    H_PLAY, H_SIM, H_TABLE, H_CALENDAR, H_SQUAD, H_SWAP, H_AGENTS,
    H_APPLY, H_NEW, H_CONTINUE, H_BACK, HUB_ROWS
};

static IssdManagerHost host;
static uint32_t forced_seed;
static IssdManager mgr;
static bool have_league;
static bool career_file;          /* a career file exists on disk */
static bool squads_applied;       /* the running cartridge already has the squads */
static bool confirm_replace;      /* the next "New league" press replaces a career */
static Screen screen;
static int hub_cursor;
static int picked_club, picked_theirs, picked_own, picked_agent;
static int calendar_round;
static bool calendar_mine;
static int stock_strength[ISSD_MGR_POOL_TEAMS];
static bool stock_strength_ready;
static bool career_probed;
static bool hub_cursor_set;       /* the player has moved the hub cursor */
static int pending_round = -1;    /* round of the match handed to the game */

/* ------------------------------------------------------------- helpers */

void issd_manager_menu_set_host(const IssdManagerHost *h) {
    if (h) host = *h; else memset(&host, 0, sizeof host);
    have_league = false;
    career_file = false;
    squads_applied = false;
    confirm_replace = false;
    stock_strength_ready = false;
    career_probed = false;
    hub_cursor_set = false;
}

void issd_manager_menu_set_seed(uint32_t seed) { forced_seed = seed; }

static void say(const char *message, int frames) { issd_menu_notify(message, frames); }

static const char *club_name(int team) {
    const char *name = host.team_name ? host.team_name(team) : NULL;
    return name ? name : "?";
}

static const char *slot_name(int slot) { return club_name(mgr.team_id[slot]); }

static const char *position_text(unsigned code, char buf[4]) {
    switch (code) {
    case 1: return "GK";
    case 2: return "DF";
    case 3: return "DM";   /* the cartridge's in-between codes: defence/midfield */
    case 4: return "MF";
    case 5: return "AM";   /* and midfield/attack */
    case 6: return "FW";
    default: snprintf(buf, 4, "%u", code % 100u); return buf;
    }
}

static void fit(char *line) { line[LINE_CHARS] = '\0'; }

static int rank_of(int slot) {
    IssdMgrStanding table[ISSD_MGR_TEAMS];
    issd_mgr_table(&mgr, table);
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i)
        if (table[i].slot == slot) return i + 1;
    return 0;
}

static bool career_path(char *out, size_t cap, bool create) {
    return host.career_path && host.career_path(out, cap, create);
}

static bool file_exists(void) {
    char path[1024];
    if (!career_path(path, sizeof path, false)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static void refresh_state(void) {
    career_file = file_exists();
    career_probed = true;
    squads_applied = false;
    if (have_league && host.applied_rom) {
        size_t size = 0;
        const uint8_t *rom = host.applied_rom(&size);
        squads_applied = rom && issd_mgr_squads_match_rom(&mgr, rom, size);
    }
}

static bool autosave(void) {
    char path[1024];
    if (!career_path(path, sizeof path, true) || !issd_mgr_save(&mgr, path)) {
        say("Career could not be saved", 240);
        return false;
    }
    return true;
}

static void go(Screen next, int cursor) {
    screen = next;
    confirm_replace = false;
    g_overlay_menu.current_item = cursor;
    g_overlay_menu.scroll = 0;
    if (cursor >= VISIBLE) g_overlay_menu.scroll = cursor - VISIBLE + 1;
}

static int deals_left(void) {
    int left = ISSD_MGR_TRANSFERS_PER_ROUND - mgr.transfers_used;
    return left < 0 ? 0 : left;
}

/* Strength of each stock team, read once from the cartridge for the picker. */
static void load_stock_strength(void) {
    if (stock_strength_ready) return;
    size_t size = 0;
    const uint8_t *rom = host.base_rom ? host.base_rom(&size) : NULL;
    static IssdManager scratch;
    uint8_t ids[ISSD_MGR_TEAMS];
    for (int i = 0; i < ISSD_MGR_TEAMS; ++i) ids[i] = (uint8_t)i;
    if (!rom || !issd_mgr_new(&scratch, 1, ids, 0)) return;
    for (int t = 0; t < ISSD_MGR_POOL_TEAMS; ++t) {
        IssdMgrPlayer squad[ISSD_MGR_SQUAD];
        stock_strength[t] = 0;
        if (issd_mgr_read_rom_squad(rom, size, t, squad) &&
            issd_mgr_set_squad(&scratch, 0, squad, ISSD_MGR_SQUAD))
            stock_strength[t] = issd_mgr_team_strength(&scratch, 0);
    }
    stock_strength_ready = true;
}

/* ------------------------------------------------------------- actions */

static bool start_league(int team) {
    size_t size = 0;
    const uint8_t *rom = host.base_rom ? host.base_rom(&size) : NULL;
    if (!rom) { say("No cartridge available", 240); return false; }
    uint32_t seed = forced_seed ? forced_seed : (uint32_t)time(NULL) * 2654435761u + (uint32_t)clock();
    uint8_t ids[ISSD_MGR_TEAMS];
    static IssdManager fresh;
    if (!issd_mgr_draw_teams(seed, team, ISSD_MGR_POOL_TEAMS, ids) ||
        !issd_mgr_new(&fresh, seed, ids, team)) {
        say("League could not be drawn", 240);
        return false;
    }
    for (int s = 0; s < ISSD_MGR_TEAMS; ++s) {
        IssdMgrPlayer squad[ISSD_MGR_SQUAD];
        if (!issd_mgr_read_rom_squad(rom, size, fresh.team_id[s], squad) ||
            !issd_mgr_set_squad(&fresh, s, squad, ISSD_MGR_SQUAD)) {
            say("Cartridge squads unreadable", 240);
            return false;
        }
    }
    mgr = fresh;
    have_league = true;
    autosave();
    refresh_state();
    return true;
}

static void continue_career(void) {
    char path[1024];
    static IssdManager loaded;
    if (!career_path(path, sizeof path, false) || !issd_mgr_load(&loaded, path)) {
        say("No usable career file", 240);
        return;
    }
    mgr = loaded;
    have_league = true;
    refresh_state();
    say("Career loaded", 150);
}

static void simulate_round(void) {
    if (!have_league || issd_mgr_finished(&mgr)) { say("The season is over", 180); return; }
    int round = mgr.round;
    if (!issd_mgr_sim_user_round(&mgr)) { say("Round could not be played", 180); return; }
    autosave();
    for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
        const IssdMgrFixture *f = &mgr.fixtures[round][i];
        if (f->home != mgr.user_slot && f->away != mgr.user_slot) continue;
        char line[96];
        snprintf(line, sizeof line, "R%d: %.10s %d-%d %.10s", round + 1,
                 slot_name(f->home), f->home_goals, f->away_goals, slot_name(f->away));
        say(line, 240);
    }
}

/* Hand the user's next fixture to the game: the link walks the original
 * Open Game menus, writes the two teams and reads the score at full time. */
static void play_my_match(void) {
    IssdMgrFixture fixture;
    uint8_t home, away;
    if (!have_league || !issd_mgr_next_user_fixture(&mgr, &fixture, &home, &away)) {
        say("The season is over", 180);
        return;
    }
    int own = mgr.team_id[mgr.user_slot];
    int other = fixture.home == mgr.user_slot ? away : home;
    if (own >= ISSD_MGR_POOL_TEAMS || other >= ISSD_MGR_POOL_TEAMS ||
        own > 35 || other > 35) {
        say("Added teams cannot play here yet", 240);
        return;
    }
    if (host.to_main_menu && !host.to_main_menu()) {
        say("Open the game's main menu first", 240);
        return;
    }
    pending_round = mgr.round;
    issd_link_arm(own, other);
    char line[96];
    snprintf(line, sizeof line, "R%d: you vs %.12s - starting", mgr.round + 1, club_name(other));
    say(line, 300);
    issd_menu_close();
}

bool issd_manager_menu_play_now(void) {
    if (!have_league) continue_career();
    if (!have_league) return false;
    play_my_match();
    return issd_link_armed();
}

bool issd_manager_menu_collect_match(void) {
    int own_goals, other_goals;
    if (!issd_link_take_result(&own_goals, &other_goals)) return false;
    if (!have_league || mgr.round != pending_round || issd_mgr_finished(&mgr)) {
        say("Match result not counted", 240);
        return false;
    }
    IssdMgrFixture fixture;
    uint8_t home, away;
    if (!issd_mgr_next_user_fixture(&mgr, &fixture, &home, &away)) return false;
    bool user_home = fixture.home == mgr.user_slot;
    int round = mgr.round;
    if (!issd_mgr_record_user_result(&mgr, user_home ? own_goals : other_goals,
                                     user_home ? other_goals : own_goals)) {
        say("Match result not counted", 240);
        return false;
    }
    autosave();
    refresh_state();
    char line[96];
    snprintf(line, sizeof line, "R%d: %d-%d  Pos #%d", round + 1, own_goals, other_goals,
             rank_of(mgr.user_slot));
    say(line, 360);
    pending_round = -1;
    return true;
}

static bool list_has_pack(const char *list, const char *name) {
    size_t len = strlen(name);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, '|');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == len && strncmp(p, name, len) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

/* Roster packs only apply at boot: write the career pack, make sure it is on,
 * and restart, the way the Mods page does. */
static void apply_and_restart(void) {
    if (!have_league) return;
    const char *dir = host.mods_dir ? host.mods_dir() : NULL;
    char path[1100];
    if (!dir || !dir[0] || snprintf(path, sizeof path, "%s/%s", dir, PACK_FILE) >= (int)sizeof path) {
        say("Mods folder unavailable", 240);
        return;
    }
    if (!autosave()) return;
    if (!issd_mgr_export_pack(&mgr, path)) { say("Pack could not be written", 240); return; }
    char *list = g_issd_config.active_mod_packs;
    size_t cap = sizeof g_issd_config.active_mod_packs, used = strlen(list);
    if (!list_has_pack(list, PACK_NAME)) {
        size_t need = strlen(PACK_NAME) + (used ? 1u : 0u);
        if (used + need >= cap) { say("Too many mod packs enabled", 240); return; }
        if (used) list[used++] = '|';
        memcpy(list + used, PACK_NAME, strlen(PACK_NAME) + 1);
    }
    issd_config_save(&g_issd_config, NULL);
    say("Applying squads...", 600);
    issd_restart_application();
}

static const char *transfer_message(IssdMgrTransferResult r) {
    switch (r) {
    case ISSD_MGR_TR_OK:          return "Transfer done";
    case ISSD_MGR_TR_SEASON_OVER: return "The season is over";
    case ISSD_MGR_TR_LIMIT:       return "No deals left this round";
    case ISSD_MGR_TR_FUNDS:       return "Not enough budget";
    case ISSD_MGR_TR_POSITION:    return "Keepers swap only for keepers";
    case ISSD_MGR_TR_REFUSED:     return "The club wants more";
    default:                      return "That deal is not possible";
    }
}

static void after_deal(IssdMgrTransferResult r) {
    say(transfer_message(r), r == ISSD_MGR_TR_OK ? 180 : 240);
    if (r != ISSD_MGR_TR_OK) return;
    autosave();
    refresh_state();
    go(S_HUB, H_APPLY);
    hub_cursor = H_APPLY;
}

/* -------------------------------------------------------------- screens */

static int other_slot(int row) { return row < mgr.user_slot ? row : row + 1; }

static int row_count(void) {
    switch (screen) {
    case S_HUB:       return HUB_ROWS;
    case S_TEAMS:     return ISSD_MGR_POOL_TEAMS;
    case S_TABLE:     return ISSD_MGR_TEAMS;
    case S_CALENDAR:  return calendar_mine ? ISSD_MGR_ROUNDS : ISSD_MGR_FIXTURES;
    case S_CLUBS:     return ISSD_MGR_TEAMS - 1;
    case S_OFFER:     return 2;
    case S_AGENTS:    return ISSD_MGR_FREE_AGENTS;
    default:          return ISSD_MGR_SQUAD;
    }
}

static bool hub_available(int row) {
    switch (row) {
    case H_PLAY:     return have_league && !issd_mgr_finished(&mgr);
    case H_SIM:      return have_league && !issd_mgr_finished(&mgr);
    case H_TABLE: case H_CALENDAR: case H_SQUAD: return have_league;
    case H_SWAP: case H_AGENTS: return have_league && !issd_mgr_finished(&mgr);
    case H_APPLY:    return have_league && !squads_applied;
    case H_CONTINUE: return career_file;
    default:         return true;
    }
}

static void hub_text(int row, char *line, size_t cap) {
    static const char *labels[HUB_ROWS] = {
        "Play my match", "Simulate my match", "League table", "Match calendar",
        "My squad", "Swap with a club", "Free agents", "Apply squads & restart",
        "New league...", "Continue saved career", "Back"
    };
    const char *label = labels[row];
    if (row == H_NEW && confirm_replace) label = "New league: A to replace!";
    if (row == H_APPLY && have_league && squads_applied) label = "Squads are in the game";
    snprintf(line, cap, "%s%s", label,
             (!hub_available(row) && row != H_APPLY) ? " (---)" : "");
}

/* Fill the text of one list row; `*mark` is set for the user's own entries. */
static void row_text(int row, char *line, size_t cap, bool *mark) {
    char pos[4];
    *mark = false;
    line[0] = '\0';
    switch (screen) {
    case S_HUB: hub_text(row, line, cap); break;
    case S_TEAMS: {
        load_stock_strength();
        snprintf(line, cap, "%-17.17s %2d", club_name(row), stock_strength[row]);
        break;
    }
    case S_TABLE: {
        IssdMgrStanding table[ISSD_MGR_TEAMS];
        issd_mgr_table(&mgr, table);
        const IssdMgrStanding *t = &table[row];
        snprintf(line, cap, "%2d %-9.9s %2d %+3d %3d", row + 1, slot_name(t->slot), t->played,
                 (int)t->goals_for - (int)t->goals_against, t->points);
        *mark = t->slot == mgr.user_slot;
        break;
    }
    case S_CALENDAR: {
        int round = calendar_mine ? row : calendar_round;
        const IssdMgrFixture *f = NULL;
        for (int i = 0; i < ISSD_MGR_FIXTURES; ++i) {
            const IssdMgrFixture *c = &mgr.fixtures[round][i];
            bool mine = c->home == mgr.user_slot || c->away == mgr.user_slot;
            if (calendar_mine ? mine : i == row) { f = c; break; }
        }
        if (!f) break;
        char score[8];
        if (f->played) snprintf(score, sizeof score, "%d-%d", f->home_goals, f->away_goals);
        else snprintf(score, sizeof score, "-");
        if (calendar_mine)
            snprintf(line, cap, "R%02d %-9.9s %3s %-9.9s", round + 1, slot_name(f->home), score,
                     slot_name(f->away));
        else
            snprintf(line, cap, "%-9.9s %5s %-9.9s", slot_name(f->home), score, slot_name(f->away));
        *mark = f->home == mgr.user_slot || f->away == mgr.user_slot;
        break;
    }
    case S_SQUAD: case S_OWN: case S_AGENT_OWN: case S_THEIRS: {
        int slot = screen == S_THEIRS ? picked_club : mgr.user_slot;
        const IssdMgrPlayer *p = &mgr.squad[slot][row];
        snprintf(line, cap, "%2d %-8.8s %-2s %2d %3dk", p->shirt_number, p->name,
                 position_text(p->position, pos), issd_mgr_player_rating(p), issd_mgr_player_value(p));
        break;
    }
    case S_CLUBS: {
        int slot = other_slot(row);
        snprintf(line, cap, "%-11.11s Str %2d  #%-2d", slot_name(slot),
                 issd_mgr_team_strength(&mgr, slot), rank_of(slot));
        break;
    }
    case S_AGENTS: {
        const IssdMgrPlayer *p = &mgr.free_agents[row];
        snprintf(line, cap, "%-8.8s %-2s %2d  fee %3dk", p->name, position_text(p->position, pos),
                 issd_mgr_player_rating(p), issd_mgr_free_agent_fee(&mgr, row));
        break;
    }
    case S_OFFER:
        snprintf(line, cap, "%s", row == 0 ? "Make the offer" : "Back");
        break;
    }
    fit(line);
}

static const char *title_text(char *buf, size_t cap) {
    switch (screen) {
    case S_HUB:       return "MANAGER MODE";
    case S_TEAMS:     return "PICK YOUR TEAM";
    case S_TABLE:     return "LEAGUE TABLE";
    case S_CALENDAR:
        if (calendar_mine) return "MY SEASON";
        snprintf(buf, cap, "ROUND %d/%d", calendar_round + 1, ISSD_MGR_ROUNDS);
        return buf;
    case S_SQUAD:     return "MY SQUAD";
    case S_CLUBS:     return "SWAP: PICK A CLUB";
    case S_THEIRS:
        snprintf(buf, cap, "%.12s PLAYERS", slot_name(picked_club));
        return buf;
    case S_OWN:       return "SWAP: YOUR PLAYER";
    case S_OFFER:     return "THE OFFER";
    case S_AGENTS:    return "FREE AGENTS";
    case S_AGENT_OWN: return "WHO LEAVES?";
    }
    return "";
}

/* ------------------------------------------------------------- the offer */

static void offer_lines(char lines[4][40]) {
    const IssdMgrPlayer *get = &mgr.squad[picked_club][picked_theirs];
    const IssdMgrPlayer *give = &mgr.squad[mgr.user_slot][picked_own];
    char a[4], b[4];
    snprintf(lines[0], 40, "Get:  %.8s %s %d", get->name, position_text(get->position, a),
             issd_mgr_player_rating(get));
    snprintf(lines[1], 40, "Give: %.8s %s %d", give->name, position_text(give->position, b),
             issd_mgr_player_rating(give));
    int price = issd_mgr_swap_price(&mgr, picked_club, picked_theirs, picked_own);
    if (price >= 0) snprintf(lines[2], 40, "You pay %dk", price);
    else snprintf(lines[2], 40, "Club pays you %dk", -price);
    snprintf(lines[3], 40, "Budget %dk  Deals left %d", mgr.budget, deals_left());
    for (int i = 0; i < 4; ++i) fit(lines[i]);
}

static void make_offer(void) {
    int price = issd_mgr_swap_price(&mgr, picked_club, picked_theirs, picked_own);
    after_deal(issd_mgr_offer_swap(&mgr, picked_club, picked_theirs, picked_own, price));
}

/* ----------------------------------------------------------------- input */

void issd_manager_menu_open(void) {
    g_overlay_menu.page = ISSD_MENU_PAGE_MANAGER;
    refresh_state();
    screen = S_HUB;
    confirm_replace = false;
    /* The first time, rest on the most useful row rather than an unavailable one. */
    if (!hub_cursor_set)
        hub_cursor = have_league ? H_PLAY : career_file ? H_CONTINUE : H_NEW;
    go(S_HUB, hub_cursor);
    if (hub_cursor < 0 || hub_cursor >= HUB_ROWS) go(S_HUB, 0);
}

void issd_manager_menu_step(int direction) {
    int rows = row_count();
    if (rows <= 0) return;
    confirm_replace = false;
    int row = (g_overlay_menu.current_item + direction + rows) % rows;
    g_overlay_menu.current_item = row;
    if (row < g_overlay_menu.scroll) g_overlay_menu.scroll = row;
    if (row >= g_overlay_menu.scroll + VISIBLE) g_overlay_menu.scroll = row - VISIBLE + 1;
    if (screen == S_HUB) { hub_cursor = row; hub_cursor_set = true; }
}

void issd_manager_menu_adjust(int direction) {
    if (screen == S_CALENDAR && !calendar_mine) {
        calendar_round = (calendar_round + direction + ISSD_MGR_ROUNDS) % ISSD_MGR_ROUNDS;
    }
}

static void confirm_hub(int row) {
    if (!hub_available(row) && row != H_APPLY) {
        say(row == H_CONTINUE ? "No saved career yet" : "Start or continue a league first", 200);
        return;
    }
    switch (row) {
    case H_PLAY:      play_my_match(); break;
    case H_SIM:       simulate_round(); refresh_state(); break;
    case H_TABLE:     go(S_TABLE, 0); break;
    case H_CALENDAR:
        calendar_round = mgr.round < ISSD_MGR_ROUNDS ? mgr.round : ISSD_MGR_ROUNDS - 1;
        calendar_mine = false;
        go(S_CALENDAR, 0);
        break;
    case H_SQUAD:     go(S_SQUAD, 0); break;
    case H_SWAP:      go(S_CLUBS, 0); break;
    case H_AGENTS:    go(S_AGENTS, 0); break;
    case H_APPLY:
        if (!have_league) say("Start or continue a league first", 200);
        else if (squads_applied) say("Squads are already in the game", 200);
        else apply_and_restart();
        break;
    case H_NEW:
        if ((have_league || career_file) && !confirm_replace) {
            confirm_replace = true;
            say("Press A again to replace your career", 240);
            break;
        }
        load_stock_strength();
        go(S_TEAMS, 0);
        break;
    case H_CONTINUE:  continue_career(); break;
    case H_BACK:      issd_manager_menu_cancel(); break;
    }
}

void issd_manager_menu_confirm(void) {
    int row = g_overlay_menu.current_item;
    switch (screen) {
    case S_HUB:       hub_cursor = row; confirm_hub(row); break;
    case S_TEAMS:
        if (start_league(row)) { go(S_HUB, H_SIM); hub_cursor = H_SIM; say("New league drawn", 150); }
        break;
    case S_CALENDAR:  calendar_mine = !calendar_mine; go(S_CALENDAR, 0); break;
    case S_CLUBS:     picked_club = other_slot(row); go(S_THEIRS, 0); break;
    case S_THEIRS:    picked_theirs = row; go(S_OWN, 0); break;
    case S_OWN:       picked_own = row; go(S_OFFER, 0); break;
    case S_OFFER:     if (row == 0) make_offer(); else go(S_OWN, picked_own); break;
    case S_AGENTS:    picked_agent = row; go(S_AGENT_OWN, 0); break;
    case S_AGENT_OWN: after_deal(issd_mgr_sign_free_agent(&mgr, picked_agent, row)); break;
    default:          break;
    }
}

void issd_manager_menu_cancel(void) {
    switch (screen) {
    case S_HUB:       issd_menu_return_from_manager(); break;
    case S_TEAMS:     go(S_HUB, H_NEW); hub_cursor = H_NEW; break;
    case S_TABLE:     go(S_HUB, H_TABLE); hub_cursor = H_TABLE; break;
    case S_CALENDAR:  go(S_HUB, H_CALENDAR); hub_cursor = H_CALENDAR; break;
    case S_SQUAD:     go(S_HUB, H_SQUAD); hub_cursor = H_SQUAD; break;
    case S_CLUBS:     go(S_HUB, H_SWAP); hub_cursor = H_SWAP; break;
    case S_THEIRS:    go(S_CLUBS, 0); break;
    case S_OWN:       go(S_THEIRS, picked_theirs); break;
    case S_OFFER:     go(S_OWN, picked_own); break;
    case S_AGENTS:    go(S_HUB, H_AGENTS); hub_cursor = H_AGENTS; break;
    case S_AGENT_OWN: go(S_AGENTS, picked_agent); break;
    }
}

void issd_manager_menu_click(int px, int py, int x, int y) {
    int row = g_overlay_menu.scroll + (py - y - LIST_TOP) / ROW_HEIGHT;
    if (screen == S_OFFER) row -= OFFER_FIRST_ROW;   /* the choices sit below the offer's text */
    if (py >= y + LIST_TOP && py < y + LIST_TOP + VISIBLE * ROW_HEIGHT && row >= 0 && row < row_count()) {
        g_overlay_menu.current_item = row;
        if (screen == S_HUB) { hub_cursor = row; hub_cursor_set = true; }
        if (px < x + 30 && screen == S_CALENDAR) issd_manager_menu_adjust(-1);
        else issd_manager_menu_confirm();
    } else if (py >= y + 190) {
        issd_manager_menu_cancel();
    }
}

void issd_manager_menu_summary(char *out, size_t cap) {
    if (!out || !cap) return;
    if (!career_probed) { career_file = file_exists(); career_probed = true; }
    if (!have_league) snprintf(out, cap, "%s", career_file ? "saved" : "no career");
    else if (issd_mgr_finished(&mgr)) snprintf(out, cap, "season over");
    else snprintf(out, cap, "R%d/%d", mgr.round + 1, ISSD_MGR_ROUNDS);
}

/* ----------------------------------------------------------------- render */

void issd_manager_menu_render(uint32_t *fb, int w, int h, int x, int y,
    void (*text)(uint32_t *, int, int, int, int, const char *, uint32_t)) {
    char title[48], line[96];
    text(fb, w, h, x + 8, y + 4, title_text(title, sizeof title), 0xffffd700);
    /* A list longer than the window says where the cursor is in it. */
    if (row_count() > VISIBLE) {
        char where[24];
        snprintf(where, sizeof where, "%d/%d", g_overlay_menu.current_item + 1, row_count());
        text(fb, w, h, x + 232 - (int)strlen(where) * 8, y + 4, where, 0xff909090);
    }

    if (screen == S_TABLE) text(fb, w, h, x + 14, y + 13, "#  Team       P  GD Pts", 0xff909090);
    if (screen == S_CALENDAR)
        text(fb, w, h, x + 14, y + 13, calendar_mine ? "A:Round view" : "< >:Round  A:My season", 0xff909090);

    if (screen == S_OFFER) {
        char lines[4][40];
        offer_lines(lines);
        for (int i = 0; i < 4; ++i) text(fb, w, h, x + 14, y + LIST_TOP + i * ROW_HEIGHT, lines[i], 0xffe0e0e0);
    }
    int first = screen == S_OFFER ? OFFER_FIRST_ROW : 0;   /* the choices sit below the offer's text */
    for (int row = g_overlay_menu.scroll; row < row_count() && row < g_overlay_menu.scroll + VISIBLE; ++row) {
        bool mark = false, available = true;
        row_text(row, line, sizeof line, &mark);
        if (screen == S_HUB) available = hub_available(row) || row == H_APPLY;
        if (screen == S_HUB && row == H_APPLY && (!have_league || squads_applied))
            available = false;
        bool selected = row == g_overlay_menu.current_item;
        uint32_t color = selected ? 0xff00ff66 : mark ? 0xff66ccff : available ? 0xffe0e0e0 : 0xff888888;
        int ly = y + LIST_TOP + (first + row - g_overlay_menu.scroll) * ROW_HEIGHT;
        if (selected) text(fb, w, h, x + 4, ly, ">", color);
        text(fb, w, h, x + 14, ly, line, color);
    }

    char status[64] = "";
    const char *help = "A:Select  ESC:Back";
    if (screen == S_HUB) {
        if (!have_league) snprintf(status, sizeof status, "No league yet");
        else if (issd_mgr_finished(&mgr)) {
            IssdMgrStanding table[ISSD_MGR_TEAMS];
            issd_mgr_table(&mgr, table);
            snprintf(status, sizeof status, "Champions: %.14s", slot_name(table[0].slot));
        } else
            snprintf(status, sizeof status, "R%d/%d Budget %dk Pos #%d", mgr.round + 1,
                     ISSD_MGR_ROUNDS, mgr.budget, rank_of(mgr.user_slot));
        if (have_league && !squads_applied) help = "Restart to apply transfers";
    } else if (screen == S_CLUBS || screen == S_THEIRS || screen == S_OWN || screen == S_OFFER ||
               screen == S_AGENTS || screen == S_AGENT_OWN) {
        snprintf(status, sizeof status, "Budget %dk  Deals left %d", mgr.budget, deals_left());
    } else if (screen == S_TEAMS) {
        snprintf(status, sizeof status, "Number = squad strength");
    }
    fit(status);
    text(fb, w, h, x + 8, y + 190, status, 0xffb0b0b0);
    text(fb, w, h, x + 8, y + 205, help, 0xff888888);
}
