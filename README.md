# AltBotMod - Release V1

**AltBotMod** adds AI bots to a Battlefield 3 dedicated server. It is a native mod: no Venice Unleashed, and players need nothing extra. It runs on the ALP, bf4play and BF-Portable builds of `bf3_server_Final.exe`; the installer checks for that file before it changes anything.

> **Version note:** this is public **Release V1**. It is built from the internal development line **V34 "Squad Up"**. The internal version number appears in the server log (`enhanced: AltBotMod V34 - Squad Up`). The full development history is in [CHANGELOG-AltBotMod.md](CHANGELOG-AltBotMod.md).

> **Status:** the features below were built from real server logs and tested offline with automated checks and sanitizer builds. Several features have not yet been run in a live round. See [Not yet tested in a live round](#not-yet-tested-in-a-live-round).

---

## Contents

- [What's included](#whats-included)
- [Requirements](#requirements)
- [Installation](#installation)
- [Upgrading and rollback](#upgrading-and-rollback)
- [Features](#features)
- [AltBotMod Command (web launcher)](#altbotmod-command-web-launcher)
- [Squad orders](#squad-orders)
- [Settings reference](#settings-reference)
- [Learning files and data folders](#learning-files-and-data-folders)
- [Log lines to look for](#log-lines-to-look-for)
- [Testing](#testing)
- [Known limitations](#known-limitations)
- [Uninstalling](#uninstalling)

---

## What's included

| Path | Contents |
|---|---|
| `Install-AltBotMod.bat` | Installer. Finds your server folder (or asks) and installs the mod. |
| `Uninstall-AltBotMod.bat` | Removes the mod and its shortcuts. |
| `Collect-Diagnostics.bat` | Gathers logs and settings into one folder for a bug report. |
| `files\` | The mod itself: `BotModCore.dll`, navigation data, routes, Server Control and AltBotMod Command. |
| `rollback\` | Earlier DLLs (V29, V32, V33) you can restore if needed. |
| `tools\` | Helper scripts used to build the navigation and zone data. |
| `validation\` | Offline test suite, see [Testing](#testing). |
| `README.txt` | Short quick-start. |
| `SOURCE-CHANGES-*.patch` | Source-level diff from the previous development line. |

## Requirements

- Windows PC hosting the Battlefield 3 dedicated server (`bf3_server_Final.exe`) from ALP, bf4play or BF-Portable.
- Server admin rights for the installer, which needs to stop the server, write to the server folder and (optionally) add firewall rules for phone access.
- Clients need nothing extra.

---

## Installation

1. **Stop** every BF3 server you run from this PC.
2. **Extract** the whole package. Keep the folder structure intact.
3. **Run** `Install-AltBotMod.bat`. It finds your BF3 server folder or asks you where it is.
4. **Start** the server as usual.

**What the installer does**

- Keeps your existing settings and adds new ones. Settings you changed yourself are never overwritten.
- Raises `JetAreaMarginMeters` from the old defaults (350 to 600, and 600 to 900 for this release). A value you set yourself is kept.
- Backs up anything it replaces into a `BotMod_backup_<date>` folder.
- Makes sure every BF-Portable server you run is set to 64 players (`vars.maxPlayers 64` in `Startup.txt` and `PLAYERS=64` in `_StartServer*.bat`). Only that value is changed, and the file is backed up first. Restart the server afterwards for it to take effect.
- Creates the desktop and Start-menu shortcuts for AltBotMod Command.

**Checking that it loaded:** open `BotMod_<port>.log` and look for `enhanced: AltBotMod V34 - Squad Up`.

---

## Upgrading and rollback

**Upgrading:** run the new installer over the old install. Your settings, learning files and route files are kept.

**Rolling back:** copy one of the DLLs from `rollback\` over `BotModCore.dll` in the server folder:

| Folder | Version |
|---|---|
| `rollback\AltBotMod-V33` | V33 |
| `rollback\AltBotMod-V32` | V32 |
| `rollback\AltBotMod-V29` | V29 |

---

## Features

The features are grouped by area. Each one lists what the bots do and any setting that controls it.

### 1. Navigation

**Objective navigation mesh (new in Release V1)**
- Around every capture point, M-COM and base, bots walk a navigation mesh that knows the level's walls, floors, stairs, doorways and the places to jump or crouch.
- The meshes ship for 97 map/mode combinations in `BotMod_traces\navzones`.
- Priority near an objective: the mesh first, then saved routes, then the navigation grid.
- Every one of the 218 Rush M-COMs (Rush and Squad Rush, all maps) sits in a mesh M-COM zone. 217 can be walked to over the mesh; Death Valley M-COM 4 still uses the older routing.
- A mesh connection that bots keep getting stuck on becomes more expensive. After 3 stuck bots it is closed for the rest of the level. 
- Setting: `FunBotsMesh` (1 on, 0 off).

**Saved routes on every map and mode**
- All 215 level/mode combinations (Team Deathmatch, Close Quarters, Squad DM, Gun Master, Domination, Conquest, Rush, Squad Rush, CTF, Air Superiority and more) have saved routes of their own or borrowed from the same level.
- Infantry and vehicles try a saved route before the grid. A bot that cannot join a route waits for the next tick and tries again. A failed join is not retried from the same spot for 10 s, which also saves server time.
- Recorded vehicle routes hold up to 256 points, so long routes are not cut into stop-go legs.

**Learning from stuck spots** (see [Learning files](#learning-files-and-data-folders))
- Each place a bot gets stuck is recorded, together with whether it got through afterwards and whether a jump did it.
- Spots bots get through are never blocked again. Spots nobody gets through after 3 tries are removed from the walking grid when the map loads.
- Spots passed with a jump: the next bot jumps there too.
- "Killing grounds": where soldiers die, routes cost up to 2.5x more, so bots prefer another way when one exists. Nothing is closed off.
- Vehicles that stall at the same patch twice have that patch removed from the driving map at the next load.

**Learning from you (and other human players)**
- Ground you walk, drive or sail over that the saved routes do not cover is recorded and becomes part of the bots' saved routes. It needs at least 25% new ground; walks start at 40 m, drives at 150 m.
- Stairs stay in one route. Ladders, ledge jumps and vaults end a route because bots cannot copy them.
- A route that leads bots to a spot they never get through is split there, so the network corrects itself.
- A helicopter you land away from where you boarded becomes a landing spot for bot transports.

**Doorways, stairwells and glass**
- Glass and thin panels (under 30 cm, floor and room on the far side) are knifed when a bot is stuck against them. If the knife fails, the bot shoots the pane.
- A pane counts as unbreakable only after two failed tries. Before, one failure was enough.
- The mod learns which materials break and keeps the list in `BotMod_breakable.txt` for each map.
- Learned glass counts as passable when a map's navigation grid is built. Run `navrebuild` on a map to rebuild its grid with the windows included.
- Stairwells: a bot blocked on a stair flight is cleared and routed round it instead of jamming the stairs for everyone.

**Breaching shut doors**
- A bot stuck at a shut door on its route holds still, then fires across and low on both sides of the door for 3.5 s. This clears gas tanks that blow open doors (for example on Epicenter).
- Triggers only when something solid sits at knee, chest and head height within 2.5 m and the route continues behind it.
- Limits: at most twice per spot every 30 s. Otherwise the usual stuck recovery applies.

**Swimming and water**
- A bot in water (surface 0.9 m or more above its feet) swims for its objective. If something stops it, it turns off for 2 s and plans again once ashore.
- A bot with water between it and every objective, with no route three times in 20 s, walks straight at the objective, over the side, and swims. This works on every map.
- Boats pick up bots, and a bot takes a boat only when its objective is across water.

**Smarter routing for the server**
- Route planning is capped at 6 ms per busy tick, so the server keeps its 30-tick rate on large games.

### 2. Objectives and modes

**Rush**
- **M-COMs indoors and behind glass:** a bot going for an M-COM uses the mesh path into the building.
- **Windows in the way:** within 15 m of the M-COM, a pane between the bot and the M-COM is knifed. If the knife fails, the bot shoots it.
- **Guards:** they stand at mesh points around the M-COM.
- **Arming and defusing:** bots arm and defuse M-COMs from the panels, and a bot at a panel takes over the job. A hold survives fight orders and small pushes.
- **Squad Rush:** A and B are the two M-COMs of the current stage.
- **Stalled bots:** combat moves are checked again from where the bot now is, so a bot no longer "moves" 2 m and stands still.

**Vehicles that keep moving**
- After the opening minutes, a bot on foot walks at most 250 m to a vehicle (`VehicleWalkMeters`). When its objective is within 120 m, it only walks to a vehicle right beside it (within 40 m). The opening rush to vehicles and spawning into vehicles are unchanged.
- Vehicles at a flag they do not own work it from inside the capture zone, moving between spots 8-14 m from the centre. They stay up to 75 s, until the flag turns.

**Supply runs**
- A medic or support that cannot reach a caller is not sent back to that caller for 45 s.

**Conquest and Domination**
- Defenders hurry to an armed M-COM.
- Bots take the objective routes from the mesh and the recorded routes.

**Capture the Flag**
- A third of the bots raid the enemy flag stand, a third guard their own, and a third hunt. Flag-stand positions for the four maps come from the level data.

**Air Superiority**
- Bots spawn into jets at each team's air spawns.

**Tickets**
- A bot respawn costs half a ticket by default (`BotTicketCost`).

### 3. Squads

- **Squad spawns:** bots do not spawn on a squad mate who is in a fight (hit in the last 5 s, or an enemy alive within 30 m).
- **Squad orders:** see [Squad orders](#squad-orders). Your squad's bots obey `!attack` and `!defend` for 4 minutes or until the objective is done.
- **Squad leaders announce** their objective in squad chat when it changes, for example `[Squad] xFoo: attack B`.

### 4. Vehicles

**Driving**
- **Smooth driving:** every vehicle's speed, braking, steering and "stuck?" checks use the vehicle's physics position, which updates every frame. Before this, drivers measured 0 m/s between server syncs and surged and slowed.
- **Fallback:** the crew-seat and body positions are used only if the physics position is missing or invalid.
- **Bumper rays:** run 6 times a second below 12 m/s (10 above). On recorded roads they are skipped.
- **Full throttle:** bots hold the throttle on open roads.

**Passengers**
- Bots spawn into a teammate's jeep, boat or transport, in Rush too.
- Cargo gets out when:
  - the vehicle is nearly wrecked (aircraft cargo bails out with a parachute);
  - a human driver stops at an objective the team still has to take (cargo gets out after 4 s and captures on foot);
  - the vehicle has not moved for 25 s (40 s for a boat);
  - a human pilot is over an objective (cargo jumps).
- Gunners stay seated. A bot driver on a job still drops its own cargo at the flag.

**Jeep taxis and boats**
- A bot driver offers a ride to bots within 35 m every 15 s, and waits up to 7 s for them to run over.
- Boats only carry bots to objectives across water.

**Damage**
- Damage taken and dealt by vehicles can be scaled (`VehicleDamageTaken`, `VehicleDamageDealt`, 0.25x to 4x).

**Large maps**
- Drivers try saved routes before the grid and do not slow down at the end of a partial route.

**Countermeasures**
- A bot tank or IFV pops smoke when a hit takes 8% or more of its health, at most once every 25 s (`VehicleCountermeasures`). Smoke is also used when a tank is fully locked.

### 5. Tanks, stationary weapons and aircraft

**Tanks**
- Tanks fire while moving. Before, the main gun only calibrated at a standstill.
- Target order: whoever just shot the tank is engaged first (for 5 s), then soldiers within 25 m of the hull (C4, mines, knives) ahead of distant targets.

**Stationary weapons**
- A gunner on a TOW, Kornet, C-RAM or Pantsir leaves the seat when an enemy soldier comes within 18 m and fights him.
- Door gunners (helicopters, Little Bird bench seats) engage at up to `DoorGunMeters` (default 600 m). If the gun turns for the bot and then stops at the end of its arc, the gunner keeps the gun and picks another target for 4 s.
- Gunner target selection is cheap: candidates are scored first and only the best 6 are traced for line of sight.

**Jets**
- **Dogfights:** a jet chases an aircraft with banked turns and tracks it with its cannon within 60° of the nose and 1200 m. The gun fires in 2 s bursts with a 0.6 s cool-down.
- **Missiles:** after 2 s with the target on the nose (250-1600 m), a jet fires its secondary weapon. It waits 10 s between missiles.
- **Air-to-air vs air-to-ground:** jets use heat-seeking missiles on aircraft and guns on ground targets. They prefer enemy attack helicopters first.
- **No ramming:** head-on passes break off at 380 m; any approach under 2 s to impact breaks off; every jet watches nearby aircraft, friend or foe, and breaks away from any that would pass within 50 m (150 m head-on).
- **Stall recovery:** a jet below flying speed lowers its nose, levels its wings and goes to full power until it flies again.
- **Wider area:** jets' patrol area margin is 900 m by default (`JetAreaMarginMeters`), at least 1000 m each way from its middle. They turn back 3 s before the edge and end 200 m inside it.
- **Flares at the right time:** a pilot pops flares when an enemy missile lock is 75% complete, before the missile fires.
- **Losses learned:** where aircraft are lost or abandoned under 35 m, jets fly up to 90 m higher within 500 m, and helicopters up to 45 m higher within 150 m.
- **Landing spots:** where a transport lands and unloads becomes a spot that transports try first.

**Attack helicopters**
- Rockets and cannon for ground targets, missiles for aircraft.

### 6. Weapons and gadgets

- **Gadget variety:** each life, engineers carry a SMAW or RPG-7, a Javelin, or (on maps with aircraft) a Stinger or Igla. Support carries a claymore or C4. Recon carries a T-UGS or SOFLAM. Set `GadgetVariety` to 0 to use only the loadouts in `BotMod_loadouts.txt`.
- **Javelin:** used on ground vehicles out to 450 m. A gunner closes in if the target is hidden.
- **Stinger / Igla:** used on aircraft in the air out to 700 m.
- **Lock-on:** the bot aims down the sight and fires after a 2.2 s lock. `LockOnSeconds` (default 0.5 s) sets how long a bot holds a lock on a target before the lock is put in place. Use `LockOnLaunchers` to turn lock-on use off.
- **C4 on vehicles:** a soldier with C4 goes for a slow enemy vehicle within 40 m. It plants two charges, backs off 15 m and detonates them. Turn off with `C4Attacks`.
- **Snipers** place their equipment (T-UGS, SOFLAM or claymore) when they reach their perch.

### 7. Command and control tools

- **AltBotMod Command:** a web launcher for managing servers, bots, rotation, rules and learning files. See [AltBotMod Command](#altbotmod-command-web-launcher).
- **Server Control:** the older Windows control panel, still installed alongside Command.
- **Live map** in Server Control and Command.

---

## AltBotMod Command (web launcher)

AltBotMod Command does the same jobs as Server Control in a modern window and works from a phone or tablet on your home network.

**What it does:** starts and stops servers; manages bots, rotation and rules; starts and stops BF-Portable services; shows a live map; manages learning files; launches the game and joins servers.

**Shortcuts:** the installer adds *AltBotMod Command* to the desktop and Start menu (under AltBotMod), plus *AltBotMod Command (browser)* to open it in your normal browser. Uninstalling removes them.

**Files:** `BotMod\ServerControl\` (`AltBotMod-Command.ps1`, `Command-*.bat`, and the `Command-Web\` folder).

**Phone access**
1. Run `BotMod\ServerControl\Command-Allow-Phone.bat` once. It adds a Private-network firewall rule.
2. In Command, choose *Connect phone* and scan the QR code.

**Safety**
- Every start creates a new private pairing key. Other hosts and other web pages are refused.
- Edits made elsewhere are detected and never overwritten.
- Launching the game, joining servers, changing launcher paths and changing IP addresses work only on the PC itself.
- The server admin password is never shown in Command or in the logs. Database passwords are hidden.

**If you used the stand-alone Command before:** exit its tray icon ("Exit launcher") once. The new one would otherwise show "Restart the Command host".

---

## Squad orders

Type these in game chat:

| Command | Effect |
|---|---|
| `!attack B` | Your squad's bots attack objective B. |
| `!defend A` | Your squad's bots defend objective A. |
| `!attack` or `!defend` alone | Uses the objective you are looking at. |
| `!orders` | Shows the current order. |
| `!orders off` | Cancels the order. |

- Orders override the team's plan for **4 minutes** or until the objective is taken or lost, whichever comes first.
- On Rush, A and B are the two M-COMs of the current stage.
- The commo rose's Attack order on an objective works too.

---

## Settings reference

Settings are in `enhanced.ini`, section `[AltBotMod]`. They are also editable in Server Control and Command. Restart the server after changing them.

| Setting | Default | What it does |
|---|---|---|
| `FunBotsMesh` | 1 | Objective navigation mesh near capture points, M-COMs and bases. 0 turns it off. |
| `VehicleWalkMeters` | 250 | Furthest a bot on foot walks to a vehicle after the opening minutes. |
| `JetAreaMarginMeters` | 900 | Extra width of the jets' patrol area. |
| `DoorGunMeters` | 600 | Range for helicopter door gunners and Little Bird bench seats. |
| `LockOnSeconds` | 0.5 | How long a bot holds a lock before it is put in place. |
| `LockOnLaunchers` | 1 | Javelin, Stinger and Igla use. |
| `GadgetVariety` | 1 | Gadget swaps per life. 0 uses only BotMod_loadouts.txt. |
| `C4Attacks` | 1 | C4 runs at slow enemy vehicles. |
| `BotTicketCost` | 0.5 | Tickets per bot respawn (0-2). |
| `VehicleCountermeasures` | 1 | Smoke from bot tanks and IFVs when hit. |
| `VehicleDamageTaken` | 1 | Damage vehicles take, 0.25x to 4x. |
| `VehicleDamageDealt` | 1 | Damage done from vehicles, 0.25x to 4x. |

---

## Learning files and data folders

| Location | Contents |
|---|---|
| `BotMod_learned\` (next to the server) | One learning file per map and mode, for example `MP_001_ConquestLarge0.learn`. Stuck spots, passable spots, killing grounds, learned routes and landing spots. Read at map load. |
| `BotMod_breakable.txt` | Materials that break when knifed or shot, and those that never do. |
| `BotMod_traces\navzones\` | Objective meshes (`<Level>_<Mode>.zon`). |
| `BotMod_loadouts.txt` | Optional gadget loadouts (used when `GadgetVariety` is 0). |
| `BotMod_backup_<date>\` | Files the installer replaced. |

To start learning from scratch on a map, delete its `.learn` file while the server is stopped.

---

## Log lines to look for

Each server writes `BotMod_<port>.log`. Useful lines:

- `enhanced: AltBotMod V34 - Squad Up` (version loaded)
- `zone: ... walks the objective mesh` (mesh routes in use)
- `zone: the mesh connection ... closed` (a stuck mesh link was closed)
- `breach: X's way is shut at ... shoots across it` (door breach)
- `breach: X shot its way open`
- `passenger:` (cargo getting out or jumping)
- `c4:` (C4 attacks)
- `rocket: ... (locked)` (lock-on)
- `vehicle: ... - smoke` (countermeasures)
- `spawn: team N bots die with no shooter`
- `borrows the ... grid` (a level without its own routes)
- `perf:` every 30 s: tick timing, including `input split avg/max ms` (what costs time in each tick)
- `vtrace:` the first six times a vehicle loses speed, the last 45 frames of that driver

---

## Testing

The validation suite lives in `validation\`.

- **Release checks:** 66+ offline check groups with AddressSanitizer and UndefinedBehaviorSanitizer. They cover the driving simulation, the throttle and lane rules, the lock-on put in place, jets' area and stall recovery, the vehicle position, and the zone and navigation logic.
- **LAN checks:** 31 checks against two fake BF-Portable backends: finding servers, registering, players per PC, and joining.
- **Installer checks:** `altbotmod_installer_tests.ps1` runs real file operations against a fixture (174 integration checks).
- **Server Control and Command tests:** cover IP targets, join command lines, the accounts file and the running Command host.

These checks are offline. They do not replace a live round.

---

## Known limitations

- Door breaches are slower for doors directly north of a bot, because the game turns it the long way round to face them.
- Death Valley M-COM 4 uses the older routing.
- Small real dips in vehicle speed can still happen when the server syncs, as they do with human drivers.
- The C4 detonation, Javelin and Stinger locks, and tank smoke depend on the game's own inputs and have not been watched closely in a live round.
- BF-Portable's Blaze emulator does not respond to player-limit changes, so BF-Portable servers must run at 64 players (the installer sets this).

---

## Not yet tested in a live round

Please report what you see for these:

- C4 detonation (the aim button)
- Javelin and Stinger locks (`rocket: ... (locked)`)
- Tank smoke (`vehicle: ... - smoke`)
- Mesh routes and stuck-link closures (`zone:` lines)
- Door breaches (`breach:` lines)
- Passenger exits (`passenger:` lines)
- Jet stall recovery and the wider patrol area

If something looks wrong, run `Collect-Diagnostics.bat` and send the folder it makes with a description of what happened.

---

## Uninstalling

1. Stop the BF3 servers.
2. Run `Uninstall-AltBotMod.bat`.
3. Shortcuts and the mod's files are removed. Your settings, learning files and backups are kept unless you delete them.
