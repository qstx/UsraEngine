[CmdletBinding()]
param(
    [string]$LegacyBuild = (Join-Path $PSScriptRoot '../Build/SrpLegacyPerformance'),
    [string]$SrpBuild = (Join-Path $PSScriptRoot '../Build/SrpPerformance'),
    [string]$LegacyProject = (Join-Path $PSScriptRoot '../Build/SrpLegacyProject'),
    [string]$SrpProject = (Join-Path $PSScriptRoot '../Build/SrpPerformanceProject'),
    [string]$OutputRoot = (Join-Path $PSScriptRoot '../Build/SrpPerformanceEvidence/Formal'),
    [ValidateRange(1, 20)][int]$Rounds = 3,
    [ValidateRange(240, 100000)][int]$Frames = 1320,
    [ValidateRange(1, 1000)][int]$Warmup = 120,
    [ValidateRange(20, 10000)][int]$Samples = 900
)

$ErrorActionPreference = 'Stop'
if ($Frames -le $Warmup + $Samples) { throw 'Frames must exceed warmup plus samples.' }
$python = Join-Path $PSScriptRoot 'Python/3.11.17/python.exe'
$captureTool = Join-Path $PSScriptRoot 'CaptureRenderingPerformance.py'
$outputPath = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $outputPath) {
    throw "Output already exists; choose a fresh evidence directory: $outputPath"
}
New-Item -ItemType Directory -Path $outputPath | Out-Null

$variants = @(
    @{ Name = 'Legacy'; Build = [IO.Path]::GetFullPath($LegacyBuild); Project = [IO.Path]::GetFullPath($LegacyProject) },
    @{ Name = 'SRP'; Build = [IO.Path]::GetFullPath($SrpBuild); Project = [IO.Path]::GetFullPath($SrpProject) }
)
$scenes = @(
    @{ Name = 'Snake'; Path = 'Snake/Snake.ursascene' },
    @{ Name = 'Materials'; Path = 'MaterialShowcase.ursascene' }
)

foreach ($round in 1..$Rounds) {
    foreach ($scene in $scenes) {
        foreach ($variant in $variants) {
            $player = Join-Path $variant.Build 'Release/UrsaScriptPlayer.exe'
            if (-not (Test-Path -LiteralPath $player) -or -not (Test-Path -LiteralPath (Join-Path $variant.Project 'Assets'))) {
                throw "Missing built Player or prepared project for $($variant.Name)."
            }
            $output = Join-Path $outputPath "$($variant.Name)-$($scene.Name)-$round"
            & $python $captureTool --player $player --project $variant.Project `
                --scene $scene.Path --frames $Frames --warmup $Warmup --samples $Samples --output $output
            if ($LASTEXITCODE -ne 0) {
                throw "Capture failed for $($variant.Name) / $($scene.Name), round $round. See $output."
            }
        }
    }
}

Write-Output "Completed $($Rounds * 4) sequential captures under $outputPath."
