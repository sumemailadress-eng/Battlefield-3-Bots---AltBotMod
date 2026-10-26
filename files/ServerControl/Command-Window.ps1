# Chromium app-window support. Uses a separate browser profile from normal browsing.
function Find-CommandChromium {
    foreach ($p in @((Join-Path $PSScriptRoot 'Chromium\chrome.exe'),
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
        "$env:LOCALAPPDATA\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe", "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe")) {
        if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { return $p }
    }
    foreach ($name in @('msedge.exe','chrome.exe','chromium.exe')) { $c=Get-Command $name -ErrorAction SilentlyContinue; if ($c) { return $c.Source } }
    return $null
}
function New-CommandWindowPlan([string]$exe,[string]$url,[string]$profile) {
    if (-not $exe) { throw 'Install Microsoft Edge or Google Chrome to use the desktop window, or use Command-Browser.bat.' }
    if ($url -notmatch '^http://127\.0\.0\.1:[0-9]+/#key=[0-9a-f]{64}$') { throw 'Invalid desktop launcher URL.' }
    if ($profile -match '"') { throw 'Invalid browser profile folder.' }
    return @{ FilePath=$exe; ArgumentList=@(('--app="'+$url+'"'),('--user-data-dir="'+$profile+'"'),'--no-first-run','--no-default-browser-check','--disable-background-mode','--window-size=1440,960'); WorkingDirectory=(Split-Path -Parent $exe) }
}
function Open-CommandWindow([string]$url) {
    $exe=Find-CommandChromium
    $profile=Join-Path $env:LOCALAPPDATA (Join-Path 'AltBotMod\Command' ('Chromium-'+$Port))
    [void](New-Item -ItemType Directory -Force -Path $profile)
    $plan=New-CommandWindowPlan $exe $url $profile
    Start-Process @plan | Out-Null
}
function New-CommandTray([string]$url) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $script:CommandWindowUrl=$url
    $script:CommandQuit=$false
    $tray=New-Object Windows.Forms.NotifyIcon
    $tray.Text='AltBotMod Command - double-click to open'
    $tray.Icon=[Drawing.SystemIcons]::Application
    $iconPath=Join-Path $PSScriptRoot 'Command-Web\command.ico'
    if (Test-Path -LiteralPath $iconPath) { try { $tray.Icon=New-Object Drawing.Icon($iconPath) } catch { } }
    $menu=New-Object Windows.Forms.ContextMenuStrip
    $open=$menu.Items.Add('Open Command')
    $open.Add_Click({ try { Open-CommandWindow $script:CommandWindowUrl } catch { [void][Windows.Forms.MessageBox]::Show($_.Exception.Message,'AltBotMod Command') } })
    [void]$menu.Items.Add('-')
    $quit=$menu.Items.Add('Exit launcher (keep game servers running)')
    $quit.Add_Click({ $script:CommandQuit=$true })
    $tray.ContextMenuStrip=$menu
    $tray.Add_DoubleClick({ try { Open-CommandWindow $script:CommandWindowUrl } catch { [void][Windows.Forms.MessageBox]::Show($_.Exception.Message,'AltBotMod Command') } })
    $tray.Visible=$true
    return $tray
}
