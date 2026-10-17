# Sends a firmware build to the remote over Wi-Fi (no USB). Build first with
# .\scripts\hm-idf.ps1 build. The remote must already run firmware with
# updates (ota_0/ota_1 partitions, main/ota.c); the first such firmware goes
# on over USB. The upload is signed with OTA_TOKEN from secrets.env: the
# token itself is never sent or printed (main/ota.h, ota_authorized()).
#
#   .\scripts\hm-ota.ps1                         # to hubmote.local
#   .\scripts\hm-ota.ps1 -Remote 192.168.1.50
#
# A new build that doesn't reach Wi-Fi is rolled back on its next restart.
param(
    [string]$Remote = "hubmote.local",
    [int]$Port = 8080,
    [string]$Image = ""
)
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
if (-not $Image) { $Image = Join-Path $Project "build\hubmote.bin" }
if (-not (Test-Path $Image)) { throw "no image at $Image; build first" }

$line = Get-Content (Join-Path $Project "secrets.env") | Where-Object { $_ -match '^\s*OTA_TOKEN\s*=' } | Select-Object -First 1
$token = if ($line) { ($line -replace '^\s*OTA_TOKEN\s*=\s*', '').Trim().Trim('"') } else { "" }
if (-not $token) { throw "OTA_TOKEN missing from secrets.env" }

# Look the name up once: after the remote restarts, Windows may not resolve
# hubmote.local again for minutes, while the address stays the same.
$addr = $Remote
try {
    $ip = [System.Net.Dns]::GetHostAddresses($Remote) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } |
        Select-Object -First 1
    if ($ip) { $addr = $ip.IPAddressToString }
} catch {
}
$base = "http://${addr}:$Port"
$byName = "http://${Remote}:$Port"
try {
    $before = Invoke-RestMethod -Uri "$base/version" -TimeoutSec 10
    Write-Host "running: $($before.built) in $($before.slot)"
} catch {
    throw "the remote doesn't answer at $base ($($_.Exception.Message))"
}

function ConvertTo-Hex([byte[]]$bytes) { ([System.BitConverter]::ToString($bytes) -replace '-', '').ToLower() }

$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $Image))
Write-Host "sending $Image ($($bytes.Length) bytes)..."
# Sign: HMAC-SHA256(token, nonce LF method LF path LF body hash), with a
# one-time nonce from the remote.
$nonce = (Invoke-WebRequest -Uri "$base/nonce" -TimeoutSec 10 -UseBasicParsing).Content.Trim()
$bodyHash = ConvertTo-Hex ([System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes))
$hmac = [System.Security.Cryptography.HMACSHA256]::new([System.Text.Encoding]::UTF8.GetBytes($token))
$auth = ConvertTo-Hex ($hmac.ComputeHash([System.Text.Encoding]::UTF8.GetBytes("$nonce`nPOST`n/ota`n$bodyHash")))
$headers = @{ "X-OTA-Nonce" = $nonce; "X-OTA-Auth" = $auth; "X-OTA-Body" = $bodyHash }
$r = Invoke-WebRequest -Uri "$base/ota" -Method Post -Body $bytes -ContentType "application/octet-stream" `
    -Headers $headers -TimeoutSec 180 -UseBasicParsing
Write-Host "remote: $($r.Content.Trim())"

# Wait for it to come back on the new build: by address first, by name in
# case the address changed.
$deadline = (Get-Date).AddSeconds(90)
Start-Sleep -Seconds 5
while ((Get-Date) -lt $deadline) {
    foreach ($u in @($base, $byName)) {
        try {
            $after = Invoke-RestMethod -Uri "$u/version" -TimeoutSec 5
            if ($after.slot -ne $before.slot) {
                Write-Host "updated: $($after.built) in $($after.slot)"
                exit 0
            }
        } catch {
        }
    }
    Start-Sleep -Seconds 3
}
throw "the remote didn't come back on the new build within 90 s"
