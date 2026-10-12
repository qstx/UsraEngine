[CmdletBinding()]
param(
	[string]$PythonExecutable = "",
	[string]$WorkspaceRoot = "",
	[switch]$PackageExistingBinding
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot))
{
	$WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
}
else
{
	$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)
}

$EngineRoot = Join-Path $WorkspaceRoot 'Engine'
$TracyRoot = Join-Path $EngineRoot 'ThirdParty\tracy'
$BuildRoot = Join-Path $EngineRoot 'Build\TracyMcp'
$CMakeBuildDirectory = Join-Path $BuildRoot 'CMakeBuild'
$VersionHeader = Join-Path $TracyRoot 'public\common\TracyVersion.hpp'

if (-not (Test-Path -LiteralPath $VersionHeader -PathType Leaf))
{
	throw "Tracy submodule is missing. Initialize Engine/ThirdParty/tracy before building."
}

if ([string]::IsNullOrWhiteSpace($PythonExecutable))
{
	$PythonCommand = Get-Command python -ErrorAction Stop
	$PythonExecutable = $PythonCommand.Source
}

$PythonInfoJson = & $PythonExecutable -c 'import json, struct, sys; print(json.dumps({"major":sys.version_info.major,"minor":sys.version_info.minor,"patch":sys.version_info.micro,"bits":struct.calcsize("P")*8}))'
if ($LASTEXITCODE -ne 0)
{
	throw "Could not query Python at $PythonExecutable"
}

$PythonInfo = $PythonInfoJson | ConvertFrom-Json
if ($PythonInfo.major -ne 3 -or $PythonInfo.minor -ne 11 -or $PythonInfo.bits -ne 64)
{
	throw "Tracy MCP release requires 64-bit Python 3.11; found $($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch) ($($PythonInfo.bits)-bit)."
}

$VersionText = Get-Content -LiteralPath $VersionHeader -Raw
$VersionParts = foreach ($Part in @('Major', 'Minor', 'Patch'))
{
	$Match = [regex]::Match($VersionText, "constexpr int $Part = (\d+);")
	if (-not $Match.Success)
	{
		throw "Could not read Tracy $Part version from $VersionHeader"
	}
	$Match.Groups[1].Value
}

$TracyVersion = $VersionParts -join '.'
$ReleaseName = "tracy-mcp-windows-x64-$TracyVersion"
$ReleaseDirectory = Join-Path $WorkspaceRoot $ReleaseName
$StagingDirectory = Join-Path $BuildRoot ("Staging-" + [guid]::NewGuid().ToString('N'))
$BackupDirectory = Join-Path $BuildRoot ("Previous-" + [guid]::NewGuid().ToString('N'))
$PythonBindingDirectory = if ($PackageExistingBinding) { Join-Path $BuildRoot 'python\Release' } else { Join-Path $CMakeBuildDirectory 'python\Release' }

New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null

if (-not $PackageExistingBinding)
{
	Write-Output "Configuring Tracy $TracyVersion MCP bindings with Python $($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch)..."
	& cmake -S $TracyRoot -B $CMakeBuildDirectory -G 'Visual Studio 17 2022' -A x64 `
		-DTRACY_CLIENT_PYTHON=ON `
		-DTRACY_STATIC=OFF `
		-DBUILD_SHARED_LIBS=OFF `
		-DNO_LTO=ON `
		'-DCPM_SOURCE_CACHE:PATH=OFF' `
		'-DCMAKE_CXX_FLAGS=/utf-8 /EHsc /D_CRT_SECURE_NO_WARNINGS /wd4018' `
		"-DPython_EXECUTABLE=$PythonExecutable"
	if ($LASTEXITCODE -ne 0)
	{
		throw 'Tracy MCP CMake configuration failed.'
	}

	Write-Output 'Building TracyServerBindings (Release)...'
	& cmake --build $CMakeBuildDirectory --config Release --target TracyServerBindings --parallel 2
	if ($LASTEXITCODE -ne 0)
	{
		throw 'TracyServerBindings build failed.'
	}
}
else
{
	Write-Output "Packaging an existing TracyServerBindings artifact from $PythonBindingDirectory..."
}

$PythonBindings = @(Get-ChildItem -LiteralPath $PythonBindingDirectory -Filter 'TracyServerBindings*.pyd' -File -ErrorAction SilentlyContinue)
if ($PythonBindings.Count -ne 1)
{
	throw "Expected one TracyServerBindings Python extension in $PythonBindingDirectory; found $($PythonBindings.Count)."
}
$PythonBinding = $PythonBindings[0].FullName

$TracyRevision = (& git -C $TracyRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0)
{
	throw 'Could not read the Tracy submodule revision.'
}

$BindingHash = (Get-FileHash -LiteralPath $PythonBinding -Algorithm SHA256).Hash.ToLowerInvariant()
$BindingMarkerPath = Join-Path $BuildRoot 'TracyServerBindings-build.json'
if ($PackageExistingBinding)
{
	if (-not (Test-Path -LiteralPath $BindingMarkerPath -PathType Leaf))
	{
		throw "Cannot reuse a binding without build provenance at $BindingMarkerPath. Build it once with this script first."
	}
	$BindingMarker = Get-Content -LiteralPath $BindingMarkerPath -Raw | ConvertFrom-Json
	if ($BindingMarker.tracyRevision -ne $TracyRevision -or $BindingMarker.pythonAbi -ne 'cp311-win_amd64' -or $BindingMarker.sha256 -ne $BindingHash)
	{
		throw 'The existing binding provenance does not match the current Tracy revision, Python ABI, or binary contents.'
	}
}
else
{
	$BindingMarker = [ordered]@{
		tracyVersion = $TracyVersion
		tracyRevision = $TracyRevision
		pythonAbi = 'cp311-win_amd64'
		sha256 = $BindingHash
	}
	[IO.File]::WriteAllText($BindingMarkerPath, (($BindingMarker | ConvertTo-Json) + "`n"), [Text.UTF8Encoding]::new($false))
}

$RequiredInputs = @(
	'Extra/Mcp/tracy_mcp.py',
	'Extra/Mcp/eval_guide.md',
	'Extra/Mcp/requirements.txt',
	'profiler/src/llm/system.prompt.md',
	'public/common/TracyProtocol.hpp',
	'LICENSE'
)
foreach ($RelativePath in $RequiredInputs)
{
	$InputPath = Join-Path $TracyRoot $RelativePath
	if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf))
	{
		throw "Required Tracy MCP input is missing: $InputPath"
	}
}

New-Item -ItemType Directory -Path $StagingDirectory -Force | Out-Null
$PackagePaths = @(
	'extra/mcp',
	'profiler/src/llm',
	'public/common'
)
foreach ($RelativePath in $PackagePaths)
{
	New-Item -ItemType Directory -Path (Join-Path $StagingDirectory $RelativePath) -Force | Out-Null
}

Copy-Item -LiteralPath (Join-Path $TracyRoot 'extra/mcp/tracy_mcp.py') -Destination (Join-Path $StagingDirectory 'extra/mcp')
Copy-Item -LiteralPath (Join-Path $TracyRoot 'extra/mcp/eval_guide.md') -Destination (Join-Path $StagingDirectory 'extra/mcp')
Copy-Item -LiteralPath (Join-Path $TracyRoot 'extra/mcp/requirements.txt') -Destination (Join-Path $StagingDirectory 'extra/mcp')
Copy-Item -LiteralPath (Join-Path $TracyRoot 'profiler/src/llm/system.prompt.md') -Destination (Join-Path $StagingDirectory 'profiler/src/llm')
Copy-Item -LiteralPath (Join-Path $TracyRoot 'public/common/TracyProtocol.hpp') -Destination (Join-Path $StagingDirectory 'public/common')
Copy-Item -LiteralPath (Join-Path $TracyRoot 'LICENSE') -Destination $StagingDirectory
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Start-TracyMcp.ps1') -Destination $StagingDirectory
Copy-Item -LiteralPath $PythonBinding -Destination $StagingDirectory
foreach ($RuntimeLibrary in Get-ChildItem -LiteralPath $PythonBindingDirectory -Filter '*.dll' -File)
{
	Copy-Item -LiteralPath $RuntimeLibrary.FullName -Destination $StagingDirectory
}

$ReadmeSource = Join-Path $PSScriptRoot 'README.md'
if (-not (Test-Path -LiteralPath $ReadmeSource -PathType Leaf))
{
	throw "Release instructions are missing: $ReadmeSource"
}
$Readme = (Get-Content -LiteralPath $ReadmeSource -Raw).Replace('@TRACY_VERSION@', $TracyVersion).Replace('@TRACY_REVISION@', $TracyRevision)
[IO.File]::WriteAllText((Join-Path $StagingDirectory 'README.md'), $Readme, [Text.UTF8Encoding]::new($false))

$RuntimeIgnore = @(
	'extra/mcp/tracy_mcp.pid',
	'extra/mcp/tracy_mcp.port',
	'extra/mcp/tracy_mcp.crash.log',
	'extra/mcp/__pycache__/',
	'Captures/'
) -join "`n"
[IO.File]::WriteAllText((Join-Path $StagingDirectory '.gitignore'), $RuntimeIgnore + "`n", [Text.UTF8Encoding]::new($false))

$CxxFlags = if ($PackageExistingBinding) { 'CMAKE_CXX_FLAGS=/utf-8 /D_CRT_SECURE_NO_WARNINGS /wd4018' } else { 'CMAKE_CXX_FLAGS=/utf-8 /EHsc /D_CRT_SECURE_NO_WARNINGS /wd4018' }
$BuildMode = if ($PackageExistingBinding) { 'reused-existing-release-artifact' } else { 'built-from-pinned-submodule' }
$Manifest = [ordered]@{
	package = $ReleaseName
	tracyVersion = $TracyVersion
	tracyRevision = $TracyRevision
	configuration = 'Release'
	architecture = 'x64'
	pythonVersion = "$($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch)"
	pythonAbi = 'cp311-win_amd64'
	buildMode = $BuildMode
	cmakeOptions = @(
		'TRACY_CLIENT_PYTHON=ON',
		'TRACY_STATIC=OFF',
		'BUILD_SHARED_LIBS=OFF',
		'NO_LTO=ON',
		$CxxFlags
	)
}
$ManifestJson = $Manifest | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $StagingDirectory 'release-manifest.json'), $ManifestJson + "`n", [Text.UTF8Encoding]::new($false))

$MovedPreviousRelease = $false
try
{
	if (Test-Path -LiteralPath $ReleaseDirectory)
	{
		Move-Item -LiteralPath $ReleaseDirectory -Destination $BackupDirectory
		$MovedPreviousRelease = $true
	}
	Move-Item -LiteralPath $StagingDirectory -Destination $ReleaseDirectory
}
catch
{
	if ($MovedPreviousRelease -and -not (Test-Path -LiteralPath $ReleaseDirectory))
	{
		Move-Item -LiteralPath $BackupDirectory -Destination $ReleaseDirectory
	}
	throw
}

Write-Output "Tracy MCP release created at $ReleaseDirectory"
Write-Output 'Follow README.md in the release directory to create the Python environment and start the local MCP server.'
