# Remote dryer debugging over Wi-Fi: status polling, telemetry capture,
# log downloads and OTA flashing helpers. No USB cable needed.
#
# Usage:
#   powershell -File tools\remote.ps1 status              one snapshot
#   powershell -File tools\remote.ps1 watch [seconds]     live telemetry to console + file
#   powershell -File tools\remote.ps1 history out.json    download /api/history
#   powershell -File tools\remote.ps1 events out.json     download /api/events
#   powershell -File tools\remote.ps1 ota                 flash firmware.bin over Wi-Fi
#
# Override the target with -HostName 192.168.1.238 when mDNS is blocked.
param(
  [Parameter(Mandatory = $true, Position = 0)][string]$Command,
  [Parameter(Position = 1)][string]$Argument,
  [string]$HostName = "dryer.local",
  [int]$IntervalSeconds = 5
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-Json([string]$Path) {
  $response = Invoke-WebRequest -Uri "http://$HostName$Path" -UseBasicParsing -TimeoutSec 15
  return ($response.Content | ConvertFrom-Json)
}

function Show-Status($state) {
  $line = "{0} v{1} {2}/{3} T={4,5:F1}C RH={5,4:F1}% AH={6,5:F2} NTC={7,5:F1}C heat={8,3:F0}% fan={9,3}%" -f `
    (Get-Date -Format "HH:mm:ss"), $state.version, $state.mode, $state.phase, `
    $state.air.temperatureC, $state.air.relativeHumidity, $state.air.absoluteHumidityGm3, `
    $state.heater.temperatureC, $state.outputs.heater, $state.outputs.fan
  if ($state.dryness) {
    $line += " purge={0} slope={1,5:F2}g/m3h dry={2}/{3}" -f `
      $state.dryness.purgePhase, $state.dryness.ahSlopeGm3PerHour, `
      $state.dryness.stableWindows, $state.dryness.slopeThreshold
  }
  Write-Host $line
}

switch ($Command.ToLower()) {
  "status" {
    $state = Get-Json "/api/state"
    $state | ConvertTo-Json -Depth 6 | Write-Host
  }
  "watch" {
    $seconds = if ($Argument) { [int]$Argument } else { 3600 }
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $logFile = Join-Path (Get-Location) "telemetry-$stamp.jsonl"
    Write-Host "Polling http://$HostName/api/state every ${IntervalSeconds}s for ${seconds}s -> $logFile"
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
      try {
        $state = Get-Json "/api/state"
        Show-Status $state
        $record = @{ ts = (Get-Date -Format o); state = $state }
        ($record | ConvertTo-Json -Depth 6 -Compress) | Add-Content -Path $logFile
      } catch {
        Write-Host ("{0} request failed: {1}" -f (Get-Date -Format "HH:mm:ss"), $_.Exception.Message)
      }
      Start-Sleep -Seconds $IntervalSeconds
    }
  }
  "history" {
    if (-not $Argument) { throw "history needs an output file argument" }
    Invoke-WebRequest -Uri "http://$HostName/api/history" -UseBasicParsing -TimeoutSec 30 `
      -OutFile $Argument
    Write-Host "Saved $Argument"
  }
  "events" {
    if (-not $Argument) { throw "events needs an output file argument" }
    Invoke-WebRequest -Uri "http://$HostName/api/events" -UseBasicParsing -TimeoutSec 30 `
      -OutFile $Argument
    Write-Host "Saved $Argument"
  }
  "ota" {
    $bin = Resolve-Path ".pio\build\esp32dev_ota\firmware.bin"
    Write-Host "POST $bin -> http://$HostName/api/ota"
    Invoke-WebRequest -Uri "http://$HostName/api/ota" -Method Post -UseBasicParsing -TimeoutSec 120 `
      -InFile $bin -ContentType "application/octet-stream" | Out-Null
    Write-Host "Uploaded; device reboots. Polling until it returns..."
    Start-Sleep -Seconds 8
    $deadline = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $deadline) {
      try {
        $state = Get-Json "/api/state"
        Write-Host ("Back online: v{0} {1}/{2}" -f $state.version, $state.mode, $state.phase)
        return
      } catch { Start-Sleep -Seconds 2 }
    }
    Write-Warning "Device did not answer within 60s"
  }
  default { throw "Unknown command: $Command (status|watch|history|events|ota)" }
}
