# hm-monitor.ps1 -- DTR/RTS-safe serial monitor.
#
# Windows toggles DTR/RTS when a serial port opens or closes, which
# hard-resets boards whose USB-UART bridge drives EN/BOOT (the 7B's CH343
# port does). That would abort a running power-test sequence. This
# monitor opens the port with DTR/RTS off so the firmware keeps running.
#
# Usage:
#   .\scripts\hm-monitor.ps1 COM10
#   .\scripts\hm-monitor.ps1 COM10 | Tee-Object -FilePath power.log

param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Port,

    [int]$Baud = 115200
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud,
    ([System.IO.Ports.Parity]::None), 8,
    ([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 200
$sp.DtrEnable   = $false
$sp.RtsEnable   = $false

try {
    $sp.Open()
} catch {
    Write-Error "Failed to open ${Port}: $($_.Exception.Message)"
    Write-Host "If the port is busy, close any other serial monitor (VS Code, PlatformIO, PuTTY)."
    Write-Host "Ports currently enumerated:"
    ([System.IO.Ports.SerialPort]::GetPortNames()) -join ", " | Write-Host
    exit 1
}

Write-Host "=== Monitoring $Port @ $Baud baud (DTR/RTS off, no reset) -- Ctrl+C to exit ==="
Write-Host ""

try {
    while ($true) {
        while ($sp.BytesToRead -gt 0) {
            Write-Host -NoNewline ([char]$sp.ReadByte())
        }
        Start-Sleep -Milliseconds 20
    }
}
finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
    Write-Host ""
    Write-Host "=== Monitor closed ==="
}
