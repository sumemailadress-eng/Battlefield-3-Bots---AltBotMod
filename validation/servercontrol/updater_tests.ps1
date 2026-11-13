# AltBotMod Release 1.2: the launcher updater (Updater-Core.ps1, Update-Page.ps1) offline - versions, picking the
# download, refusing other hosts, the update script; and that Server Control wires the Updates page in.
param([string]$Dir)
$ErrorActionPreference = 'Stop'
$fail = 0
function Assert($ok, [string]$what) { if (-not $ok) { Write-Host "FAIL: $what"; $script:fail++ } else { Write-Host "ok: $what" } }
foreach ($n in @('Updater-Core.ps1', 'Update-Page.ps1')) {
    $f = Join-Path $Dir $n; $t = $null; $e = $null; [void][Management.Automation.Language.Parser]::ParseFile($f, [ref]$t, [ref]$e)
    Assert ($e.Count -eq 0) "$n parses"
    Assert (-not ([IO.File]::ReadAllBytes($f) | Where-Object { $_ -gt 127 })) "$n is plain ASCII"
}
. (Join-Path $Dir 'Updater-Core.ps1')

$i = Get-BmInstalledRelease $Dir
Assert ($i.Release -eq '1.2' -and $i.Version -eq [version]'1.2.0' -and $i.Build -eq 'V35.2 - New Ground' -and $i.Repo -eq 'sumemailadress-eng/Battlefield-3-Bots---AltBotMod') "installed: Release $($i.Release), build $($i.Build)"
$none = Get-BmInstalledRelease (Join-Path $Dir 'no-such-folder')
Assert (-not $none.Release -and -not $none.Version -and $none.Repo -eq $script:BmUpdateRepoDefault) 'no release file: unknown release, the default repo'
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('upd-test-' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Path $tmp | Out-Null
try {
    [IO.File]::WriteAllText((Join-Path $tmp 'AltBotMod-Release.txt'), "release=1.3`r`nrepo=evil/../x y`r`n")
    $odd = Get-BmInstalledRelease $tmp
    Assert ($odd.Version -eq [version]'1.3.0' -and $odd.Repo -eq $script:BmUpdateRepoDefault) 'a malformed repo falls back to the default'
} finally { Remove-Item -LiteralPath $tmp -Recurse -Force }

foreach ($case in @(@('V1.1', '1.1.0'), @('v1.2', '1.2.0'), @('Release 1.2 (internal V35.2)', '1.2.0'), @('V1.0', '1.0.0'), @('2', '2.0.0'), @('1.2.3', '1.2.3'))) {
    Assert ((ConvertTo-BmReleaseVersion $case[0]) -eq [version]$case[1]) "version of '$($case[0])' is $($case[1])"
}
Assert ($null -eq (ConvertTo-BmReleaseVersion 'Release') -and $null -eq (ConvertTo-BmReleaseVersion '')) 'no number: no version'

$zipball = 'https://api.github.com/repos/sumemailadress-eng/Battlefield-3-Bots---AltBotMod/zipball/V1.3'
$raw = [pscustomobject]@{ tag_name = 'V1.3'; name = 'Release 1.3'; body = "### What's new`n- **Faster** bots`n- ``code``"; html_url = 'https://github.com/x/y/releases/tag/V1.3'; zipball_url = $zipball; published_at = '2026-12-01T10:00:00Z'
    assets = @([pscustomobject]@{ name = 'AltBotMod-full-part1.zip'; size = 1; browser_download_url = 'https://github.com/x/y/releases/download/V1.3/AltBotMod-full-part1.zip' },
               [pscustomobject]@{ name = 'AltBotMod-1.3.zip'; size = 123456; browser_download_url = 'https://github.com/x/y/releases/download/V1.3/AltBotMod-1.3.zip' }) }
$r = ConvertFrom-BmGitHubRelease $raw
Assert ($r.Version -eq [version]'1.3.0' -and $r.Url -like '*/AltBotMod-1.3.zip' -and $r.Size -eq 123456) 'a release .zip asset is preferred (split part files are not)'
Assert ($r.Notes -notmatch '\*\*|###|`' -and $r.Notes -match 'Faster bots') 'notes lose the markdown marks'
$raw.assets = @([pscustomobject]@{ name = 'evil.zip'; size = 5; browser_download_url = 'https://evil.example.com/evil.zip' })
Assert ((ConvertFrom-BmGitHubRelease $raw).Url -eq $zipball) 'an asset on another host is ignored: the source ZIP'
$old = [pscustomobject]@{ tag_name = 'Release'; name = 'V1.0'; body = ''; html_url = ''; zipball_url = $zipball; published_at = ''; assets = @() }
Assert ((ConvertFrom-BmGitHubRelease $old).Version -eq [version]'1.0.0') "tag 'Release' named 'V1.0' reads as 1.0"

$v12 = [pscustomobject]@{ Version = [version]'1.2.0' }
Assert (Test-BmUpdateNewer $v12 $r) '1.3 is newer than 1.2'
Assert (-not (Test-BmUpdateNewer $v12 (ConvertFrom-BmGitHubRelease $old))) '1.0 is not newer than 1.2'
Assert (-not (Test-BmUpdateNewer $v12 ([pscustomobject]@{ Version = [version]'1.2.0' }))) 'the same release is not newer'
Assert (Test-BmUpdateNewer ([pscustomobject]@{ Version = $null }) $r) 'an unknown installed release takes any release'
Assert (-not (Test-BmUpdateNewer $v12 $null)) 'no release known: nothing to install'

foreach ($u in @('https://github.com/a/b', 'https://api.github.com/x', 'https://codeload.github.com/x', 'https://objects.githubusercontent.com/x')) { Assert (Test-BmUpdateUrl $u) "allowed: $u" }
foreach ($u in @('http://github.com/a', 'https://github.com.evil.example/x', 'https://evil.example/github.com', 'file:///c:/x.zip', 'not a url')) { Assert (-not (Test-BmUpdateUrl $u)) "refused: $u" }
$refused = $false; try { [void](Start-BmUpdateDownload ([pscustomobject]@{ Url = 'https://evil.example/x.zip'; Version = $null; Tag = 'x'; Size = 0 })) } catch { $refused = $true }
Assert $refused 'a download from another host is refused before it starts'

# the update script (Start-Process stubbed): waits for the launcher, runs the package's installer for the server folder
$pkg = Join-Path ([IO.Path]::GetTempPath()) ('upd-pkg-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path ([IO.Path]::Combine($pkg, 'files', 'server')) -Force | Out-Null
[IO.File]::WriteAllText([IO.Path]::Combine($pkg, 'files', 'AltBotMod-Setup.ps1'), '# stub')
$script:Started = $null
function Start-Process { param($FilePath, $ArgumentList, $WorkingDirectory, $WindowStyle) $script:Started = "$ArgumentList" }
$env:ComSpec = 'cmd.exe'
try {
    $cmd = Start-BmUpdateInstall $pkg 'C:\BF-Portable\Battlefield_3_Server' 4321 'C:\BF-Portable\BotMod\ServerControl\Server-Control.bat' 'Release 1.3'
    $text = [IO.File]::ReadAllText($cmd)
    Assert ($script:Started -like '*Run-Update.cmd*') 'the update script is started'
    Assert ($text -match 'Wait-Process -Id 4321') 'it waits for the launcher to close'
    Assert ($text -match '-Mode install -ObjectivePreset -ServerDir "C:\\BF-Portable\\Battlefield_3_Server"') 'it runs the installer for this server folder, as Install-AltBotMod.bat does'
    Assert ($text -match 'if errorlevel 1' -and $text -match 'start "" "C:\\BF-Portable\\BotMod\\ServerControl\\Server-Control.bat"') 'a failure stops with a message; success opens the launcher again'
    $bad = $false; try { [void](Start-BmUpdateInstall $pkg 'C:\Tom&Jerry\Server' 1 'x.bat' 'R') } catch { $bad = $true }
    Assert $bad 'a path the script cannot quote safely is refused'
    $bad = $false; try { [void](Start-BmUpdateInstall (Join-Path $pkg 'nothing') 'C:\S' 1 'x.bat' 'R') } catch { $bad = $true }
    Assert $bad 'a package without the installer is refused'
} finally { Remove-Item -LiteralPath $pkg -Recurse -Force; Remove-Item -LiteralPath (Join-Path (Get-BmUpdateWorkDir) 'Run-Update.cmd') -Force -ErrorAction SilentlyContinue }

# Server Control wires the page in; the installer copies the files (the whole ServerControl folder)
$gui = [IO.File]::ReadAllText((Join-Path $Dir 'AltBotMod-ServerControl.ps1'))
Assert ($gui -match "Updater-Core\.ps1" -and $gui -match "Update-Page\.ps1" -and $gui -match 'CheckUpdates = \$true' -and $gui -match 'Upd-StartCheck \$true') 'Server Control loads the Updates page and checks at start (pref CheckUpdates)'
$page = [IO.File]::ReadAllText((Join-Path $Dir 'Update-Page.ps1'))
Assert ($page -match 'Get-BmRunningServers' -and $page -match 'Stop-BmServer' -and $page -match "Server-Control\.bat") 'the page stops running servers only after asking, and reopens Server Control'
if ($fail) { Write-Host "$fail FAILED"; exit 1 }
Write-Host 'updater: all checks passed'
