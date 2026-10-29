# Compatibility entry; the implementation lives in Python.
[CmdletBinding()]
param([string]$BuildDirectory = '', [string]$ProjectDirectory = '', [string]$CMakePath = 'cmake', [switch]$Tests,
    [string]$EngineDirectory = '')
$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'GenerateSolution.py'), '--cmake', $CMakePath)
if ($BuildDirectory) { $arguments += @('--build-dir', $BuildDirectory) }
if ($ProjectDirectory) { $arguments += @('--project-dir', $ProjectDirectory) }
if ($EngineDirectory) { $arguments += @('--engine-dir', $EngineDirectory) }
if ($Tests) { $arguments += '--tests' }
& (Join-Path $PSScriptRoot 'RunPython.bat') @arguments
if ($LASTEXITCODE -ne 0) { throw "Workspace generation failed ($LASTEXITCODE)." }
