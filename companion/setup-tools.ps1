# Installs JDK 17 and the Android SDK pieces build.ps1 needs, under
# %LOCALAPPDATA%\Android (about 750 MB). No PATH or system changes; safe to
# re-run. Downloads are checked against their published SHA-256; bump the
# URLs and hashes together when updating. Accepts the Android SDK licences.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Join-Path $env:LOCALAPPDATA 'Android'
$Sdk = Join-Path $Root 'Sdk'
$Jdk = Join-Path $Root 'jdk-17'
$Dl = Join-Path $Root 'downloads'
New-Item -ItemType Directory -Force $Dl | Out-Null

function Get-Checked($url, $file, $sha256) {
    $path = Join-Path $Dl $file
    if (-not (Test-Path $path) -or (Get-FileHash $path -Algorithm SHA256).Hash -ne $sha256.ToUpper()) {
        Write-Host "downloading $file"
        & "$env:SystemRoot\System32\curl.exe" -sS -L -o $path $url
        if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
    }
    $h = (Get-FileHash $path -Algorithm SHA256).Hash
    if ($h -ne $sha256.ToUpper()) { throw "checksum mismatch for $file ($h)" }
    Write-Host "verified $file"
    return $path
}

# JDK 17 (Microsoft Build of OpenJDK)
if (-not (Test-Path (Join-Path $Jdk 'bin\java.exe'))) {
    $zip = Get-Checked 'https://aka.ms/download-jdk/microsoft-jdk-17.0.20.1-windows-x64.zip' 'microsoft-jdk-17.0.20.1-windows-x64.zip' '3d9006956fc8af5601cd24ffc4f468bef48279c7ebd8171b9bdf90d0aabfbf1f'
    $tmp = Join-Path $Root 'jdk-unpack'
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null
    & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $tmp
    $inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
    Move-Item $inner.FullName $Jdk
    Remove-Item -Recurse -Force $tmp
}
& (Join-Path $Jdk 'bin\java.exe') -version

# Android command-line tools
$cmdline = Join-Path $Sdk 'cmdline-tools\latest'
if (-not (Test-Path (Join-Path $cmdline 'bin\sdkmanager.bat'))) {
    $zip = Get-Checked 'https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip' 'commandlinetools-win-15859902_latest.zip' '90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a'
    $tmp = Join-Path $Sdk 'cmdline-unpack'
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null
    & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $tmp
    New-Item -ItemType Directory -Force (Join-Path $Sdk 'cmdline-tools') | Out-Null
    Move-Item (Join-Path $tmp 'cmdline-tools') $cmdline
    Remove-Item -Recurse -Force $tmp
}

# SDK packages
$env:JAVA_HOME = $Jdk
$yes = Join-Path $Dl 'yes.txt'
[System.IO.File]::WriteAllText($yes, ("y`r`n" * 30))
$sm = Join-Path $cmdline 'bin\sdkmanager.bat'
$p = Start-Process $sm -ArgumentList "--sdk_root=`"$Sdk`"", '--licenses' -RedirectStandardInput $yes -RedirectStandardOutput (Join-Path $Dl 'licenses.log') -RedirectStandardError (Join-Path $Dl 'licenses.err.log') -NoNewWindow -PassThru -Wait
"licenses exit $($p.ExitCode)"
$p = Start-Process $sm -ArgumentList "--sdk_root=`"$Sdk`"", 'platform-tools', 'platforms;android-35', 'build-tools;35.0.0' -RedirectStandardInput $yes -RedirectStandardOutput (Join-Path $Dl 'packages.log') -RedirectStandardError (Join-Path $Dl 'packages.err.log') -NoNewWindow -PassThru -Wait
"packages exit $($p.ExitCode)"
foreach ($f in 'platform-tools\adb.exe', 'platforms\android-35\android.jar', 'build-tools\35.0.0\aapt2.exe', 'build-tools\35.0.0\d8.bat', 'build-tools\35.0.0\apksigner.bat', 'build-tools\35.0.0\zipalign.exe') {
    "{0}: {1}" -f $f, (Test-Path (Join-Path $Sdk $f))
}
Remove-Item -Recurse -Force $Dl
