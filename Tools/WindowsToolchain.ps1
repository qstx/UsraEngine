# Compatibility entry for existing packaging and test callers.
function Get-UrsaWindowsToolchain {
    [CmdletBinding()]
    param([string]$CMakePath = 'cmake', [string]$BuildDirectory = '')
    $ErrorActionPreference = 'Stop'
    $arguments = @((Join-Path $PSScriptRoot 'WindowsToolchain.py'), '--cmake', $CMakePath)
    if ($BuildDirectory) { $arguments += @('--build-dir', $BuildDirectory) }
    $output = & (Join-Path $PSScriptRoot 'RunPython.bat') @arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
    ($output -join "`n") | ConvertFrom-Json
}
