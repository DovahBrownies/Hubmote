# Builds build\companion.apk with the Android SDK tools directly (no Gradle).
# Runs on Windows (PowerShell 5.1 or 7) and, in the GitHub workflow, on
# Linux (pwsh).
#
#   .\build.ps1                     # the version in version.txt
#   .\build.ps1 -VersionName 0.5    # another version (versionCode 5)
#
# Writes build\companion.apk and build\companion.json (the version, the
# version code, and "key": "release" if HM_KEYSTORE signed it, else "debug").
#
# Tools: on Windows, JDK 17 and the Android SDK under %LOCALAPPDATA%\Android
# (setup-tools.ps1); elsewhere, $env:JAVA_HOME and $env:ANDROID_HOME.
#
# Signing: $env:HM_KEYSTORE (a keystore file) with $env:HM_KEYSTORE_PASS
# (store and key password) and $env:HM_KEY_ALIAS (default "companion"), as
# the workflow does for releases. Without them, debug.keystore next to this
# script, made on the first build. Keep it: with a new key, Android refuses
# the update until the old app is uninstalled.
param(
    [string]$VersionName = "",
    [int]$VersionCode = 0
)
$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot
$OnWindows = $env:OS -eq 'Windows_NT'

if (-not $VersionName) {
    $VersionName = (Get-Content (Join-Path $Here 'version.txt') -TotalCount 1).Trim()
}
if ($VersionCode -le 0) {
    # MAJOR.MINOR -> MAJOR * 100 + MINOR: 0.4 is 4, 1.2 is 102.
    $p = $VersionName.Split('.')
    $VersionCode = [int]$p[0] * 100 + [int]$p[1]
}

if ($OnWindows) {
    $Root = Join-Path $env:LOCALAPPDATA 'Android'
    $Jdk = Join-Path $Root 'jdk-17'
    $Sdk = Join-Path $Root 'Sdk'
    $Exe = '.exe'
    $Bat = '.bat'
} else {
    $Jdk = $env:JAVA_HOME
    $Sdk = $env:ANDROID_HOME
    $Exe = ''
    $Bat = ''
}
if (-not $Jdk -or -not $Sdk) { throw 'JDK or Android SDK not found (set JAVA_HOME and ANDROID_HOME)' }
$Bt = Join-Path $Sdk (Join-Path 'build-tools' '35.0.0')
$Jar = Join-Path $Sdk (Join-Path 'platforms' (Join-Path 'android-35' 'android.jar'))
$JdkBin = Join-Path $Jdk 'bin'
$Out = Join-Path $Here 'build'
$env:JAVA_HOME = $Jdk

function Tool([string]$dir, [string]$name) { return Join-Path $dir $name }
function Out([string]$name) { return Join-Path $Out $name }

function Invoke-Tool($exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path $exe -Leaf) failed with exit code $LASTEXITCODE" }
}

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force (Out 'classes'), (Out 'dex') | Out-Null

# Resources and manifest
Invoke-Tool (Tool $Bt "aapt2$Exe") @('compile', '--dir', (Join-Path $Here 'res'), '-o', (Out 'res.zip'))
Invoke-Tool (Tool $Bt "aapt2$Exe") @('link', '-o', (Out 'base.apk'), '-I', $Jar,
    '--manifest', (Join-Path $Here 'AndroidManifest.xml'),
    '--min-sdk-version', '26', '--target-sdk-version', '34',
    '--version-code', "$VersionCode", '--version-name', $VersionName, (Out 'res.zip'))

# Code
$src = @(Get-ChildItem (Join-Path $Here 'src') -Recurse -Filter *.java | ForEach-Object { $_.FullName })
Invoke-Tool (Tool $JdkBin "javac$Exe") (@('--release', '8', '-encoding', 'UTF-8', '-Xlint:all', '-Xlint:-options', '-classpath', $Jar,
    '-d', (Out 'classes')) + $src)
$cls = @(Get-ChildItem (Out 'classes') -Recurse -Filter *.class | ForEach-Object { $_.FullName })
Invoke-Tool (Tool $Bt "d8$Bat") (@('--release', '--min-api', '26', '--lib', $Jar, '--output', (Out 'dex')) + $cls)

# Package, align, sign
Copy-Item (Out 'base.apk') (Out 'unsigned.apk')
Invoke-Tool (Tool $JdkBin "jar$Exe") @('uf0M', (Out 'unsigned.apk'), '-C', (Out 'dex'), 'classes.dex')
Invoke-Tool (Tool $Bt "zipalign$Exe") @('-f', '-p', '4', (Out 'unsigned.apk'), (Out 'aligned.apk'))

$ks = $env:HM_KEYSTORE
$pass = $env:HM_KEYSTORE_PASS
$alias = if ($env:HM_KEY_ALIAS) { $env:HM_KEY_ALIAS } else { 'companion' }
if (-not $ks) {
    $ks = Join-Path $Here 'debug.keystore'
    $pass = 'android'
    $alias = 'companion'
    if (-not (Test-Path $ks)) {
        Invoke-Tool (Tool $JdkBin "keytool$Exe") @('-genkeypair', '-keystore', $ks, '-storepass', $pass,
            '-keypass', $pass, '-alias', $alias, '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
            '-dname', 'CN=Hubmote Companion')
    }
}
# The password goes through the environment, not the command line, where
# other processes could read it.
$env:HM_APKSIGNER_PASS = $pass
try {
    Invoke-Tool (Tool $Bt "apksigner$Bat") @('sign', '--ks', $ks, '--ks-key-alias', $alias,
        '--ks-pass', 'env:HM_APKSIGNER_PASS', '--key-pass', 'env:HM_APKSIGNER_PASS',
        '--out', (Out 'companion.apk'), (Out 'aligned.apk'))
} finally {
    Remove-Item Env:HM_APKSIGNER_PASS -ErrorAction SilentlyContinue
}
# For the release site (firmware/remote/tools/web/make_site.py): the version,
# and whether a key given in HM_KEYSTORE signed it (remotes only offer
# updates signed with the project's key).
$key = if ($env:HM_KEYSTORE) { 'release' } else { 'debug' }
'{"version":"' + $VersionName + '","code":' + $VersionCode + ',"key":"' + $key + '"}' |
    Set-Content -Encoding ascii (Out 'companion.json')
Write-Host "built $(Out 'companion.apk') (version $VersionName, code $VersionCode)"
