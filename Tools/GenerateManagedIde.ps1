# Compatibility entry; CMake invokes the Python implementation directly.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$BuildRoot, [switch]$IncludeTests,
    [string]$RoslynDirectory = '', [string]$ProjectDirectory = '')
$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'GenerateManagedIde.py'), '--engine-root', $EngineRoot, '--build-root', $BuildRoot)
if ($IncludeTests) { $arguments += '--include-tests' }
if ($RoslynDirectory) { $arguments += @('--roslyn-directory', $RoslynDirectory) }
if ($ProjectDirectory) { $arguments += @('--project-directory', $ProjectDirectory) }
& (Join-Path $PSScriptRoot 'RunPython.bat') @arguments
if ($LASTEXITCODE -ne 0) { throw "Managed IDE generation failed ($LASTEXITCODE)." }
