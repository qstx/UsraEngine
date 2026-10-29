# Compatibility entry for existing automation.
param([Parameter(Mandatory)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$output = & (Join-Path $PSScriptRoot 'RunPython.bat') (Join-Path $PSScriptRoot 'CheckProjectStructure.py') --build-dir $BuildDir 2>&1
if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
$output
