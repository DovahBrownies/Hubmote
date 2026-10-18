# Records the remote's log over Wi-Fi (no USB needed). The firmware serves it
# on TCP port 2323 (CONFIG_HM_NETLOG); a new connection first gets the last
# 32 KB, so recent start-up lines are included. TCP keepalive probes notice
# a dead connection (the remote restarted, went to sleep or out of range),
# and the script connects again by itself. The PC connects out, so no
# firewall rule is needed. tools/hm_netlog.py does the same on any OS.
# The remote sends the log only to a client with the update token (OTA_TOKEN
# in secrets.env): it opens with a challenge, answered with an HMAC of it
# (main/netlog.c), so the token itself never crosses the network.
#
#   .\scripts\hm-netlog.ps1 -Remote hubmote.local                 # live, until Ctrl+C
#   .\scripts\hm-netlog.ps1 -Remote 192.168.1.50 -Seconds 600 -Out test.log
#
# The remote's address is in its log ("net: up: ...") or the router's list.
param(
    [Parameter(Mandatory = $true)][string]$Remote,
    [int]$Port = 2323,
    [int]$Seconds = 0,          # 0 = until stopped
    [string]$Out = "hm-netlog.log",
    [switch]$Quiet              # file only, no console echo
)
$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
$line = Get-Content (Join-Path $Project "secrets.env") | Where-Object { $_ -match '^\s*OTA_TOKEN\s*=' } | Select-Object -First 1
$token = if ($line) { ($line -replace '^\s*OTA_TOKEN\s*=\s*', '').Trim().Trim('"') } else { "" }
if (-not $token) { throw "OTA_TOKEN missing from secrets.env" }
$hmac = [System.Security.Cryptography.HMACSHA256]::new([System.Text.Encoding]::UTF8.GetBytes($token))
$refused = $false
$end = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [DateTime]::MaxValue }
# .NET resolves relative paths against its own current folder, not
# PowerShell's: make -Out absolute first.
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
[System.IO.File]::WriteAllText($Out, "")
$buf = New-Object byte[] 4096

while ((Get-Date) -lt $end) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $connect = $client.BeginConnect($Remote, $Port, $null, $null)
        if (-not $connect.AsyncWaitHandle.WaitOne(3000) -or -not $client.Connected) {
            throw "no answer"
        }
        $client.EndConnect($connect)
        # Keepalive: probes after 5 s of silence, every 2 s. A restarted
        # remote answers a probe for the old connection with a reset at once;
        # one that's gone doesn't answer, and the connection fails.
        $ka = New-Object byte[] 12
        [BitConverter]::GetBytes([uint32]1).CopyTo($ka, 0)
        [BitConverter]::GetBytes([uint32]5000).CopyTo($ka, 4)
        [BitConverter]::GetBytes([uint32]2000).CopyTo($ka, 8)
        [void]$client.Client.IOControl([System.Net.Sockets.IOControlCode]::KeepAliveValues, $ka, $null)
        $stream = $client.GetStream()
        $stream.ReadTimeout = 1000
        # The challenge: answer with HMAC-SHA256(token, "hubmote-log" LF nonce).
        # Firmware from before it sends the log at once.
        $hello = ""
        $helloEnd = (Get-Date).AddSeconds(5)
        while (-not $hello.Contains("`n") -and (Get-Date) -lt $helloEnd) {
            try { $n = $stream.Read($buf, 0, $buf.Length) } catch [System.IO.IOException] { continue }
            if ($n -le 0) { break }
            $hello += [System.Text.Encoding]::UTF8.GetString($buf, 0, $n)
        }
        $text = $hello
        if ($hello -match '^hubmote-log ([0-9a-f]+)\n') {
            $mac = ([System.BitConverter]::ToString($hmac.ComputeHash([System.Text.Encoding]::UTF8.GetBytes("hubmote-log`n" + $Matches[1]))) -replace '-', '').ToLower()
            $answer = [System.Text.Encoding]::ASCII.GetBytes("$mac`n")
            $stream.Write($answer, 0, $answer.Length)
            $text = $hello.Substring($Matches[0].Length)
        }
        [System.IO.File]::AppendAllText($Out, "`n[connected to ${Remote}:$Port at $(Get-Date -Format s)]`n")
        $first = $true
        while ((Get-Date) -lt $end) {
            if (-not $text) {
                try {
                    $n = $stream.Read($buf, 0, $buf.Length)
                } catch [System.IO.IOException] {
                    if ($client.Connected) { continue } else { break }   # read timeout: keep waiting
                }
                if ($n -le 0) { break }
                $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $n)
            }
            if ($first -and $text.StartsWith("refused`n")) { $refused = $true; break }
            $first = $false
            # Control characters out (all but tab and line ends): anything
            # posing as the remote could otherwise send terminal escapes.
            $text = $text -replace '[\x00-\x08\x0B\x0C\x0E-\x1F\x7F-\x9F]', ''
            [System.IO.File]::AppendAllText($Out, $text)
            if (-not $Quiet) { Write-Host -NoNewline $text }
            $text = ""
        }
        [System.IO.File]::AppendAllText($Out, "`n[disconnected at $(Get-Date -Format s)]`n")
    } catch {
        Start-Sleep -Seconds 3   # remote asleep, restarting or out of range: try again
    } finally {
        $client.Close()
    }
    if ($refused) {
        Write-Host "The remote refused the token: OTA_TOKEN in secrets.env must be the one in Settings > About."
        exit 1
    }
}
