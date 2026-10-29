param([Parameter(Mandatory)][string]$BuildDirectory, [Parameter(Mandatory)][string]$Compiler)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$fixture = Join-Path $buildRoot ('Temp/UnifiedRefresh-' + [guid]::NewGuid().ToString('N'))
$assets = Join-Path $fixture 'Assets'
[IO.Directory]::CreateDirectory($assets) | Out-Null
$description = [IO.File]::ReadAllText((Join-Path $buildRoot 'UnifiedWorkspace.json')) | ConvertFrom-Json
$description.ProjectRoot = $fixture.Replace('\','/')
$description.Ready = $true
$descriptorPath = Join-Path $fixture 'Workspace.json'
$description | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $descriptorPath -Encoding utf8
function Generate {
    & $Compiler $fixture --ide --workspace $descriptorPath
    if ($LASTEXITCODE -ne 0) { throw "Unified fixture generation failed: $LASTEXITCODE" }
}
function Expect([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
$source = Join-Path $assets 'Probe.cs'
[IO.File]::WriteAllText($source, 'class Probe : MissingType { this is invalid; }')
Generate
$gameProject = Join-Path $fixture 'Library/ScriptIDE/Projects/Game/Ursa.Game.csproj'
$editorProject = Join-Path $fixture 'Library/ScriptIDE/Projects/Editor/Ursa.EditorScripts.csproj'
Expect ([IO.File]::ReadAllText($gameProject).Contains('Probe.cs')) 'Invalid source was not included'
Expect (-not (Test-Path -LiteralPath (Join-Path $fixture 'Library/ScriptBuild'))) 'IDE generation published formal script output'
$timestamp = [IO.File]::GetLastWriteTimeUtc($gameProject)
$metadataHash = (Get-FileHash -LiteralPath ($source + '.meta')).Hash
Generate
Expect ([IO.File]::GetLastWriteTimeUtc($gameProject) -eq $timestamp) 'No-op generation rewrote the project'
$editorSource = Join-Path $assets 'Editor/Moved.cs'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($editorSource)) | Out-Null
Move-Item -LiteralPath $source -Destination $editorSource
Move-Item -LiteralPath ($source + '.meta') -Destination ($editorSource + '.meta')
Generate
Expect ((Get-FileHash -LiteralPath ($editorSource + '.meta')).Hash -eq $metadataHash) 'Cross-group movement changed script metadata'
Expect (-not [IO.File]::ReadAllText($gameProject).Contains('Probe.cs') -and [IO.File]::ReadAllText($editorProject).Contains('Moved.cs')) 'Cross-group movement left stale members'
Remove-Item -LiteralPath $editorSource
Remove-Item -LiteralPath ($editorSource + '.meta')
Generate
Expect (-not [IO.File]::ReadAllText($editorProject).Contains('Moved.cs')) 'Deleted source remained in the project'
$library = [IO.Path]::GetFullPath((Join-Path $fixture 'Library'))
if (-not $library.StartsWith([IO.Path]::GetFullPath($fixture) + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Library cleanup escaped fixture' }
Remove-Item -LiteralPath $library -Recurse -Force
Generate
Expect (Test-Path -LiteralPath (Join-Path $library 'ScriptIDE/UnifiedWorkspace.json')) 'Library reconstruction lost the unified association'
& $Compiler $fixture --ide --workspace (Join-Path $fixture 'MissingWorkspace.json')
Expect ($LASTEXITCODE -ne 0) 'Missing workspace descriptor was accepted'
Generate
Write-Output 'Fresh Library, error scripts, no-op refresh, cross-group movement, deletion and failure recovery passed.'
