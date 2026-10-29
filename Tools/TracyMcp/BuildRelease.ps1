[CmdletBinding()]
param(
	[string]$PythonExecutable = "",
	[string]$WorkspaceRoot = "",
	[switch]$PackageExistingBinding,
	[string]$CMakePath = 'cmake'
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
$CMakeBuildDirectory = Join-Path $BuildRoot 'CMakeBuildVS2026'
$BundledPythonVersion = '3.11.17'
$PythonDistributionTag = '20261009'
$PythonRuntimeArchiveName = "cpython-$BundledPythonVersion+$PythonDistributionTag-x86_64-pc-windows-msvc-install_only_stripped.tar.gz"
$PythonRuntimeArchive = Join-Path $BuildRoot $PythonRuntimeArchiveName
$PythonRuntimeExtractionDirectory = Join-Path $BuildRoot "PythonRuntime-$BundledPythonVersion-$PythonDistributionTag"
$PythonRuntimeSha256 = 'c3045115465bb4aa7d1ca6bfe6ce311ba157d726d768bbb3520fad2580831c3f'
$PythonRuntimeUrl = "https://github.com/astral-sh/python-build-standalone/releases/download/$PythonDistributionTag/$($PythonRuntimeArchiveName.Replace('+', '%2B'))"
$SharedPythonRoot = Join-Path $EngineRoot 'Tools\Python'
$SharedPythonDirectory = Join-Path $SharedPythonRoot $BundledPythonVersion
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
$ReleaseName = "TracyMcp-WindowsX64-$TracyVersion"
$ReleaseRoot = Join-Path $PSScriptRoot 'Release\WindowsX64'
$ReleaseDirectory = Join-Path $ReleaseRoot $TracyVersion
$StagingDirectory = Join-Path $BuildRoot ("Staging-" + [guid]::NewGuid().ToString('N'))
$BackupDirectory = Join-Path $BuildRoot ("Previous-" + [guid]::NewGuid().ToString('N'))
$PythonBindingDirectory = if ($PackageExistingBinding) { Join-Path $BuildRoot 'python\Release' } else { Join-Path $CMakeBuildDirectory 'python\Release' }

New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null
New-Item -ItemType Directory -Path $ReleaseRoot -Force | Out-Null

if (-not (Test-Path -LiteralPath $PythonRuntimeArchive -PathType Leaf))
{
	Write-Output "Downloading portable CPython $BundledPythonVersion x64 runtime..."
	Invoke-WebRequest -Uri $PythonRuntimeUrl -OutFile $PythonRuntimeArchive
}

$DownloadedRuntimeHash = (Get-FileHash -LiteralPath $PythonRuntimeArchive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($DownloadedRuntimeHash -ne $PythonRuntimeSha256)
{
	throw "Portable Python archive checksum mismatch. Expected $PythonRuntimeSha256, found $DownloadedRuntimeHash."
}

$PythonRuntimeInstallDirectory = Join-Path $PythonRuntimeExtractionDirectory 'python'
if (-not (Test-Path -LiteralPath (Join-Path $PythonRuntimeInstallDirectory 'python.exe') -PathType Leaf))
{
	New-Item -ItemType Directory -Path $PythonRuntimeExtractionDirectory -Force | Out-Null
	& tar.exe -xzf $PythonRuntimeArchive -C $PythonRuntimeExtractionDirectory
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $PythonRuntimeInstallDirectory 'python.exe') -PathType Leaf))
	{
		throw 'Could not extract the portable Python runtime.'
	}
}

if (-not $PackageExistingBinding)
{
	. (Join-Path $PSScriptRoot '../WindowsToolchain.ps1')
	$toolchain = Get-UrsaWindowsToolchain -CMakePath $CMakePath -BuildDirectory $CMakeBuildDirectory
	Write-Output "Configuring Tracy $TracyVersion MCP bindings with Python $($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch)..."
	& $toolchain.CMake -S $TracyRoot -B $CMakeBuildDirectory -G $toolchain.Generator -A x64 -T v145 `
		"-DCMAKE_GENERATOR_INSTANCE=$($toolchain.VisualStudio)" `
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
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
	& $toolchain.CMake --build $CMakeBuildDirectory --config Release --target TracyServerBindings --parallel 2
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

$PythonPackagesDirectory = Join-Path $StagingDirectory 'PythonPackages'
New-Item -ItemType Directory -Path $PythonPackagesDirectory -Force | Out-Null

Write-Output 'Installing Tracy MCP Python dependencies into the release package...'
$PythonRuntimeExecutable = Join-Path $PythonRuntimeInstallDirectory 'python.exe'
& $PythonRuntimeExecutable -m pip install --disable-pip-version-check --no-warn-script-location `
	--target $PythonPackagesDirectory `
	-r (Join-Path $StagingDirectory 'extra/mcp/requirements.txt')
if ($LASTEXITCODE -ne 0)
{
	throw 'Could not install the Tracy MCP Python dependencies into the release package.'
}

$SiteCustomization = @"
import site
from pathlib import Path

site.addsitedir(str(Path(__file__).resolve().parent / "PythonPackages"))
"@
[IO.File]::WriteAllText((Join-Path $StagingDirectory 'sitecustomize.py'), $SiteCustomization, [Text.UTF8Encoding]::new($false))

$PreviousPythonPath = $env:PYTHONPATH
$PreviousPythonBytecodeSetting = $env:PYTHONDONTWRITEBYTECODE
$env:PYTHONPATH = $StagingDirectory
$env:PYTHONDONTWRITEBYTECODE = '1'
& $PythonRuntimeExecutable -c 'import mcp, TracyServerBindings'
$RuntimeImportExitCode = $LASTEXITCODE
$env:PYTHONPATH = $PreviousPythonPath
$env:PYTHONDONTWRITEBYTECODE = $PreviousPythonBytecodeSetting
if ($RuntimeImportExitCode -ne 0)
{
	throw 'The bundled Python runtime could not import MCP dependencies and TracyServerBindings.'
}

$LockedPythonDependencies = & $PythonRuntimeExecutable -m pip freeze --path $PythonPackagesDirectory
if ($LASTEXITCODE -ne 0)
{
	throw 'Could not record the resolved MCP Python dependencies.'
}
[IO.File]::WriteAllText((Join-Path $StagingDirectory 'requirements.lock.txt'), (($LockedPythonDependencies -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))

$OptionalMcpDirectories = @(
	'PythonPackages\pythonwin',
	'PythonPackages\win32com',
	'PythonPackages\win32comext',
	'PythonPackages\adodbapi',
	'PythonPackages\isapi',
	'PythonPackages\bin'
)
foreach ($RelativePattern in $OptionalMcpDirectories)
{
	$PatternPath = Join-Path $StagingDirectory $RelativePattern
	foreach ($OptionalPath in Get-ChildItem -Path $PatternPath -Force -ErrorAction SilentlyContinue)
	{
		$OptionalFullPath = [IO.Path]::GetFullPath($OptionalPath.FullName)
		if (-not $OptionalFullPath.StartsWith($StagingDirectory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
		{
			throw "Refusing to trim an MCP dependency path outside the release staging directory: $OptionalFullPath"
		}
		Remove-Item -LiteralPath $OptionalFullPath -Recurse -Force
	}
}

Get-ChildItem -LiteralPath $StagingDirectory -Directory -Recurse -Filter '__pycache__' | ForEach-Object {
	Remove-Item -LiteralPath $_.FullName -Recurse -Force
}

$PreviousPythonPath = $env:PYTHONPATH
$PreviousPythonBytecodeSetting = $env:PYTHONDONTWRITEBYTECODE
$env:PYTHONPATH = $StagingDirectory
$env:PYTHONDONTWRITEBYTECODE = '1'
& $PythonRuntimeExecutable -c 'import mcp, TracyServerBindings'
$TrimmedRuntimeImportExitCode = $LASTEXITCODE
$env:PYTHONPATH = $PreviousPythonPath
$env:PYTHONDONTWRITEBYTECODE = $PreviousPythonBytecodeSetting
if ($TrimmedRuntimeImportExitCode -ne 0)
{
	throw 'The shared Python runtime could not import MCP dependencies and TracyServerBindings.'
}

if (Test-Path -LiteralPath $SharedPythonDirectory -PathType Container)
{
	$InstalledRuntimeManifestPath = Join-Path $SharedPythonDirectory 'runtime-manifest.json'
	if (-not (Test-Path -LiteralPath (Join-Path $SharedPythonDirectory 'python.exe') -PathType Leaf) -or -not (Test-Path -LiteralPath $InstalledRuntimeManifestPath -PathType Leaf))
	{
		throw "Existing shared Python runtime is incomplete: $SharedPythonDirectory"
	}
	$InstalledRuntimeManifest = Get-Content -LiteralPath $InstalledRuntimeManifestPath -Raw | ConvertFrom-Json
	if ($InstalledRuntimeManifest.archiveSha256 -ne $PythonRuntimeSha256)
	{
		throw "The shared Python runtime at $SharedPythonDirectory does not match the pinned distribution."
	}
}
else
{
	$PythonRuntimeStageDirectory = Join-Path $BuildRoot ("PythonStage-" + [guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $PythonRuntimeStageDirectory -Force | Out-Null
	Copy-Item -Path (Join-Path $PythonRuntimeInstallDirectory '*') -Destination $PythonRuntimeStageDirectory -Recurse -Force
	$PythonRuntimeManifest = [ordered]@{
		version = $BundledPythonVersion
		architecture = 'x64'
		distribution = "astral-sh/python-build-standalone@$PythonDistributionTag"
		archive = $PythonRuntimeArchiveName
		archiveUrl = $PythonRuntimeUrl
		archiveSha256 = $PythonRuntimeSha256
	}
	[IO.File]::WriteAllText((Join-Path $PythonRuntimeStageDirectory 'runtime-manifest.json'), (($PythonRuntimeManifest | ConvertTo-Json -Depth 4) + "`n"), [Text.UTF8Encoding]::new($false))
	$InstalledPythonVersion = & (Join-Path $PythonRuntimeStageDirectory 'python.exe') -c 'import sys; print("{}.{}.{}".format(*sys.version_info[:3]))'
	if ($LASTEXITCODE -ne 0 -or $InstalledPythonVersion.Trim() -ne $BundledPythonVersion)
	{
		throw "Portable Python runtime version mismatch. Expected $BundledPythonVersion, found $InstalledPythonVersion."
	}
	New-Item -ItemType Directory -Path $SharedPythonRoot -Force | Out-Null
	Move-Item -LiteralPath $PythonRuntimeStageDirectory -Destination $SharedPythonDirectory
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
	'__pycache__/',
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
	buildPythonVersion = "$($PythonInfo.major).$($PythonInfo.minor).$($PythonInfo.patch)"
	runtimePythonVersion = $BundledPythonVersion
	pythonRuntimePath = "Engine/Tools/Python/$BundledPythonVersion"
	pythonDistribution = "astral-sh/python-build-standalone@$PythonDistributionTag"
	pythonRuntimeArchive = $PythonRuntimeArchiveName
	pythonRuntimeUrl = $PythonRuntimeUrl
	pythonRuntimeSha256 = $PythonRuntimeSha256
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
Write-Output "Shared Python runtime is at $SharedPythonDirectory; follow README.md to start the local MCP server."
