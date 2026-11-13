#!/usr/bin/env python3
"""AltBotMod V35.2 - offline checks.

1. Capture the Flag meshes for the four End Game levels (XP5_001-004) and the BFLAG mesh, converted like the others:
   their "base us" / "base ru" zones sit on AltBotMod's own flag stands (modes_native.inc g_ctfBases, from the level
   data), so a raid on the enemy flag and the guards at home walk the mesh near the stands.
2. Bots no longer man or spawn into the TOW / Kornet launchers (their gunners shot at little and sat out the fight);
   the AA emplacements (C-RAM / Pantsir) stay.
3. Release 1.2 setting names: SavedRoutes / ObjectiveMesh; the installer renames the old keys in the active settings.
4. Release 1.2 updater: the release file matches the build; Server Control and Command load the updater.
5. Version.
"""
from pathlib import Path
import argparse, re
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
ZON = ROOT / 'files/server/BotMod_traces/navzones'
SC = ROOT / 'files/ServerControl'

def zones(path):
    out = {}
    for line in path.read_text().splitlines():
        m = re.match(r'^(\d+) (\S+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) \d+ ([a-z_]+\D*)$', line)   # kind radius x y z inside name
        if m: out[m.group(6).replace('_', ' ')] = (int(m.group(1)), float(m.group(2)), tuple(float(m.group(k)) for k in (3, 4, 5)))
    return out

def check_ctf():
    mn = (SRC / 'modes_native.inc').read_text()
    stands = {m.group(1): ((float(m.group(2)), float(m.group(3)), float(m.group(4))), (float(m.group(5)), float(m.group(6)), float(m.group(7))))
              for m in re.finditer(r'\{"(XP5_00\d)",\{(-?[\d.]+)f,(-?[\d.]+)f,(-?[\d.]+)f\},\{(-?[\d.]+)f,(-?[\d.]+)f,(-?[\d.]+)f\}\}', mn)}
    assert len(stands) == 4, stands
    for lv, (us, ru) in stands.items():
        z = zones(ZON / ('%s_CaptureTheFlag0.zon' % lv))
        assert set(z) >= {'base us', 'base ru'}, (lv, z)
        for name, at in (('base us', us), ('base ru', ru)):
            kind, radius, c = z[name]
            assert kind == 3 and radius >= 30, (lv, name, kind, radius)
            d = sum((a - b) ** 2 for a, b in zip(c, at)) ** 0.5
            assert d < 2.0, (lv, name, d)
    assert (ZON / 'MP_001_BFLAG.zon').is_file()
    assert '"CaptureTheFlag0"' in (SRC / 'trace_native.inc').read_text()
    print('PASS: Capture the Flag meshes on XP5_001-004 - their base zones sit on the flag stands (within 2 m); BFLAG mesh shipped')

def check_launchers():
    emp = (SRC / 'emplacement_native.inc').read_text(); sv = (SRC / 'spawn_vehicle.inc').read_text(); core = (SRC / 'core.c').read_text()
    assert 'if (kind == 1) continue;   // AltBotMod V35.2' in emp
    assert emp.index('if (kind == 1) continue;') < emp.index('((EnterVehicle_t)ADDR_PLAYER_ENTER_VEHICLE)(pl, e, 0);')
    assert 'if (emp == 1) continue;   // AltBotMod V35.2' in sv and sv.index('if (emp == 1) continue;') < sv.index('int rank = emp == 2 ? 58 : 44;')
    assert 'if (!strcmp(n, "TOW") || strstr(n, "TOWLAUNCHER") || strstr(n, "KORNET") || strstr(n, "M220")) return 1;' in core   # (kind 1 = the launchers)
    print('PASS: bots no longer man or spawn into TOW / Kornet launchers (AA emplacements unchanged)')

def check_version():
    assert '#define BM_ENHANCED_VERSION "AltBotMod V35.2 - New Ground"' in (SRC / 'enhanced.h').read_text()
    assert 'New-BmBanner "AltBotMod V35.2"' in (SC / 'AltBotMod-ServerControl.ps1').read_text()
    print('PASS: version V35.2')

def check_setting_names():
    enh = (SRC / 'enhanced.c').read_text()
    assert 'BOOL_KEY("SavedRoutes", savedRoutes)' in enh and 'BOOL_KEY("ObjectiveMesh", objectiveMesh)' in enh
    ini = (ROOT / 'files/BotMod/enhanced.ini').read_text()
    assert 'SavedRoutes=1' in ini and 'ObjectiveMesh=1' in ini
    core = (SC / 'ServerControl-Core.ps1').read_text()
    assert "New-BmSetting 'SavedRoutes'" in core and "New-BmSetting 'ObjectiveMesh'" in core
    setup = (ROOT / 'files/AltBotMod-Setup.ps1').read_text()
    i = setup.index('$renamed = Rename-OldKeys $botModDir'); assert i < setup.index('$added = Add-MissingSettings $defaults $active')
    print('PASS: setting names SavedRoutes / ObjectiveMesh; the installer renames the old keys (values kept) before adding missing ones')

def check_updater():
    rel = {}
    for line in (SC / 'AltBotMod-Release.txt').read_text().splitlines():
        if '=' in line: k, v = line.split('=', 1); rel[k.strip()] = v.strip()
    setup = (ROOT / 'files/AltBotMod-Setup.ps1').read_text()
    assert rel.get('release') == '1.2' and "$Product = 'AltBotMod " + rel.get('build', '') + "'" in setup, rel
    for f in ('Updater-Core.ps1', 'Update-Page.ps1'): assert (SC / f).is_file(), f
    cmd = (SC / 'AltBotMod-Command.ps1').read_text(); js = (SC / 'Command-Web/command.js').read_text()
    ver = re.search(r"CommandVersion='([^']+)'", cmd).group(1)
    assert ver.startswith('V35.2') and ("state.meta.Version!=='%s'" % ver) in js
    assert "'/api/update/install'" in cmd and "/api/update/install" in js and 'update-dialog' in (SC / 'Command-Web/index.html').read_text()
    print('PASS: Release 1.2 updater: release file 1.2 = %s; Server Control Updates page; Command %s with /api/update' % (rel['build'], ver))

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); ap.parse_args()
    check_ctf(); check_launchers(); check_setting_names(); check_updater(); check_version()
