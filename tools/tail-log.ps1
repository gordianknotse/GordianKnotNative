# Live-tails the GordianKnot SKSE log into the current console.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/tail-log.ps1
#
# Wire it into CLion as a Shell Script run configuration (see README) for a
# one-click "log console". The log is recreated each game launch, so this waits
# for the file to (re)appear and then follows it.

$ErrorActionPreference = 'Stop'

$log = Join-Path ([Environment]::GetFolderPath('MyDocuments')) `
    'My Games\Skyrim Special Edition\SKSE\GordianKnot.log'

Write-Host "Tailing: $log" -ForegroundColor Cyan

while (-not (Test-Path $log)) {
    Write-Host 'Waiting for log to appear (launch the game via MO2)...' -ForegroundColor DarkGray
    Start-Sleep -Seconds 2
}

Get-Content -Path $log -Wait -Tail 200
