COMMAND 1.4 / ART EDITION
Six bundled original artworks: Signal, Blue frontier, Fracture, Arctic relay, Night city, Airspace.
They appear on Overview as clickable artwork cards and across the app headers.
Exit the previous Command host via its tray icon or console before updating. Then run Install-Command.bat.
All art files are verified at install time.

ALTBOTMOD / COMMAND 1.4
A separate modern desktop + phone launcher for AltBotMod V31 / BF-Portable

QUICK START
1. Extract this ZIP to a normal folder (do not run it inside the ZIP).
2. Double-click Install-Command.bat. Select BF-Portable if asked.
3. Open BotMod\ServerControl\Command-Desktop.bat for the standalone app window.
   Or use Command-Admin.bat for an elevated app window, like PortableBF.exe.
4. Use Connect phone for LAN access. The tray icon reopens or exits the launcher.
   Command.bat / Command-Browser.bat remain available for browser + console mode.

You can also run Open-Command-Desktop.bat directly from this extracted package.
Open-Command.bat is the browser-mode alternative.
It finds the BF3 server or asks you to select its folder. The installer is
recommended so the new launcher uses your current V31 launcher preferences.

YOUR ORIGINAL MOD AND UI
The add-on installer copies only the new Command files and artwork. It does
not replace Server-Control.bat, Start-BF-Portable.bat, the original UI/core
scripts, the bot DLL, your AI source, routes, or mod settings. Original V31
ServerControl scripts are included unchanged so this extracted package can
also run by itself. No bots or gameplay logic were modified.

Use the Original desktop launcher button for the complete original V31 UI,
including LAN backend discovery, registration, auto-join and embedded Battlelog.
Direct launch/join with existing saved accounts or a manual login is now also
available from the alternate UI.

LAUNCH BF3 AND JOIN A SERVER
On the game PC, click Launch & join on a running server card in Overview, or
Launch & join this server in Server control. Choose a saved player or enter
your existing BF-Portable player name/password. Select the backend server and
click Launch BF3 & join. This uses the existing BF Launcher backend settings,
Path.txt and BFUlauncher.exe, with the original V31 game-join protocol.

If several servers share a name, Command uses an exposed game port to match the
instance. Otherwise choose the target explicitly. It never silently joins the
first listed server. Start the game server first if its Join button is disabled.
The game launches on the host PC; phone requests cannot access player logins
or launch the game. You can still manage server settings from the phone.

Passwords are not saved by default. Remember login stores one using Windows
DPAPI in the original Accounts.json, with a first-edit backup. Existing saved
players, default-player choices and LAN backend accounts remain available.

LEARNING LIBRARY UPDATE
The alternate reader now isolates malformed files/entries, displays warnings
and the actual learning folder, and shows up to 100 entries per detail page.
Unknown or invalid records are preserved on disk. Loading is read-only, and
removing a selected entry leaves unrelated and malformed lines intact.
Refresh library reloads the list after a map has saved new learning.

IMPORTANT AFTER INSTALLING AN UPDATE
Exit the old launcher HOST from its tray menu or close its PowerShell console
before reopening this update. Closing only the app/browser window leaves the
old host running. This version detects an older host instead of silently
reusing it. The BF3 game servers do not need to be stopped for this UI update.

DESKTOP APP WINDOW
The desktop option uses Microsoft Edge or Google Chrome in Chromium app mode: a
dedicated window with no browser tabs or address bar, and its own separate
profile. It is not a new game engine or a native rewrite. No Electron download
is required. Microsoft Edge or Google Chrome must be installed, or you can put
a complete compatible portable Chromium distribution at
ServerControl\Chromium\ (with chrome.exe directly inside that folder).

The desktop host runs without a console. An AltBotMod tray icon stays active
when the app window closes so the phone can still control the server. Double-
click the tray icon to reopen the window. Right-click > Exit launcher to stop
the Command host; game servers stay running. Opening the desktop batch again
reuses the running host on that port. After updating Command files, exit the
old launcher from its tray icon (or close its console), then launch it again.
Start errors are shown in a dialog and recorded in Command-error.log.

ARTWORK AND MAP IMAGES
Appearance and the Overview artwork cards offer six complete bundled artworks:
Signal, Blue frontier, Fracture, Arctic relay, Night city and Airspace. Your
choice is remembered on that device. The installer verifies all six files.
Artwork also appears in page headers. PNGs are in Command-Web\art, except
the original command-art.png at the Command-Web root.
Real images for all 29 maps appear in the picker, rotation list and active
server cards. These are bundled EA/DICE Battlelog thumbnails (146 x 79), not
generated images of the maps. They work offline. See MAP-IMAGE-CREDITS.txt and
MAP-IMAGE-SOURCES.json for credits/URLs, and ARTWORK-PROMPTS.txt for the new art.

PHONE / LOCAL NETWORK
On the host PC, click Connect phone and select your Wi-Fi/Ethernet address.
Scan the QR with the phone camera. Both devices must reach the same LAN.
Changes target the selected server. No QR website or internet service is used.

If the phone cannot connect:
- Keep the desktop tray host or Command console running on the PC.
- Double-click Allow-Phone-Access.bat in this extracted package once.
  This asks Windows for administrator rights and adds a TCP 8787 rule for
  Private networks, limited to the local subnet.
- Ensure the PC's network is marked Private and the phone is not on isolated
  guest Wi-Fi. Try another address in the QR dialog if you have a VPN/virtual
  network adapter. No router port forwarding is required.
- To remove that firewall rule later:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File
    "BotMod\ServerControl\Command-Firewall.ps1" -Remove
  (Put that command on one line, with your full file path.)

The pairing link gives control of the launcher. It is private to this launch
session and changes after a restart. Reopen/scan the new link after restarting.
The launcher uses local HTTP. Launcher executable/IP settings and desktop/game
launch actions are restricted to the host PC, even on paired phones.

WHAT'S IN COMMAND
- Overview of every discovered _StartServer*.bat instance, players, bot targets,
  map, mode, tickets, backend services and launch progress.
- Start/stop each server; next map; restart; end the round for US or RU.
- Select any of the original 29 maps and its compatible game modes, now/next.
- Bot count, all 93 V31 bot settings with groups, help, search and validation.
- Per-instance settings and all-server base values; Inherit removes an override.
- Save, or save + queue ehreload (SETTINGS ONLY). Some values take effect on
  a bot's next spawn or next round. The compiled core is never hot-reloaded.
- All 30 server rules, Normal/Hardcore presets, ticket percentage, health, HUD,
  friendly fire, vehicles and spawn timing; save and optional live apply.
- Player list, kick, and broadcast messages.
- Offline/live map rotation editor: add, remove, drag to reorder, rounds, presets.
  Drag a map by its six-dot grip with a mouse or finger. Scroll normally on the
  rest of the row. Long lists scroll while you drag near the screen edge.
  Keyboard: focus a grip and use Up/Down, Home/End. Escape cancels a drag.
  Reordering changes the draft; Save or Save & apply keeps the new order.
- Live battlefield: positions, objectives, terrain picture when available,
  routes/goals, selection, pan/zoom, axis flips, four heatmap layers, round
  history, kills, combat scoreboard and recurring stuck spots.
- Learning files: inspect effects, remove entries, export ZIP, merge ZIP imports.
- BF-Portable ordered one-click launch and individual service start/stop.
- Host-side launcher path/timing/DNS options and selected backend IP updates.
- Responsive touch layout, offline QR and private session pairing.

SAVING / MULTIPLE DEVICES
Settings, rules and rotation saves check whether the file changed since it was
read. If another device or original UI changed it, refresh before saving.
First edits keep a .before-command backup of each relevant file. Existing
original backup rules also remain. Avoid editing the same files simultaneously
in multiple tools; the original desktop UI has its own save behavior.

Game servers keep running if you close the app window, browser or Command console.
An in-progress launch sequence needs the console to remain open. Closing a
browser tab alone does not stop any service.

ADVANCED START OPTIONS (in BotMod\ServerControl)
Command.bat -ServerDir "D:\BF-Portable\Battlefield_3_Server"
Command.bat -Port 8790
Command.bat -LocalOnly
For a different LAN port, run Command-Firewall.ps1 -Port 8790 once as well.
The default port is 8787. A port already in use is reported in the console.

REQUIREMENTS
Windows PowerShell 5.1, Edge/Chrome/Firefox, existing AltBotMod V31, BF-Portable.
No Python, Node, npm, separate web hosting, or new game-client install required.
Desktop mode uses an installed Edge/Chrome engine; browser mode uses your browser.
The QR encoder, CSS, JavaScript and supplied abstract art are bundled locally.

VALIDATION
See VALIDATION.txt for the checks run and their limits. This package was not
run against your actual Windows server or a live BF3 round in this workspace.
The existing V31 server functions are reused without modification.

REMOVAL
Stop the Command console and remove only AltBotMod-Command.ps1, Command-Http.cs,
Command.bat, Command-Browser.bat, Command-Desktop.bat, Command-Window.ps1,
Command-Admin.bat, Command-Admin.ps1, Command-Firewall.ps1, Command-Join.ps1,
Command-Learning.ps1,
Command-Web, Command-last-folder.txt and Command-NOTICES.txt from ServerControl.
Do not remove the original V31 files. Optionally remove the firewall rule above.
