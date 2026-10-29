param([Parameter(Mandatory)][string]$BuildDirectory, [Parameter(Mandatory)][string]$VisualStudioInstallDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AcceptanceProcess.ps1')
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$vsRoot = (Resolve-Path -LiteralPath $VisualStudioInstallDirectory).Path
$description = [IO.File]::ReadAllText((Join-Path $buildRoot 'UnifiedWorkspace.json')) | ConvertFrom-Json
if (-not $description.Ready) { throw 'Generate a complete unified workspace before IDE acceptance.' }
$acceptanceRoot = Join-Path $buildRoot 'IdeAcceptance'
$controllerRoot = Join-Path $acceptanceRoot 'VisualStudioController'
New-Item -ItemType Directory -Force -Path $acceptanceRoot | Out-Null
& dotnet build (Join-Path $PSScriptRoot 'VisualStudioAcceptance/VisualStudioAcceptance.csproj') -c Release --nologo --disable-build-servers -m:1 -nr:false "-p:VisualStudioInstallDirectory=$vsRoot" "-p:UrsaAcceptanceBuildRoot=$controllerRoot"
if ($LASTEXITCODE -ne 0) { throw 'Cannot build Visual Studio acceptance controller.' }
$controller = Join-Path $controllerRoot 'bin/Release/net10.0-windows/VisualStudioAcceptance.dll'
$solution = $description.Solution
$game = Join-Path $description.ProjectRoot 'Assets/Scripts/Snake/SnakeController.cs'
$editor = Join-Path $description.ProjectRoot 'Assets/Scripts/Editor/SnakeEditor.cs'
$framework = Join-Path $description.EngineRoot 'Managed/Ursa.Runtime/Transform.cs'
$sources = @($game, $editor, $framework)
$hashes = @{}
foreach ($source in $sources) { $hashes[$source] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash }
function Invoke-Controller([string]$Action, [string[]]$Extra = @()) {
    for ($attempt = 0; $attempt -lt 40; ++$attempt) {
        $errors = Join-Path $acceptanceRoot 'language-controller-error.log'
        $output = & dotnet $controller $Action $solution @Extra 2> $errors
        if ($LASTEXITCODE -eq 0) { return (($output -join "`n") | ConvertFrom-Json) }
        $text = [IO.File]::ReadAllText($errors)
        $languageStillLoading = $attempt -lt 3 -and $text -match 'IDE did not complete member|IDE did not navigate'
        if (-not $languageStillLoading -and $text -notmatch 'RPC_E_CALL_REJECTED|RPC_E_SERVERCALL_RETRYLATER|0x80010001|0x8001010A|No Visual Studio instance') { throw $text }
        Start-Sleep -Milliseconds 250
    }
    throw "VS2026 remained busy: $text"
}
$bridge = Join-Path $buildRoot 'Editor/Debug/UrsaIdeBridge.exe'
$devenv = Join-Path $vsRoot 'Common7/IDE/devenv.exe'
$ownedIde = [Collections.Generic.List[Diagnostics.Process]]::new()
$records = [Collections.Generic.List[object]]::new()
try {
    $bridgeOutput = (& $bridge $devenv $solution $game 118 4 2>&1) -join "`n"
    $bridgeStatus = $LASTEXITCODE
    $startedIde = Get-UrsaStartedIde $bridgeOutput
    if ($startedIde) { $ownedIde.Add($startedIde) }
    Write-Host $bridgeOutput
    if ($bridgeStatus -ne 0) { throw 'Could not open the unified solution in VS2026.' }
    $info = Invoke-Controller 'info'
    if ([version]$info.Version -lt [version]'18.0') { throw 'Acceptance requires VS2026.' }
    $records.Add((Invoke-Controller 'complete' @($game)))
    $records.Add((Invoke-Controller 'complete-member' @($editor, 'EditorGui.PropertyField', 'PropertyFiel', 'PropertyField')))
    $records.Add((Invoke-Controller 'complete-member' @($framework, 'NativeRuntimeBindings.GetLocalTransform', 'GetLocalTransfor', 'GetLocalTransform')))
    $records.Add((Invoke-Controller 'navigate-symbol' @($game, (Join-Path $description.EngineRoot 'Managed/Ursa.Runtime/GameObject.cs'), 'Ursa.GameObject')))
    $records.Add((Invoke-Controller 'navigate-symbol' @($editor, $game, 'SnakeController')))
    # A second navigation request exercises selection of the already-open solution.
    $bridgeOutput = (& $bridge $devenv $solution $editor 11 3 2>&1) -join "`n"
    $bridgeStatus = $LASTEXITCODE
    $startedIde = Get-UrsaStartedIde $bridgeOutput
    if ($startedIde) { $ownedIde.Add($startedIde) }
    if ($bridgeStatus -ne 0) { throw 'Existing-solution source navigation failed.' }
    @{ IDE=$info; Solution=$solution; Records=$records.ToArray(); VerifiedAt=[DateTime]::UtcNow.ToString('o') } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $acceptanceRoot 'UnifiedLanguageEvidence.json') -Encoding utf8
} finally {
    try {
        foreach ($source in $sources) { if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $hashes[$source]) { throw "IDE verification changed source on disk: $source" } }
    } finally {
        try {
            if ($ownedIde.Count) { [void](Invoke-Controller 'close-fixture-documents' @($buildRoot)) }
        } finally {
            foreach ($process in $ownedIde) { Stop-UrsaAcceptanceProcess -Process $process -ExpectedExecutable $devenv }
        }
    }
}
Write-Host 'VS2026 completion, framework/Game definition navigation and unified-solution positioning passed.'
