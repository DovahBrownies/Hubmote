# Records the remote's log over Wi-Fi (no USB needed). The firmware serves it
# on TCP port 2323 (CONFIG_HM_NETLOG); a new connection first gets the last
# 32 KB, so recent start-up lines are included. TCP keepalive probes notice
# a dead connection (the remote restarted, went to sleep or out of range),
# and the script connects again by itself. The PC connects out, so no
# firewall rule is needed. tools/hm_netlog.py does the same on any OS.
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
        [System.IO.File]::AppendAllText($Out, "`n[connected to ${Remote}:$Port at $(Get-Date -Format s)]`n")
        while ((Get-Date) -lt $end) {
            try {
                $n = $stream.Read($buf, 0, $buf.Length)
            } catch [System.IO.IOException] {
                if ($client.Connected) { continue } else { break }   # read timeout: keep waiting
            }
            if ($n -le 0) { break }
            # Control characters out (all but tab and line ends): anything
            # posing as the remote could otherwise send terminal escapes.
            $text = [System.Text.Encoding]::UTF8.GetString($buf, 0, $n) -replace '[\x00-\x08\x0B\x0C\x0E-\x1F\x7F-\x9F]', ''
            [System.IO.File]::AppendAllText($Out, $text)
            if (-not $Quiet) { Write-Host -NoNewline $text }
        }
        [System.IO.File]::AppendAllText($Out, "`n[disconnected at $(Get-Date -Format s)]`n")
    } catch {
        Start-Sleep -Seconds 3   # remote asleep, restarting or out of range: try again
    } finally {
        $client.Close()
    }
}
