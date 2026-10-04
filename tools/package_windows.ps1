param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$OutputDir
)
$ErrorActionPreference = "Stop"
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command python3 -ErrorAction Stop }
& $Python.Source "$PSScriptRoot/package_windows.py" $BuildDir $OutputDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
