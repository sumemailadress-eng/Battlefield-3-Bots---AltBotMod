# AltBotMod V34 - Squad Up

This is a native bot mod for the Battlefield 3 dedicated server: no Venice Unleashed, and clients need nothing extra. It works with the ALP, bf4play and BF-Portable builds of `bf3_server_Final.exe`; the installer checks for that file.

## Install (any PC)

1. Stop the BF3 server(s).
2. Extract this package.
3. Double-click `Install-AltBotMod.bat`.

The installer keeps your settings and adds the new ones (V31: `LockOnSeconds` and `DoorGunMeters`). `JetAreaMarginMeters` still at the old default of 350 is moved to 600; a value you changed yourself is kept. Anything it replaces is backed up to `BotMod_backup_<date>`.

**Checking that it loaded:** the log `BotMod_<port>.log` shows `enhanced: AltBotMod V34 - Squad Up`.

**Going back:** the V33, V32 and V29 DLLs are in `rollback\AltBotMod-V33`, `rollback\AltBotMod-V32` and `rollback\AltBotMod-V29`. Copy one over `BotModCore.dll` in the server folder.

## V34: Rush M-COMs, fewer bots standing around, air combat, squad orders, Fun-Bots' objective mesh (2026-10-23)

All of this is built from your last games' logs (V33 on Caspian Border, V32 Rush on Seine and Caspian) and tested offline. None of it has been played live yet.

**Fun-Bots' new navigation at the objectives.** Fun-Bots' development version (3.6.0-dev7) has a walking mesh around every capture point, M-COM and base. It knows the walls, floors, stairs and doorways there, and where to jump and crouch. V34 ships those meshes for 97 map/mode combinations (`BotMod_traces\navzones`).
- **Near an objective:** bots walk the mesh first, then Fun-Bots routes, then the grid as before.
- **A spot bots get stuck on:** it costs more, and after 3 stuck bots it is closed for that round (as Fun-Bots does).
- **Setting:** `FunBotsMesh=1` (0 turns it off).
- **Coverage:** every one of the 218 Rush M-COMs (Rush and Squad Rush, all maps) sits in a mesh M-COM zone. 217 of them can be walked to over the mesh; the other one (Death Valley M-COM 4) uses the old routing.

**Rush: M-COMs indoors and behind glass.**
- **Getting there:** the bot going for the M-COM uses the mesh way into the building.
- **A window in the way:** within 15 m of the M-COM, a pane between the bot and the M-COM gets knifed. If it is further in and the bot gets no closer, it walks up to the pane first. If the knife does not break it, the bot shoots it.
- **Glass no longer written off:** a pane counted as "unbreakable" only after a second failed try. Before, one failure was enough (64 kinds of glass were never tried again).
- **Guards:** they stand at mesh points around the M-COM.

**Bots standing still or turning in circles.** Your Caspian Border log showed the biggest cause: in 17 minutes, bots dropped their objective 242 times to walk 300-700 m back to base for a vehicle. 84 of those vehicles were taken at the spawn screen and 92 destroyed before they got there, so the bots turned round again and again.
- **Vehicle walks:** after the opening minutes, a bot on foot walks at most 250 m to a vehicle (`VehicleWalkMeters`). With its objective within 120 m, only to a vehicle right beside it (40 m). The round's opening rush to the vehicles, and spawning into vehicles, are unchanged.
- **Supply runs:** a medic or support that could not reach a caller is not sent to the same caller again for 45 s. In the log, one walked back and forth 3 times.
- **Combat moves:** a move that waited for the planner is checked again from where the bot now is. Before, bots "moved" 2 m and stood.

**Air combat.**
- **Stalled jets recover:** a jet that falls below flying speed puts its nose down, levels its wings and goes to full power until it flies again. Your log had F/A-18s at 9-26 m/s, 900 m up, nose high, still pulling - that is the "jets don't do anything".
- **More room:** the jets' area is wider (margin 600 → 900 m, at least 1.4 km each way). Jets spent about half the round turning back at the edge. `JetAreaMarginMeters` still at the old 600 is moved to 900.
- **Flares and smoke at the right time:** a pilot pops flares when an enemy missile lock on it is 75% complete, before the missile is fired. Tanks pop smoke when fully locked. This is read from the game's own lock-on system. Before, both only came after a hit.
- **Air-to-air vs air-to-ground:** jets keep their heat-seeking missiles (air-to-air) for aircraft and use guns on the ground. They also go for enemy attack helicopters first. Attack helicopters keep their rockets and cannon for the ground and missiles for aircraft.

**Squad orders.**
- **Giving orders:** in game chat, `!attack B` or `!defend A` sends the bots in **your squad** to that objective, ahead of the team's plan for 4 minutes or until it is done. Typing `!attack` or `!defend` alone picks the objective you are looking at.
- **Rush:** A and B are the two M-COMs of the current stage.
- **Other commands:** `!orders` shows the current order and `!orders off` cancels it. The commo rose's Attack order on an objective works too.
- **Bot squad leaders:** they announce their squad's objective in the squad chat when it changes, e.g. "[Squad] xFoo: attack B".

## V33: smooth driving, AltBotMod Command (2026-10-23)

**Vehicles no longer stop-and-go.** In the V31 driver traces the throttle was held at 1.0 on every frame, so the bots never let go of the gas. The cause was the position the mod read for a moving vehicle:
- **Body position:** it never moved while the vehicle was driven.
- **Crew seat position:** it only caught up about every 60 frames (~2 s, the server's periodic sync).
- **Effect:** between syncs the drive code measured 0 m/s and then a jump. Its speed, braking, steering and "am I stuck?" checks all came from that, so the vehicle surged and slowed.
- **Fix:** every vehicle position now comes from the vehicle's physics transform, which moves smoothly every frame. Body and seat are only the fallback.
- **What stays:** small real dips can still happen when the server syncs, as for human drivers.

**The disconnect on your third server (port 25302) was not caused by the bots.** That server's `Instance3\Admin\Startup.txt` set `vars.maxPlayers 32`, and BF-Portable's Blaze emulator never answers a player-limit change.
- **What happened:** the game server retried every 90 s ("setPlayerCapacity ... SDK_ERR_RPC_TIMEOUT"). At 23:05:56 it gave up on Blaze ("ProtoFire disconnect detected ... ERR_TIMEOUT") and dropped every player.
- **Your PC:** that line is now `vars.maxPlayers 64`, and `_StartServer3.bat` has `PLAYERS=64`, like your other three servers. **Restart server 3** for it to take effect. The AltBotMod tick ran normally the whole time (30 ticks/s).
- **The installer:** it now puts any BF-Portable server that isn't at 64 (`Startup.txt` or `_StartServer*.bat`) back to 64, changing only that value and backing it up first. Server Control and Command already only allow 64.

**AltBotMod Command (the web launcher) is installed with AltBotMod.** It does the same jobs as Server Control (servers, bots, rotation, rules, BF-Portable services, live map, learning, launch and join) in a modern window. A phone or tablet on your home network can use it too.
- **Shortcuts:** the installer puts *AltBotMod Command* on the desktop and in Start menu > AltBotMod, plus *AltBotMod Command (browser)* there to open it in your normal browser. Uninstall removes them.
- **Files:** they live in `BotMod\ServerControl` (`AltBotMod-Command.ps1`, `Command-*.bat`, `Command-Web\`). Server Control and `Start-BF-Portable.bat` are unchanged.
- **Phone access:** run `BotMod\ServerControl\Command-Allow-Phone.bat` once. It adds a Private-network firewall rule. Then use *Connect phone* in Command for the QR code.
- **Safety:** each start makes a new private pairing key. Other hosts and other web pages are refused. Edits made elsewhere are detected and not overwritten. Launcher paths, IP changes and game launch/join only work on the PC itself. The server admin password is never sent to the page.
- **If you used the stand-alone Command before:** exit its tray icon ("Exit launcher") once. The new one then opens normally; the old one would show "Restart the Command host".

## V32: open doors, better use of the Fun-Bots routes (2026-10-23)

**Doors that open with a bang (Epicenter and anywhere else).** A bot stuck at a shut door on its route holds still, then fires across the door and low on both sides of it for 3.5 s. That catches the red gas tanks that blow Epicenter's doors open, and also boards and thin doors. Then it checks again.
- **When it triggers:** something solid at knee, chest and head height within 2.5 m, and the route continues behind it. So not a slope or rocks.
- **Limits:** twice per spot every 30 s at most; otherwise the usual stuck recovery goes on.
- **Log lines:** `breach: X's way is shut at … - shoots across it and low on both sides` and `breach: X shot its way open`.
- **Limitation:** a door straight north of the bot (heading close to 0°) takes longer to sweep. The game only turns a bot past that heading the long way round.

**Infantry onto the Fun-Bots routes.** Your last log, Gulf of Oman: 30,231 of 39,347 route attempts ended in "no way onto a route", and the bot walked the navigation grid instead.
- **Cause:** many of those had only run out of the tick's planning time.
- **Fix:** a bot now waits for the next tick and tries again. A join that really failed is not retried from the same spot for 10 s, which also saves server time.
- **Vehicles:** they already take the Fun-Bots routes almost every time: 696 of about 730 drives in that session.
- **Log:** the `trace:` minute line now also counts these.

**Squad spawns:** as in Fun-Bots' latest development version, bots no longer spawn on a squad mate who is in a fight: one hit in the last 5 s, or a living enemy within 30 m.

**All modes.** Every one of the 215 level / mode combinations, Team Deathmatch, Close Quarters TDM, Squad DM, Gun Master, Domination, Rush, Squad Rush, CTF and Air Superiority included, has Fun-Bots routes of its own or borrowed from the same level (`ROUTE-COVERAGE.txt`). All 40 Rush route files lead to every M-COM.

**The Fun-Bots repository (checked 2026-10-23).** The development branch is at 3.6.0-dev7, last commit 2026-10-21, and it is a big change:
- **New navigation.** Bots walk a navigation mesh at the objectives and spawns, and the recorded routes now stop at the mesh.
- **No objectives in its route files.** Its Conquest, Rush and Domination files carry no objective tags any more, so they can't replace ours: our bots find their objectives through those tags. Its Team / Squad Deathmatch files are the same as ours.
- **Taken over:** "no spawn on a mate in a fight", above.
- **Already in AltBotMod:** defenders hurry to an armed M-COM, and bots get out of the water.
- **Not taken over (yet):** the navigation meshes.

**Tests:** 68 check groups pass offline, including a simulated door with a gas tank beside it. Nothing here has been run live yet.

## V31.1: less server load (2026-10-22, from the V31 rounds)

**What the log showed.** With 58 bots, the bot mod took 7-14 ms of every 33 ms server tick on average, and up to 60-69 ms on its worst ticks. On Gulf of Oman the server fell to 23-26 ticks a second. A tick that runs late means players' moves and shots are checked late: you get pulled back, and hits don't count. Most of that time went to:
- **Gunners' sight checks.** Every gunner traced a line of sight to its target every tick, and, every third of a second, to every candidate target that scored better than the best so far. With 600 m door guns that could be dozens of traces per gunner.
- **Route planning:** up to 8 ms in a busy tick.

**What changed.**
- **Gunners pick targets the cheap way.** Every candidate is scored first, without tracing, and only the 6 best are traced, best first. The first one in sight is the pick, the same one tracing them all would give when it ranks in the best 6. A simulation of 400 random rounds with 48 soldiers confirms it: at most 6 traces per pick.
- **Fewer repeat checks.** The sight to the current target is checked every 6th tick instead of every tick, and targets are re-picked every 0.4-0.5 s (was 0.33 s), staggered across bots.
- **Vehicles' bumper rays** run 6 times a second below 12 m/s (10 above). On a recorded road they don't run at all, as in V29.
- **Route planning** takes at most 6 ms in a busy tick (was 8).
- **More detail in the log.** The 30-second `perf:` line now also splits the input time: `input split avg/max ms: driving … aircraft … gunners …`. If the server still lags, the next log shows which part.

**Tests.** 67 check groups pass offline, plus the new gunner-pick simulation. Not tested in a live round yet.

## V31: V29 plus the safe V30 features (2026-10-19)

V30 and V30.1 crashed the server. V31 starts again from V29, the last version that never crashed, and adds only these:

**Door gunners keep shooting.**
- A gun that has turned for the bot and then stops is at the end of its arc. The gunner keeps the gun and picks another target for 4 s.
- V28-V29 dropped the gun there ("does not turn - looking for its real one").
- Helicopter door guns and Little Bird bench seats reach `DoorGunMeters` (default 600 m). V29: 220 m at soldiers.

**Jets turn back less.**
- The turn back starts 3 s before the edge of the area (was 6 s) and ends 200 m inside, heading in.
- The area is wider: `JetAreaMarginMeters` 600 (was 350), at least 1000 m each way from its middle (was 800 m).

**Swimming.**
- A bot in the water (the surface 0.9 m or more above its feet) swims for its objective. If something stops it (a rock, a hull), it turns off for 2 s. Once ashore it plans its route again.
- A bot cut off with water on the way (Wake Island's carrier, a pier, a rock), with no route to any objective three times in 20 s, walks straight at the objective, over the side, and swims.
- This works on every map.

**Faster locks.** `LockOnSeconds` (default 0.5 s) is how long a bot holds a lock-on weapon on its target before the lock is put in place. V29 used the weapon's own 1.5-2.5 s.

**Rush routes, checked.** All 40 Rush and Squad Rush route files have 2 or more Fun-Bots routes to each of their 222 M-COMs, and an arm / disarm point. If bots ignore an M-COM, it happens in the game logic. I need a log from a Rush round to find it.

**A safety check on getting out.**
- The mod now calls the game's "exit vehicle" only for a bot that the vehicle still has in a seat, and at most once per frame.
- V30's two crashes happened in the game's exit code, when a helicopter crew bailed out together.

**Driver trace (read only).** The first six times a vehicle loses its speed on a level, the log gets the last 45 frames of that driver (`vtrace:` lines). Each frame shows:
- where the game says the vehicle is: its body, the driver's seat, and its physics;
- the throttle, brake and steering in the input;
- the time of every server frame.

This is to find the stop-go, which is not fixed.

**Left out of V31 (from V30 / V30.1):**
- **Vehicle weapons and kit** (heat seekers, rocket pods, guided and TV missiles, AA missiles, TOW, flares added to the bots' unlocks). Both crashes came right after an attack helicopter crew bailed out, with those items equipped.
- **Hooks on the vehicle's input calls.**

**Tests.** 67 offline check groups pass. They include a check that V31's vehicle input and kit loading are V29's, line for line, and 164 installer checks. Nothing here has been run in a live round yet.

## V29: full throttle - from the V28 match log (2026-10-19, Operation Firestorm)

**What the log showed.**
- **Stop-go.** The V27 "wait your turn" rule fired about 600 times in 16 minutes; one Growler waited about once a second for 70 seconds. Drivers averaged 3.4–5.5 m/s on open Fun-Bots roads. On a road the brakes still answered to the middle sensors within 4 m (479 "sees something ahead" at 0 m/s), and after every dip the speed target climbed back 8 m/s per second with a partial throttle (0.35 + 0.12 × the error). That gave "accelerates slowly, brakes, accelerates slowly".
- **Lock-ons.** 44 attempts. The game's lock controller was active in all of them, but its target search never picked the target ("a target picked 0.0 s" every time), so the lock never built up. The 3-second fallback shots never left the tube: the launchers only fire when locked.
- **Door miniguns.** The gunners aimed but had to be within 0.35 m of the target to fire (0.2° at 100 m) - from a moving helicopter, never.
- **Jets.** Strike runs were set up 1.7 km from the target, and after a pass a jet flew straight on for up to 7 s. Both took jets off the map.

**Driving, redone the Fun-Bots way.**
- **Full throttle unless there is a real reason**: a bend in the recorded road tighter than the vehicle can take, the end of the route, hard steering, or a vehicle in its lane. No smoothing and no partial throttle on the way back up to speed (`bm_drive_throttle`).
- **On a recorded road nothing static counts any more**: no sensor braking, no swerving. People drove that road. Off the road (grid legs, joins) the sensors work as before.
- **Vehicles by where they are, not by what a ray hit.** A vehicle in the lane (straight ahead up to 15 m, then along the route itself) is followed at its own speed. The driver stops 6 m short of a stopped one and, after 1.5 s, passes it on the side it stands less on, then pulls back onto the road. Two vehicles head on both stop and both pass on their right.
- **"Wait your turn" is gone.** The lane rule above replaces it.
- **No on-foot buttons at the wheel.** Drivers and pilots never get the soldier's jump, crouch, prone or sprint presses (stairs, stance, learned "jump here"). In a vehicle seat those can act as the handbrake.
- **Re-plans keep the throttle.** A change of objective or a re-plan keeps the last throttle for up to 3 s (was 1.5 s); a change of objective used to cut it to zero.
- **Tested in a simulation:** a tank and a jeep on the longest Fun-Bots road of 8 maps. Full throttle 59–71% of the time for the tank (2–13% the V28 way). No stop-go. A vehicle stopped on the road is stopped for and passed with 4–5.6 m between them. Only a simulation - the round will tell.
- **New log lines to check:** the 20 s report now reads `full throttle N%, braking N% (bends, vehicle in lane, hard steering, obstacle, stop), N speed drop(s) with the throttle down, N re-plan(s)`. A line `lost N m/s in 1/3 s with the throttle down` means something outside the driving code took the speed - a collision, the ground, or something else - and gives where and what the sensors and the lane showed.

**Lock-ons lock.** Once a bot has held its aim on a vehicle for the weapon's own lock time, the lock is put in place the way the game's own target search does it when it finds one: a target holder for that vehicle (made with the game's allocator and weak reference), then the game's "set locked". The weapon then fires its guided missile. This applies to Stingers, Iglas and Javelins, to jets' and attack helicopters' heat seekers, to an attack helicopter gunner's guided missile, and to AA vehicles' missiles. Switch it off with `LockOnAssist=0`.
- Log lines: `lock: X's lock on the Mi28 Havoc is put in place after 1.0 s on target`, then `rocket: X's Stinger / Igla is away` (infantry) or `air: X - the game locks its missile on - fires` (aircraft).
- **Not tested in a live round yet.** It uses game functions found in the game's code; the offline check runs the logic against stand-ins.

**Door guns fire.** A transport helicopter's door gun now fires once it points within 3 m of a soldier or 5 m of a vehicle (at least 1.7°); the minigun's spread does the rest. The search for the seat's gun now nudges it for 0.4 s at full input (was 0.2 s), because door guns turn slower.

**AA vehicles** use their missiles against aircraft for 4 s in every 12 s. A vehicle that shows no missile lock in its first window stays on the cannon.

**Jets stay on the map.**
- A jet now keeps to a box round every flag, both bases and the airfields, plus `JetAreaMarginMeters` (350 m), at least 1.6 km across.
- If a jet leaves the box, or would within 6 s at its speed and heading, it turns back towards the middle, whatever it was doing.
- Strike runs are set up inside the box, the patrol goal stays inside it, and an extend lasts 5 s at most (was 7).
- **Simulated:** jets flying straight out from 600 m inside never leave. From 150 m inside at up to 200 m/s, they are out at most 167 m for 6 s. Strike runs and dogfights stay inside and still dive, gun and fire missiles.
- The box is not the game's real combat area, which BotMod does not read. A jet turning back shows as `air: X (F/A-18E) turns back into the combat area`.

**New settings (also in Server Control):**

| Setting | Default | What it does |
|---|---|---|
| `LockOnAssist` | 1 | Put a bot's lock-on lock in place after the weapon's lock time on target. |
| `JetAreaMarginMeters` | 350 | The jets' box: flags, bases and airfields plus this much. 0 = no box. |

## V28: full crew

**Gamertags.**
- Bots now join under made-up gamer tags instead of "[BOT] Support RU 2". Each bot still keeps its roster identity behind the scenes, so its side, class and place on the team work as before.
- The tags come from `BotMod\BotMod_names.txt`. If the file is missing, the mod writes one with 300 made-up tags for you to edit; delete it to get a fresh list. A name a player on the server already has is never used.
- To switch it off: `BotNames=0` in Server Control (Classes) or `enhanced.ini`. The setting applies to bots created after the change.

**Every gun gets used.**
- Every seat of a helicopter or jet other than the pilot's now looks for a gun, including the transport helicopters' door miniguns. Before, some seats were skipped.
- A seat where no gun was found searches again every 20 seconds. Before, it gave up for good after four tries: three rounds logged 182 "found no weapon", mostly tank roof guns and helicopter door guns.
- Jeep and buggy machine guns (seat 1) already worked and keep working.

**Staying on the road.**
- When a vehicle is more than 2 m off the recorded road, its steering target moves closer (down to 8 m), so it steers back onto the road instead of running along the verge through fences.
- The steering target also stops at bends of 17° (was 34°), so corners are no longer cut across the inside verge.
- Simulated on 5 maps, a tank stays within 0.3 to 0.7 m of the road on average.

**Tickets.**
- The mod now holds back its own ticket bleed only when the game's bleed is running.
- Before, a human player dying (1 ticket) also counted as "the game is bleeding". While you were on a team, that held back your team's bleed and only the other side bled. That is why RU tickets dropped faster.

**Lock-on weapons.**
- Every lock-on attempt now writes a `lock:` line when it ends, however it ends: how long the game's lock controller was active, whether the game picked a target, the best lock progress, and how far off the weapon pointed.
- If the game is still "locking" after 3 seconds with the aim on the target, the bot fires anyway. The `lock: … launch was …` line then says whether a missile left without a lock.

**128 players: not possible.**
- The BF3 server holds at most 64 players.
- BF-Portable needs the limit to stay at 64: its login emulator drops every player about 2 minutes in at any other value (found in V16.2.2).

**AC-130 gunship.** Bots board it and attack it when a map has one. Operation Firestorm Conquest has none: none of the three logged rounds had one in the vehicle list.

## V27: clear road - from the V26 match log (2026-10-19, Operation Firestorm)

**What the log showed:** V26's re-plan loop was gone (84 route plans in 6 minutes, down from 2,286 in 26 minutes in V25). But drivers averaged 5 m/s: 129 of the 176 twenty-second reports were between 2 and 6 m/s, and they slowed down for something only 11% of the time. The vehicles were not braking. They were turning.
- **Tanks turning in place.** With the road behind or beside it, a tank turned on full lock with throttle on. Its aim point was only 8 m ahead, inside its own turning circle, so it drove circles round it.
- **Spawn jams.** Team vehicles that spawned side by side pushed into each other, and both turned on the spot.

**Driving.**
- **Tanks pivot.** A tank now turns on its tracks with full lock and no throttle until it faces the road (within 20°), then drives. Wheeled vehicles still turn forwards on full lock.
- **Aim point further ahead.** It is now 15 m ahead when slow and up to 30 m at speed (was 8–25 m). Steering is smoother, and the speed is not capped by large heading errors.
- **One at a time.** When two team vehicles are nose to nose or one is just behind the other, both barely moving, the one lower on the roster goes first. The other waits up to 6 s and logs `waits its turn`. A moving vehicle is simply followed, and a vehicle driven by a person or standing empty is driven round.
- **Tested in a simulation:** a tank that starts facing the wrong way on the longest Fun-Bots road of 5 maps turns round once (about 4 s) and then averages 12–15 m/s for the whole road.

## V26: air superiority - from the V25 match log (2026-10-19, Operation Firestorm)

**What the log showed:**
- **Lock-ons.** The V25 hook ran: 124,793 lock updates, 22,397 of them on a bot's weapon. The game's lock still never got past 0%.
  - The controllers were already switched on, so that was not what blocked the lock.
  - The cause: the game searches for a target along the weapon's own aim on the server. For a bot that aim does not follow where the bot is aiming closely enough, because a bot has no camera on a client.
- **Stop-go.** There were 2,286 route plans in 26 minutes, for example one tank took the same route 20 times in 2 seconds.
  - The cause was V25's three-point turn. When the vehicle's rear was blocked, the reverse was cancelled, the route dropped and the same route planned again.
- **Jets.** They set up 178 attack runs but dived only 50 times. Most set-ups were flown round again because the target was not lined up closely enough.
- **Helicopter gunners** used the jeep gunner's target list: soldiers first, and vehicles and aircraft treated the same. A gunner kept shooting at a soldier while an enemy helicopter shot it down.

**Lock-on weapons.** The game's target search now gets a copy of the weapon's aim that points exactly where the bot is aiming, from the weapon's own position. The game's own weapon is not touched.
- This covers Stinger / Igla, Javelin, aircraft missiles and gunner seats with lock-on weapons.
- When no missile leaves the tube, the log line gives how far off the weapon was pointing.

**Driving.**
- A blocked rear no longer drops the route. The vehicle turns forwards on full lock instead.
- Every re-plan now records its reason. When a driver re-plans 6 times in 10 seconds, a `WARNING … re-planned its road 6 times` line names the cause.
- Every 20 seconds each driver logs its average speed and what it slowed down for: bends, an obstacle ahead, hard steering, or something else. That shows any remaining braking in black and white.

**Jets.**
- They start an attack run whenever the target is within 34° of the nose (was 20°), from 300 m to 2.5 km away (was 400 m to 2.2 km).
- They dive deeper (down to 200 m away and 50 m up) and stay in the dive longer, so the guns get their time.
- The firing window is wider (30 m).
- Heat-seeking missiles against aircraft go through the game's own lock, as above.

**Target priorities.**
- **Helicopter gunners:** enemy aircraft within 900 m first, then armour and AA vehicles within 700 m, soldiers with launchers within 400 m, then other soldiers.
- **Tanks:** unchanged. A soldier close by comes first, then enemy armour within 400 m, then soldiers further out.

**Infantry engage from further away:**

| Level | Engages from | Was (V25) | Sees from | Was (V25) |
|---|---|---|---|---|
| Elite | 300 m | 240 m | 350 m | 290 m |
| Veteran | 230 m | 170 m | 270 m | 200 m |
| Hard | 130 m | 90 m | 150 m | 100 m |

Elite also gets a 170° field of view.

**Server Control.** All 86 bot settings are now on the settings page. These 22 were added:
- **Vehicles:** return distance, steering gains, stuck time, bail-out health, armour repair and tactics, ballistic aim, diagnostics.
- **Aircraft:** jet speed, ceiling and orbit; helicopter orbit; wait and attack times; deployment learning; the input signs.

**Not tested in a game yet:** the lock-on fix, the jet runs and the driving changes. Check these log lines:
- `rocket: … the game locks …`;
- `drive: … last 20 s: …`;
- `drive: WARNING …`.

## V25: lock and load - from the V24 match log (2026-10-18, Operation Firestorm)

**What the log showed:**
- **Lock-ons never launched.** In V23, bots tried 81 times and no missile left the tube.
  - The cause: the server's lock-on controller (`ServerLockingController`) only searches for a target once the game has activated it.
  - The game activates it from a player's own input, which a bot never sends, so a bot's launcher never locked.
- **Vehicles re-planned constantly.** There were 939 route plans in 16 minutes, one T90 alone re-planned 40 times in 2 seconds, and every re-plan took the driver off the throttle.
- **Why drivers re-planned:**
  - A route that started behind the vehicle made it reverse.
  - With the rear blocked, it dropped the route and planned the same route again.
- **Why drivers left the Fun-Bots roads:**
  - Every plan counted as a route "try", so drivers soon used them up.
  - The convoy system ("blocked behind a stopped vehicle") and a stuck check both dropped Fun-Bots roads for the rest of the trip.
- **Braking with nothing ahead.** Drivers braked for anything any of the 7 bumper sensors saw, including walls and parked vehicles beside the road. Speed was also capped at 12 m/s whenever the aim point was 31° or more off the nose.

**Lock-on weapons: the game's own lock.**
- V25 hooks `ServerLockingController::update`. When a bot's aim is on a target, its weapon's lock controller is switched on, exactly as the game does it for a player.
- From then on the game does everything itself: it finds the target, counts the lock time and launches the missile.
- **Where it applies:**
  - Stinger / Igla and Javelin: the bot fires the moment the game reports a lock.
  - Aircraft missiles.
  - Gunner seats with lock-on weapons.
- **What to look for in the log:**
  - `rocket: … the game locks the … on the …` and `… is away`.
  - On a failure, the line names the controller state and the hook's counters.

**Driving.**
- **The convoy system is gone.** No following gaps, no waiting, no "routes round a stopped vehicle". A vehicle in the way is treated like any obstacle: the driver steers round it on the clearer side and brakes only when it is right in front.
- **Braking:**
  - The brakes only answer to what is in the vehicle's path: the middle sensors, or the outer ones within 4 m.
  - Speed is only capped when the driver must steer hard (60° or more off the aim).
- **Re-plans no longer touch the throttle.** The last command is held for up to 1.5 s while the new route is planned.
- **Turning round:** a route that starts behind the vehicle gets a turn on the spot (full lock forwards, or a three-point turn), not a reverse and a new plan.
- **Fun-Bots and player routes stay in use:**
  - Planning a route is no longer counted as a "try"; only getting stuck on a road counts.
  - No check drops the Fun-Bots roads for the rest of the trip.
- **Joining the road:** when the road is more than 15 m away and the straight way to it is blocked, a short path over the driving map leads onto it. That map has the learned stuck spots struck off.

**Flags: nearest first.**
- Each team ranks the enemy and neutral flags by how far they are from its own ground.
- Soldiers take the two nearest, and vehicles and aircraft the three nearest. The next flags follow as they fall.
- Before V25 the team spread over every flag at once.

**Harder bots.**
- **Elite:**
  - engages from 240 m (was 130 m) and sees 290 m (was 150 m) in a 160° view;
  - reacts in 0.05 s;
  - aim lag is 0.015 s;
  - opens fire within 0.45 m of the aim point.
- **Veteran:** engages from 170 m and sees 200 m.
- **Defaults for new installs:** infantry 260 m, recon 450 m, armour 800 m (400 m against soldiers).

**Not tested in a game yet:**
- the lock-on hook, which is new code in the game's weapon update;
- the new driving and flag order.

The log lines above show each one.

## V24: on the road - from the V23 match log (2026-10-18, MP_012)

**What the log showed:**
- **Stop-go on recorded roads.** Drivers had to reach every Fun-Bots waypoint, and to within 1.75 m at any "corner". On a hand-recorded road almost every point is one, so the driver aimed at a point a few metres off its nose and braked for its own heading error. Bends were judged over ±10 m, so a recording's wobble looked like a hairpin.
- **Brakes for the roadside.** There were 147 "sees something ahead" brakes on the recorded road itself, nearly all of them walls, rocks and verges beside it.
- **Convoys.** A follower stopped dead whenever it got within 65% of the gap to the vehicle in front (13–18 m at speed), then went again.
- **Helicopters.** They only went after an enemy helicopter inside 600 m, so soldiers on the ground almost always came first. A duel was broken off after 8 s, and the pilot flew out to a 350 m ring before coming back.

**Driving (Fun-Bots style).**
- Steering aims at the road 8–25 m ahead (pure pursuit), not at the next waypoint. Progress is the nearest road point ahead.
- Full speed along the road. Drivers only slow for a real bend (radius under 28 m, judged over ±25 m of the road itself) and for the end of the route.
- On a Fun-Bots road (within 4 m of it) the bumper sensors ignore the verge. Only a vehicle in the way, or a wall dead ahead within 4 m, counts. Off the road, every sensor still counts.
- Convoys run at the leader's pace and stop only when about to touch it. A leader on a bend is still followed.
- Drivers retry Fun-Bots routes up to 40 times on a trip (was 6) before taking the driving grid.
- Tested on the longest Fun-Bots road of 8 maps in a simulation:

| Road | Pointless slow-downs (V23 → V24) | Average speed (m/s) |
|---|---|---|
| Alborz, 8.8 km | 95 → 2 | 15.8 → 22.6 |
| Bandar Desert, 9.5 km | 84 → 5 | 19.6 → 26.7 |
| Firestorm, 6.3 km | 46 → 0 | 13.9 → 17.2 |
| Armored Shield, 7.5 km | 56 → 2 | 17.4 → 20.0 |

**Helicopters.**
- An enemy helicopter within 1.2 km is the pilot's first target: attack helicopters before scouts, and ahead of soldiers on the ground.
- A duel goes on until it is settled. The pilot breaks off only inside 80 m, or inside 150 m when closing fast, and comes back from 250 m.
- The firing window on an aircraft is 12 m (was 8 m).

**Not tested in a game yet:** the new driving and the helicopter duels. The log shows them as before (`drive:` and `air:` lines).

## V23: air and armour - from the V22 match log (2026-10-18, Alborz and Bandar Desert)

**What the log showed:**
- **Lock-on launchers:** 80 tries, 0 missiles. The game never launched one for a bot that aims down the sight.
- **Jets:** they spent the round chasing each other. Every enemy aircraft within 3.5 km came before anything on the ground.
- **Armour:** tanks never fired their guns at anyone closer than 15 m. Their own front plate also counted as "cover" for a soldier standing right in front.
- **Vehicles:** they crept along at about 3 m/s whenever their bumper sensors, which looked straight off the bonnet, saw roadside objects on a bend (1,299 times). Armour also slowed for 2 s every 8 s whenever an enemy vehicle was in sight.

**Lock-on launchers (Stinger, Igla, Javelin).**
- Bots try three ways of firing, and the one the game actually launches is kept:
  - Fun-Bots style: no sight, fire tapped while the aim is on the target. This is tried first.
  - Sight held, fire tapped.
  - Sight held, fire held.
- A launch counts when the launcher's magazine drops or a new missile appears beside the bot.
- The log says which way worked: `rocket: ... is away ... - way N`.

**Missiles in flight.**
- The server's missile objects are tracked twice a second, from a background thread, only while bots are in vehicles or at a launcher.
- A bot whose vehicle or aircraft a missile is heading for, within 350 m, pops flares or smoke before the hit. This works the way Fun-Bots does it. Before, they only reacted after being hit.
- Look for `missile: ... - a missile N m away is coming at it: flares / smoke`.
- The first missiles of a round calibrate where the game keeps a missile's position: `missile: missiles in flight are tracked (transform at +0x...)`.

**The AC-130 gunship.**
- **Using it:** a bot that has just spawned takes a free gun seat in its team's gunship, as Fun-Bots does. One seat stays free for you.
- **Its gunners:** their guns reach 1.6 km, the gunship's own speed is in their aim, and nothing pulls them out of it.
- **Attacking it:** AA launchers (up to 850 m), AA vehicles, stationary AA and jets treat it as an aircraft and go for it.

**Jets take out people.**
- Jets go for armour and groups of soldiers.
- An enemy aircraft is their target only when it is within 900 m, or when it is the gunship.

**Armour shoots soldiers.**
- The coax gun fires at any range, and the main gun from 6 m.
- The line of sight goes past the tank's own hull.
- A soldier within 60 m comes before an enemy vehicle more than ~150 m away. A nearby enemy tank still comes first.

**Driving.**
- The sensors look down the route, so bends no longer brake for walls and trees beside the road.
- Armour no longer slows down to shoot.
- After slowing, speed comes back twice as fast.
- Drivers join Fun-Bots roads from 90 m (was 45 m) before using the driving map.
- In the first 2 minutes of a round every bot may run to a vehicle (before: 2 at a time).

**On foot.**
- Fun-Bots routes for every trip from 8 m.
- Bursts stay short at long range for better accuracy.

**Not possible:** Fun-Bots route files have no aircraft paths, so aircraft fly their own way (as in Fun-Bots).

**Not tested in a game yet:**
- which way of firing the game launches;
- the missile calibration and flares;
- bots boarding the gunship.

The log lines above show each one.

## V22: sharp shooters - and the fixes from the 2026-10-17 evening

**Respawns.**
- **TDM (Epicenter).** Some bots stopped respawning. A V16 rule for Rush was resting a spawn point for 3 minutes when bots died there with nobody shooting them. Epicenter's TDM has only two spawn points, so one team had none left and its bots were retired in a loop. That rule now applies to Rush only, and a refused team-play spawn rests for 3 seconds.
- **Domination (Office).** The red team never spawned. Its HQ was found, but no spawn point was matched to it. Outside Conquest, such a team now tries the level's team-less spawn points. Conquest keeps the old rule, so bots never spawn in the wrong base.

**Fun-Bots routes first.**
- Bots take a recorded Fun-Bots route for any trip from 15 m. It used to be 40 m, and shorter trips went to the grid, which walked into walls and past stairs.
- When a route gets a bot nowhere, it takes the route again from another waypoint twice before the grid gets a single try. V21.1 gave up on the route at once, 638 times in 3 minutes on Office.

**Lock-on weapons.**
- **Stinger, Igla and Javelin.** The bot keeps the sight on the target and presses fire again every half second until the launcher's magazine shows a missile has left. A launch is only counted then. Look for `rocket: ... is away ... (the game locked after N s)` in the log. Until now, "fired (locked)" only meant the bot pressed the button.
- **Jets and gunships.** The heat seeker is selected from the moment the target is on the nose and held while it locks (2.8 s), then fired. Before, it was selected 0.1 s before firing, too short for any lock.

**Every crew member uses the right weapon.** Following Fun-Bots:
- a tank's coax gun on soldiers within 150 m;
- an IFV's missile on armour (4 s in every 12);
- an attack-helicopter gunner's guided missile on vehicles, with the sight held on the target.

**Accuracy.**
- **Gunners in a moving vehicle.** Gunners in a moving helicopter or tank lead by the target's speed relative to their own. Their rounds leave with the vehicle's speed (the game's `InheritWeaponSpeedAmount`), which was the big miss for helicopter gunners.
- **Turning aircraft.** Against aircraft, gunners also allow for how the target is turning.
- **Bursts.** Vehicle guns fire 1.8 s bursts with 0.2 s breaks.
- **Pilots.** Helicopter pilots lead relative to their own speed, and fire only with the nose within about 6 m of the aim point at the target's range. It used to be up to 11 degrees, or 60 m at 300 m.

**Difficulty.** Elite is sharper:
- reactions 0.07 s (was 0.12);
- less aim lag;
- longer bursts and shorter pauses;
- they see and engage 20 m further.

**Vehicles drive at speed.** A recorded road has a point every few metres and a hand's wobble. Drivers used to read every wobble as a bend: in a test on a straight road they braked to 8 m/s 16 times. Bends are now measured over 10 m either side, and the speed comes from the bend's radius. A straight is driven flat out (29 m/s in the same test), a real bend is taken at the speed it allows, and a junction turn at 5 m/s. Convoys follow at that pace.

**Boats.** A boat is now used when a recorded Fun-Bots boat lane runs from near it to within 150 m of a flag at least 300 m away, as on Armored Shield's river. Before, boats were used only when the flag was across water.

**Stationary weapons.**
- Bots man a C-RAM, Pantsir, TOW or Kornet only for a target in reach: aircraft within 850 m, armour within 400 m. It used to be any aircraft within 1.5 km.
- They leave after 15 s without a shot. Before, any target they had picked kept them there.
- At most 2 per team.

**Not tested in a game yet:**
- the missile launch confirmation lines;
- the weapon switches (`gunner: ... switches to weapon 2`);
- the boat lanes on Armored Shield.

## V21.1: vehicle routes go around stall spots, and LAN servers

**Stall spots now change the routes, not only the driving map.** In V21, a place where drivers kept stalling was struck off the driving map. But drivers mostly follow the Fun-Bots vehicle routes, and the driving map is only the fallback when there is no route. So the routes still led into the spot.

Now every route link that passes within 10 m of a learned stall spot (one drivers never got out of) costs 60 times more. Drivers and boats take another road when there is one.
- It happens when the map loads, and also as soon as a spot is learned during a match. The bots don't wait for the next map.
- Nothing is cut off. If every road goes through bad spots, the shortest one is used again.
- The log says `learn: N stall spot(s) drivers never got out of: M vehicle route link(s) avoided` at load, and `drivers never get out at ...` when a new one is learned.
- Deleting the spot on the Learning page undoes it at the next map load.

**LAN servers (Server Control, BF-Portable tab, LAN servers...)**
- The window opens with this PC's own servers.
- **Search my network** asks every PC on your network (the same /24, for example 192.168.1.1-254) for a BF-Portable backend on port 80. It lists the BF3 servers each one is running, with PC, server, map, players and game id.
- **Look there** asks one PC by name or address, also `pc:port`. Use it when the search doesn't find a PC.
- **Joining.** Pick a server and one of your players on that PC, then choose Join. BF3 starts through the BF Launcher, logged in on that PC's backend and pointed at its Blaze address.
- **No player there yet?**
  - Register makes one on that PC's backend. Names use letters, digits and spaces. Register also tests the login and saves the player.
  - Save existing keeps a player you already have there.
- **Where players are saved.** Passwords are saved encrypted for your Windows user. Players on other PCs are kept apart from this PC's own players, so Accounts and Quick join show only this PC's players.

**For another PC to be found and joined,** check these on that PC:
- Its web server (XAMPP Apache) is running and port 80 is open in its Windows firewall.
- The Blaze / game ports of its BF-Portable redirector are open in its firewall.
- Its server is set up with its LAN address, not 127.0.0.1. Use the IP configurator on its BF-Portable tab. Otherwise the backend hands out an address the other PCs can't reach.

## V21: bots learn - from getting stuck, from finding a way, and from you

Every map and mode gets its own learning file in **`BotMod_learned`** next to the BF3 server (for example `Battlefield_3_Server\BotMod_learned\MP_001_ConquestLarge0.learn`). The server reads it when the map loads and saves it every minute and at every map change.

**On foot**
- **Stuck spots.** Every place a bot gets stuck is remembered, together with whether it got through afterwards and whether a jump did it.
- **Spots bots get through** are never blocked again. This fixes the old problem of a bot stuck in a stairwell closing the stairs for everyone.
- **Spots passed with a jump:** the next bot jumps on the way in.
- **Spots nobody ever gets through** (3 tries) are taken out of the bots' walking grid when the map loads.
- **Killing grounds.** Where soldiers die (10 m cells, fading 20% per map load), the bots' routes cost up to 2.5x more, so they take another way when there is one. Nothing is closed.

**Vehicles and boats.** Where drivers stall and never drive out (2 times), that patch is struck off the driving map at the next load.

**Aircraft**
- **Losses.** Where a bot's aircraft is lost or abandoned while flying under 35 m, jets keep up to 90 m more height within 500 m, and helicopters up to 45 m more within 150 m.
- **Landing spots.** Where a transport lands and unloads becomes a landing spot. Transports try the known spots first.

**Learning from you** (and any human player)
- **Your routes.** Where you walk, drive and sail over ground the Fun-Bots routes don't cover (at least 25% new ground; walks from 40 m, drives from 150 m) is recorded. It becomes part of the bots' route network from the next map load.
  - Stairs stay in one route. A ladder, a jump off a ledge or a vault ends it, because bots can't copy those.
  - A route that leads bots into a spot they then never get through is split there, so the network corrects itself.
- **Your helicopter landings.** Where you set a helicopter down, away from where you got in, becomes a landing spot for the bot transports.

**From version to version**
- The installer never replaces or deletes `BotMod_learned`. Every new version keeps it.
- Each install puts a copy of it in its backup folder.
- V18-V20's `BotMod_breakable.txt` (glass) moves into the folder.
- A learning file keeps any line it doesn't understand, so files go forward and back between versions without losing anything.
- To move learning to another PC or another BF-Portable folder, use Export and Import on the Learning page.

**Server Control: the Learning page**
- **Pick a map** to see everything learned there, listed and drawn on the map (the live map's picture when there is one):
  - red: avoided or blocked
  - green: bots get through / a landing spot
  - yellow: a jump spot
  - purple: aircraft keep height here
  - grey: still being learned
  - red squares: killing grounds
  - lines: your routes
- **Details.** Click an entry, in the list or on the map, to see what it makes the bots do.
- **Deleting.**
  - Tick entries and choose Delete ticked, or use Forget this map, Forget killing grounds, or Forget glass and panels.
  - A running server merges deletions at its next save (within a minute): what you deleted stays deleted, and what the bots learned meanwhile is kept.
- **Moving learning.** Export makes a zip. Import takes a zip, `.learn` files or a whole folder (an older install's server folder too). It adds only what isn't there yet.
- **Settings.** In the bot settings: `Learning` (everything on or off) and `LearnFromPlayers` (only the recording of human players). Both are on by default.

**Log lines**
- `learn: loaded <map> - stuck spots ...` and `learn: nothing learned yet for ...`
- `learned: bots get through here - kept open`, `learned: jumps through here`
- `learn: N spot(s) on foot nobody ever got through: N grid link(s) closed`
- `learn: Player1 showed a 120 m walking route (70% new ground) - bots use it from the next map load`
- `learn: a new helicopter landing spot at ...`
- `trace: + N waypoint(s) from routes human players showed (learned)`
- `trace: N route link(s) through learned killing grounds cost more`
- `learn: this session - ...` every 5 minutes

**Not yet tested in game.** All of it is tested offline:
- the learning file both ways
- a scripted game world for the in-game side
- the Learning page's data side against files the real code writes
- the installer keeping learning across upgrades and uninstall

What still needs a live round is how much better bots get on a map over a few rounds.

## V20.1: strike runs fixed after the first live match

On Caspian Border, V20 jets dogfought and fired 9 missiles, but they never completed a strike dive.
- **Pull-out after takeoff.** 12 "pulls out" happened about 10 s after takeoff. A jet still climbing out low was treated as falling and sent into a pull-out and extend. A climbing jet now just keeps climbing.
- **Target switch during setup.** A strike run switched targets during setup and kept flying to the old target's initial point. The target is now held while the run is set up (up to 40 s), and a new target gets a new initial point.
- **Easier roll-in.** A dive now starts at 6 deg (was 7 deg) and up to 20 deg off the nose (was 17 deg), with 135 m of clearance needed (was 155 m).

## V20: air combat

**Why jets never fought.** The server logs show jets taking off and flying for 3-5 minutes, then getting shot down, with nothing in between. Two jets that targeted each other flew straight at each other's aim point, with no collision avoidance and no break-off, so they met head-on. In a flight simulation of the V19 controller, 294 of 300 jet-vs-jet fights ended in a ram, and 100 of 100 friendly jets flying head-on collided.

**Dogfights.**
- **Pursuit.** A jet chases an enemy aircraft with hard banked turns (lift vector on the target), then tracks it with the cannon once it is within 60 deg of the nose and 1200 m.
- **Firing.** The gun fires in 2 s bursts with a 0.6 s pause against overheating, and only when the shot passes the lead point. The firing window is wide up close and narrow far out.
- **Missiles.** After 2 s with the target on the nose (250-1600 m), the jet fires its secondary weapon. It waits 10 s between missiles.
- **No more ramming.**
  - Head-on gun passes break off at 380 m.
  - Any approach with under 2 s to impact breaks off, as does an overshoot inside 160 m.
  - Every jet watches all aircraft nearby, friends included, and breaks away from any that would pass within 50 m (150 m head-on). The higher jet climbs and the lower one dives.
  - After a break the jet extends for 3-7 s at full throttle, then comes round again.
- **Target choice.** Jets prefer aircraft. A target ahead of the nose beats one behind (Fun-Bots' angle penalty), and an attack keeps its target until the pass is over.

**Strike runs (vehicles and groups of infantry).**
- **Setup.** The jet flies to an initial point 1700 m out, 300-450 m above the target, then turns in.
- **Dive.** It rolls in when the target sits on the nose at a 7-45 deg dive angle. It dives at about 400 km/h, fires the cannon, and fires one rocket or missile salvo per pass at 450-1100 m.
- **Pull-out and repeat.** It levels its wings and pulls out before the ground, extends, then comes round again with each run swung 30 deg.
- **Bombs.** Battlefield 3 jets carry no bombs, so their air strikes use the cannon plus rocket pods or missiles.
- In the simulation, a jet made 5-6 strike passes every 150 s and never went below 130 m.

**Helicopters are more aggressive.**
- Gunships engage from 1000 m and scouts from 800 m (both were 700 m).
- They dive at targets up to 34 deg below (was 30 deg) from 140 m out (was 180 m), and hold 60 m over a ground target (was 80 m).
- They keep a ground target under fire three times as long and come back in after 3 s (was 6 s).
- They fire 0.95 s of every 1.2 s (was 0.3 of every 0.6 s).
- Gunships fire missiles at aircraft after a 2 s lock.
- They prefer infantry more than before, and look for targets out to 1500 m.
- In the simulation, gunships fired 64% of the time against a ground target (V19: 18%).

**Log lines to look for.**
- `air: <bot> (<jet>) pursues / guns <target>`
- `breaks off`, `evades traffic`
- `dives on <target>`, `pulls out`
- `fires its secondary weapon`
- `air: combat so far - N dogfight gun runs, N strike dives, N secondary weapon shots, N collision breaks` (in the status block written at startup, by `ehstatus` and by `!botdiag`)

**Not yet tested in game.** The tactics were tested only in a flight model: 300 dogfights from random starts, strike runs on static and moving targets, and five variants with slower or faster controls and gravity, with no rams and no crashes. Real Frostbite handling will differ. The secondary weapon uses Select Weapon 2 → fire → Select Weapon 1, the same input Fun-Bots uses for vehicle slot 2, and still needs a live check. If a jet's secondary is a heat-seeker and no lock exists, the shot may just fly straight.

## V19: Fun-Bots routes on every map and mode

Every map and game mode Server Control offers (29 maps, 215 map/mode combinations) now loads Fun-Bots routes, and a check proves it for each one. `ROUTE-COVERAGE.txt` lists every combination.

**The right files.**
- The Conquest and Rush routes are the last complete Fun-Bots recordings. This month Fun-Bots' own development branch cut its Conquest and Rush paths short at objectives and spawns, because it now relies on a navigation mesh there that AltBotMod doesn't have. Those cut files are deliberately not used.
- Every Rush and Squad Rush file covers all of its M-COM stations.
- 156 combinations have their own file. The other 59 use the closest file from the same map:
  - Squad Rush uses Rush.
  - TDM Close Quarters, Gun Master, Scavenger and CTF use Team Deathmatch.
  - Tank Superiority and Air Superiority use Conquest.
- Fun-Bots' Air Superiority files hold a single point. AltBotMod recognises them as placeholders and uses the map's Conquest routes instead.
- Added the co-op and campaign Team Deathmatch files.

**More routes per mode.** Each map and mode also gets a companion file in `BotMod_traces\companions`:
- It holds the walking paths the same map's other modes recorded, kept only where they lie within that mode's area (within 20 m of its own paths).
- Only new ground is added, and each added piece is linked into the existing network.
- On Operation 925 Close Quarters, for example, the 6 recorded TDM paths gain the rooms and stairwells from the Squad Rush, Squad Deathmatch and Domination recordings.
- In total, 642,000 waypoints were added across all modes.
- The log shows `trace: + N waypoint(s) from the level's other Fun-Bots recordings`.
- A companion is only used with the exact file it was built for. If you replace a Fun-Bots file, run `tools\build_route_companions.py` again.

**Floors.** A trip to another floor (2.5 m or more up or down) now takes the recorded route from 8 m away, not 40 m. The recording knows where the stairs are.

## V18.1: stairwells and doorways fixed

On Office, V18 left a crowd of bots in a room, facing a wall and ignoring the stairs. There were four causes, and each is fixed:

- **The make-way rule stopped bots that were already walking.** It now only moves a bot that is standing idle, and only to a side that is actually clear (never into a wall).
  - A bot walking the same way queues behind.
  - Two bots meeting head-on both keep right.
  - Nobody is pushed off a flight of stairs.
- **The "not moving" straight walk aimed at the final destination.** That was usually upstairs or through a wall. It now heads for the furthest route point in sight on the bot's own floor, or the next route point (the stairs).
  - The bot waits up to 6 s for a teammate in front to move instead of walking into the wall.
  - It walks straight at the destination only when that is in plain sight on the same floor.
- **Bots walked into the wall at stair landings.** On a switchback landing a bot kept walking 2 m along the flight, straight into the landing wall, then gave the stairs up. It now turns at the landing and carries on to the next flight.
- **Stairwells and crowds were cut out of the map.** A bot stuck at stairs, or held up by another soldier, blocked those links for 3 minutes, so every bot in the building stayed on its floor. Neither case blocks links now. The running-in-circles check also no longer blocks links or changes objective in a crowd.

**Fun-Bots routes:**
- From inside a room, the walk to a recorded route now tries the three best route waypoints, not only the nearest, which was often behind a wall.
- A route point is never skipped at a flight of stairs.

## V18: glass, doorways, stairs, interiors, rooftops, circles

Your Office (XP2_Office) rounds had about 370 stuck-and-back-off events, 619 straight walks with no route, and 148 bots blocked on a flight of stairs. This update addresses each problem in turn.

**Glass and thin panels**
- A bot stuck against something thin knifes it. "Thin" means under 30 cm, with floor and room on the other side: a window, a glass wall or a glass door. If the knife does nothing, the bot shoots it three times.
- The mod learns which materials break. They are kept in `BotMod_breakable.txt` next to the server, for every map. A material that nothing breaks (bulletproof panels, solid boards) is learned as unbreakable and left alone after that.
- Learned glass also counts as passable when a map's navigation grid is built. To give an existing map routes through its windows, send `navrebuild` (in `BotMod_<port>.cmd` or the console) while on that map, once the log has shown `glass: ... learned as breakable`. The grid then rebuilds in the background.

**Doorways and stairwells**
- A bot that means to walk but stands still asks the teammate bot in front of it to side-step out of its line for 1.3 s. Before, the bot in the way never moved, and the one behind backed off and re-routed.
- If a person is in the way, the bot steps round them itself.
- A bot held up on a flight of stairs (a step lip, a body, a teammate) now hops once and asks the way clear before giving the flight up.

**Running in circles**
- A bot that walks 20 m or more in 12 s without getting more than 5 m from where it started is caught. The old stuck checks never saw this, because the bot was moving.
- When caught, it stops using the links it keeps taking and goes another way. The second time, it also picks a new objective.
- Fighting, sniping, working and capturing don't count.

**Building interiors**
- **Rush:** every third guard post is inside a building near the M-COM.
- **TDM / Squad DM / Gun Master:** every other patrol stop searches a building near the patrol point.
- "Inside" means a roof within 14 m overhead and walls on at least two sides, on the ground floor or the one above.

**Snipers on rooftops**
- Sniper perches are now also looked for 45-70 m from the objective, so buildings round a town flag count, not only distant hills.
- A rooftop scores higher than an upper-floor window at the same height. A rooftop is a spot 6 m or more above the objective with open sky over it.

Log lines:
- `glass: ... knifes a thin panel`
- `glass: ... broke it ... learned as breakable`
- `way: X is in Y's way ... steps aside`
- `stairs: ... held up on the flight ... hops`
- `nav: ... runs in circles ...`

## V17: vehicles that keep moving

**Stuck vehicles.** Your Alborz round (2026-10-14) had 118 stuck episodes, 21 of them over a minute. The worst was a Growler that nosed into the same parked vehicles at the US base 132 times in 202 s. Every back-off moved it a few metres, which reset the old stuck watches, and every re-plan took the same recorded road. Now each driver is watched for real progress: it must get 35 m on from where it stalled within 35 s, however much it backs and fills.
- **35 s:** it backs straight out, strikes a 7 m patch ahead off the driving map and takes a different recorded road.
- **70 s:** it heads for another objective on the driving map.
- **105 s:** it gives the vehicle up and goes on foot. No bot takes that vehicle for 2 minutes.

None of this applies while the vehicle is at its objective, fighting from the hull, picking up riders, waiting for an engineer, or driven by a player.

**Newer Fun-Bots vehicle routes.** I compared every vehicle route in the Fun-Bots `dev` and `zone-nav-meshs` branches (15 Oct) with ours. They are identical except for new routes on two Rush maps, which are now merged in:

| Map | Land routes | Boat routes |
|---|---|---|
| MP_013 Rush (Damavand) | 17 (was 16) | - |
| MP_017 Rush (Noshahr) | 23 (was 22) | 6 (was 2) |

Every infantry path stays. The dev branch's own Rush and Conquest files are cut down for its navmesh, so they were not taken whole. Recorded routes still come first for drivers.

**Not obsessed with helicopters.** Aircraft are the Stinger / Igla engineers' and AA vehicles' job.
- **Tank / IFV guns:** soldiers within 300 m and enemy vehicles now come before a helicopter beyond 100 m. A low helicopter within 100 m (it is attacking) is still engaged, and so is one that just hit the tank. A far one (up to 300 m) is engaged only when nothing else is in sight, and never past 300 m. Before, any aircraft within 500 m beat soldiers more than ~150 m away.
- **Jeep / boat machine guns:** never at aircraft while a soldier is in reach, and not past 150 m. Before, they ranked aircraft ahead of every soldier.
- **Soldiers on foot:** they look at soldiers before vehicle crews, so a helicopter overhead no longer hides the soldier behind it.
- **Engineers:** while enemy aircraft are in the air, they carry a Stinger / Igla three times as often.
- **AA guns** (Pantsir, Tunguska, LAV-AD, C-RAM) still go for aircraft first.

**Rush safety net.** The V16.2.2 bug counted one M-COM destruction twice: the bots jumped a sector early, spawned outside the play area and died, defenders worst. If six bots die outside the play area within 90 s of a sector change, the change is undone. A station the game itself reported destroyed stays destroyed; one the mod only guessed is standing again.

Log lines:
- `drive: X has not got 35 m on in 35 s ...`
- `... gives the Growler up ... - on foot`
- `rush: bots keep dying outside the play area since the move to sector N ...: back`

## V16.2.3: onto the panel, arm it, hold it

The 11:19 round on Metro showed why bots stood at the M-COM without arming.
- **The walk stopped short.** The last straight walk to the panel ends 2 m before its point, so bots held the key from 2.3-2.6 m.
- **The game wants the panel's own spot.** It arms or defuses from the recorded spot with the recorded aim; Fun-Bots puts its bots right on it.
- From 2.5 m: 8 tries, 0 arms. From 0.3 m: armed in 2.7 s.

Fixes (arming and defusing alike):
- **Onto the spot:** the bot walks right onto it (within 0.6 m), then holds. It only holds from further out (up to 2.6 m) if it can't get closer in 4 s. If 8 s brings no arm or defuse, it tries again from 0.4 m.
- **Nothing breaks a hold:** no firing or aiming down sights while holding. Being shot doesn't stop it.
- **Any station close by:** a bot within 10 m of a station it can arm or defuse does it, unless a teammate is already at that panel. It doesn't matter which station it was sent to.
- **Guard and backup:** once a station is armed, up to five attackers close to it (within 120 m) stay and guard it 4-10 m around the panel until it blows. Before, they left for the other station.
- **Holds survive other news:** a bot in the middle of a hold keeps its station when another station changes state.

**The bots' suicides.** After A blew, the mod moved the bots on to the next sector while B still stood. The game's "objective success" message came with A's destruction, and V16.2.2 also counted it as B. The bots then spawned at the next sector's bases outside the play area and died there, about once every 3 s on MP_011 at 11:30 and XP1_002 at 11:37. Each death cost a ticket, so the rounds ended in about 7 minutes. Now only the game's own armed / disarmed / destroyed events move the state, so the next sector starts only when both M-COMs are really destroyed.

Log lines: `rush: X is next to M-COM n - goes to arm it` and `... held the key 8 s ... moves onto the spot and tries again`.

## V16.2.2: the kicks, and arming for real

**You were kicked by the 32-player limit, not by the bots.** BF-Portable's Blaze emulator fails every time the server tells it a limit other than 64 (`setPlayerCapacity ... index out of range` in `BF3-EMU\logs\BlazeServer_.log`). About 2 minutes after each start, the server loses the backend and drops everyone. This happened at 11:03 with only 28 bots, and Server 3 at 32 players had the same emulator error on Oct 11.
- Server 1 is back at 64 players. The old files are kept as `Startup.txt.before-64` and `_StartServer.bat.before-64`.
- Server Control's Max players setting now only accepts 64 and says why. To have fewer players, use the bot count instead.

**Arming:** the mod now reads the game's own M-COM events: `StatEvent_CrateArmed`, `CrateDisarmed` and `CrateDestroyed`, and who did it.
- The bots' M-COM state is now the game's, not a guess.
- In your 11:03 round a bot armed in 3 s from 2.5 m and the M-COM blew 25 s later, but two other holds from 2.6 m did nothing.
- Now, if 8 s of holding brings no arm or defuse, the bot walks to within 1.2 m of the spot and holds again.
- Log lines: `rush: the game says M-COM armed by ...` and `... held the key 8 s ... moves closer and tries again`.

## V16.2.1: the Rush crash (2026-10-15)

**Cause:** the server was set to 32 players, but the bots filled 60 places.
- The bots took the player limit from the server's `serverInfo`, which reports the level's 64, not `vars.maxPlayers`.
- With 60 players in a 32-player game, the server kept telling the Blaze backend "capacity 32". The BF-Portable emulator threw an error on each request (`setPlayerCapacity`, index out of range in `BF3-EMU\logs\BlazeServer_.log`).
- About 2 minutes after each start, the server's backend link timed out. The server dropped every client, which is what kicked you out of the game. It then failed to reconnect and crashed.
- Before today the servers ran at 64 players, so it never showed.

**Fixes:**
- **Player limit:** the bots now use the smallest of `vars.maxPlayers`, `serverInfo` and the `.bat` `PLAYERS=` value. At 32 players that is 28 bots, leaving 2 places a team for people. The log shows `chat: server allows 32 players (serverInfo 64, vars.maxPlayers 32)`.
- **Arming:** a bot that held the key for the full 8 s now gets it marked armed / defused. In V16.2 the per-frame step ended the hold at exactly 8.0 s, just before the brain could mark it, so the bot re-armed in a loop. The game did take the arm: your log has `StatEvent_CrateArmed` for `[BOT] Support US 7`.
- **Crash trace:** if the server ever dies inside the game's code, `BotMod_<port>.log` now gets an `exception:` line naming where.

## V16.2: arming and defusing that sticks

You saw bots arm once, then never stay at a panel. Three things pulled them off it:

- **The job belonged to someone else.** Only one bot may work a panel. When that bot was fighting, stuck or dead elsewhere, the bots standing on the spot walked past to their guard posts.
  - **Fix:** a bot standing at the panel now takes the job from a teammate who holds it from far away.
- **The reach was too short.** The bot had to be within 1.7 m of the recorded spot; the crate often stopped it further out.
  - **Fix:** it starts within 2.6 m. Once it holds the key it keeps at it within 3.2 m, so a push does not end it. (Fun-Bots uses 4 m.)
- **Anything could interrupt it.** A fight order, a task or a squad call pulled the bot away.
  - **Fix:** while it holds the key, nothing else runs for that bot: no cover, no fights, no tasks. It crouches and holds for the full 8 s, however badly it is being shot. Only death, a vehicle, or being moved more than 3.2 m off ends it. This applies to arming and defusing alike.

**The game's messages, re-read from your rounds:**

| Message | Meaning |
|---|---|
| `ServerMetrics.ObjectiveSuccess` | One M-COM destroyed, about 26 s after an arm. It is applied to the armed M-COM. |
| `ServerMission.ObjectiveCompleted` | The sector is finished. Bots move up and deploy at the next sector's bases. |

V16.1 read the first one as "armed". That kept the attackers away from M-COMs they still had to blow.

New log lines: `rush: <bot> arms M-COM 2 (1.4 m from the spot)` and `rush: <bot> stops at M-COM 2 after 3.1 s`. The second one appears only when a hold ends early.

## V16.2: Fun-Bots routes

- **The new navmeshes** (the `zone-nav-meshs` branch) were not taken. Their Rush and Conquest route files are cut down to suit the navmesh, so they have fewer points than the ones AltBotMod already uses.
- **From the `dev` branch, 80 route files were taken:**

  | Mode | Files |
  |---|---|
  | TDM | 29 |
  | Squad DM | 29 |
  | Gun Master | 11 |
  | Domination | 4 |
  | Scavenger | 4 |
  | CTF | 1 |
  | Tank Superiority | 1 |
  | Conquest Small (co-op map) | 1 |

  They have the same points as before, plus recorded links between paths (doorways, stairs, junctions). AltBotMod reads those links, so bots switch between routes where a person would.
- **Fun-Bots routes still come first,** before AltBotMod's own grid.

## V16.2: Server Control

- **New look:** dark Battlefield 3 colours with orange highlights and a banner. Every dialog uses it.
- **Network: IP and DNS…**
  - Pick this PC's address and write it into every place that needs it, in one go: the redirector's `ServerIP.txt` and `config.ini`, the LAN DNS answer, and the BF Launcher backend. Each one is a checkbox, and each file is backed up once.
  - Enter the DNS domain once and tick *Start it with the one click*. The DNS then answers by itself every time, with nothing to type.
- **Accounts and quick join…**
  - Save logins. Passwords are encrypted for your Windows user.
  - Test a login, pick a default account and a server, then *Join now*. Quick join logs in through the BF Launcher's own backend and starts BF3 with the launcher's own command line.
  - *Auto-join* joins the chosen server after the one-click start, once the server is up.
  - *Join server now* is also on the main window.

## V16.2: vehicle damage

There are two new multipliers (0.25-4, default 1):

- **`VehicleDamageTaken`:** all damage vehicles take, from anyone.
- **`VehicleDamageDealt`:** damage done by anyone in a vehicle, to soldiers and to other vehicles.

Both use the game's vehicle damage function. That function was found in the game's code but has not been tested in a live round yet. The mod checks its first 8 hits; if the damage record does not check out, it switches the multipliers off. The log shows `damage: vehicles take the scaled damage` or `damage: vehicles ignored the scaled damage ... off`.

**Not possible: stronger explosions only.** The damage record has no field yet known to tell an explosion from a bullet or shell, so there is no explosions setting.

## V16.1: arming and defusing M-COMs in Rush

Your Metro Rush test on V16 (2026-10-13 23:03) showed why the bots never armed or defused.

- **Defenders blocked the panels.** The game's arm / defuse / destroy events are not readable yet, so every M-COM stayed "unknown". The defenders were allowed to work an unknown M-COM, and only one bot at a time may work a panel. RU bots, closer to it, took that turn and stood holding the key at an unarmed crate, so no attacker could ever arm it.
  - **Fix:** defenders only go to the panel of an M-COM known to be armed. The rest of the time they guard around it.
- **Being shot broke every attempt.** A hit stopped the hold, and any fight took the bot away from the panel.
  - **Fix:** the bot working the panel keeps going under fire. Within 20 m of the panel it ignores enemies more than 6 m away.
  - It now holds the key for 8 s (was 6 s).
  - It walks the last 5 m straight onto the spot; the crate had stopped routed bots 2-4 m short.
- **The sector never advanced.** With no events, bots kept going to the first two M-COMs all round, and kept deploying at the first bases. The bot's M-COM state now moves on from what can be seen:
  - **Armed:** a bot finishes its 8 s hold, or a player stands at the panel for 5 s. The defenders then come to defuse it.
  - **Destroyed:** the game sends a message `ServerMission.ObjectiveCompleted`. In your test it came about 30 s after you armed M-COM B. It is applied to the armed M-COM (or the one with an attacker nearest).
  - **Next sector:** both M-COMs destroyed → bots deploy at the next sector's bases, since the deployment zones move up with the objectives.
  - **Defused:** a bot or player defuses it, or nothing is reported for 75 s after arming. The attackers then try again.
  - `!rush stage <n>` and `!rush destroyed <n>` (admin chat) still set it by hand.
- **Spawning in Rush:** each team now deploys at the spawn entity whose deployment zone is nearest its current sector base. V16 tried every spawn entity in turn.

Log lines to look for: `rush: M-COM 1 armed (...)`, `rush: game message ... -> destroyed M-COM 2`, `rush: routing sector 2/4 (...)`. Every objective message is now logged with its count, so the next log will confirm what each one means.

## Rush: the bots weren't moving

Your MP_Subway Rush log on V15 showed two separate problems.

- **The defenders died in a loop.** V1-V15 read the wrong field of the spawn point's data as its team, so Rush spawn points looked team-less. V15 then tried all of them, and RU landed on the sector 3 spawn. The out-of-bounds timer killed them every few seconds: dozens of "went down" lines, none with a shooter.
  - **Fix:** the spawn team is read from the field the game itself uses, `CharacterSpawnReferenceObjectData.Team`. It was found in the level data and checked against Metro's eight Rush spawns. RU now gets its sector-1 spawn by the park.
  - **Safety net:** if two bots die with no shooter within 25 s of spawning at the same spawn point, that point rests for that team for 3 minutes.
- **No navigation grid for Rush on that map.** V15 started building a fresh one, which takes several minutes. Meanwhile the bots walked straight lines and mostly stood.
  - **Fix:** a mode played on a map for the first time borrows that map's grid from another mode (Conquest first). The grid covers the same streets and stairs, and V16 only surveys the area it lacks, in the background.
- **The server 3 crash** (V15, Rush) is fixed too. Flag lookups are now safe for any index; the crash came from an M-COM number used as a Conquest flag number.

## Passengers

- **Spawning in:** bots now spawn into a teammate's jeep, boat or transport, not only into helicopters and tanks. This works in Rush too.
- **Getting out:**
  - The vehicle is nearly wrecked: everyone gets out (with a parachute from an aircraft).
  - A human driver stops at an objective the team still has to take: the cargo gets out after 4 s and captures on foot.
  - The vehicle has not moved for 25 s (40 s for a boat): the cargo gets out.
  - A human pilot is over an objective: the cargo jumps.
- **Gunners stay seated.** A bot driver on a job still drops its own cargo at the flag, as before.

## Jeep taxis and boats (after Brian's beta)

- **Pickups:** a bot driver now offers a ride to bots within earshot (35 m; it was 25) every 15 s, and waits up to 7 s while they run over. Boats pick up too.
- **Boats only when needed:** a bot takes a boat only when its objective is on another landmass. Bots no longer sail along the coast to a flag they could walk to. At the spawn screen, a boat is picked only when the next objective is across water.

## Every weapon and gadget

- **Gadget variety:** each life, engineers carry a SMAW / RPG-7, a Javelin, or (on maps with aircraft) a Stinger / Igla. Support carries a claymore or C4; recon a T-UGS or a SOFLAM.
- **Lock-on launchers:**
  - The Javelin is used on ground vehicles out to 450 m; the Stinger / Igla on aircraft in the air out to 700 m.
  - The bot aims down the sight and fires after a 2.2 s lock.
  - A Javelin gunner closes in when the target is hidden.
- **C4:** a soldier carrying C4 goes for a slow enemy vehicle within 40 m. It plants two charges, backs off 15 m and sets them off.
- **Snipers place their equipment** (T-UGS / SOFLAM / claymore) when they reach their perch.

## Tanks, stationary weapons, jets

- **Tanks fight on the move.** V15 only calibrated the main gun at a standstill, so a tank on the move never fired.
- **Tank target order:**
  - Whoever just shot the tank is engaged first, for 5 s.
  - A soldier within 25 m of the hull (C4, a mine, a knife) is engaged before far targets.
- **Smoke:** a bot tank or IFV driver pops smoke when a hit takes 8 % or more of its health, at most once every 25 s.
- **Stationary weapons:** a gunner on a TOW, Kornet, C-RAM or Pantsir gets off when an enemy soldier comes within 18 m, and fights him. They no longer get knifed in the seat.
- **Jets** attack soldiers and prefer groups (V15 already attacked soldiers).

## Vehicles on large maps

- **Longer routes:** recorded Fun-Bots vehicle routes now hold 256 points (128 before), so a long route is no longer cut into short stop-go legs. Drivers keep trying the Fun-Bots routes before the grid, and do not slow down at the end of a partial route.
- **Flags no longer ignored:** a vehicle at a flag it doesn't own now works it from inside the capture zone, moving between spots 8-14 m from the centre. It stays for up to 75 s, until the flag turns. V9-V15 parked 28-48 m out and left after 8 s.

## Other modes

- **Air Superiority:** bots spawn in the jets at each team's air spawns.
- **Capture the Flag:** a third of the bots raid the enemy flag stand, a third guard their own, and a third hunt. The positions of the four maps' flag stands come from the level data.
- **Tickets:** a bot respawn costs half a ticket by default (`BotTicketCost`).

## New settings (enhanced.ini, [AltBotMod]; also in Server Control)

| Setting | Default | What it does |
|---|---|---|
| `GadgetVariety` | 1 | Gadget swaps per life (above). 0 = the gadgets of BotMod_loadouts.txt only. |
| `C4Attacks` | 1 | C4 runs at slow enemy vehicles. |
| `LockOnLaunchers` | 1 | Javelin / Stinger / Igla use. |
| `BotTicketCost` | 0.5 | Tickets per bot respawn (0-2). |
| `VehicleCountermeasures` | 1 | Smoke from bot tanks and IFVs when hit. |
| `VehicleDamageTaken` | 1 | V16.2: damage vehicles take, x0.25-4. |
| `VehicleDamageDealt` | 1 | V16.2: damage done from vehicles, x0.25-4. |

## Not yet tested in game

- The C4 detonation (aim button), the launchers' lock, and tank smoke use the game's own inputs; please watch for them.
- Look for these log lines:
  - `c4:`
  - `rocket: ... (locked)`
  - `vehicle: ... - smoke`
  - `passenger:`
  - `spawn: team N bots die with no shooter`
  - `borrows the ... grid`

## Checks

- 66 offline check groups (348 checks), with AddressSanitizer and UndefinedBehaviorSanitizer. V29 adds `altbotmod_v29_checks.py`: the driving simulation (tank and jeep on 8 maps), the throttle and lane rules, the lock put in place (against stand-ins for the game functions) and the jets' area simulation.
- V21.1 adds a route test (a road and a detour: the route moves to the detour once the stall spot is learned) and 31 LAN checks against two fake BF-Portable backends: finding them, their servers, registering, players per PC, and joining.
- Installer checks run against this package.
- V16.2 adds tests run on all 25 Rush layouts: a bot at a panel takes over the job, and a hold survives fight orders and a 2 m push. Server Control tests now cover the IP targets, the join command line and the accounts file.
- The new V16 group (`altbotmod_v16_checks.py`) runs the real functions with stubs: passengers, smoke, the C4 steps, the lock-on timing, safe flag lookups and the boat rule.
- None of this replaces a live game.
