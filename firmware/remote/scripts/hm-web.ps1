# hm-web.ps1 -- builds the web installer (ESP Web Tools): a page that flashes
# the remote from Chrome or Edge, no ESP-IDF needed (docs/getting-started.md).
#
# Usage (from anywhere):
#   .\scripts\hm-web.ps1             # build every board's firmware and the site
#   .\scripts\hm-web.ps1 -Board waveshare_s3_touch_lcd_7b   # one board (or a list)
#   .\scripts\hm-web.ps1 -Serve      # ... then serve it at http://localhost:8000
#   .\scripts\hm-web.ps1 -Serve -Port 8080
#
# The firmware is the release build, one per board (the boards are the
# components\board\boards\<id>.defaults files): each built in
# build-web\<id>\ from a fresh sdkconfig (sdkconfig.defaults +
# sdkconfig.release + the board's defaults, so no developer tools: no
# updates from a PC, no log over Wi-Fi, no screenshots), and with
# HM_NO_SECRETS, so nothing from secrets.env ends up in it: the remote asks
# for Wi-Fi on its first start. The site goes to build-web\site
# (tools/web/make_site.py): index.html (from web\), update.json and a
# folder per board with its four images and manifest.json. Browsers only
# allow Web Serial on https or localhost, so test it with -Serve on this
# machine.

param(
    [string[]]$Board,
    [switch]$Serve,
    [int]$Port = 8000
)

$ErrorActionPreference = "Stop"

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$site       = Join-Path $projectDir "build-web\site"

if (-not $Board) {
    $Board = Get-ChildItem (Join-Path $projectDir "components\board\boards") -Filter *.defaults |
        ForEach-Object { $_.BaseName }
}

$siteArgs = @()
foreach ($id in $Board) {
    $buildPosix = "build-web/$id"
    # A fresh sdkconfig each time, so the release options always come from
    # the defaults files and never from an earlier build.
    $sdkconfig = Join-Path $projectDir "build-web\$id\sdkconfig"
    if (Test-Path $sdkconfig) { Remove-Item -Force $sdkconfig }
    & (Join-Path $scriptDir "hm-idf.ps1") -B $buildPosix -D HM_NO_SECRETS=1 -D "HM_BOARD=$id" `
        -D "SDKCONFIG=$buildPosix/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.release" build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Firmware build for $id failed (exit $LASTEXITCODE)."
    }
    $siteArgs += @("--build", $buildPosix)
}

# The site: index.html, ESP Web Tools, update.json and the boards' folders.
# The same script assembles it in the GitHub workflow.
& python (Join-Path $projectDir "tools\web\make_site.py") @siteArgs --out build-web/site
if ($LASTEXITCODE -ne 0) {
    Write-Error "Assembling the site failed (exit $LASTEXITCODE)."
}

if ($Serve) {
    Write-Host "Serving at http://localhost:$Port  (Ctrl+C to stop)"
    & python -m http.server $Port --bind 127.0.0.1 --directory $site
}
