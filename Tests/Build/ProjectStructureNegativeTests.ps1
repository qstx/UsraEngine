param([Parameter(Mandatory)][string]$BuildDir, [Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$Scratch)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$scratchRoot = [IO.Path]::GetFullPath($Scratch)
New-Item -ItemType Directory -Path $scratchRoot -Force | Out-Null
foreach ($filename in @('CMakeCache.txt','UrsaEngine.sln','UrsaProjectStructure.json')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot $filename) -Destination (Join-Path $scratchRoot $filename) -Force
}
$solution = [IO.File]::ReadAllText((Join-Path $buildRoot 'UrsaEngine.sln'))
foreach ($match in [regex]::Matches($solution, 'Project\("\{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942\}"\) = "[^"]+", "([^"]+\.vcxproj)"')) {
    foreach ($suffix in @('', '.filters')) {
        $source = Join-Path $buildRoot ($match.Groups[1].Value + $suffix)
        if (-not (Test-Path -LiteralPath $source)) { continue }
        $target = [IO.Path]::GetFullPath((Join-Path $scratchRoot ($match.Groups[1].Value + $suffix)))
        if (-not $target.StartsWith($scratchRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Negative fixture escaped scratch root' }
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $target -Force
    }
}
$filterPath = Join-Path $scratchRoot 'Runtime/UrsaRuntime.vcxproj.filters'
$text = [IO.File]::ReadAllText($filterPath)
$broken = $text.Replace('<Filter>Runtime\Core</Filter>', '<Filter>WrongDirectory</Filter>')
if ($broken -ceq $text) { throw 'No filter was changed in the negative fixture' }
[IO.File]::WriteAllText($filterPath, $broken, [Text.UTF8Encoding]::new($false))
$detected = $false
try {
    & (Join-Path $EngineRoot 'Tools/CheckProjectStructure.ps1') -BuildDir $scratchRoot
} catch {
    if ($_.Exception.Message -notmatch '^Wrong filter') { throw }
    $detected = $true
}
if (-not $detected) { throw 'Structure check accepted a filter that does not match disk' }
Write-Output 'Structure check correctly rejected an incorrect filter in an isolated generated-project copy.'
