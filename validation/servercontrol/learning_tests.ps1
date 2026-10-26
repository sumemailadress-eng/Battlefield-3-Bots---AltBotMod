# AltBotMod V21: Server Control's learning data side (Learning-Core.ps1) against files the real store writes
# (learnfile_tool, built from learn.c): reading and judging every kind of entry, deleting entries the server has moved
# meanwhile, files the server still reads afterwards, clearing a map, export / import / merge, an old install's
# BotMod_breakable.txt, and the Learning page script parsing. No window is opened.
param([Parameter(Mandatory)][string]$Dir, [Parameter(Mandatory)][string]$Tool)
$ErrorActionPreference = 'Stop'
. (Join-Path $Dir 'Learning-Core.ps1')
$script:n = 0; $script:fail = 0
function Assert($c, [string]$what) { $script:n++; if (-not $c) { $script:fail++; Write-Host "FAIL: $what" } }
function Tool([string]$mode, [string]$path) { return (& $Tool $mode $path | Out-String).Trim() }

$root = Join-Path ([IO.Path]::GetTempPath()) ('bm-sc-learn-' + [Guid]::NewGuid().ToString('N'))
$srv = Join-Path $root 'Battlefield_3_Server'; $learn = Join-Path $srv 'BotMod_learned'
New-Item -ItemType Directory -Path $learn -Force | Out-Null
try {
    Assert (@(Get-BmLearnFiles $srv).Count -eq 0) 'empty folder: no files'
    $file = Join-Path $learn 'MP_001_ConquestLarge0.learn'
    Tool 'write' $file | Out-Null
    # 1. reading and what each entry does
    $f = Read-BmLearnFile $file
    Assert ($f -and $f.Key -eq 'MP_001_ConquestLarge0' -and $f.Level -eq 'MP_001' -and $f.Mode -eq 'ConquestLarge0') 'header: key, level, mode'
    Assert ($f.Spots -eq 6 -and $f.Heat -eq 1 -and $f.Routes -eq 1 -and $f.Pads -eq 1 -and $f.Entries.Count -eq 9) 'counts per type'
    Assert ($f.Map -and $f.Map.File -eq 'map_MP_001.bmp' -and $f.Map.MinX -eq -1000 -and $f.Map.MaxZ -eq 2100) 'map picture line'
    $s = @($f.Entries | Where-Object Type -eq 'Spot')
    Assert (($s | Where-Object { $_.X -eq 10 }).Status -eq 'avoided') 'foot spot nobody passes: avoided'
    Assert (($s | Where-Object { $_.X -eq 40 }).Status -eq 'kept open') 'foot spot passed: kept open'
    Assert (($s | Where-Object { $_.X -eq 70 }).Status -eq 'jump') 'foot spot passed with a jump: jump'
    Assert (($s | Where-Object { $_.X -eq 100 }).Status -eq 'learning') 'foot spot not decided: learning'
    Assert (($s | Where-Object { $_.Kind -eq 'ground' }).Status -eq 'avoided' -and ($s | Where-Object { $_.Kind -eq 'ground' }).Effect -match 'driving map') 'driving stall: struck off the driving map'
    $air = $s | Where-Object { $_.Kind -eq 'air' }
    Assert ($air.Status -eq 'more height' -and $air.Effect -match '35 m more' -and $air.Effect -match '15 m within 150') 'aircraft loss: the heights the server uses'
    $h = @($f.Entries | Where-Object Type -eq 'Heat')[0]
    Assert ($h.X -eq 505 -and $h.Z -eq 505 -and $h.Seen -eq 8 -and $h.Effect -match '2x') 'death cell: centre, deaths, cost (8 deaths: 2x)'
    $r = @($f.Entries | Where-Object Type -eq 'Route')[0]
    Assert ($r.Points.Count -eq 20 -and [Math]::Abs($r.Length - 57) -lt 0.5 -and $r.Effect -match 'walking') 'player route: 20 points, 57 m, walking'
    $p = @($f.Entries | Where-Object Type -eq 'Pad')[0]
    Assert ($p.Status -eq 'landing spot' -and $p.Seen -eq 2 -and $p.Effect -match 'transports try it first') 'landing spot'
    Assert ((Format-BmLearnTime 1760000000) -match '^2025-10-0\d') 'times shown local'
    # 2. delete: the server moved the spot 1 m and counted once more meanwhile - still the same entry
    $text = [IO.File]::ReadAllText($file).Replace("spot`tfoot`t10.0 1.0 20.0`t1.00 0.00`t1 0 3 0", "spot`tfoot`t11.0 1.0 20.3`t1.00 0.00`t2 0 4 0")
    [IO.File]::WriteAllText($file, $text)
    $gone = Remove-BmLearnEntries $file @(($s | Where-Object { $_.X -eq 10 }), $p)
    $f2 = Read-BmLearnFile $file
    Assert ($gone -eq 2 -and $f2.Spots -eq 5 -and $f2.Pads -eq 0 -and $f2.Map) 'delete: moved spot and pad gone, the rest and the map line kept'
    Assert ((Tool 'read' $file) -match 'records=7 spots=5 heat=1 routes=1 pads=0 extra=1') 'the server store reads the edited file'
    Assert (-not (Test-Path -LiteralPath ($file + '.sc.tmp'))) 'no temporary file left behind'
    # an entry that is not there any more: nothing happens
    Assert ((Remove-BmLearnEntries $file @($p)) -eq 0) 'deleting twice: 0'
    # 3. clearing: killing grounds only, then the map
    Assert ((Clear-BmLearnFile $file @('Heat')) -eq 1 -and (Read-BmLearnFile $file).Heat -eq 0) 'forget killing grounds'
    Tool 'write' (Join-Path $learn 'MP_003_ConquestLarge0.learn') | Out-Null
    Assert (@(Get-BmLearnFiles $srv).Count -eq 2) 'two maps listed'
    Assert ((Clear-BmLearnFile $file) -eq 6) 'forget this map: every entry'
    $f3 = Read-BmLearnFile $file
    Assert ($f3 -and $f3.Entries.Count -eq 0 -and $f3.Map) 'a cleared map keeps its header and picture line'
    Assert ((Tool 'read' $file) -match 'records=0 spots=0') 'the server store reads a cleared file'
    # 4. export, import into another install (new), again (nothing new), merge
    [IO.File]::WriteAllText((Join-Path $learn 'breakable.txt'), "0a0b0c0d b`r`n01020304 n`r`n")
    $g = Get-BmBreakableInfo $srv
    Assert ($g.Breakable -eq 1 -and $g.Unbreakable -eq 1) 'glass and panels counted'
    $zip = Join-Path $root 'export.zip'
    Assert ((Export-BmLearning $srv $zip) -eq 3 -and (Test-Path $zip)) 'export: 2 maps + breakable.txt'
    Assert ((Export-BmLearning $srv (Join-Path $root 'one.zip') @('MP_003_ConquestLarge0')) -eq 2) 'export one map (+ breakable.txt)'
    $srv2 = Join-Path $root 'Other\Battlefield_3_Server'.Replace('\', [IO.Path]::DirectorySeparatorChar); New-Item -ItemType Directory -Path $srv2 -Force | Out-Null
    $res = Import-BmLearning $srv2 $zip
    Assert ($res[0] -eq 2 -and (Get-BmBreakableInfo $srv2).Breakable -eq 1) 'import into a new install: 2 maps, glass'
    $res = Import-BmLearning $srv2 $zip
    Assert ($res[0] -eq 0 -and $res[1] -eq 0 -and $res[2] -eq 0) 'the same import again adds nothing'
    $f4 = Join-Path (Get-BmLearnDir $srv2) 'MP_003_ConquestLarge0.learn'
    Clear-BmLearnFile $f4 @('Route', 'Pad') | Out-Null
    $res = Import-BmLearning $srv2 $zip
    Assert ($res[1] -eq 1 -and $res[2] -eq 2 -and (Read-BmLearnFile $f4).Routes -eq 1 -and (Read-BmLearnFile $f4).Pads -eq 1) 'merge: only the missing entries come back'
    Assert ((Tool 'read' $f4) -match 'routes=1 pads=1') 'the server store reads a merged file'
    # 5. an older install (V18-V20: BotMod_breakable.txt next to the server) - the folder import takes it
    $old = Join-Path $root 'OldInstall'; New-Item -ItemType Directory -Path (Join-Path $old 'BotMod_learned') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $old 'BotMod_breakable.txt'), "deadbeef b`r`n")
    Copy-Item -LiteralPath (Join-Path $learn 'MP_003_ConquestLarge0.learn') -Destination (Join-Path $old 'BotMod_learned\XP1_001_ConquestAssaultLarge0.learn'.Replace('\', [IO.Path]::DirectorySeparatorChar))
    $res = Import-BmLearning $srv2 $old
    Assert ($res[0] -eq 1 -and (Get-BmBreakableInfo $srv2).Breakable -eq 2) 'folder import: a new map and the old glass file'
    # 6. a file that is not a learned file is not listed and not touched
    [IO.File]::WriteAllText((Join-Path $learn 'junk.learn'), 'hello')
    Assert (@(Get-BmLearnFiles $srv).Count -eq 2 -and [IO.File]::ReadAllText((Join-Path $learn 'junk.learn')) -eq 'hello') 'foreign file ignored'
    # 7. the page scripts parse
    foreach ($ps in @('Learning-Core.ps1', 'Learning-Page.ps1', 'AltBotMod-ServerControl.ps1', 'ServerControl-Core.ps1')) {
        $errs = $null; [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $Dir $ps), [ref]$null, [ref]$errs)
        Assert (-not $errs.Count) "$ps parses ($($errs | Select-Object -First 1))"
    }
    $sc = Get-Content -Raw -LiteralPath (Join-Path $Dir 'AltBotMod-ServerControl.ps1')
    Assert ($sc -match "Learning-Core\.ps1" -and $sc -match "Learning-Page\.ps1" -and $sc -match 'tabLearn') 'Server Control loads the Learning page'
    $core = Get-Content -Raw -LiteralPath (Join-Path $Dir 'ServerControl-Core.ps1')
    Assert ($core -match "New-BmSetting 'Learning'" -and $core -match "New-BmSetting 'LearnFromPlayers'") 'settings rows: Learning, LearnFromPlayers'
} finally { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
if ($script:fail) { Write-Host "$($script:fail) of $($script:n) check(s) failed"; exit 1 }
Write-Host "PASS: $($script:n) Server Control learning checks (read and judge every entry type, delete moved entries, clear, glass, export, import, merge, older install, foreign file, page scripts parse, settings rows)"
