param([Parameter(Mandatory)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$cache = [IO.File]::ReadAllText((Join-Path $buildRoot 'CMakeCache.txt'))
if ($cache -notmatch 'CMAKE_GENERATOR:INTERNAL=Visual Studio') { throw 'This check requires a Visual Studio generator' }
$manifest = [IO.File]::ReadAllText((Join-Path $buildRoot 'UrsaProjectStructure.json')) | ConvertFrom-Json
. (Join-Path $PSScriptRoot 'ReadVisualStudioSolution.ps1')
$projects = Get-UrsaSolutionProjects $buildRoot
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
$managedCount = 0
$managedManifest = Join-Path $buildRoot 'Ide/Managed/Projects.json'
if ($dotnetEnabled -and (Test-Path -LiteralPath $managedManifest)) {
    foreach ($entry in ([IO.File]::ReadAllText($managedManifest) | ConvertFrom-Json)) {
        if (-not $projects.ContainsKey($entry.name)) { throw "Missing managed VS project: $($entry.name)" }
        $project = $projects[$entry.name]
        if ($project.Folder -cne $entry.folder -or [IO.Path]::GetFullPath($project.Path) -ne [IO.Path]::GetFullPath($entry.path)) { throw "Wrong managed project mapping: $($entry.name)" }
        [xml]$xml = [IO.File]::ReadAllText($project.Path)
        foreach ($compile in $xml.SelectNodes('//Compile[@Include]')) {
            $include = $compile.GetAttribute('Include')
            if ($include.Contains('$(') -or $include.Contains('*')) { continue }
            if (-not [IO.Path]::IsPathRooted($include) -or -not (Test-Path -LiteralPath $include -PathType Leaf)) { throw "Managed source is not an existing original file: $include" }
            if ($include.StartsWith($buildRoot + '/', [StringComparison]::OrdinalIgnoreCase) -or $include.StartsWith($buildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Managed IDE source points into build output: $include" }
        }
        foreach ($reference in $xml.SelectNodes('//ProjectReference[@Include]')) {
            $name = [IO.Path]::GetFileNameWithoutExtension($reference.GetAttribute('Include'))
            if (-not $projects.ContainsKey($name)) { throw "Unregistered managed reference: $name" }
        }
        if ($project.Node -and -not $project.Node.SelectSingleNode('Platform[@Project="Any CPU"]')) { throw "Missing managed platform mapping: $($entry.name)" }
        ++$managedCount
    }
    $descriptionPath = Join-Path $buildRoot 'UnifiedWorkspace.json'
    if (Test-Path -LiteralPath $descriptionPath) {
        $description = [IO.File]::ReadAllText($descriptionPath) | ConvertFrom-Json
        if ($description.Ready) {
            foreach ($name in @('Ursa.Game','Ursa.EditorScripts')) {
                if (-not $projects.ContainsKey($name)) { throw "Missing project script VS project: $name" }
                [xml]$xml = [IO.File]::ReadAllText($projects[$name].Path)
                if ($xml.SelectNodes('//Reference').Count -or -not $xml.SelectNodes('//ProjectReference').Count) { throw "Unified script project still uses binary framework references: $name" }
                foreach ($compile in $xml.SelectNodes('//Compile[@Include]')) {
                    $source = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($projects[$name].Path)) $compile.GetAttribute('Include')))
                    $assetsRoot = [IO.Path]::GetFullPath((Join-Path $description.ProjectRoot 'Assets'))
                    if (-not $source.StartsWith($assetsRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $source)) { throw "Script project does not link an original asset: $source" }
                }
            }
        }
    }
}
Write-Output "Checked $($manifest.targets.Count) first-party targets, $checked C++ file entries and $managedCount SDK C# projects; source paths, folders and references match."
