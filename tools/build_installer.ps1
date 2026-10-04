param(
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$Makensis
)
$ErrorActionPreference = "Stop"
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command python3 -ErrorAction Stop }
$ArgsList = @("$PSScriptRoot/build_installer.py")
if ($BuildDir) { $ArgsList += @("--build-dir", $BuildDir) }
if ($OutputDir) { $ArgsList += @("--output-dir", $OutputDir) }
if ($Makensis) { $ArgsList += @("--makensis", $Makensis) }
& $Python.Source @ArgsList
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
