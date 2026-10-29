param([Parameter(Mandatory)][string]$BuildDirectory, [Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$engineRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$workspacePath = Join-Path $buildRoot 'UnifiedWorkspace.json'
$workspace = [IO.File]::ReadAllText($workspacePath) | ConvertFrom-Json
$beforeSolution = Get-FileHash -LiteralPath $workspace.Solution
$beforeWorkspace = Get-FileHash -LiteralPath $workspacePath
$root = Join-Path $buildRoot ('Temp/UnifiedIdeFixture-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory((Join-Path $root 'Assets/Editor')) | Out-Null
[IO.File]::WriteAllText((Join-Path $root 'Assets/Probe.cs'), 'using Ursa; public sealed class Probe : Behaviour {}')
[IO.File]::WriteAllText((Join-Path $root 'Assets/Editor/Tool.cs'), 'public static class Tool {}')
& $Compiler $root --ide
if ($LASTEXITCODE) { throw 'Cannot generate the fixture script workspace.' }
$sourceProject = Join-Path $root 'Library/ScriptIDE/Projects/Game/Ursa.Game.csproj'
$sourceHash = (Get-FileHash $sourceProject).Hash
$solutionPath = & (Join-Path $engineRoot 'Tests/Editor/Scripting/GenerateUnifiedIdeFixture.ps1') -BuildDirectory $buildRoot -ProjectDirectory $root
if ((Get-FileHash $workspace.Solution).Hash -ne $beforeSolution.Hash -or (Get-FileHash $workspacePath).Hash -ne $beforeWorkspace.Hash) { throw 'Debugger fixture changed the main workspace.' }
if ((Get-FileHash $sourceProject).Hash -ne $sourceHash) { throw 'Debugger fixture changed the host IDE workspace.' }
$solution = [xml][IO.File]::ReadAllText($solutionPath)
foreach ($project in $solution.SelectNodes('//Project[@Path]')) {
    if (-not [IO.Path]::IsPathRooted($project.Path) -or -not (Test-Path -LiteralPath $project.Path)) { throw "Fixture project is not a valid absolute path: $($project.Path)" }
}
foreach ($group in @('Game/Ursa.Game', 'Editor/Ursa.EditorScripts')) {
    $path = Join-Path $root "Library/UnifiedIDE/Projects/$group.csproj"
    $project = [xml][IO.File]::ReadAllText($path)
    if ($project.SelectNodes('//Reference/HintPath').Count) { throw 'Fixture leaked binary framework references.' }
    foreach ($source in $project.SelectNodes('//Compile[@Include]')) {
        if (-not $source.Include.StartsWith((Join-Path $root 'Assets'), [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $source.Include)) { throw 'Fixture source is outside its real project Assets.' }
    }
    if (-not @($solution.SelectNodes('//Project[@Path]') | Where-Object { [string]::Equals([IO.Path]::GetFullPath($_.Path), $path, [StringComparison]::OrdinalIgnoreCase) }).Count) { throw 'Fixture C# project is missing from its solution.' }
}
Write-Host 'Independent solution, original source membership, framework project references and main-workspace isolation passed.'
