# Manager mode

Status: **league, squads, overlay screens, the Scenario button and playing
your fixtures inside the game.** The app was built and run headless with a
real USA cartridge: "Play my match" walked the original menus, the two
league clubs played and the final score went into the calendar; the Scenario
button opened Manager Mode; the main menu shows "MANAGER" in its place.
Nobody has tried it on Windows or with a real controller yet.

## Where it is

**Pause overlay (Esc / F1) -> Gameplay Tweaks -> Manager Mode.** The Gameplay
Tweaks page now has six rows, so they are spaced 20 pixels apart instead of 24.
The row shows the state of the career (`no career`, `saved`, `R5/22`,
`season over`).

| Screen | What it does |
|---|---|
| Hub | Everything below, plus *New league*, *Continue saved career* and *Apply squads & restart*. The status line shows round, budget and your position. |
| Pick your team | The 36 countries, each with its squad strength. Replacing a career asks twice. |
| Simulate my match | Plays your fixture and the other five automatically and saves. The result is shown as a notice. |
| League table | Position, points, goal difference; your club is highlighted. |
| Match calendar | One round at a time (left/right), or *my season* with all 22 fixtures. |
| My squad | Shirt number, name, position, rating and value. |
| Swap with a club | Pick a club, one of its players, one of yours, then the offer. The price is computed for you; the screen shows what you pay. |
| Free agents | Twelve, two of them keepers. Pick one, then who leaves. |

Every change is written to `<saves>/manager/career.mgr` at once, so closing the
app loses nothing. Transfers show *Restart to apply transfers* until the
cartridge in memory carries them: **Apply squads & restart** writes
`manager_career.json` into the mods folder, switches the pack **ISSD Manager
Career** on and restarts, like the Mods page. On the next start the hub reads
*Squads are in the game*. A new league always starts from the untouched
cartridge squads, whatever packs are on.

Positions 3 and 5, the cartridge's in-between codes, are shown as `DM` and `AM`.

The plan replaces the original **Scenario** entry of the main menu with
**Manager**: pick a team, start a league, play your fixtures and watch the
rest of the table move.

## The league

- **12 teams, double round-robin, 22 rounds.** The user's team plus 11 others
  drawn from the 36 countries (`issd_mgr_draw_teams`). The added-team cells
  (36-41) are not supported: they are written with `new_team` rather than a
  `team_id`, so a career pack cannot carry them.
- **A drawn fixture list.** The clubs are shuffled before the circle method
  lays out the first leg; the order of the rounds inside each leg is drawn
  too. The return leg swaps home and away, so every club plays 11 at home and
  11 away. A return leg never opens with a rematch of the round before it, and
  no pair meets in two consecutive rounds.
- **Deterministic.** The seed fixes the draw, the fixture list, the free
  agents and every automatic result. The generator is SplitMix64 and the
  simulation uses only integers, so a seed gives the same league on Windows,
  Linux and Android.
- **Table:** 3 points for a win, 1 for a draw; ties are broken by goal
  difference, goals scored, then league position.

## Results of other matches

Matches the user does not play are decided by `issd_mgr_team_strength`:

- Player rating: the average of the nine attributes the cartridge stores.
  It has no goalkeeping rating, so keepers are rated like everyone else.
- Team strength: starting eleven 85% (keeper counted twice), the eight outfield
  substitutes 15%.
- Each side gets ten chances; every rating point of difference changes the
  chance of scoring by 2%, and the home side gets +3 points. About 2.6 goals
  a match at equal strength.

Measured over 200 seasons with the user's whole squad shifted against eleven
equal clubs: equal strength wins the league 14% of the time, +5 points 32%,
+10 points 56%, +20 points 91%. The 2% step is `GROWTH_PERCENT` in the source.

With the real squads of the USA cartridge, team strength runs from 33 to 74,
a much wider spread than that test. Over 300 seasons of random 12-team leagues
the strongest side won 65% of them.

Each fixture's result is derived from the seed, the round and its position in
the round, so it does not depend on the order things were simulated in.

## Transfers (basic)

Every squad keeps exactly 20 slots, because that is what the cartridge holds
(see `docs/MODDING.md`, "Slots, not players"). So a transfer is always a swap.

- **Swap with a club.** One of the user's players for one at another league
  club, with cash settling the difference. The other club asks for 115% of its
  player's value; `issd_mgr_swap_price` says what is needed on top. A
  negative price means the club pays the user. Keepers only swap for keepers.
- **Free agents.** Twelve are drawn at the start (two keepers). Signing one
  costs half his value and sends one of the user's players to the list.
- **Value** is `(rating - 35)^2 / 8` thousand, with a floor of 5.
- **Budget** starts at 300. Each round adds 20, and a win adds 15 more.
- **Two transfers per round.** Failed offers cost nothing.
- Players take over the shirt number of the one they replace, so numbers stay
  unique and slot positions keep their meaning (slot 0 and 11 are keepers).

AI-to-AI transfers are not modelled.

## Saves and the roster pack

`issd_mgr_save` writes a small checksummed binary file (about 6.5 KB; the
temporary file is renamed over the old one). `issd_mgr_load` checks the
checksum, every range, that the fixture list is a real double round-robin,
and that exactly the rounds before `round` are played, before it replaces
anything.

`issd_mgr_export_pack` writes the 12 squads as an ordinary roster pack named
**ISSD Manager Career**. Roster packs only apply at boot, so after a transfer
the host writes the pack, enables it and restarts, as the Mods page already
does. `tests/test_manager_pack.c` loads the pack with the real mod loader and
compares every field.

Attributes are stored 0-99 and the game quantises them to eight steps, so a
swap between two players closer than about 12 points may not be audible on the
pitch even though the league treats them differently.

## Playing a match, the Scenario button and its label

`ISSDNative/issd_manager_link.c` is the bridge to the running cartridge. It
only watches and writes work RAM, and it takes the RAM as an argument so it is
tested on a synthetic image.

- **Scenario button.** On the main menu (`$32=6`, `$70=12`) with the cursor
  (`$4E`) on item 4, A or Start opens Manager Mode instead.
- **Play my match.** The link presses A by itself to walk the Open Game menus,
  writes the two team codes at the pre-match boundary (`$0DA0`/`$0EA0`, twice
  the team id; the user is always side 1, controlled by player 1), and when
  the clock hits 00:00 at `$70=18` it reads the goals (`$0DA2`/`$0EA2`) and
  records them. The user's club is always the left side, whether the
  fixture is home or away; the result is mapped back to home and away.
- **Label.** "SCENARIO" is cartridge artwork and stays in the ROM. While the
  main menu shows, the frame is repainted with "MANAGER" in the button's
  colours (`issd_menu_render_main_menu_label`). Nothing in the cartridge changes.
- Extra-time/penalty rules are not forced yet: a drawn match that goes to
  extra time ends when the game says it does.
- A headless check: `tests/make_career.c` writes a career file, and
  `--match-action FRAME:manager-play` starts the next fixture.

Still not done: AI-to-AI transfers, injuries, contracts, and the added teams
36-41.

## Squads from the cartridge

`issd_mgr_read_rom_squad` reads a stock team (0-35) from a cartridge image,
following the roster pointer table. Ratings come back at the middle of each
quantisation band, so a squad read from the ROM and written back through a
pack stores the same values. The cartridge keeps nine attributes per player
(no goalkeeping), in six or so distinct steps, so players are less distinct
than the 0-99 scale suggests.

## Tests

```
python -m pytest tests/test_manager.py -q
```

Three tests need your own cartridge dump and are skipped without one. Put it in
the repository root under its usual name or set `ISSD_ROM`. One compares all
720 players read in C with the Mod Studio reader. One applies a career
pack, transfer included, to a copy of the ROM and reads the squads back; teams
outside the league stay untouched. The third drives the screens with key
presses (new league, simulating, table, calendar, squad, swap, free agent,
apply, restart, continue) and checks that nothing is drawn wider than the box.

`tests/test_manager.c` covers the draw, 300 seeds of fixture lists, the table
invariants over full seasons, determinism, goal averages, strength, squads,
every transfer outcome, save/load including corrupted and tampered files with
a valid checksum, and the pack text. The first two tests need no ROM.

To look at the screens without the game, `tests/render_manager_screens.c` draws
each one with the real overlay renderer and writes it as an enlarged BMP. It
links the same sources as `tests/test_menu_graphics.py`, plus
`issd_manager_menu.c` and `issd_manager.c`, and takes the cartridge dump and an
output folder as arguments.
