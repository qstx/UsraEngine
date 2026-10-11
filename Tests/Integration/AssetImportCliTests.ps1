param([string]$Importer, [string]$Player, [string]$PackageTool, [string]$Project, [string]$Source, [string]$Configuration)
$ErrorActionPreference = 'Stop'

function Invoke-Checked([string]$Executable, [string[]]$Arguments, [int]$Expected = 0) {
    $output = & $Executable @Arguments 2>&1
    $result = $LASTEXITCODE
    if ($result -ne $Expected) { throw "Unexpected exit $result from $Executable : $output" }
    return ($output -join "`n")
}

function Library-State {
    return (Get-ChildItem -LiteralPath "$Project/Library" -Recurse -File | Sort-Object FullName | ForEach-Object {
        "$($_.FullName)|$($_.Length)|$($_.LastWriteTimeUtc.Ticks)|$((Get-FileHash -LiteralPath $_.FullName).Hash)"
    }) -join "`n"
}

New-Item -ItemType Directory -Force -Path "$Project/Assets" | Out-Null
Get-ChildItem -LiteralPath "$Source/Assets" | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination "$Project/Assets" -Recurse -Force }
$importArgs = @('--project', $Project, '--configuration', $Configuration)
Invoke-Checked $Importer $importArgs | Out-Null
$warm = Invoke-Checked $Importer $importArgs
if ($warm -notmatch 'Editor: imported=0' -or $warm -notmatch 'Player: imported=0') { throw "Repeated import missed valid cache: $warm" }
Invoke-Checked $Importer ($importArgs + @('--scope', 'prj:Textures/UrsaEngine.png', '--force')) | Out-Null
$readyPath = "$Project/Library/RuntimeContent/WindowsVulkan/$Configuration/Player/Ready.json"
$ready = Get-Content -LiteralPath $readyPath -Raw
Invoke-Checked $Importer ($importArgs + @('--target','Unsupported')) 1 | Out-Null
if ((Get-Content -LiteralPath $readyPath -Raw) -ne $ready) { throw 'Failed target replaced complete content' }
Invoke-Checked $Importer @('--unknown') 2 | Out-Null
$lease = [System.IO.File]::Open("$Project/Library/Import.lock", [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
try {
    $busy = Invoke-Checked $Importer $importArgs 1
    if ($busy -notmatch 'busy|locked') { throw "Missing writer conflict diagnostic: $busy" }
} finally { $lease.Dispose() }

$before = Library-State
Invoke-Checked $Player @('--project', $Project, '--scene','Snake/Snake.ursascene','--hidden','--frames','3') | Out-Null
if ((Library-State) -ne $before) { throw 'Loose Player wrote project Library' }
$shader = "$Project/Assets/Materials/Gold.ursamat"
$original = [System.IO.File]::ReadAllBytes($shader)
try {
    [System.IO.File]::AppendAllText($shader, "`n ")
    $stale = Invoke-Checked $Player @('--project',$Project,'--hidden','--frames','1') 1
    if ($stale -notmatch 'UrsaAssetImport') { throw "Missing stale-content repair instruction: $stale" }
    $before = Library-State
    Invoke-Checked $Importer $importArgs | Out-Null
} finally { [System.IO.File]::WriteAllBytes($shader,$original) }
Invoke-Checked $Importer $importArgs | Out-Null

$entry = ($ready | ConvertFrom-Json).entries | Where-Object { $_.path -eq 'prj:Textures/UrsaEngine.ursatex' } | Select-Object -First 1
$artifact = Join-Path "$Project/Library" $entry.storage.Substring(4)
$resolved = [System.IO.Path]::GetFullPath($artifact)
$libraryRoot = [System.IO.Path]::GetFullPath("$Project/Library") + [System.IO.Path]::DirectorySeparatorChar
if (-not $resolved.StartsWith($libraryRoot, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test artifact deletion' }
Remove-Item -LiteralPath $resolved
Invoke-Checked $Player @('--project',$Project,'--hidden','--frames','1') 1 | Out-Null
Invoke-Checked $Importer $importArgs | Out-Null
if (-not (Test-Path -LiteralPath $resolved)) { throw 'Missing output did not recover' }

$package = "$Project/Content.pak"
Invoke-Checked $PackageTool @('--project-root',$Project,'--configuration',$Configuration,'--output',$package) | Out-Null
$firstHash = (Get-FileHash -LiteralPath $package).Hash
Invoke-Checked $PackageTool @('--project-root',$Project,'--configuration',$Configuration,'--output',$package) | Out-Null
if ((Get-FileHash -LiteralPath $package).Hash -ne $firstHash) { throw 'Current content packaging was not deterministic' }
$invalid = "$Project/Assets/Invalid.ursashader"
[System.IO.File]::WriteAllText($invalid,'invalid shader source')
try {
    Invoke-Checked $PackageTool @('--project-root',$Project,'--configuration',$Configuration,'--output',$package) 1 | Out-Null
    if ((Get-FileHash -LiteralPath $package).Hash -ne $firstHash) { throw 'Failed import replaced complete pak' }
} finally {
    Remove-Item -LiteralPath $invalid
    if (Test-Path -LiteralPath "$invalid.meta") { Remove-Item -LiteralPath "$invalid.meta" }
}
Invoke-Checked $Player @('--pak',$package,'--scene','Snake/Snake.ursascene','--hidden','--frames','3') | Out-Null
Write-Output 'CLI scope/cache/errors/writer lock/recovery, read-only Player and deterministic content build passed'
