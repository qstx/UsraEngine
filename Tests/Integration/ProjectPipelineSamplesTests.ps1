param([string]$Importer, [string]$Player, [string]$Editor, [string]$PackageTool, [string]$Source,
      [string]$OutputRoot, [string]$Configuration)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        # Windows PowerShell treats native stderr as ErrorRecord, including ordinary logs.
        $ErrorActionPreference = 'Continue'
        $output = & $Executable @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    $output = @($output | ForEach-Object { $_.ToString() })
    $output | Out-File -LiteralPath "$project/Execution.log" -Append -Encoding utf8
    if ($exitCode -ne 0) {
        throw "Exit $exitCode from $Executable (see $project/Execution.log): $($output | Select-Object -First 10)"
    }
}

function Read-AssetLocalId([string]$Path) {
    # Windows PowerShell's JSON reader rounds UInt64 values above Int64.MaxValue through double.
    $text = Get-Content -LiteralPath $Path -Raw
    $match = [Regex]::Match($text, '"localFileId"\s*:\s*(\d+)')
    if (-not $match.Success) { throw "Missing asset local ID in $Path" }
    return [UInt64]$match.Groups[1].Value
}

# Each run owns its project and imported content; an open authoring project is unaffected.
$project = Join-Path $OutputRoot ([Guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path "$project/ProjectSettings" -Force | Out-Null
Copy-Item -LiteralPath "$Source/Assets" -Destination $project -Recurse
$savedUserRoot = $env:URSA_USER_DATA_ROOT
$env:URSA_USER_DATA_ROOT = "$project/UserData"
try {
    $assetLocalId = Read-AssetLocalId "$project/Assets/Rendering/ShowcasePipeline.ursaasset"
    $metadata = Get-Content -LiteralPath "$project/Assets/Rendering/ShowcasePipeline.ursaasset.meta" -Raw | ConvertFrom-Json
    $settings = [ordered]@{format='UrsaGraphicsSettings';version=1;renderPipeline=$null}
    foreach ($mode in @('Default', 'Composite')) {
        if ($mode -eq 'Composite') {
            $settings.renderPipeline = @{guid=$metadata.guid;localFileId=$assetLocalId}
        }
        $settings | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$project/ProjectSettings/Graphics.json" -Encoding utf8
        Invoke-Checked $Importer @('--project', $project, '--configuration', $Configuration)
        Invoke-Checked $Player @('--project', $project, '--scene', 'MaterialShowcase.ursascene',
            '--hidden', '--frames', '8', '--capture', "$project/$mode.png", '--capture-frame', '5')
    }

    $baseline = [System.Drawing.Bitmap]::new("$project/Default.png")
    $composite = [System.Drawing.Bitmap]::new("$project/Composite.png")
    try {
        if ($baseline.Width -ne 1024 -or $baseline.Height -ne 720 -or
            $composite.Size -ne $baseline.Size) { throw 'Unexpected Player target dimensions' }
        $border = $composite.GetPixel(2, 2)
        if ([Math]::Abs($border.R - 25) -gt 1 -or [Math]::Abs($border.G - 204) -gt 1 -or
            [Math]::Abs($border.B - 76) -gt 1) { throw 'Project fullscreen shader did not produce its green frame' }
        # Checker, ceramic, gold, floor and a top face must all survive the pipeline transition.
        $points = @(@(290,350), @(512,350), @(732,350), @(512,475), @(512,283))
        foreach ($point in $points) {
            $a = $baseline.GetPixel($point[0], $point[1])
            $b = $composite.GetPixel($point[0], $point[1])
            if (($a.R + $a.G + $a.B) -lt 100) { throw "Default Forward omitted showcase geometry at $point" }
            if ([Math]::Abs($b.R - $a.R * 0.9) -gt 2 -or [Math]::Abs($b.G - $a.G) -gt 2 -or
                [Math]::Abs($b.B - $a.B * 0.85) -gt 2) { throw "Offscreen composition changed geometry/orientation at $point" }
        }
    } finally { $baseline.Dispose(); $composite.Dispose() }

    if ($Editor) {
        Invoke-Checked $Editor @('--project', $project, '--scene', 'MaterialShowcase.ursascene', '--play',
            '--hidden', '--frames', '80', '--capture', "$project/Editor.png", '--capture-frame', '70')
        $editorImage = [System.Drawing.Bitmap]::new("$project/Editor.png")
        try {
            $green = 0
            for ($y = 0; $y -lt $editorImage.Height; $y += 2) {
                for ($x = 0; $x -lt $editorImage.Width; $x += 2) {
                    $pixel = $editorImage.GetPixel($x, $y)
                    if ([Math]::Abs($pixel.R - 25) -le 1 -and [Math]::Abs($pixel.G - 204) -le 1 -and
                        [Math]::Abs($pixel.B - 76) -le 1) { ++$green }
                }
            }
            if ($green -lt 100) { throw 'Editor did not display the project pipeline composite' }
        } finally { $editorImage.Dispose() }
    }

    $computeAssetLocalId = Read-AssetLocalId "$project/Assets/Rendering/ComputeShowcasePipeline.ursaasset"
    $computeMetadata = Get-Content -LiteralPath "$project/Assets/Rendering/ComputeShowcasePipeline.ursaasset.meta" -Raw | ConvertFrom-Json
    $settings.renderPipeline = @{guid=$computeMetadata.guid;localFileId=$computeAssetLocalId}
    $settings | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$project/ProjectSettings/Graphics.json" -Encoding utf8
    Invoke-Checked $Importer @('--project', $project, '--configuration', $Configuration)
    Invoke-Checked $Player @('--project', $project, '--scene', 'MaterialShowcase.ursascene',
        '--hidden', '--frames', '8', '--capture', "$project/Compute.png", '--capture-frame', '5')
    $computed = [System.Drawing.Bitmap]::new("$project/Compute.png")
    try {
        foreach ($x in @(290,512,732)) {
            $pixel = $computed.GetPixel($x, 350)
            if ($pixel.B -le $pixel.R -or $pixel.B -le $pixel.G) { throw 'RendererList replacement material was not applied' }
        }
        if ($PackageTool) {
            Invoke-Checked $PackageTool @('--project-root', $project, '--configuration', $Configuration,
                '--output', "$project/Content.pak")
            Invoke-Checked $Player @('--pak', "$project/Content.pak", '--scene', 'MaterialShowcase.ursascene',
                '--hidden', '--frames', '8', '--capture', "$project/ComputePak.png", '--capture-frame', '5')
            $packed = [System.Drawing.Bitmap]::new("$project/ComputePak.png")
            try {
                foreach ($point in $points) {
                    if ($computed.GetPixel($point[0],$point[1]) -ne $packed.GetPixel($point[0],$point[1])) {
                        throw "Loose and Pak compute output differs at $point"
                    }
                }
            } finally { $packed.Dispose() }
        }
    } finally { $computed.Dispose() }
    Write-Output "Demo pipeline pixels passed; evidence: $project"
} finally { $env:URSA_USER_DATA_ROOT = $savedUserRoot }
