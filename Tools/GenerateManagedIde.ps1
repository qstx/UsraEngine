[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$BuildRoot, [switch]$IncludeTests,
    [string]$RoslynDirectory = '', [string]$ProjectDirectory = '')
$ErrorActionPreference = 'Stop'
$EngineRoot = [IO.Path]::GetFullPath($EngineRoot).Replace('\','/')
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot).Replace('\','/')
$ideRoot = "$BuildRoot/Ide/Managed"
function Write-Changed([string]$Path, [string]$Text) {
    $Text = $Text.Replace("`r`n", "`n")
    if ((Test-Path -LiteralPath $Path) -and [IO.File]::ReadAllText($Path) -ceq $Text) { return }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    $temporary = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary, $Text, [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary, $Path, $true)
}
function Escape-Xml([string]$Text) { [Security.SecurityElement]::Escape($Text) }
$inputs = @(Get-ChildItem -LiteralPath "$EngineRoot/Managed" -Filter '*.csproj' -Recurse -File)
if ($IncludeTests) { $inputs += @(Get-ChildItem -LiteralPath "$EngineRoot/Tests/Managed" -Filter '*.csproj' -Recurse -File) }
$inputs = @($inputs | Where-Object { $_.FullName -notmatch '[/\\](bin|obj)[/\\]' } | Sort-Object FullName)
if (-not $ProjectDirectory -or -not (Test-Path -LiteralPath (Join-Path $ProjectDirectory 'Assets/Scripts/Snake/SnakeGame.cs'))) {
    $inputs = @($inputs | Where-Object { $_.BaseName -ne 'Ursa.SnakeLogicTests' })
}
$projects = @{}
foreach ($inputProject in $inputs) {
    $name = $inputProject.BaseName
    if ($projects.ContainsKey($name)) { throw "Duplicate managed project name: $name" }
    $digest = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes("Ursa/Managed/$name"))
    $guidBytes = [byte[]]$digest[0..15]
    $projects[$name] = [ordered]@{ name = $name; path = "$ideRoot/$name/$name.csproj"; guid = [guid]::new($guidBytes).ToString().ToUpperInvariant();
        folder = $(if ($inputProject.FullName.Replace('\','/').Contains('/Tests/')) { 'Tests/Managed' } else { 'Managed/Framework' });
        sourceProject = $inputProject.FullName.Replace('\','/') }
}
$escapedRoot = Escape-Xml $ideRoot
$escapedSource = Escape-Xml "$EngineRoot/Managed"
$props = @"
<Project>
  <PropertyGroup>
    <BaseIntermediateOutputPath>$escapedRoot/Build/`$(MSBuildProjectName)/`$(Configuration)/obj/</BaseIntermediateOutputPath>
    <OutputPath>$escapedRoot/Build/`$(MSBuildProjectName)/`$(Configuration)/bin/</OutputPath>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <UrsaBindingsSourceDir>$escapedSource</UrsaBindingsSourceDir>
    <UrsaBindingsGeneratedDir>$escapedRoot/Build/`$(MSBuildProjectName)/`$(Configuration)/Bindings</UrsaBindingsGeneratedDir>
    <UrsaBindingsGenerator>$escapedRoot/Build/Ursa.BindingsGenerator/`$(Configuration)/bin/Ursa.BindingsGenerator.dll</UrsaBindingsGenerator>
  </PropertyGroup>
</Project>
"@
Write-Changed "$ideRoot/Directory.Build.props" ($props + "`n")
Write-Changed "$ideRoot/Directory.Build.targets" ("<Project><Import Project=`"" + (Escape-Xml "$EngineRoot/Managed/Directory.Build.targets") + "`" /></Project>`n")
foreach ($entry in $projects.Values) {
    [xml]$xml = [IO.File]::ReadAllText($entry.sourceProject)
    $directory = [IO.Path]::GetDirectoryName($entry.sourceProject)
    if ($xml.OuterXml.Contains('$(UrsaProjectDir)')) {
        $group = $xml.CreateElement('PropertyGroup')
        $property = $xml.CreateElement('UrsaProjectDir')
        $property.InnerText = $ProjectDirectory
        $group.AppendChild($property) | Out-Null
        $xml.DocumentElement.AppendChild($group) | Out-Null
    }
    if ($RoslynDirectory) {
        foreach ($hint in $xml.SelectNodes('//Reference/HintPath')) {
            $hint.InnerText = $hint.InnerText.Replace('$(MSBuildSDKsPath)/../Roslyn/bincore', $RoslynDirectory)
        }
    }
    foreach ($reference in @($xml.SelectNodes('//ProjectReference[@Include]'))) {
        $referenceName = [IO.Path]::GetFileNameWithoutExtension($reference.GetAttribute('Include'))
        if (-not $projects.ContainsKey($referenceName)) { throw "Unknown project reference: $referenceName in $($entry.name)" }
        $reference.SetAttribute('Include', $projects[$referenceName].path)
    }
    foreach ($compile in @($xml.SelectNodes('//Compile[@Include]'))) {
        $include = $compile.GetAttribute('Include')
        if (-not $include.Contains('$(') -and -not [IO.Path]::IsPathRooted($include)) {
            $compile.SetAttribute('Include', [IO.Path]::GetFullPath((Join-Path $directory $include)).Replace('\','/'))
        }
    }
    $items = $xml.CreateElement('ItemGroup')
    foreach ($source in Get-ChildItem -LiteralPath $directory -Filter '*.cs' -Recurse -File | Where-Object { $_.FullName -notmatch '[/\\](bin|obj)[/\\]' } | Sort-Object FullName) {
        $compile = $xml.CreateElement('Compile')
        $compile.SetAttribute('Include', $source.FullName.Replace('\','/'))
        $compile.SetAttribute('Link', [IO.Path]::GetRelativePath($directory, $source.FullName).Replace('\','/'))
        $items.AppendChild($compile) | Out-Null
    }
    if ($entry.name -in @('Ursa.Runtime','Ursa.Rendering','Ursa.Editor','Ursa.ManagedHost')) {
        $reference = $xml.CreateElement('ProjectReference')
        $reference.SetAttribute('Include', $projects['Ursa.BindingsGenerator'].path)
        $reference.SetAttribute('ReferenceOutputAssembly', 'false')
        $items.AppendChild($reference) | Out-Null
    }
    $xml.DocumentElement.AppendChild($items) | Out-Null
    Write-Changed $entry.path ($xml.OuterXml + "`n")
}
Write-Changed "$ideRoot/Projects.json" (ConvertTo-Json -InputObject @($projects.Values | Sort-Object name) -Depth 5)
