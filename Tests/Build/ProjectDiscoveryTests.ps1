param([Parameter(Mandatory)][string]$Scratch, [Parameter(Mandatory)][string]$EngineRoot)
$ErrorActionPreference = 'Stop'
$scratchRoot = [IO.Path]::GetFullPath($Scratch)
$sourceRoot = Join-Path $scratchRoot 'Source'
$buildRoot = Join-Path $scratchRoot 'Build'
$moduleRoot = Join-Path $sourceRoot 'Runtime/Core'
New-Item -ItemType Directory -Path $moduleRoot -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'CMake') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $EngineRoot 'CMake/SourceGroups.cmake') -Destination (Join-Path $sourceRoot 'CMake/SourceGroups.cmake')
$cmake = @'
cmake_minimum_required(VERSION 3.20)
project(DiscoveryProbe LANGUAGES CXX)
set(URSA_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(CMake/SourceGroups.cmake)
ursa_collect_sources(sources "${CMAKE_CURRENT_SOURCE_DIR}/Runtime")
add_library(DiscoveryProbe STATIC ${sources})
ursa_source_groups(DiscoveryProbe "Runtime")
'@
[IO.File]::WriteAllText((Join-Path $sourceRoot 'CMakeLists.txt'), $cmake, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $moduleRoot 'Base.cpp'), 'int Base() { return 1; }', [Text.UTF8Encoding]::new($false))
& cmake -S $sourceRoot -B $buildRoot -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Discovery fixture configuration failed' }
function Build-Probe {
    & cmake --build $buildRoot --config Debug --parallel -- /nodeReuse:false
    if ($LASTEXITCODE -ne 0) { throw 'Normal discovery fixture build failed' }
}
Build-Probe
$probePaths = @()
foreach ($extension in @('cpp','hpp','h','inl')) {
    $path = Join-Path $moduleRoot "Added.$extension"
    $probePaths += $path
    $text = if ($extension -eq 'cpp') { 'int Added() { return 2; }' } else { '// Source discovery fixture' }
    [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
}
Build-Probe
$projectPath = Join-Path $buildRoot 'DiscoveryProbe.vcxproj'
$project = [IO.File]::ReadAllText($projectPath)
$filters = [IO.File]::ReadAllText($projectPath + '.filters')
foreach ($path in $probePaths) {
    if (-not $project.Contains($path) -or -not $filters.Contains($path)) { throw "Added file not discovered: $path" }
}
if (-not $filters.Contains('<Filter>Runtime\Core</Filter>')) { throw 'Directory filter did not match disk' }
foreach ($path in $probePaths) {
    $resolved = (Resolve-Path -LiteralPath $path).Path
    if (-not $resolved.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Probe file escaped scratch source root' }
    Remove-Item -LiteralPath $resolved
}
Build-Probe
$project = [IO.File]::ReadAllText($projectPath)
foreach ($path in $probePaths) {
    if ($project.Contains($path)) { throw "Deleted file retained in generated project: $path" }
}
Write-Output 'Normal builds discovered additions and removals of all supported code extensions.'
