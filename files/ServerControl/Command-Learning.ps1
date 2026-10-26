# Robust read-only snapshots for the alternate UI. Original Learning-Core.ps1 stays unchanged.
$script:CommandLearnCache=@{}
function Read-CommandLearnLines([string]$path) {
    $stream=New-Object IO.FileStream($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader=New-Object IO.StreamReader($stream,[Text.Encoding]::UTF8,$true)
    try { return ,($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Dispose() }
}
function Read-CommandLearnFile([string]$path) {
    $item=Get-Item -LiteralPath $path -ErrorAction Stop
    $signature=[string]$item.Length+':'+[string]$item.LastWriteTimeUtc.Ticks
    $cached=$script:CommandLearnCache[$path]
    if ($cached -and $cached.Signature -eq $signature) { return $cached.File }
    $lines=Read-CommandLearnLines $path
    if (-not $lines.Count -or -not $lines[0].TrimStart([char]0xFEFF).StartsWith('AltBotMod learned')) { throw 'Not a complete AltBotMod learned-file header; it may still be being written.' }
    $key=$item.BaseName
    if ($key -notmatch '^[A-Za-z0-9_]+$') { throw 'Unsupported learned-file name.' }
    $f=[pscustomobject]@{ Path=$path; Key=$key; Level=$key; Mode=''; Spots=0; Heat=0; Routes=0; Pads=0; Written=$item.LastWriteTime; Entries=(New-Object Collections.Generic.List[object]); Skipped=0; Warnings=(New-Object Collections.Generic.List[string]) }
    if ($key -match '^(.+?)_((?:Conquest|Rush|Squad|TeamDeathMatch|GunMaster|Domination|TankSuperiority|Scavenger|CaptureTheFlag|AirSuperiority).*)$') { $f.Level=$Matches[1]; $f.Mode=$Matches[2] }
    for ($n=1;$n -lt $lines.Count;$n++) {
        $line=$lines[$n]; if (-not $line -or $line.TrimStart().StartsWith('#')) { continue }
        try {
            $e=ConvertFrom-BmLearnLine $line
            if ($e.Type -notin @('Spot','Heat','Route','Pad')) { if ($line -match '^(spot|heat|route|pad)\t') { throw 'Incomplete entry.' }; continue }
            foreach ($field in @('X','Y','Z','Seen','Passed','Failed','Jumps','First','Last','Length')) {
                if ($e.PSObject.Properties[$field]) { $v=[double]$e.$field; if ([double]::IsNaN($v) -or [double]::IsInfinity($v)) { throw 'Incomplete numeric field.' } }
            }
            $f.Entries.Add($e)
            switch ($e.Type) { 'Spot' {$f.Spots++} 'Heat' {$f.Heat++} 'Route' {$f.Routes++} 'Pad' {$f.Pads++} }
        } catch {
            $f.Skipped++
            if ($f.Warnings.Count -lt 8) { $f.Warnings.Add(('Line {0} is incomplete or invalid; preserved on disk.' -f ($n+1))) }
        }
    }
    if ($script:CommandLearnCache.Count -ge 32) { $script:CommandLearnCache.Clear() }
    $script:CommandLearnCache[$path]=@{ Signature=$signature; File=$f }
    return $f
}
function Get-CommandLearning([string]$serverDir) {
    $folder=Get-BmLearnDir $serverDir; $rows=New-Object Collections.Generic.List[object]; $warnings=New-Object Collections.Generic.List[string]
    $exists=Test-Path -LiteralPath $folder -PathType Container
    if ($exists) {
        try {
            foreach ($file in @(Get-ChildItem -LiteralPath $folder -Filter '*.learn' -File -ErrorAction Stop | Sort-Object Name)) {
                try { $f=Read-CommandLearnFile $file.FullName; $rows.Add(@{ Key=$f.Key; Level=$f.Level; Mode=$f.Mode; Spots=$f.Spots; Heat=$f.Heat; Routes=$f.Routes; Pads=$f.Pads; Written=$f.Written.ToString('o'); Skipped=$f.Skipped }); if ($f.Skipped) { $warnings.Add("$($file.Name): $($f.Skipped) incomplete entries were skipped in this view and kept on disk.") } }
                catch { $warnings.Add($file.Name+': '+$_.Exception.Message) }
            }
        } catch { $warnings.Add('Could not list the learning folder: '+$_.Exception.Message) }
    }
    $breakable=@{ Breakable=0; Unbreakable=0 }
    try { $breakable=Get-BmBreakableInfo $serverDir } catch { $warnings.Add('Material library could not be read: '+$_.Exception.Message) }
    return @{ Files=$rows.ToArray(); Warnings=$warnings.ToArray(); Directory=$folder; Exists=[bool]$exists; Breakable=$breakable }
}
function Get-CommandLearnPage($file,[int]$offset) {
    $offset=[Math]::Max(0,[Math]::Min($offset,[Math]::Max(0,$file.Entries.Count-1))); $take=100
    $rows=@(for ($n=$offset;$n -lt [Math]::Min($file.Entries.Count,$offset+$take);$n++) { $e=$file.Entries[$n]; @{ Type=$e.Type; Kind=$e.Kind; X=$e.X; Z=$e.Z; Effect=$e.Effect; Status=$e.Status; Line=$e.Line } })
    return @{ Key=$file.Key; Entries=$rows; Total=$file.Entries.Count; Offset=$offset; Limit=$take; Skipped=$file.Skipped; Warnings=$file.Warnings.ToArray() }
}
function Remove-CommandLearnLines([string]$path,[string[]]$selected) {
    $remove=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($line in $selected) { [void]$remove.Add($line) }
    $lines=Read-CommandLearnLines $path; $keep=New-Object Collections.Generic.List[string]; $gone=0
    for ($n=0;$n -lt $lines.Count;$n++) { if ($n -gt 0 -and $remove.Contains($lines[$n])) { $gone++ } else { $keep.Add($lines[$n]) } }
    if ($gone) { Write-BmLearnLines $path $keep; $script:CommandLearnCache.Remove($path) }
    return $gone
}
