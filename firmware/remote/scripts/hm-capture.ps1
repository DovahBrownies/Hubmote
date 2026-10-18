# Records the board's log to a file without opening idf.py (the build is not
# touched). By default the board is NOT reset; -Reset restarts it first so the
# boot is captured. The file is written as data arrives, so it can be read
# while the capture runs.
#
#   .\scripts\hm-capture.ps1 -Port COM5 -Seconds 120 -Out run.log
#   .\scripts\hm-capture.ps1 -Port COM5 -Seconds 60 -Out boot.log -Reset
#
# Logs come out of the native USB port (USB-Serial/JTAG). After a reset the
# port disappears for a moment; the script waits for it to come back.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Seconds = 60,
    [string]$Out = "hm-capture.log",
    [switch]$Reset
)
$ErrorActionPreference = 'Stop'

function Open-Port {
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $deadline) {
        if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) {
            try {
                $sp = New-Object System.IO.Ports.SerialPort $Port, 115200
                $sp.DtrEnable = $false
                $sp.RtsEnable = $false
                $sp.ReadTimeout = 200
                $sp.Open()
                return $sp
            } catch {
                Start-Sleep -Milliseconds 100
            }
        } else {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "port $Port not available"
}

if ($Reset) {
    # EN low for 150 ms through RTS, with IO0 left high: a normal boot.
    $sp = Open-Port
    $sp.RtsEnable = $true
    Start-Sleep -Milliseconds 150
    $sp.RtsEnable = $false
    try { $sp.Close() } catch {}
    Start-Sleep -Milliseconds 300
}

# .NET resolves relative paths against its own current folder, not
# PowerShell's: make -Out absolute first.
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
[System.IO.File]::WriteAllText($Out, "")
$sp = Open-Port
$end = (Get-Date).AddSeconds($Seconds)
try {
    while ((Get-Date) -lt $end) {
        $n = $sp.BytesToRead
        if ($n -gt 0) {
            $buf = New-Object byte[] $n
            $read = $sp.Read($buf, 0, $n)
            [System.IO.File]::AppendAllText($Out, [System.Text.Encoding]::UTF8.GetString($buf, 0, $read))
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
} catch {
    [System.IO.File]::AppendAllText($Out, "`n[capture error: $($_.Exception.Message)]")
} finally {
    $sp.Close()
    $sp.Dispose()
}
Write-Host "captured to $Out"
