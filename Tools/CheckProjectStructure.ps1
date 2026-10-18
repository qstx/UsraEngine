param([Parameter(Mandatory)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$cache = [IO.File]::ReadAllText((Join-Path $buildRoot 'CMakeCache.txt'))
if ($cache -notmatch 'CMAKE_GENERATOR:INTERNAL=Visual Studio') { throw 'This check requires a Visual Studio generator' }
$manifest = [IO.File]::ReadAllText((Join-Path $buildRoot 'UrsaProjectStructure.json')) | ConvertFrom-Json
$solution = [IO.File]::ReadAllText((Join-Path $buildRoot 'UrsaEngine.sln'))
$entries = @{}
$parents = @{}
foreach ($match in [regex]::Matches($solution, 'Project\("\{([^}]+)\}"\) = "([^"]+)", "([^"]+)", "\{([^}]+)\}"')) {
    $entries[$match.Groups[4].Value] = @{ Type = $match.Groups[1].Value; Name = $match.Groups[2].Value; Path = $match.Groups[3].Value }
}
$nested = [regex]::Match($solution, '(?s)GlobalSection\(NestedProjects\).*?EndGlobalSection').Value
foreach ($match in [regex]::Matches($nested, '\{([^}]+)\} = \{([^}]+)\}')) {
    $parents[$match.Groups[1].Value] = $match.Groups[2].Value
}
$projects = @{}
foreach ($id in @($entries.Keys)) {
    $entry = $entries[$id]
    if ($entry.Type -eq '2150E333-8FDC-42A3-9474-1A3956D46DE8') { continue }
    $parts = @()
    $cursor = $id
    while ($parents.ContainsKey($cursor)) {
        $cursor = $parents[$cursor]
        $parts = @($entries[$cursor].Name) + $parts
    }
    $projects[$entry.Name] = @{ Path = Join-Path $buildRoot $entry.Path; Folder = $parts -join '/' }
}
$registered = @{}
$checked = 0
foreach ($target in $manifest.targets) {
    if (-not $projects.ContainsKey($target.name)) { throw "Missing VS project: $($target.name)" }
    $project = $projects[$target.name]
    if ($project.Folder -cne $target.folder) { throw "Wrong solution folder for $($target.name): $($project.Folder)" }
    [xml]$xml = [IO.File]::ReadAllText($project.Path)
    [xml]$filters = [IO.File]::ReadAllText($project.Path + '.filters')
    $filterMap = @{}
    foreach ($node in $filters.SelectNodes('//*[@Include]')) {
        $filterMap[[string]$node.GetAttribute('Include')] = [string]$node.Filter
    }
    foreach ($node in $xml.SelectNodes("//*[(local-name()='ClCompile' or local-name()='ClInclude') and @Include]")) {
        $path = [string]$node.GetAttribute('Include')
        if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path ([IO.Path]::GetDirectoryName($project.Path)) $path }
        $path = [IO.Path]::GetFullPath($path)
        if (-not $path.StartsWith($sourceRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($path.StartsWith($sourceRoot + '\ThirdParty\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing source on disk: $path" }
        $relative = $path.Substring($sourceRoot.Length + 1)
        $expectedFilter = [IO.Path]::GetDirectoryName($relative)
        $actualFilter = $filterMap[[string]$node.GetAttribute('Include')]
        if ($actualFilter -cne $expectedFilter) { throw "Wrong filter for $relative : $actualFilter (expected $expectedFilter)" }
        if ($node.LocalName -eq 'ClCompile' -and $registered.ContainsKey($path)) { throw "Source compiled in multiple targets: $relative" }
        $registered[$path] = $target.name
        ++$checked
    }
}
$editorEnabled = $cache -match 'URSA_BUILD_EDITOR:BOOL=ON'
$dotnetEnabled = $cache -match 'URSA_ENABLE_DOTNET:BOOL=ON'
$testsEnabled = $cache -match 'URSA_BUILD_TESTS:BOOL=ON'
foreach ($directory in @('Runtime', 'Engine', 'Editor', 'Tools', 'Tests')) {
    if ($directory -eq 'Editor' -and -not $editorEnabled) { continue }
    if ($directory -eq 'Tests' -and -not $testsEnabled) { continue }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $sourceRoot $directory) -Recurse -File) {
        if ($file.Extension -notin @('.cpp','.hpp','.h','.inl')) { continue }
        $relative = $file.FullName.Substring($sourceRoot.Length + 1).Replace('\','/')
        # Bundled tool runtimes carry dependency headers, not engine translation units.
        if ($relative.StartsWith('Tools/Python/') -or $relative.StartsWith('Tools/TracyMcp/Release/')) { continue }
        if (-not $dotnetEnabled -and $relative -match '^Engine/Scripting/DotNet/') { continue }
        if (-not $editorEnabled -and $relative -match '^Tools/(AssetImport|AssetMigrate|MeshMigrate|ScriptCompile|ScriptPlayer)/') { continue }
        if (-not $dotnetEnabled -and $relative -match '^Tools/(ScriptCompile|ScriptPlayer)/') { continue }
        if ((-not $editorEnabled -or -not $dotnetEnabled) -and $relative.StartsWith('Tools/IdeBridge/')) { continue }
        if ($relative.StartsWith('Tests/')) {
			if (-not $dotnetEnabled -and $relative.StartsWith('Tests/Bindings/')) { continue }
            if (-not $editorEnabled -and $relative.StartsWith('Tests/Editor/')) { continue }
            if (-not $editorEnabled -and $relative -eq 'Tests/Headers/MaterialInspectorHeaderSmoke.cpp') { continue }
            if (-not $dotnetEnabled -and $relative -match '^Tests/Editor/Scripting/') { continue }
            if ($relative.StartsWith('Tests/Integration/')) {
                if ($relative -match '^Tests/Integration/(DotNetHostTests.cpp$|DotNetHost/)') { if (-not $dotnetEnabled) { continue } }
                elseif (-not $dotnetEnabled -or -not $editorEnabled -or $cache -match 'URSA_PROJECT_DIR:PATH=\r?\n') { continue }
            }
        }
        if (-not $registered.ContainsKey($file.FullName)) { throw "Unregistered source: $relative" }
    }
}
if (-not $testsEnabled) {
    if (@($projects.Keys | Where-Object { $_ -match '(Tests|Smoke|Benchmarks)$' -or $_ -eq 'UrsaManagedTestAssemblies' }).Count) {
        throw 'Test targets leaked into a production-only build'
    }
}
foreach ($name in @('EASTL','TracyClient','glfw','spdlog','assimp','zlibstatic','UrsaImGui')) {
    if ($projects.ContainsKey($name) -and -not $projects[$name].Folder.StartsWith('ThirdParty')) {
        throw "Dependency is misclassified: $name"
    }
}
Write-Output "Checked $($manifest.targets.Count) first-party projects and $checked C++ file entries; folders and filters match disk."
