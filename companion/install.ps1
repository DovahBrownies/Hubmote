# Installs the companion APK (build\companion.apk, or -Apk) on the Streamer
# over ADB, turns on its accessibility service and lets it update itself.
# The Streamer must have Wireless debugging on (docs/companion.md).
param(
    [Parameter(Mandatory = $true)][string]$Device,  # host:port shown under Wireless debugging
    [string]$Apk = (Join-Path $PSScriptRoot 'build\companion.apk')
)
$ErrorActionPreference = 'Stop'
$Adb = Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe'
if (-not (Test-Path $Apk)) { throw "no APK at $Apk; build it (build.ps1) or pass -Apk" }
$Svc = 'io.github.dovahbrownies.hubmote/.CompanionService'

function Invoke-Adb([string[]]$argv) {
    $out = & $Adb @argv
    if ($LASTEXITCODE -ne 0) { throw "adb $($argv -join ' ') failed: $out" }
    return $out
}

Invoke-Adb @('connect', $Device) | Write-Host

Invoke-Adb @('-s', $Device, 'install', '-r', $Apk) | Write-Host

# Add the service to the enabled list without dropping others (e.g. TalkBack).
$cur = "$(Invoke-Adb @('-s', $Device, 'shell', 'settings', 'get', 'secure', 'enabled_accessibility_services'))".Trim()
$cur = (($cur -split ':') | Where-Object { $_ -and $_ -ne 'null' }) -join ':'
if ($cur -eq '' -or $cur -eq 'null') {
    $new = $Svc
} elseif (($cur -split ':') -contains $Svc) {
    $new = $cur
} else {
    $new = $cur + ':' + $Svc
}
Invoke-Adb @('-s', $Device, 'shell', 'settings', 'put', 'secure', 'enabled_accessibility_services', $new) | Out-Null
Invoke-Adb @('-s', $Device, 'shell', 'settings', 'put', 'secure', 'accessibility_enabled', '1') | Out-Null

# "Install unknown apps" for the companion: where the TV lets apps install
# at all, Android then lets the companion update itself without asking on
# the TV (Settings > Remotes > Update companion on the remote).
Invoke-Adb @('-s', $Device, 'shell', 'appops', 'set', 'io.github.dovahbrownies.hubmote', 'REQUEST_INSTALL_PACKAGES', 'allow') | Out-Null

# Notification access for its media listener, so it can tell the remote what
# is playing (0.6+). It reads no notifications.
Invoke-Adb @('-s', $Device, 'shell', 'cmd', 'notification', 'allow_listener', 'io.github.dovahbrownies.hubmote/.MediaListener') | Out-Null
Write-Host "installed; accessibility services now: $new"
