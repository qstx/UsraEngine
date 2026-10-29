[CmdletBinding()]
param([string]$BuildDirectory = '', [string]$ProjectDirectory = '', [string]$CMakePath = 'cmake', [switch]$Tests)
$ErrorActionPreference = 'Stop'
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $engineRoot 'Build/VS2026' }
if (-not $ProjectDirectory) { $ProjectDirectory = Join-Path $engineRoot '../Demo' }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$ProjectDirectory = [IO.Path]::GetFullPath($ProjectDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $ProjectDirectory 'Assets') -PathType Container)) { throw "Project Assets directory is missing: $ProjectDirectory" }
. (Join-Path $PSScriptRoot 'WindowsToolchain.ps1')
$toolchain = Get-UrsaWindowsToolchain -CMakePath $CMakePath -BuildDirectory $BuildDirectory
Write-Host "$($toolchain.Version); VS2026/v145 at $($toolchain.VisualStudio)"
$testValue = if ($Tests) { 'ON' } else { 'OFF' }
$configure = @('-S', $engineRoot, '-B', $BuildDirectory, '-G', $toolchain.Generator, '-A', 'x64', '-T', 'v145',
    "-DCMAKE_GENERATOR_INSTANCE=$($toolchain.VisualStudio)", '-DURSA_BUILD_EDITOR=ON', '-DURSA_ENABLE_DOTNET=ON',
    '-DURSA_ENABLE_TRACY=ON', '-DURSA_MSVC_PARALLEL_COMPILE=ON', "-DURSA_BUILD_TESTS=$testValue", "-DURSA_PROJECT_DIR=$ProjectDirectory")
Write-Host '[1/5] Configure workspace tools'
& $toolchain.CMake @configure -DURSA_WORKSPACE_BOOTSTRAP=ON
if ($LASTEXITCODE -ne 0) { throw 'Workspace bootstrap configuration failed.' }
Write-Host '[2/5] Build the VS2026 script workspace tool (no game compilation or content import)'
& $toolchain.CMake --build $BuildDirectory --config Debug --parallel 3 --target UrsaScriptCompile -- /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw 'Workspace tool build failed.' }
$compiler = Join-Path $BuildDirectory 'Debug/UrsaScriptCompile.exe'
$description = Join-Path $BuildDirectory 'UnifiedWorkspace.json'
Write-Host '[3/5] Collect project scripts and generate C# projects'
& $compiler $ProjectDirectory --ide --workspace $description --prepare-workspace
if ($LASTEXITCODE -ne 0) { throw 'Project IDE generation failed.' }
Write-Host '[4/5] Prepare IDE framework bindings and both configurations'
foreach ($configuration in @('Debug','Release')) {
    foreach ($project in @('Ursa.Editor','Ursa.ManagedHost')) {
        $ideProject = Join-Path $BuildDirectory "Ide/Managed/$project/$project.csproj"
        & dotnet build $ideProject --configuration $configuration --nologo -m:1 --disable-build-servers -nr:false
        if ($LASTEXITCODE -ne 0) { throw "IDE framework preparation failed: $project / $configuration" }
    }
}
Write-Host '[5/5] Complete the unified solution and publish the association'
& $toolchain.CMake @configure -DURSA_WORKSPACE_BOOTSTRAP=OFF
if ($LASTEXITCODE -ne 0) { throw 'Final unified solution configuration failed.' }
& $compiler $ProjectDirectory --ide --workspace $description
if ($LASTEXITCODE -ne 0) { throw 'Could not publish the completed workspace association.' }
& (Join-Path $PSScriptRoot 'CheckProjectStructure.ps1') -BuildDir $BuildDirectory
Write-Host "Solution ready: $(Join-Path $BuildDirectory 'UrsaEngine.slnx')"
Write-Host 'Open in VS2026. Build UrsaEditor or UrsaScriptPlayer; IDE script builds only check compilation.'
