import json
import subprocess
from pathlib import Path

from test_config_persistence import compile_c


def build(tmp_path, name, sources):
    repo = Path(__file__).resolve().parents[1]
    exe = tmp_path / name
    compile_c(exe, repo, [repo / s for s in sources], [repo / "ISSDNative"])
    return exe


def test_manager_league_logic(tmp_path):
    """Draw, fixture list, table, simulation, transfers, save/load, export."""
    exe = build(tmp_path, "test_manager.exe",
                ["tests/test_manager.c", "ISSDNative/issd_manager.c"])
    result = subprocess.run([str(exe), str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout, result.stdout
    pack = json.loads((tmp_path / "career.json").read_text())
    assert pack["name"] == "ISSD Manager Career"
    # The test marks one squad as missing, so eleven teams are written.
    assert len(pack["teams"]) == 11
    for team in pack["teams"]:
        assert 0 <= team["team_id"] < 36
        assert len(team["players"]) == 20
        assert [p["position"] for p in team["players"]][0] == "GK"
        assert team["players"][11]["position"] == "GK"
        for player in team["players"]:
            assert 1 <= player["shirt_number"] <= 99
            assert len(player["name"]) <= 8


def test_manager_link_to_the_cartridge(tmp_path):
    """Scenario hand-over, menu walking, team hand-off and the final score."""
    exe = build(tmp_path, "test_manager_link.exe",
                ["tests/test_manager_link.c", "ISSDNative/issd_manager_link.c"])
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout, result.stdout


def test_manager_pack_loads_in_mod_loader(tmp_path):
    """A transfer reaches the game through the ordinary roster pack loader."""
    exe = build(tmp_path, "test_manager_pack.exe",
                ["tests/test_manager_pack.c", "ISSDNative/issd_manager.c",
                 "ISSDNative/issd_mod.c"])
    result = subprocess.run([str(exe), str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout, result.stdout


def _cartridge():
    import os
    repo = Path(__file__).resolve().parents[1]
    for candidate in (os.environ.get("ISSD_ROM"),
                      repo / "International Superstar Soccer Deluxe (USA).sfc"):
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    return None


def test_manager_rom_squads_match_mod_studio(tmp_path):
    """All 720 stock players read from a ROM equal what the editor reads."""
    import pytest
    rom_path = _cartridge()
    if rom_path is None:
        pytest.skip("needs your own cartridge dump: set ISSD_ROM or put it in the repo root")
    import sys
    repo = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(repo / "tools"))
    from mod_studio import cartridge

    exe = build(tmp_path, "test_manager_rom.exe",
                ["tests/test_manager_rom.c", "ISSDNative/issd_manager.c"])
    result = subprocess.run([str(exe), str(rom_path)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    lines = result.stdout.splitlines()
    assert len(lines) == 36 * 20

    rom = cartridge.load_rom(str(rom_path))
    keys = ["acceleration", "speed", "shooting", "technique", "balance",
            "intelligence", "dribbling", "jumping", "stamina"]
    expected = []
    for team in range(36):
        for slot, p in enumerate(cartridge.read_team(rom, team)["players"]):
            code = {"GK": 1, "DF": 2, "MF": 4, "FW": 6}.get(p["position"])
            if code is None:
                code = int(p["position"])
            expected.append("%d|%d|%s|%d|%d|%d|%s" % (
                team, slot, p["name"], p["shirt_number"], code,
                p["skin_tone"] * 16 + p["hair_style"],
                ",".join(str(p[k]) for k in keys)))
    assert lines == expected
    # The cartridge keeps a keeper in slots 0 and 11 of every squad.
    for team in range(36):
        for slot in (0, 11):
            assert lines[team * 20 + slot].split("|")[4] == "1"


def _build_without_werror(tmp_path, name, sources):
    """issd_mod_rom.c has format-truncation warnings under newer gcc, which the
    shared -Werror helper would turn into failures; the tests that link it build
    without -Werror so they exercise the code rather than the compiler."""
    from test_config_persistence import c_compiler
    repo = Path(__file__).resolve().parents[1]
    exe = tmp_path / name
    compiler = c_compiler()
    if compiler == "cl":
        cmd = [compiler, "/nologo", "/I", str(repo / "ISSDNative")] + \
              [str(repo / s) for s in sources] + ["/Fe:" + str(exe)]
    else:
        cmd = [compiler, "-std=c11", "-I", str(repo / "ISSDNative")] + \
              [str(repo / s) for s in sources] + ["-o", str(exe)]
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=repo)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


_MOD_ROM_SOURCES = ["ISSDNative/issd_manager.c", "ISSDNative/issd_mod_rom.c",
                    "ISSDNative/issd_mod.c", "ISSDNative/issd_formation.c"]


def test_manager_career_reaches_the_cartridge(tmp_path):
    """Stock squads -> league -> transfer -> pack -> patched ROM -> squads."""
    import pytest
    rom_path = _cartridge()
    if rom_path is None:
        pytest.skip("needs your own cartridge dump: set ISSD_ROM or put it in the repo root")
    exe = _build_without_werror(tmp_path, "test_manager_career.exe",
                                ["tests/test_manager_career.c"] + _MOD_ROM_SOURCES)
    result = subprocess.run([str(exe), str(rom_path), str(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout, result.stdout


def test_manager_menu_drives_the_screens(tmp_path):
    """New league, simulate, table, calendar, squad, swap, free agent, apply."""
    import pytest
    rom_path = _cartridge()
    if rom_path is None:
        pytest.skip("needs your own cartridge dump: set ISSD_ROM or put it in the repo root")
    exe = _build_without_werror(
        tmp_path, "test_manager_menu.exe",
        ["tests/test_manager_menu.c", "ISSDNative/issd_manager_menu.c",
         "ISSDNative/issd_manager_link.c"] + _MOD_ROM_SOURCES)
    result = subprocess.run([str(exe), str(rom_path), str(tmp_path)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "all checks passed" in result.stdout, result.stdout
