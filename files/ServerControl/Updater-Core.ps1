# AltBotMod updater - core (no UI), shared by Server Control and AltBotMod Command (Release 1.2).
#  1. Get-BmInstalledRelease reads AltBotMod-Release.txt beside the launcher (release number, build, GitHub repo).
#  2. Start-BmUpdateCheck asks GitHub for the repo's latest release in the background (no window freeze).
#  3. Start-BmUpdateDownload downloads that release (its .zip asset if it has one, otherwise the source ZIP) and
#     extracts it under %TEMP%\AltBotMod-Update, in the background; it checks the package has the installer.
#  4. Start-BmUpdateInstall writes and starts Run-Update.cmd: it waits for the launcher to close, runs the package's
#     own installer for this server folder (settings, routes and learning are kept, as with Install-AltBotMod.bat),
#     then opens the launcher again.
# Background work runs in its own runspace; the caller polls the returned job (.State: running / done / failed).
# Works in Windows PowerShell 5.1 and PowerShell 7.

$script:BmUpdateRepoDefault = 'sumemailadress-eng/Battlefield-3-Bots---AltBotMod'
$script:BmUpdateHosts = @('github.com', 'api.github.com', 'codeload.github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com')

function Get-BmUpdateWorkDir {
    $tmp = [IO.Path]::GetTempPath()
    return (Join-Path $tmp 'AltBotMod-Update')
}

function Get-BmInstalledRelease([string]$scDir) {
    $info = [ordered]@{ Release = ''; Build = ''; Repo = $script:BmUpdateRepoDefault; Version = $null }
    $file = Join-Path $scDir 'AltBotMod-Release.txt'
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        foreach ($line in [IO.File]::ReadAllLines($file)) {
            if ($line -match '^\s*(release|build|repo)\s*=\s*(.*?)\s*$') {
                $key = (Get-Culture).TextInfo.ToTitleCase($Matches[1]); $info[$key] = $Matches[2]
            }
        }
    }
    if ($info.Repo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { $info.Repo = $script:BmUpdateRepoDefault }
    $info.Version = ConvertTo-BmReleaseVersion $info.Release
    return [pscustomobject]$info
}

# "V1.1", "v1.2", "Release 1.2 (internal V35.2)", "V1.0" -> [version] 1.1 / 1.2 / 1.0; $null when there is no number.
function ConvertTo-BmReleaseVersion([string]$text) {
    if (-not $text) { return $null }
    if ($text -match '(\d+)(?:\.(\d+))?(?:\.(\d+))?') {
        $parts = @([int]$Matches[1], $(if ($Matches[2]) { [int]$Matches[2] } else { 0 }), $(if ($Matches[3]) { [int]$Matches[3] } else { 0 }))
        return New-Object Version($parts[0], $parts[1], $parts[2])
    }
    return $null
}

function Test-BmUpdateUrl([string]$url) {
    try { $u = [Uri]$url } catch { return $false }
    return ($u.Scheme -eq 'https' -and $script:BmUpdateHosts -contains $u.Host.ToLowerInvariant())
}

# The release fields the launchers show. $raw: GitHub's JSON for one release.
function ConvertFrom-BmGitHubRelease($raw) {
    $version = ConvertTo-BmReleaseVersion ([string]$raw.tag_name)
    if (-not $version -or $version.Major -gt 20) { $version = ConvertTo-BmReleaseVersion ([string]$raw.name) }
    $url = [string]$raw.zipball_url; $size = 0; $kind = 'source ZIP'
    foreach ($a in @($raw.assets)) {
        if ($a -and [string]$a.name -like '*.zip' -and [string]$a.name -notmatch 'part\d' -and (Test-BmUpdateUrl ([string]$a.browser_download_url))) {
            $url = [string]$a.browser_download_url; $size = [long]$a.size; $kind = [string]$a.name; break
        }
    }
    $notes = [string]$raw.body
    $notes = $notes -replace '\r?\n', "`r`n" -replace '(?m)^#{1,6}\s*', '' -replace '\*\*', '' -replace '`', ''
    return [pscustomobject]@{
        Tag = [string]$raw.tag_name; Name = [string]$raw.name; Version = $version; Notes = $notes
        Page = [string]$raw.html_url; Url = $url; Size = $size; Package = $kind; Published = [string]$raw.published_at
    }
}

function Test-BmUpdateNewer($installed, $latest) {
    if (-not $latest -or -not $latest.Version) { return $false }
    if (-not $installed -or -not $installed.Version) { return $true }
    return ($latest.Version -gt $installed.Version)
}

# The bf3_server_Final.exe processes running from this server folder.
function Get-BmRunningServers([string]$serverDir) {
    $exe = Join-Path $serverDir 'bf3_server_Final.exe'
    $out = @()
    try {
        foreach ($p in @(Get-CimInstance Win32_Process -Filter "Name='bf3_server_Final.exe'" -ErrorAction Stop)) {
            if (-not $p.ExecutablePath -or [string]::Equals($p.ExecutablePath, $exe, [StringComparison]::OrdinalIgnoreCase)) { $out += $p }
        }
    } catch {
        $out = @(Get-Process -Name 'bf3_server_Final' -ErrorAction SilentlyContinue)
    }
    return ,$out
}

# ---- background jobs ----
function Start-BmUpdateJob([scriptblock]$work, [hashtable]$arguments) {
    $state = [hashtable]::Synchronized(@{ State = 'running'; Message = 'Starting...'; Bytes = 0L; Total = 0L; Result = $null; Error = '' })
    $ps = [PowerShell]::Create()
    [void]$ps.AddScript($work).AddArgument($state).AddArgument($arguments)
    $handle = $ps.BeginInvoke()
    return [pscustomobject]@{ Shared = $state; PowerShell = $ps; Handle = $handle }
}
# Call from a timer / poll: true once the job has finished (then read .Shared.State / .Result / .Error).
function Test-BmUpdateJobDone($job) {
    if (-not $job) { return $true }
    if (-not $job.Handle.IsCompleted) { return $false }
    try { [void]$job.PowerShell.EndInvoke($job.Handle) } catch { if ($job.Shared.State -eq 'running') { $job.Shared.State = 'failed'; $job.Shared.Error = $_.Exception.Message } }
    if ($job.Shared.State -eq 'running') { $job.Shared.State = 'failed'; if (-not $job.Shared.Error) { $job.Shared.Error = 'The background task stopped without a result.' } }
    try { $job.PowerShell.Dispose() } catch { }
    return $true
}

$script:BmUpdateCheckWork = {
    param($s, $a)
    try {
        try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
        $s.Message = 'Asking GitHub for the latest release...'
        $r = Invoke-RestMethod -Uri ('https://api.github.com/repos/' + $a.Repo + '/releases/latest') -UseBasicParsing -TimeoutSec 20 `
            -Headers @{ 'User-Agent' = 'AltBotMod-Updater'; 'Accept' = 'application/vnd.github+json' }
        $s.Result = $r; $s.Message = 'Done.'; $s.State = 'done'
    } catch {
        $m = $_.Exception.Message
        if ($m -match '404') { $m = 'The repository has no published release yet.' }
        elseif ($m -match '403|rate limit') { $m = 'GitHub refused the request (HTTP 403). This is usually its limit of 60 checks an hour per network - try again later.' }
        $s.Error = 'Could not reach GitHub: ' + $m; $s.State = 'failed'
    }
}
function Start-BmUpdateCheck([string]$repo) { return Start-BmUpdateJob $script:BmUpdateCheckWork @{ Repo = $repo } }
# After Start-BmUpdateCheck finished: the latest release as ConvertFrom-BmGitHubRelease gives it ($null if it failed).
function Get-BmUpdateCheckResult($job) {
    if (-not $job -or $job.Shared.State -ne 'done') { return $null }
    return (ConvertFrom-BmGitHubRelease $job.Shared.Result)
}

$script:BmUpdateDownloadWork = {
    param($s, $a)
    try {
        try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch { }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $work = $a.WorkDir
        if (-not (Test-Path -LiteralPath $work)) { New-Item -ItemType Directory -Path $work -Force | Out-Null }
        # earlier downloads: only the newest folder of each run is needed
        foreach ($old in @(Get-ChildItem -LiteralPath $work -ErrorAction SilentlyContinue)) {
            try { Remove-Item -LiteralPath $old.FullName -Recurse -Force -ErrorAction Stop } catch { }
        }
        $label = ($a.Label -replace '[^A-Za-z0-9._-]', '_')
        $zip = Join-Path $work ("AltBotMod-$label.zip"); $part = $zip + '.partial'
        $s.Message = 'Downloading...'
        $req = [Net.HttpWebRequest]::Create($a.Url)
        $req.UserAgent = 'AltBotMod-Updater'; $req.Timeout = 30000; $req.ReadWriteTimeout = 60000; $req.AllowAutoRedirect = $true
        $resp = $req.GetResponse()
        try {
            $final = $resp.ResponseUri
            if ($final.Scheme -ne 'https' -or $a.Hosts -notcontains $final.Host.ToLowerInvariant()) { throw "the download was redirected to an unexpected address ($($final.Host))." }
            $s.Total = [long]$resp.ContentLength; if ($s.Total -le 0 -and $a.Size -gt 0) { $s.Total = [long]$a.Size }
            $in = $resp.GetResponseStream(); $out = [IO.File]::Create($part)
            try {
                $buf = New-Object byte[] 262144
                while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
                    $out.Write($buf, 0, $n); $s.Bytes += $n
                    if ($a.Cancel -and $a.Cancel.Requested) { throw 'cancelled.' }
                }
            } finally { $out.Dispose(); $in.Dispose() }
        } finally { $resp.Close() }
        Move-Item -LiteralPath $part -Destination $zip -Force
        try { Unblock-File -LiteralPath $zip -ErrorAction SilentlyContinue } catch { }
        $s.Message = 'Extracting...'
        $dest = Join-Path $work ("AltBotMod-$label")
        [IO.Compression.ZipFile]::ExtractToDirectory($zip, $dest)
        # the package root: the folder with Install-AltBotMod.bat and files\AltBotMod-Setup.ps1 (GitHub's source ZIP
        # puts everything one folder down)
        $root = $null
        foreach ($cand in @($dest) + @(Get-ChildItem -LiteralPath $dest -Directory | ForEach-Object { $_.FullName })) {
            if ((Test-Path -LiteralPath ([IO.Path]::Combine($cand, 'files', 'AltBotMod-Setup.ps1')) -PathType Leaf) -and
                (Test-Path -LiteralPath ([IO.Path]::Combine($cand, 'files', 'server', 'BotModCore.dll')) -PathType Leaf)) { $root = $cand; break }
        }
        if (-not $root) { throw 'the download is not an AltBotMod package (no files\AltBotMod-Setup.ps1).' }
        foreach ($f in @(Get-ChildItem -LiteralPath $root -Recurse -File -Include '*.ps1', '*.bat', '*.cmd', '*.dll', '*.cs' -ErrorAction SilentlyContinue)) {
            try { Unblock-File -LiteralPath $f.FullName -ErrorAction SilentlyContinue } catch { }
        }
        try { Remove-Item -LiteralPath $zip -Force } catch { }
        $s.Result = $root; $s.Message = 'Ready to install.'; $s.State = 'done'
    } catch {
        $s.Error = 'Download failed: ' + $_.Exception.Message; $s.State = 'failed'
        try { if ($part -and (Test-Path -LiteralPath $part)) { Remove-Item -LiteralPath $part -Force } } catch { }
    }
}
function Start-BmUpdateDownload($release, $cancel = $null) {
    if (-not $release -or -not (Test-BmUpdateUrl $release.Url)) { throw 'The release has no GitHub download address.' }
    $label = $(if ($release.Version) { [string]$release.Version } else { $release.Tag })
    return Start-BmUpdateJob $script:BmUpdateDownloadWork @{ Url = $release.Url; Size = $release.Size; Label = $label; WorkDir = (Get-BmUpdateWorkDir); Hosts = $script:BmUpdateHosts; Cancel = $cancel }
}

function Format-BmUpdateProgress($job) {
    if (-not $job) { return '' }
    $s = $job.Shared
    if ($s.Bytes -le 0) { return $s.Message }
    $mb = [Math]::Round($s.Bytes / 1MB, 1)
    if ($s.Total -gt 0) { return ('{0} {1} of {2} MB ({3}%)' -f $s.Message, $mb, [Math]::Round($s.Total / 1MB, 1), [Math]::Floor(100 * $s.Bytes / $s.Total)) }
    return ('{0} {1} MB' -f $s.Message, $mb)
}

# Starts the installer of the extracted package for this server folder, in its own console window, once the process
# $launcherPid has closed; afterwards it opens $relaunch (a .bat in the installed ServerControl folder) again.
# The caller closes itself right after. Returns the .cmd path.
function Start-BmUpdateInstall([string]$packageRoot, [string]$serverDir, [int]$launcherPid, [string]$relaunch, [string]$label) {
    $setup = [IO.Path]::Combine($packageRoot, 'files', 'AltBotMod-Setup.ps1')
    if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) { throw "The downloaded package is incomplete: $setup is missing." }
    foreach ($p in @($packageRoot, $serverDir, $relaunch)) { if ($p -match '[%!"^&|<>]') { throw "Path not usable in the update script (contains a special character): $p" } }
    $work = Get-BmUpdateWorkDir
    if (-not (Test-Path -LiteralPath $work)) { New-Item -ItemType Directory -Path $work -Force | Out-Null }
    $cmd = Join-Path $work 'Run-Update.cmd'
    $lines = @(
        '@echo off',
        "title AltBotMod update to $label",
        'echo AltBotMod update: waiting for the launcher to close...',
        "powershell.exe -NoProfile -Command `"try { Wait-Process -Id $launcherPid -Timeout 60 -ErrorAction Stop } catch { }`"",
        "powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File `"$setup`" -Mode install -ObjectivePreset -ServerDir `"$serverDir`"",
        'if errorlevel 1 (',
        '  echo.',
        '  echo The update did not finish - see the message above. Anything it replaced is in the BotMod_backup folder it names.',
        '  pause',
        '  exit /b 1',
        ')',
        'echo.',
        "echo AltBotMod $label is installed. Opening the launcher again...",
        "if exist `"$relaunch`" start `"`" `"$relaunch`"",
        'timeout /t 8',
        'exit /b 0'
    )
    [IO.File]::WriteAllText($cmd, ($lines -join "`r`n") + "`r`n", (New-Object Text.ASCIIEncoding))
    Start-Process -FilePath $env:ComSpec -ArgumentList ('/c "' + $cmd + '"') -WorkingDirectory (Split-Path -Parent $setup) -WindowStyle Normal
    return $cmd
}
