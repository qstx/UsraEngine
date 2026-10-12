[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$PythonExecutable,
	[string]$CapturesDirectory = (Join-Path $env:LOCALAPPDATA 'UrsaEngine\TracyMcp\Captures'),
	[int]$Port = 47380
)

$ErrorActionPreference = 'Stop'
$ReleaseDirectory = $PSScriptRoot
$PythonCommand = Get-Command $PythonExecutable -ErrorAction Stop
$PythonExecutable = $PythonCommand.Source
$PythonInfoJson = & $PythonExecutable -c 'import json, struct, sys; print(json.dumps({"major":sys.version_info.major,"minor":sys.version_info.minor,"patch":sys.version_info.micro,"bits":struct.calcsize("P")*8}))'
if ($LASTEXITCODE -ne 0)
{
	throw "Could not query Python at $PythonExecutable"
}

$PythonInfo = $PythonInfoJson | ConvertFrom-Json
if ($PythonInfo.major -ne 3 -or $PythonInfo.minor -ne 11 -or $PythonInfo.bits -ne 64)
{
	throw "This release requires 64-bit Python 3.11; found $($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch) ($($PythonInfo.bits)-bit)."
}

$BindingPaths = @(Get-ChildItem -LiteralPath $ReleaseDirectory -Filter 'TracyServerBindings*.pyd' -File -ErrorAction SilentlyContinue)
$ServerPath = Join-Path $ReleaseDirectory 'extra\mcp\tracy_mcp.py'
if ($BindingPaths.Count -ne 1 -or -not (Test-Path -LiteralPath $ServerPath -PathType Leaf))
{
	throw "The Tracy MCP release is incomplete at $ReleaseDirectory"
}

New-Item -ItemType Directory -Path $CapturesDirectory -Force | Out-Null
$env:PYTHONPATH = if ($env:PYTHONPATH) { "$ReleaseDirectory;$env:PYTHONPATH" } else { $ReleaseDirectory }
$env:TRACY_CAPTURES_DIR = [IO.Path]::GetFullPath($CapturesDirectory)
$env:TRACY_MCP_PORT = $Port.ToString()

& $PythonExecutable -c 'import mcp, TracyServerBindings'
if ($LASTEXITCODE -ne 0)
{
	throw "Python MCP dependencies or TracyServerBindings could not be imported. Install the release requirements using $PythonExecutable -m pip install -r `"$(Join-Path $ReleaseDirectory 'extra\mcp\requirements.txt')`"."
}

Write-Output "Starting Tracy MCP at http://127.0.0.1:$Port/mcp"
Write-Output "Capture directory: $env:TRACY_CAPTURES_DIR"
& $PythonExecutable $ServerPath
exit $LASTEXITCODE
