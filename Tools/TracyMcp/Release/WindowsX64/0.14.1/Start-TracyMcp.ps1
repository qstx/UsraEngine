[CmdletBinding()]
param(
	[string]$CapturesDirectory = (Join-Path $env:LOCALAPPDATA 'UrsaEngine\TracyMcp\Captures'),
	[int]$Port = 47380
)

$ErrorActionPreference = 'Stop'
$ReleaseDirectory = $PSScriptRoot
$ReleaseManifestPath = Join-Path $ReleaseDirectory 'release-manifest.json'
if (-not (Test-Path -LiteralPath $ReleaseManifestPath -PathType Leaf))
{
	throw "Tracy MCP release manifest is missing: $ReleaseManifestPath"
}
$ReleaseManifest = Get-Content -LiteralPath $ReleaseManifestPath -Raw | ConvertFrom-Json
$EngineToolsDirectory = [IO.Path]::GetFullPath((Join-Path $ReleaseDirectory '..\..\..\..'))
$PythonExecutable = Join-Path $EngineToolsDirectory "Python\$($ReleaseManifest.runtimePythonVersion)\python.exe"
$PythonRuntimeManifestPath = Join-Path (Split-Path -Parent $PythonExecutable) 'runtime-manifest.json'

if (-not (Test-Path -LiteralPath $PythonExecutable -PathType Leaf) -or -not (Test-Path -LiteralPath $PythonRuntimeManifestPath -PathType Leaf))
{
	throw "Shared Python runtime is missing under Engine/Tools/Python: $PythonExecutable"
}
$PythonRuntimeManifest = Get-Content -LiteralPath $PythonRuntimeManifestPath -Raw | ConvertFrom-Json
if ($PythonRuntimeManifest.archiveSha256 -ne $ReleaseManifest.pythonRuntimeSha256)
{
	throw "The shared Python runtime does not match the Tracy MCP release manifest: $PythonRuntimeManifestPath"
}
$PythonInfoText = & $PythonExecutable -c "import sys,struct;print(sys.version_info.major,sys.version_info.minor,sys.version_info.micro,struct.calcsize(chr(80))*8)"
if ($LASTEXITCODE -ne 0)
{
	throw "Could not query Python at $PythonExecutable"
}

$PythonInfoValues = $PythonInfoText.Trim() -split '\s+'
if ($PythonInfoValues.Count -ne 4)
{
	throw "Could not parse Python version information from $PythonExecutable`: $PythonInfoText"
}
$PythonInfo = [PSCustomObject]@{
	major = [int]$PythonInfoValues[0]
	minor = [int]$PythonInfoValues[1]
	patch = [int]$PythonInfoValues[2]
	bits = [int]$PythonInfoValues[3]
}
$PythonVersion = "$($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch)"
if ($PythonVersion -ne $ReleaseManifest.runtimePythonVersion -or $PythonInfo.bits -ne 64)
{
	throw "This release requires Python $($ReleaseManifest.runtimePythonVersion) x64 from Engine/Tools/Python; found $PythonVersion ($($PythonInfo.bits)-bit)."
}

$BindingPaths = @(Get-ChildItem -LiteralPath $ReleaseDirectory -Filter 'TracyServerBindings*.pyd' -File -ErrorAction SilentlyContinue)
$ServerPath = Join-Path $ReleaseDirectory 'extra\mcp\tracy_mcp.py'
if ($BindingPaths.Count -ne 1 -or -not (Test-Path -LiteralPath $ServerPath -PathType Leaf))
{
	throw "The Tracy MCP release is incomplete at $ReleaseDirectory"
}

New-Item -ItemType Directory -Path $CapturesDirectory -Force | Out-Null
$env:PYTHONPATH = if ($env:PYTHONPATH) { "$ReleaseDirectory;$env:PYTHONPATH" } else { $ReleaseDirectory }
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:TRACY_CAPTURES_DIR = [IO.Path]::GetFullPath($CapturesDirectory)
$env:TRACY_MCP_PORT = $Port.ToString()

& $PythonExecutable -c 'import mcp, TracyServerBindings'
if ($LASTEXITCODE -ne 0)
{
	throw "Python MCP dependencies or TracyServerBindings could not be imported. Rebuild the Tracy MCP release package and verify its PythonPackages directory."
}

Write-Output "Starting Tracy MCP at http://127.0.0.1:$Port/mcp"
Write-Output "Capture directory: $env:TRACY_CAPTURES_DIR"
& $PythonExecutable $ServerPath
exit $LASTEXITCODE
