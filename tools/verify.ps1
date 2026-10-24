# Verification complete cote PC : tests JavaScript puis compilation ARM.
# Usage : powershell -ExecutionPolicy Bypass -File tools\verify.ps1 [-SkipBuild]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root
$node = 'C:\Program Files\Adobe\Adobe Creative Cloud Experience\libs\node.exe'
if (-not (Test-Path $node)) { $node = 'node' }
$tests = Get-ChildItem tests/web/*.test.js | ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') }
& $node --test @tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $SkipBuild) {
  $pio = Join-Path $env:APPDATA 'Python\Python312\Scripts\platformio.exe'
  if (-not (Test-Path $pio)) { $pio = 'platformio' }
  & $pio run -e blackpill_f411ce
  exit $LASTEXITCODE
}
