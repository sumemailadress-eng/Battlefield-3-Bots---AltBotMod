# AltBotMod Server Control - the Updates page (Release 1.2): which release is installed, the latest one on GitHub, its
# notes, and download + install in one click. The work is in Updater-Core.ps1 (shared with AltBotMod Command).
# Dot-sourced by AltBotMod-ServerControl.ps1 after the Learning page.

$tabUpd = New-Object Windows.Forms.TabPage; $tabUpd.Text = 'Updates'
$tabs.TabPages.Add($tabUpd)
$script:Upd = [pscustomobject]@{ Installed = (Get-BmInstalledRelease $PSScriptRoot); Latest = $null; Check = $null; Download = $null; Root = $null; Auto = $false }

$updInstalled = New-Label '' 10 14 (New-Object Drawing.Font('Segoe UI', 10, [Drawing.FontStyle]::Bold))
$updLatest = New-Label 'Latest release: not checked yet.' 10 42
$updCheck = New-Button 'Check for updates' 10 72 160
$updInstall = New-Button 'Download and install' 180 72 180; $updInstall.Enabled = $false
$updPage = New-Button 'Open release page' 370 72 150; $updPage.Enabled = $false
$updAutoBox = New-Object Windows.Forms.CheckBox; $updAutoBox.Text = 'Check when Server Control opens'; $updAutoBox.AutoSize = $true
$updAutoBox.Location = New-Object Drawing.Point(540, 77); $updAutoBox.Checked = [bool]$script:Prefs.CheckUpdates
$updProgress = New-Label '' 10 110
$updNotes = New-Object Windows.Forms.TextBox; $updNotes.Multiline = $true; $updNotes.ReadOnly = $true; $updNotes.ScrollBars = 'Vertical'; $updNotes.WordWrap = $true
$updNotes.Location = New-Object Drawing.Point(10, 136); $updNotes.Size = New-Object Drawing.Size(976, 400); $updNotes.Anchor = 'Top,Left,Right,Bottom'
$updHelp = New-Label ('Download and install stops nothing by itself: stop the servers first (it offers to). It closes this window, runs the ' +
    'release''s own installer for this server folder - your settings, saved routes, admins and what the bots learned are kept, ' +
    'and anything replaced is backed up - then opens Server Control again.') 10 545
$updHelp.AutoSize = $false; $updHelp.Size = New-Object Drawing.Size(976, 44); $updHelp.Anchor = 'Left,Right,Bottom'
$tabUpd.Controls.AddRange(@($updInstalled, $updLatest, $updCheck, $updInstall, $updPage, $updAutoBox, $updProgress, $updNotes, $updHelp))

function Upd-ShowInstalled {
    $i = $script:Upd.Installed
    $updInstalled.Text = $(if ($i.Release) { "Installed: AltBotMod Release $($i.Release)  (build $($i.Build))" } else { 'Installed: unknown release (installed before Release 1.2)' })
}
Upd-ShowInstalled

function Upd-ShowLatest {
    $l = $script:Upd.Latest
    if (-not $l) { return }
    $newer = Test-BmUpdateNewer $script:Upd.Installed $l
    $when = ''; try { $when = ([datetime]$l.Published).ToString('yyyy-MM-dd') } catch { }
    $title = $(if ($l.Name) { $l.Name } else { $l.Tag })
    if ($newer) {
        $updLatest.Text = "Update available: $title  (published $when)"; $updLatest.ForeColor = $Green
        $tabUpd.Text = 'Updates (new)'; $updInstall.Enabled = $true
    } else {
        $updLatest.Text = "Up to date. Latest release: $title  (published $when)"; $updLatest.ForeColor = $Fg
        $tabUpd.Text = 'Updates'; $updInstall.Enabled = $false
    }
    $updPage.Enabled = [bool]$l.Page
    $updNotes.Text = $(if ($l.Notes) { $l.Notes } else { '(This release has no notes.)' })
}

function Upd-StartCheck([bool]$auto) {
    if ($script:Upd.Check -or $script:Upd.Download) { return }
    $script:Upd.Auto = $auto
    $updCheck.Enabled = $false; $updProgress.ForeColor = $Fg; $updProgress.Text = 'Asking GitHub for the latest release...'
    $script:Upd.Check = Start-BmUpdateCheck $script:Upd.Installed.Repo
    $updTimer.Start()
}

function Upd-Install {
    $l = $script:Upd.Latest
    if (-not $l) { return }
    $running = @(Get-BmRunningServers $dir)
    if ($running.Count) {
        $a = [Windows.Forms.MessageBox]::Show("$($running.Count) BF3 server(s) are running from this folder. The update has to replace the bot mod files they use.`r`n`r`nStop them now and update? Players on them are disconnected.",
            'AltBotMod update', 'YesNo', 'Warning')
        if ($a -ne 'Yes') { Say-Status 'Update cancelled - the servers are still running.'; return }
        foreach ($inst in $Instances) { try { [void](Stop-BmServer $inst) } catch { } }
        Start-Sleep -Milliseconds 1500
        if (@(Get-BmRunningServers $dir).Count) {
            [Windows.Forms.MessageBox]::Show('A BF3 server from this folder is still running (not started from a _StartServer*.bat?). Close it, then try again.', 'AltBotMod update', 'OK', 'Warning') | Out-Null
            return
        }
    }
    $size = $(if ($l.Size -gt 0) { ' (' + [Math]::Round($l.Size / 1MB) + ' MB)' } else { '' })
    $a = [Windows.Forms.MessageBox]::Show("Download $($l.Name)$size from GitHub and install it into`r`n$dir ?`r`n`r`nServer Control closes while the installer runs and opens again afterwards.",
        'AltBotMod update', 'OKCancel', 'Question')
    if ($a -ne 'OK') { return }
    try { $script:Upd.Download = Start-BmUpdateDownload $l }
    catch { $updProgress.ForeColor = $Red; $updProgress.Text = $_.Exception.Message; return }
    $updInstall.Enabled = $false; $updCheck.Enabled = $false; $updProgress.ForeColor = $Fg; $updProgress.Text = 'Downloading...'
    $updTimer.Start()
}

$updTimer = New-Object Windows.Forms.Timer; $updTimer.Interval = 400
$updTimer.Add_Tick({
    $u = $script:Upd
    if ($u.Check) {
        if (-not (Test-BmUpdateJobDone $u.Check)) { return }
        $job = $u.Check; $u.Check = $null; $updCheck.Enabled = $true
        if ($job.Shared.State -eq 'done') {
            try { $u.Latest = Get-BmUpdateCheckResult $job } catch { $u.Latest = $null }
            $updProgress.Text = 'Checked ' + (Get-Date -Format 'HH:mm') + '.'
            Upd-ShowLatest
            if ($u.Latest -and (Test-BmUpdateNewer $u.Installed $u.Latest)) { Say-Status "AltBotMod update available: $($u.Latest.Name) - see the Updates page." }
        } else {
            $updProgress.ForeColor = $Red; $updProgress.Text = $job.Shared.Error
            if (-not $u.Auto) { Say-Status $job.Shared.Error $true }
        }
    }
    if ($u.Download) {
        if (-not (Test-BmUpdateJobDone $u.Download)) { $updProgress.Text = Format-BmUpdateProgress $u.Download; return }
        $job = $u.Download; $u.Download = $null
        if ($job.Shared.State -ne 'done') {
            $updProgress.ForeColor = $Red; $updProgress.Text = $job.Shared.Error; $updCheck.Enabled = $true; $updInstall.Enabled = $true
            Say-Status $job.Shared.Error $true; return
        }
        $updProgress.Text = 'Downloaded. Starting the installer...'
        try {
            $label = $(if ($u.Latest.Version) { 'Release ' + $u.Latest.Version.ToString(2) } else { $u.Latest.Tag })
            [void](Start-BmUpdateInstall $job.Shared.Result $dir $PID (Join-Path $PSScriptRoot 'Server-Control.bat') $label)
        } catch {
            $updProgress.ForeColor = $Red; $updProgress.Text = 'Could not start the installer: ' + $_.Exception.Message
            $updCheck.Enabled = $true; $updInstall.Enabled = $true; return
        }
        $updTimer.Stop(); $form.Close()
        return
    }
    if (-not $u.Check -and -not $u.Download) { $updTimer.Stop() }
})

$updCheck.Add_Click({ Upd-StartCheck $false })
$updInstall.Add_Click({ Upd-Install })
$updPage.Add_Click({ if ($script:Upd.Latest -and $script:Upd.Latest.Page -match '^https://github\.com/') { Start-Process $script:Upd.Latest.Page } })
$updAutoBox.Add_CheckedChanged({ $script:Prefs.CheckUpdates = $updAutoBox.Checked; Save-Settings $null })
