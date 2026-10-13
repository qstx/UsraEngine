param(
    [string]$BuildDir,
    [string]$ProjectDir,
    [string]$OutputDir,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [switch]$ManifestOnly,
    [ValidateSet('SnakePlayer', 'MaterialsPlayer', 'SnakeScene', 'SnakeGame', 'MaterialsScene', 'MaterialsGame', 'GoldPreview')]
    [string[]]$CaptureNames
)

$ErrorActionPreference = 'Stop'
$engineRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $BuildDir) { $BuildDir = Join-Path $engineRoot 'Build' }
if (-not $ProjectDir) { $ProjectDir = Join-Path $engineRoot '../Demo' }
if (-not $OutputDir) { $OutputDir = Join-Path $BuildDir ('SrpBaseline/' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
if ((Test-Path -LiteralPath $OutputDir) -and -not $ManifestOnly) { throw 'Choose a new output directory to preserve earlier evidence.' }
if (Test-Path -LiteralPath (Join-Path $OutputDir 'Manifest.json')) { throw 'A manifest already exists; choose a new evidence directory.' }
$playerPath = Join-Path $BuildDir "$Configuration/UrsaScriptPlayer.exe"
$editorPath = Join-Path $BuildDir "Editor/$Configuration/UrsaEditor.exe"
foreach ($executable in @($playerPath, $editorPath)) {
    if (-not (Test-Path -LiteralPath $executable)) { throw "Missing executable: $executable" }
}
if (-not $ManifestOnly) { New-Item -ItemType Directory -Path $OutputDir | Out-Null }
$previousUserRoot = $env:URSA_USER_DATA_ROOT
$captures = @(
    @{ Name = 'SnakePlayer'; Exe = $playerPath; Args = @('--scene', 'Snake/Snake.ursascene') },
    @{ Name = 'MaterialsPlayer'; Exe = $playerPath; Args = @('--scene', 'MaterialShowcase.ursascene') },
    @{ Name = 'SnakeScene'; Exe = $editorPath; Args = @('--scene', 'Snake/Snake.ursascene', '--play', '--view', 'Scene') },
    @{ Name = 'SnakeGame'; Exe = $editorPath; Args = @('--scene', 'Snake/Snake.ursascene', '--play', '--view', 'Game') },
    @{ Name = 'MaterialsScene'; Exe = $editorPath; Args = @('--scene', 'MaterialShowcase.ursascene', '--play', '--view', 'Scene') },
    @{ Name = 'MaterialsGame'; Exe = $editorPath; Args = @('--scene', 'MaterialShowcase.ursascene', '--play', '--view', 'Game') },
    @{ Name = 'GoldPreview'; Exe = $editorPath; Args = @('--asset', 'Materials/Gold.ursamat', '--view', 'Material Preview') }
)
$records = @()
if ($CaptureNames) { $captures = @($captures | Where-Object { $_.Name -in $CaptureNames }) }
try {
    foreach ($capture in $captures) {
        $env:URSA_USER_DATA_ROOT = Join-Path $OutputDir ('User/' + $capture.Name)
        $png = Join-Path $OutputDir ($capture.Name + '.png')
        $arguments = @('--project', $ProjectDir) + $capture.Args + @('--hidden', '--frames', '120', '--capture', $png, '--capture-frame', '120')
        $quotedArguments = $arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }
        if (-not $ManifestOnly) {
            $process = Start-Process -FilePath $capture.Exe -ArgumentList $quotedArguments -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput (Join-Path $OutputDir ($capture.Name + '.out.log')) `
                -RedirectStandardError (Join-Path $OutputDir ($capture.Name + '.log'))
            $null = $process.Handle
            # Wait for the application itself, not the shared .NET compiler server it may start.
            if (-not $process.WaitForExit(180000)) {
                $process.Kill()
                $process.WaitForExit()
                throw "$($capture.Name) timed out. Inspect its log."
            }
            $process.Refresh()
            if ($process.ExitCode -ne 0) { throw "$($capture.Name) exited with $($process.ExitCode). Inspect its log." }
        }
        if (-not (Test-Path -LiteralPath $png)) { throw "Missing capture: $png" }
        $records += [ordered]@{
            name = $capture.Name
            executable = $capture.Exe
            arguments = $arguments
            userRoot = $env:URSA_USER_DATA_ROOT
            imageSha256 = (Get-FileHash -LiteralPath $png -Algorithm SHA256).Hash
            imageCreatedAtUtc = (Get-Item -LiteralPath $png).LastWriteTimeUtc.ToString('o')
        }
        Write-Host "Captured $($capture.Name)"
    }
    Write-Host 'Recording asset identities'
    $assets = foreach ($sourceRoot in @((Join-Path $ProjectDir 'Assets'), (Join-Path $engineRoot 'Assets'))) {
        foreach ($meta in (Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Filter '*.meta' | Sort-Object FullName)) {
            $metadata = Get-Content -LiteralPath $meta.FullName -Raw | ConvertFrom-Json
            if ($metadata.folder) { continue }
            $source = $meta.FullName.Substring(0, $meta.FullName.Length - 5)
            [ordered]@{
                path = $source
                guid = $metadata.guid
                sourceSha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
                metaSha256 = (Get-FileHash -LiteralPath $meta.FullName -Algorithm SHA256).Hash
            }
        }
    }
    Write-Host 'Recording build identity'
    $manifest = [ordered]@{
        recordedAtUtc = [DateTime]::UtcNow.ToString('o')
        configuration = $Configuration
        engineRevision = ((& git -C $engineRoot rev-parse HEAD).Trim())
        projectRevision = ((& git -C $ProjectDir rev-parse HEAD).Trim())
        engineStatus = @(& git -C $engineRoot status --short | ForEach-Object { $_.ToString().TrimEnd() })
        # Remove Get-Content's extended properties before Windows PowerShell 5 JSON serialization.
        cmakeOptions = @(Get-Content -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') | Where-Object { $_ -match '^(URSA_|CMAKE_GENERATOR:)' } | ForEach-Object { $_.ToString().Trim() })
        binaries = @($playerPath, $editorPath) | ForEach-Object { @{ path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash } }
        captureFrame = 120
        playerResolution = @(1024, 720)
        editorResolution = @(1280, 720)
        vsync = 'ON / Vulkan FIFO'
        performanceWarmupFrames = 120
        performanceSampleFrames = 900
        performanceRepeats = 3
        captures = $records
        assets = @($assets)
    }
    Write-Host 'Writing manifest'
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDir 'Manifest.json') -Encoding UTF8
    Write-Host "Evidence: $OutputDir"
} finally {
    $env:URSA_USER_DATA_ROOT = $previousUserRoot
}
