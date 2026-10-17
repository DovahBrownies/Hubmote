# hm-idf.ps1 -- ESP-IDF wrapper for the Hubmote firmware (firmware/remote).
#
# What it does: activate ESP-IDF v5.4.4 for THIS process only, run idf.py
# from this project's root, keep the build dir local. Several ESP32 projects share this machine; never call idf.py
# directly here (a build without the activated PATH writes a missing
# compiler into build/CMakeCache.txt and poisons every later build).
#
# Usage (from anywhere):
#   .\scripts\hm-idf.ps1 build
#   .\scripts\hm-idf.ps1 menuconfig            # Hubmote options (Wi-Fi is set on the remote)
#   .\scripts\hm-idf.ps1 -p COM10 flash
#   .\scripts\hm-idf.ps1 -p COM10 flash monitor
#   .\scripts\hm-idf.ps1 fullclean
#   .\scripts\hm-idf.ps1 -B build-4_3 -D HM_BOARD=waveshare_s3_touch_lcd_4_3 `
#       -D SDKCONFIG=build-4_3/sdkconfig build   # another board (docs/porting.md)
#
# All arguments are passed through to idf.py unchanged.
#
# Do not pipe this script through 2>&1 or redirect it with *> in
# PowerShell 5.1: CMake warnings on stderr become terminating errors and
# the build silently never runs. To capture a log use
#   Start-Process powershell.exe -ArgumentList '-File','.\scripts\hm-idf.ps1','build'
#     -RedirectStandardOutput out.log -RedirectStandardError err.log -Wait -NoNewWindow
#
# IDF location is resolved, not hardcoded: HM_IDF_PATH override first,
# then the current profile's esp\v5.4.4\esp-idf.

$ErrorActionPreference = "Stop"

$IDF_CANDIDATES = @(
    $env:HM_IDF_PATH,
    (Join-Path $env:USERPROFILE "esp\v5.4.4\esp-idf"),
    "C:\Espressif\frameworks\esp-idf-v5.4.4"
) | Where-Object { $_ -and (Test-Path (Join-Path $_ "tools\idf.py")) }

if (-not $IDF_CANDIDATES) {
    Write-Error ("ESP-IDF v5.4.4 not found. Looked at HM_IDF_PATH, " +
                 "$env:USERPROFILE\esp\v5.4.4\esp-idf and C:\Espressif\frameworks\esp-idf-v5.4.4. " +
                 "Install it there or set HM_IDF_PATH.")
}

$IDF_PATH = @($IDF_CANDIDATES)[0]
# The ESP-IDF tools (HM_IDF_TOOLS_PATH, else the installer's C:\Espressif).
$IDF_TOOLS_PATH = "C:\Espressif"
if ($env:HM_IDF_TOOLS_PATH) { $IDF_TOOLS_PATH = $env:HM_IDF_TOOLS_PATH }
# Pinned: export.ps1 would pick the env matching `python` on PATH, which
# may be a version whose env is incomplete. HM_IDF_PYTHON_ENV overrides;
# otherwise the py3.12 env, else the first idf5.4 env there.
$IDF_PYTHON_ENV_PATH = $env:HM_IDF_PYTHON_ENV
if (-not $IDF_PYTHON_ENV_PATH) {
    $IDF_PYTHON_ENV_PATH = Join-Path $IDF_TOOLS_PATH "python_env\idf5.4_py3.12_env"
    if (-not (Test-Path $IDF_PYTHON_ENV_PATH)) {
        $found = Get-ChildItem (Join-Path $IDF_TOOLS_PATH "python_env") -Directory -Filter "idf5.4_py*_env" `
            -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { $IDF_PYTHON_ENV_PATH = $found.FullName }
    }
}

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
Push-Location $projectDir

try {
    $env:IDF_PATH            = $IDF_PATH
    $env:IDF_TOOLS_PATH      = $IDF_TOOLS_PATH
    $env:IDF_PYTHON_ENV_PATH = $IDF_PYTHON_ENV_PATH
    $env:PYTHONIOENCODING    = "utf-8"

    $venvPython = Join-Path $IDF_PYTHON_ENV_PATH "Scripts\python.exe"
    $idfPy      = Join-Path $IDF_PATH "tools\idf.py"
    $activate   = Join-Path $IDF_PATH "tools\activate.py"

    if (-not (Test-Path $venvPython)) {
        Write-Error "IDF venv python not found at $venvPython. Edit `$IDF_PYTHON_ENV_PATH in hm-idf.ps1."
    }

    # Apply EVERY line activate.py emits (PATH prepends, ESP_IDF_VERSION,
    # and the generated activation script), not just the last one.
    # activate.py writes progress to stderr with exit code 0, which would
    # trip ErrorActionPreference=Stop, so relax it for this call only.
    $prevErrAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $exports = & $venvPython $activate --export 2>$null
    $ErrorActionPreference = $prevErrAction
    foreach ($line in $exports) {
        if ($line -and $line.Trim()) {
            try { Invoke-Expression $line } catch { }
        }
    }

    & $venvPython $idfPy @args
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
