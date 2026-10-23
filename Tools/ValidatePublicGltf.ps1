param(
    [string]$ModelPath
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$validator = Join-Path $repository 'Build\Debug\UrsaModelImportCandidateTests.exe'
$assetImporter = Join-Path $repository 'Build\Editor\Debug\UrsaAssetImport.exe'
if (-not (Test-Path -LiteralPath $validator)) {
    throw "Build the validator first: cmake --build Build --target UrsaModelImportCandidateTests --config Debug"
}
if (-not (Test-Path -LiteralPath $assetImporter)) {
    throw "Build UrsaAssetImport first: cmake --build Build --target UrsaAssetImport --config Debug"
}

if (-not $ModelPath) {
    # KhronosGroup/glTF-Sample-Assets, Models/RiggedSimple; CC BY 4.0 (Cesium).
    $sampleDirectory = Join-Path $repository 'Build\ExternalSamples'
    New-Item -ItemType Directory -Force -Path $sampleDirectory | Out-Null
    $ModelPath = Join-Path $sampleDirectory 'RiggedSimple.glb'
    if (-not (Test-Path -LiteralPath $ModelPath)) {
        $url = 'https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/RiggedSimple/glTF-Binary/RiggedSimple.glb'
        Invoke-WebRequest -Uri $url -OutFile $ModelPath
    }
}

$resolvedModelPath = (Resolve-Path -LiteralPath $ModelPath).Path
if ([System.IO.Path]::GetExtension($resolvedModelPath) -ine '.glb') {
    throw 'This smoke validator expects a self-contained .glb file with mesh, skin, and animation content.'
}
if ((Get-Item -LiteralPath $resolvedModelPath).Length -eq 0) {
    throw "The glTF model is empty: $resolvedModelPath"
}
Write-Output "Validating $resolvedModelPath (SHA-256 $((Get-FileHash -LiteralPath $resolvedModelPath -Algorithm SHA256).Hash))"

& $validator --external-glb $resolvedModelPath
if ($LASTEXITCODE -ne 0) {
    throw "Public glTF validation failed with exit code $LASTEXITCODE."
}

$project = Join-Path $repository 'Build\ExternalSamples\RiggedSimpleProject'
$projectAssets = Join-Path $project 'Assets'
$projectSettings = Join-Path $project 'ProjectSettings'
$projectLibrary = Join-Path $project 'Library'
New-Item -ItemType Directory -Force -Path $projectAssets, $projectSettings | Out-Null
Copy-Item -LiteralPath $resolvedModelPath -Destination (Join-Path $projectAssets 'RiggedSimple.glb') -Force
$rules = '{"format":"UrsaManagedImportRules","version":1,"required":[],"sources":[]}'
Set-Content -LiteralPath (Join-Path $projectSettings 'ManagedImportRules.json') -Value $rules -Encoding utf8 -NoNewline

# Keep shader compiler scratch files out of the restricted Windows sandbox Temp mount.
$compilerTemp = Join-Path $repository 'Build\Temp\ShaderCompiler'
New-Item -ItemType Directory -Force -Path $compilerTemp | Out-Null
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
try {
    $env:TEMP = $compilerTemp
    $env:TMP = $compilerTemp
    & $assetImporter --project $project --engine (Join-Path $repository 'Assets') --configuration Debug --profile Editor --no-scripts
    if ($LASTEXITCODE -ne 0) {
        throw "UrsaAssetImport failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
}

$databasePath = Join-Path $projectLibrary 'AssetDatabase\prj.json'
if (-not (Test-Path -LiteralPath $databasePath)) {
    throw "UrsaAssetImport did not publish a project asset database: $databasePath"
}
$database = Get-Content -LiteralPath $databasePath -Raw | ConvertFrom-Json
$modelImport = $database.imports | Where-Object { $_.artifacts.outputKey -contains 'main' -and $_.importer -eq 'ursa.Model' } | Select-Object -Last 1
if (-not $modelImport -or $modelImport.target -ne 'model-v9' -or
    @($modelImport.artifacts | Where-Object { $_.kind -eq 'Mesh' }).Count -eq 0 -or
    @($modelImport.artifacts | Where-Object { $_.kind -eq 'Skeleton' }).Count -eq 0 -or
    @($modelImport.artifacts | Where-Object { $_.kind -eq 'AnimationClip' }).Count -eq 0) {
    throw 'UrsaAssetImport did not publish the public sample model, mesh, skeleton, and animation artifacts.'
}
Write-Output "UrsaAssetImport published model-v9 with $(@($modelImport.artifacts).Count) sub-assets to $projectLibrary"
