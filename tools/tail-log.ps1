# Live-tails the GordianKnot SKSE log into the current console.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/tail-log.ps1
#
# Wire it into CLion as a Shell Script run configuration (see README) for a
# one-click "log console".
#
# NOTE: CommonLibSSE-NG resolves its log folder from the running game, and under
# some (MO2) setups that is NOT "My Games\Skyrim Special Edition" but e.g.
# "My Games\Skyrim.INI". So rather than hardcode a path, this searches under
# Documents\My Games for the most recent GordianKnot.log. The log is also
# recreated each launch, so this waits for it to (re)appear and then follows it.

$ErrorActionPreference = 'Stop'

$myGames = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games'

function Find-Log {
    Get-ChildItem -Path $myGames -Recurse -Filter 'GordianKnot.log' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

$log = Find-Log
while (-not $log) {
    Write-Host 'Waiting for GordianKnot.log under My Games (launch the game via MO2)...' -ForegroundColor DarkGray
    Start-Sleep -Seconds 2
    $log = Find-Log
}

Write-Host "Tailing: $log" -ForegroundColor Cyan
Get-Content -Path $log -Wait -Tail 200
