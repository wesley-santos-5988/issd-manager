#ifndef ISSD_MANAGER_H
#define ISSD_MANAGER_H

/* Manager mode: a 12-team, double round-robin league (22 rounds) with a drawn
 * fixture list, a table, automatic results for every match the user does not
 * play, and basic slot-preserving transfers.
 *
 * This module is pure logic. It has no dependency on the cartridge, the
 * renderer or the overlay, so it builds and tests without a ROM. The host
 * adapter (not in this file) is responsible for:
 *   - supplying the starting squads with issd_mgr_set_squad(),
 *   - poking the two team codes into the exhibition setup and reading the
 *     final score back, then calling issd_mgr_record_user_result(),
 *   - writing the career roster pack (issd_mgr_export_pack) and restarting,
 *     because roster packs only apply at boot.
 *
 * Team ids are the cartridge's own ids: 0-35 for the countries in select
 * screen order, 36-41 for the six added-team cells. A "slot" is a league
 * position 0..ISSD_MGR_TEAMS-1; fixtures and standings use slots. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISSD_MGR_TEAMS            12
#define ISSD_MGR_ROUNDS           22   /* (TEAMS - 1) * 2 */
#define ISSD_MGR_FIXTURES         6    /* TEAMS / 2 per round */
#define ISSD_MGR_SQUAD            20
#define ISSD_MGR_FREE_AGENTS      12
/* Countries only. The six added-team cells (36-41) are written with
 * "new_team" rather than a team_id, so a career pack cannot carry them yet. */
#define ISSD_MGR_POOL_TEAMS       36
#define ISSD_MGR_NAME_LEN         8
#define ISSD_MGR_MAX_GOALS        99

/* Economy, in thousands. Tunable in one place. */
#define ISSD_MGR_START_BUDGET     300
#define ISSD_MGR_INCOME_PER_ROUND 20
#define ISSD_MGR_WIN_BONUS        15
#define ISSD_MGR_TRANSFERS_PER_ROUND 2

/* Attribute order matches the mod pack schema. */
typedef enum {
    ISSD_ATTR_ACCELERATION = 0,
    ISSD_ATTR_SPEED,
    ISSD_ATTR_SHOOTING,
    ISSD_ATTR_TECHNIQUE,
    ISSD_ATTR_BALANCE,
    ISSD_ATTR_INTELLIGENCE,
    ISSD_ATTR_DRIBBLING,
    ISSD_ATTR_JUMPING,
    ISSD_ATTR_STAMINA,
    ISSD_ATTR_GOALKEEPING,
    ISSD_ATTR_COUNT
} IssdMgrAttr;

typedef struct {
    uint8_t shirt_number;               /* 1-99 */
    char    name[ISSD_MGR_NAME_LEN + 1];/* A-Z a-z space '.' only */
    uint8_t position;                   /* cartridge code 0-15; 1 = goalkeeper */
    uint8_t skin_tone;                  /* 0-1 (really hair colour) */
    uint8_t hair_style;                 /* 0-15 */
    uint8_t attr[ISSD_ATTR_COUNT];      /* 0-99 */
} IssdMgrPlayer;

typedef struct {
    uint8_t home, away;                 /* league slots */
    bool    played;
    uint8_t home_goals, away_goals;
} IssdMgrFixture;

typedef struct {
    uint8_t  slot;
    uint8_t  played, won, drawn, lost;
    uint16_t goals_for, goals_against;
    uint16_t points;
} IssdMgrStanding;

typedef struct {
    uint32_t seed;
    uint8_t  team_id[ISSD_MGR_TEAMS];   /* cartridge id per slot */
    uint8_t  user_slot;
    uint8_t  round;                     /* next round; ISSD_MGR_ROUNDS = over */
    uint8_t  transfers_used;            /* in the current round */
    int32_t  budget;                    /* the user's club, in thousands */
    IssdMgrFixture fixtures[ISSD_MGR_ROUNDS][ISSD_MGR_FIXTURES];
    bool          has_squad[ISSD_MGR_TEAMS];
    IssdMgrPlayer squad[ISSD_MGR_TEAMS][ISSD_MGR_SQUAD];
    IssdMgrPlayer free_agents[ISSD_MGR_FREE_AGENTS];
} IssdManager;

/* ---- creating a league ------------------------------------------------ */

/* Draw the user's team plus 11 others from ids [0, pool_size) without
 * repeats, returned in ascending order. The result is deterministic for a
 * seed. pool_size is between 12 and ISSD_MGR_POOL_TEAMS. */
bool issd_mgr_draw_teams(uint32_t seed, int user_team, int pool_size,
                         uint8_t out[ISSD_MGR_TEAMS]);

/* Build a league from 12 distinct team ids (one must be user_team) and draw
 * the fixture list. Squads start empty; fill them with issd_mgr_set_squad. */
bool issd_mgr_new(IssdManager *m, uint32_t seed,
                  const uint8_t team_ids[ISSD_MGR_TEAMS], int user_team);

/* Copy exactly 20 players into a slot (the cartridge's squad size, slot order
 * as in docs/MODDING.md), sanitising names and clamping values. */
bool issd_mgr_set_squad(IssdManager *m, int slot,
                        const IssdMgrPlayer *players, int count);

/* Read one stock team's squad (team 0-35) from a cartridge image: names,
 * positions, looks and the nine stored attributes, converted to the 0-99
 * scale at the middle of each quantisation band, so writing them back
 * through a roster pack stores the same values. The cartridge has no
 * goalkeeping attribute; that field is filled with a cosmetic estimate and
 * is not used for ratings. Shirt numbers are the slot numbers, as in the
 * editor. Returns false for a bad team or a cartridge that is not the USA
 * revision's layout. */
bool issd_mgr_read_rom_squad(const uint8_t *rom, size_t size, int team,
                             IssdMgrPlayer out[ISSD_MGR_SQUAD]);

/* True when every squad the league holds reads back from this cartridge image
 * as the manager has it (names, positions, looks, shirt numbers and the
 * quantised attributes). False after a transfer until the career pack has been
 * applied to the running cartridge. */
bool issd_mgr_squads_match_rom(const IssdManager *m, const uint8_t *rom, size_t size);

/* ---- reading the league ----------------------------------------------- */

int  issd_mgr_slot_of_team(const IssdManager *m, int team_id); /* -1 if absent */
bool issd_mgr_finished(const IssdManager *m);

/* The user's fixture in the current round. Team ids are the cartridge's. */
bool issd_mgr_next_user_fixture(const IssdManager *m, IssdMgrFixture *fixture,
                                uint8_t *home_team_id, uint8_t *away_team_id);

/* Table, best first: points, goal difference, goals for, then slot order. */
void issd_mgr_table(const IssdManager *m, IssdMgrStanding out[ISSD_MGR_TEAMS]);

/* ---- playing ---------------------------------------------------------- */

/* Record the score of the user's match, play the other fixtures of the round
 * automatically and move to the next round. */
bool issd_mgr_record_user_result(IssdManager *m, int home_goals, int away_goals);

/* Let the whole round, the user's match included, be decided automatically. */
bool issd_mgr_sim_user_round(IssdManager *m);

/* ---- strength ---------------------------------------------------------- */

int issd_mgr_player_rating(const IssdMgrPlayer *p);          /* 0-99 */
int issd_mgr_team_strength(const IssdManager *m, int slot);  /* 0-99, 50 if no squad */

/* ---- transfers --------------------------------------------------------- */

typedef enum {
    ISSD_MGR_TR_OK = 0,
    ISSD_MGR_TR_INVALID,     /* bad index, or squads not loaded */
    ISSD_MGR_TR_SEASON_OVER,
    ISSD_MGR_TR_LIMIT,       /* transfers for this round used up */
    ISSD_MGR_TR_FUNDS,       /* the cash part exceeds the budget */
    ISSD_MGR_TR_POSITION,    /* goalkeepers only swap for goalkeepers */
    ISSD_MGR_TR_REFUSED      /* the other club wants more */
} IssdMgrTransferResult;

/* Worth of a player in thousands, from his rating. */
int issd_mgr_player_value(const IssdMgrPlayer *p);

/* The least cash the club needs on top of `own_index` for `target_index`.
 * Negative means the club would pay the user. */
int issd_mgr_swap_price(const IssdManager *m, int target_slot, int target_index,
                        int own_index);

/* Swap one of the user's players for one at another club, settling the value
 * difference in cash (positive = the user pays). The two take each other's
 * shirt number so every squad keeps its slots and its numbers. */
IssdMgrTransferResult issd_mgr_offer_swap(IssdManager *m, int target_slot,
                                          int target_index, int own_index,
                                          int cash);

/* Sign a free agent in place of one of the user's players, who joins the
 * free-agent list. The fee is half the signed player's value. */
IssdMgrTransferResult issd_mgr_sign_free_agent(IssdManager *m, int agent_index,
                                               int own_index);

int issd_mgr_free_agent_fee(const IssdManager *m, int agent_index);

/* ---- saving ------------------------------------------------------------ */

/* Binary career file with a checksum. Load validates every field and the
 * whole fixture list before replacing *m; on failure *m is untouched. */
bool issd_mgr_save(const IssdManager *m, const char *path);
bool issd_mgr_load(IssdManager *m, const char *path);

/* Write the 12 squads as an ordinary roster pack (see docs/MODDING.md). The
 * pack's name is stable so it keeps its tick in the mod list. */
bool issd_mgr_export_pack(const IssdManager *m, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ISSD_MANAGER_H */
