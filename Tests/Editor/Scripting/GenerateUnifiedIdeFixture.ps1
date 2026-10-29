param([Parameter(Mandatory)][string]$BuildDirectory, [Parameter(Mandatory)][string]$ProjectDirectory)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$projectRoot = (Resolve-Path -LiteralPath $ProjectDirectory).Path
$workspace = [IO.File]::ReadAllText((Join-Path $buildRoot 'UnifiedWorkspace.json')) | ConvertFrom-Json
if (-not $workspace.Ready -or -not $workspace.Solution.EndsWith('.slnx')) { throw 'Generate a ready CMake unified workspace before creating the debugger fixture.' }
$sourceRoot = Join-Path $projectRoot 'Library/ScriptIDE'
$fixtureRoot = Join-Path $projectRoot 'Library/UnifiedIDE'
$projects = @{
    'Ursa.Game' = Join-Path $fixtureRoot 'Projects/Game/Ursa.Game.csproj'
    'Ursa.EditorScripts' = Join-Path $fixtureRoot 'Projects/Editor/Ursa.EditorScripts.csproj'
}
foreach ($name in $projects.Keys) {
    $group = if ($name -eq 'Ursa.Game') { 'Game' } else { 'Editor' }
    $sourceProject = Join-Path $sourceRoot "Projects/$group/$name.csproj"
    $sourceDirectory = [IO.Path]::GetDirectoryName($sourceProject)
    $xml = [xml][IO.File]::ReadAllText($sourceProject)
    foreach ($compile in $xml.SelectNodes('//Compile[@Include]')) {
        $source = [IO.Path]::GetFullPath((Join-Path $sourceDirectory $compile.GetAttribute('Include')))
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Fixture source is missing: $source" }
        $compile.SetAttribute('Include', $source)
    }
    foreach ($reference in @($xml.SelectNodes('//Reference[@Include]'))) {
        $assembly = $reference.GetAttribute('Include')
        $path = if ($projects.ContainsKey($assembly)) { $projects[$assembly] } else { Join-Path $workspace.ManagedProjects "$assembly/$assembly.csproj" }
        if (-not $projects.ContainsKey($assembly) -and -not (Test-Path -LiteralPath $path)) { throw "Unknown fixture framework: $assembly" }
        $replacement = $xml.CreateElement('ProjectReference')
        $replacement.SetAttribute('Include', $path)
        [void]$reference.ParentNode.ReplaceChild($replacement, $reference)
    }
    foreach ($reference in $xml.SelectNodes('//ProjectReference[@Include]')) {
        $assembly = [IO.Path]::GetFileNameWithoutExtension($reference.GetAttribute('Include'))
        $path = if ($projects.ContainsKey($assembly)) { $projects[$assembly] } else { Join-Path $workspace.ManagedProjects "$assembly/$assembly.csproj" }
        $reference.SetAttribute('Include', $path)
    }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($projects[$name])) | Out-Null
    [IO.File]::WriteAllText($projects[$name], $xml.OuterXml + "`n", [Text.UTF8Encoding]::new($false))
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'Directory.Build.props') -Destination $fixtureRoot -Force
$solution = [xml][IO.File]::ReadAllText($workspace.Solution)
foreach ($node in $solution.SelectNodes('//Project[@Path]|//BuildDependency[@Project]')) {
    $attribute = if ($node.Name -eq 'Project') { 'Path' } else { 'Project' }
    $path = $node.GetAttribute($attribute)
    $assembly = [IO.Path]::GetFileNameWithoutExtension($path)
    if ($projects.ContainsKey($assembly)) { $path = $projects[$assembly] }
    elseif (-not [IO.Path]::IsPathRooted($path)) { $path = [IO.Path]::GetFullPath((Join-Path $buildRoot $path)) }
    $node.SetAttribute($attribute, $path.Replace('\','/'))
}
foreach ($folder in $solution.SelectNodes('//Folder[@Name]')) {
    if ($folder.GetAttribute('Name').StartsWith('/Projects/') -and $folder.GetAttribute('Name') -ne '/Projects/') { $folder.SetAttribute('Name', '/Projects/Acceptance/') }
}
$solutionPath = Join-Path $fixtureRoot 'UrsaEngine.slnx'
[IO.File]::WriteAllText($solutionPath, $solution.OuterXml + "`n", [Text.UTF8Encoding]::new($false))
$solutionPath
