param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$EvidenceRoot
)

$ErrorActionPreference = 'Stop'
foreach ($previewVariant in @('Loose', 'Pak')) {
    $previewProject = Join-Path (Join-Path $EvidenceRoot $previewVariant) ([guid]::NewGuid().ToString())
    foreach ($previewMode in 0..3) {
        # Separate processes exercise actual persistence and keep each .NET host independent.
        & $Executable $previewProject $previewVariant $previewMode
        if ($LASTEXITCODE -ne 0) {
            throw "$previewVariant application restart mode $previewMode exited $LASTEXITCODE"
        }
    }
}
