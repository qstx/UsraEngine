param([Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$Scratch)
$ErrorActionPreference = 'Stop'
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot)
$Scratch = [IO.Path]::GetFullPath($Scratch)
[IO.Directory]::CreateDirectory($Scratch) | Out-Null
. (Join-Path $EngineRoot 'Tools/WindowsToolchain.ps1')
function Expect([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$oldCache = Join-Path $Scratch 'OldCache'
[IO.Directory]::CreateDirectory($oldCache) | Out-Null
$cachePath = Join-Path $oldCache 'CMakeCache.txt'
[IO.File]::WriteAllText($cachePath, 'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022')
$rejected = $false
try { Get-UrsaWindowsToolchain -BuildDirectory $oldCache | Out-Null } catch { $rejected = $_.Exception.Message -match 'Incompatible build cache' }
Expect $rejected 'Old generator cache was accepted'
Expect ([IO.File]::ReadAllText($cachePath).Contains('2022')) 'Old cache was modified'
$oldCmake = Join-Path $Scratch 'OldCMake.ps1'
[IO.File]::WriteAllText($oldCmake, "Write-Output 'cmake version 3.28.0'; exit 0")
$rejected = $false
try { Get-UrsaWindowsToolchain -CMakePath $oldCmake | Out-Null } catch { $rejected = $_.Exception.Message -match '4.4.5 or newer' }
Expect $rejected 'Old CMake version was accepted'
$fixtureRoot = Join-Path $Scratch 'Fixture'
$module = Join-Path $fixtureRoot 'Managed/Probe'
[IO.Directory]::CreateDirectory($module) | Out-Null
[IO.File]::WriteAllText((Join-Path $module 'Probe.csproj'), '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="Microsoft.CodeAnalysis"><HintPath>$(MSBuildSDKsPath)/../Roslyn/bincore/Microsoft.CodeAnalysis.dll</HintPath></Reference></ItemGroup></Project>')
[IO.File]::WriteAllText((Join-Path $module 'A.cs'), 'class A {}')
$fixtureBuild = Join-Path $Scratch 'Generated'
$generator = Join-Path $EngineRoot 'Tools/GenerateManagedIde.ps1'
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild
$projectPath = Join-Path $fixtureBuild 'Ide/Managed/Probe/Probe.csproj'
$before = [IO.File]::ReadAllText($projectPath)
$timestamp = [IO.File]::GetLastWriteTimeUtc($projectPath)
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild
Expect ([IO.File]::GetLastWriteTimeUtc($projectPath) -eq $timestamp) 'Unchanged managed generation rewrote project'
Expect ($before.Contains('<AllowUnsafeBlocks>true</AllowUnsafeBlocks>') -and $before.Contains('$(MSBuildSDKsPath)')) 'Project metadata or SDK reference was lost'
Expect ($before.Contains((Join-Path $module 'A.cs').Replace('\','/')) -and $before.Contains('Link="A.cs"')) 'Generated project does not link original source'
[IO.File]::WriteAllText((Join-Path $module 'Added.cs'), 'class Added {}')
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild
Expect ([IO.File]::ReadAllText($projectPath).Contains('Added.cs')) 'Added managed source was omitted'
Remove-Item -LiteralPath (Join-Path $module 'Added.cs')
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild
Expect ([IO.File]::ReadAllText($projectPath) -ceq $before) 'Removed managed source was retained'
$testRoot = Join-Path $fixtureRoot 'Tests/Managed/Ursa.SnakeLogicTests'
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
[IO.File]::WriteAllText((Join-Path $testRoot 'Ursa.SnakeLogicTests.csproj'), '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup><ItemGroup><Compile Include="$(UrsaProjectDir)/Assets/Scripts/Snake/SnakeGame.cs" /></ItemGroup></Project>')
$projectRoot = Join-Path $Scratch 'Project'
[IO.Directory]::CreateDirectory($projectRoot) | Out-Null
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild -IncludeTests -ProjectDirectory $projectRoot
$manifest = [IO.File]::ReadAllText((Join-Path $fixtureBuild 'Ide/Managed/Projects.json'))
Expect (-not $manifest.Contains('Ursa.SnakeLogicTests')) 'A different game included Demo-specific source tests'
$snakeRoot = Join-Path $projectRoot 'Assets/Scripts/Snake'
[IO.Directory]::CreateDirectory($snakeRoot) | Out-Null
[IO.File]::WriteAllText((Join-Path $snakeRoot 'SnakeGame.cs'), 'class SnakeGame {}')
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild -IncludeTests -ProjectDirectory $projectRoot
$manifest = [IO.File]::ReadAllText((Join-Path $fixtureBuild 'Ide/Managed/Projects.json'))
Expect ($manifest.Contains('Ursa.SnakeLogicTests')) 'Demo-specific source tests were omitted when their source exists'
$snakeProject = [IO.File]::ReadAllText((Join-Path $fixtureBuild 'Ide/Managed/Ursa.SnakeLogicTests/Ursa.SnakeLogicTests.csproj'))
Expect ($snakeProject.Contains("<UrsaProjectDir>$projectRoot</UrsaProjectDir>")) 'Project-specific source root was not preserved'
$propsPath = Join-Path $fixtureBuild 'Ide/Managed/Directory.Build.props'
$propsTimestamp = [IO.File]::GetLastWriteTimeUtc($propsPath)
& $generator -EngineRoot $fixtureRoot -BuildRoot $fixtureBuild -IncludeTests
Expect ([IO.File]::GetLastWriteTimeUtc($propsPath) -eq $propsTimestamp) 'Switching game projects rewrote shared framework properties'
Write-Output 'Toolchain rejection, source links, metadata, additions/removals and no-op generation verified.'
