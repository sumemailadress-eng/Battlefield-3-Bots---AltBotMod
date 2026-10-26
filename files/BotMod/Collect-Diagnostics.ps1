# Read-only local collection. Never uploads files or changes server state.
param([string]$ServerDir='', [string]$OutputDir='')
$ErrorActionPreference='Stop'
function Resolve-DiagnosticServer([string]$path) {
    if(-not $path -or -not (Test-Path -LiteralPath $path)){return $null}
    $item=Get-Item -LiteralPath $path
    $parent=if($item.PSIsContainer){$item.FullName}else{$item.Directory.FullName}
    for($i=0;$i -lt 6 -and $parent;$i++) {
        if(Test-Path -LiteralPath (Join-Path $parent 'bf3_server_Final.exe')){return $parent}
        $sub=Join-Path $parent 'Battlefield_3_Server'
        if(Test-Path -LiteralPath (Join-Path $sub 'bf3_server_Final.exe')){return $sub}
        $next=Split-Path $parent -Parent;if($next -eq $parent){break};$parent=$next
    }
    return $null
}
function Get-DiagnosticInstancePath([string]$command) {
    # Parse this one path, never save the full command line or credentials.
    if($command -match '(?i)(?:^|\s)-serverInstancePath\s+(?:"([^"]+)"|([^\s]+))') {
        if($Matches[1]){return $Matches[1]}else{return $Matches[2]}
    }
    return $null
}
function Read-NavHeader([IO.FileInfo]$file) {
    $stream=$null;$reader=$null
    try {
        $stream=[IO.File]::Open($file.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        if($stream.Length -lt 60){return "$($file.Name): truncated header"}
        $reader=New-Object IO.BinaryReader($stream)
        $magic=$reader.ReadUInt32()
        $level=[Text.Encoding]::ASCII.GetString($reader.ReadBytes(32)).TrimEnd([char]0)
        $x=$reader.ReadSingle();$z=$reader.ReadSingle();$cell=$reader.ReadSingle()
        $w=$reader.ReadInt32();$h=$reader.ReadInt32();$layers=$reader.ReadInt32()
        return ('{0}: magic={1:X8} level={2} dimensions={3}x{4} layers={5} cell={6} bounds_x={7}..{8} bounds_z={9}..{10} bytes={11}' -f $file.Name,$magic,$level,$w,$h,$layers,$cell,$x,($x+$w*$cell),$z,($z+$h*$cell),$stream.Length)
    } catch {return "$($file.Name): header read failed: $($_.Exception.Message)"}
    finally {if($reader){$reader.Dispose()}elseif($stream){$stream.Dispose()}}
}
function Invoke-DiagnosticCollection([string]$srv,[string]$output) {
    if(-not (Test-Path -LiteralPath (Join-Path $srv 'bf3_server_Final.exe'))){throw 'Select the folder containing bf3_server_Final.exe.'}
    $srv=(Get-Item -LiteralPath $srv).FullName;$root=Split-Path $srv -Parent;$bot=Join-Path $root 'BotMod'
    if(-not $output){$output=Join-Path $bot 'Diagnostics'}
    $null=New-Item -ItemType Directory -Path $output -Force
    $output=(Get-Item -LiteralPath $output).FullName
    $archive=Join-Path $output ('BotMod-Diagnostics-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6)+'.zip')
    $dirs=New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach($dir in @($srv,$bot,(Join-Path $srv 'Logs'),(Join-Path $root 'Logs'),(Join-Path $bot 'LiveMap'),(Join-Path $bot 'ServerControl'),(Join-Path $srv 'BotMod_learned'))){if(Test-Path -LiteralPath $dir -PathType Container){$null=$dirs.Add($dir)}}
    # Bounded known instance containers: two directory levels, no drive scan.
    foreach($container in @((Join-Path $srv 'Instance'),(Join-Path $srv 'Instances'),(Join-Path $srv 'instance'),(Join-Path $root 'Instance'),(Join-Path $root 'Instances'))) {
        if(-not (Test-Path -LiteralPath $container -PathType Container)){continue}
        $null=$dirs.Add($container);$logs=Join-Path $container 'Logs';if(Test-Path -LiteralPath $logs){$null=$dirs.Add($logs)}
        foreach($child in @(Get-ChildItem -LiteralPath $container -Directory | Select-Object -First 64)){
            $logs=Join-Path $child.FullName 'Logs';if(Test-Path -LiteralPath $logs){$null=$dirs.Add($logs)}
        }
    }
    try {
        foreach($process in @(Get-CimInstance Win32_Process -Filter "name='bf3_server_Final.exe'" -ErrorAction Stop)) {
            if($process.ExecutablePath -and -not [string]::Equals($process.ExecutablePath,(Join-Path $srv 'bf3_server_Final.exe'),[StringComparison]::OrdinalIgnoreCase)){continue}
            $instance=Get-DiagnosticInstancePath $process.CommandLine
            if($instance){if(-not [IO.Path]::IsPathRooted($instance)){$instance=Join-Path $srv $instance};$logs=Join-Path $instance 'Logs';if(Test-Path -LiteralPath $logs -PathType Container){$null=$dirs.Add($logs)}}
        }
    } catch { } # Still collects retained instance dumps when the server stopped.
    $chosen=New-Object 'System.Collections.Generic.Dictionary[string,string]' ([StringComparer]::OrdinalIgnoreCase)
    $allDumps=New-Object System.Collections.Generic.List[IO.FileInfo]
    $report=New-Object System.Collections.Generic.List[string]
    $report.Add('AltBotMod V14 - Steady')
    $report.Add('Created UTC: '+[DateTime]::UtcNow.ToString('o'))
    $report.Add('Run ehdiag in the BotMod console (or admin !botdiag) before collecting for current bot/vehicle decisions.')
    $report.Add('No live BF3 test is represented by this report. A dump is needed to investigate an unconfirmed native crash.')
    $index=0
    foreach($dir in $dirs) {
        $index++;$files=@(Get-ChildItem -LiteralPath $dir -File -ErrorAction SilentlyContinue)
        foreach($file in @($files | Where-Object {$_.Name -like 'BotMod*.log' -or $_.Name -like 'diagnostics_*.txt' -or $_.Name -eq 'enhanced.ini' -or $_.Name -match '^enhanced_[0-9]+\.ini$' -or $_.Name -match '^(live|events)_[0-9]+\.txt$' -or $_.Extension -ieq '.learn' -or ($_.Name -eq 'breakable.txt' -and $_.DirectoryName -like '*BotMod_learned') -or $_.Name -match '^Launcher\.(log|json)$'} | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 16)) {
            $chosen[$file.FullName]=('logs/'+$index+'/'+$file.Name)
        }
        foreach($file in $files | Where-Object {$_.Extension -ieq '.dmp'}){$allDumps.Add($file)}
        if($dir -eq $srv){foreach($file in $files | Where-Object {$_.Name -like 'BotMod_nav_*.bin'}){$report.Add((Read-NavHeader $file))}}
    }
    $latest=$allDumps | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if($latest){$chosen[$latest.FullName]='crash/'+$latest.Name;$report.Add('Latest native dump: '+$latest.FullName)}else{$report.Add('No native .dmp found in known instance Logs folders. Client crashes need the client dump separately.')}
    foreach($name in @('bf3_server_Final.exe','BotModCore.dll','xinput9_1_0.dll')) {
        $file=Join-Path $srv $name
        if(Test-Path -LiteralPath $file){try{$report.Add($name+' SHA256 '+(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash)}catch{$report.Add($name+' hash unavailable')}}
    }
    Add-Type -AssemblyName System.IO.Compression
    $stream=[IO.File]::Open($archive,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $zip=New-Object IO.Compression.ZipArchive($stream,[IO.Compression.ZipArchiveMode]::Create,$false)
    try {
        foreach($item in $chosen.GetEnumerator()) {
            $inputStream=$null;$entryStream=$null
            try {
                $inputStream=[IO.File]::Open($item.Key,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
                $entry=$zip.CreateEntry($item.Value,[IO.Compression.CompressionLevel]::Optimal);$entryStream=$entry.Open()
                # Snapshot the starting length: a continuously growing live log
                # cannot make collection loop indefinitely.
                $remaining=$inputStream.Length;$buffer=New-Object byte[] 65536
                while($remaining -gt 0){$read=$inputStream.Read($buffer,0,[int][Math]::Min($buffer.Length,$remaining));if(-not $read){break};$entryStream.Write($buffer,0,$read);$remaining-=$read}
                $report.Add('Included: '+$item.Value)
            } catch {$report.Add('Could not collect '+[IO.Path]::GetFileName($item.Key)+': '+$_.Exception.Message)}
            finally {if($entryStream){$entryStream.Dispose()};if($inputStream){$inputStream.Dispose()}}
        }
        $entry=$zip.CreateEntry('REPORT.txt');$writer=New-Object IO.StreamWriter($entry.Open(),(New-Object Text.UTF8Encoding($false)))
        try {$writer.Write(($report -join "`r`n")+"`r`n")}finally{$writer.Dispose()}
    } finally {$zip.Dispose();$stream.Dispose()}
    return $archive
}
function Start-DiagnosticCollection {
    $srv=if($ServerDir){Resolve-DiagnosticServer $ServerDir}else{Resolve-DiagnosticServer $PSScriptRoot}
    if(-not $srv -and -not $ServerDir){foreach($path in @('D:\BF-Portable\Battlefield_3_Server','C:\BF-Portable\Battlefield_3_Server')){$srv=Resolve-DiagnosticServer $path;if($srv){break}}}
    if(-not $srv){$srv=Resolve-DiagnosticServer (Read-Host 'BF3 server folder')}
    if(-not $srv){throw 'bf3_server_Final.exe was not found.'}
    $zip=Invoke-DiagnosticCollection $srv $OutputDir
    Write-Host "Diagnostics saved: $zip" -ForegroundColor Green
}
try {Start-DiagnosticCollection} catch {Write-Host ('ERROR: '+$_.Exception.Message) -ForegroundColor Red;exit 1}
