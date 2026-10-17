param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$VisualStudioInstallDirectory,
    [string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$vsRoot = (Resolve-Path -LiteralPath $VisualStudioInstallDirectory).Path
$acceptanceRoot = Join-Path $buildRoot 'IdeAcceptance'
$runRoot = Join-Path $acceptanceRoot ('DebugRun-' + [Guid]::NewGuid().ToString('N'))
$controllerRoot = Join-Path $acceptanceRoot 'VisualStudioController'
$controllerProject = Join-Path $PSScriptRoot 'VisualStudioAcceptance/VisualStudioAcceptance.csproj'
& dotnet build $controllerProject -c Release --nologo --disable-build-servers -m:1 -nr:false "-p:VisualStudioInstallDirectory=$vsRoot" "-p:UrsaAcceptanceBuildRoot=$controllerRoot"
if ($LASTEXITCODE -ne 0) { throw 'Cannot build Visual Studio acceptance controller' }
$controller = Join-Path $controllerRoot 'bin/Release/net10.0-windows/VisualStudioAcceptance.dll'
$solution = Join-Path $runRoot 'Library/ScriptIDE/Ursa.Scripts.sln'
$commandPath = Join-Path $runRoot 'Library/IdeAcceptance/Command.json'
$statePath = Join-Path $runRoot 'Library/IdeAcceptance/State.json'
$gameSource = Join-Path $runRoot 'Assets/DebugProbe.cs'
$editorSource = Join-Path $runRoot 'Assets/Editor/EditorProbe.cs'
$records = [Collections.Generic.List[object]]::new()
$script:commandSequence = 0

function Invoke-Controller([string]$Action, [string[]]$Extra = @()) {
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        $output = Join-Path $acceptanceRoot 'controller-output.log'
        $errors = Join-Path $acceptanceRoot 'controller-error.log'
        $arguments = @(('"' + $controller + '"'), $Action, ('"' + $solution + '"'))
        foreach ($value in $Extra) { $arguments += '"' + $value + '"' }
        $process = Start-Process -FilePath (Get-Command dotnet).Source -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError $errors
        if (-not $process.WaitForExit(15000)) { $process.Kill(); throw "Controller timed out: $Action" }
        $text = [IO.File]::ReadAllText($output)
        if ($process.ExitCode -eq 0) { return $text }
        $errorText = [IO.File]::ReadAllText($errors)
        if ($errorText -notmatch 'RPC_E_CALL_REJECTED|RPC_E_SERVERCALL_RETRYLATER|0x80010001|0x8001010A|No Visual Studio instance') { throw $errorText }
        Start-Sleep -Milliseconds 150
    }
    throw "IDE remained busy: $Action : $errorText"
}

function Wait-Until([scriptblock]$Check, [int]$Seconds, [string]$Description) {
    $limit = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        if (& $Check) { return }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $limit)
    throw "Timed out: $Description"
}

function Read-Json([string]$Path) {
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        try {
            $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
            $reader = [IO.StreamReader]::new($stream)
            try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
            return $text | ConvertFrom-Json
        }
        catch [IO.IOException] { Start-Sleep -Milliseconds 20 }
    }
    throw "Cannot read acceptance state: $Path"
}

function Read-State { return Read-Json $statePath }

function Send-Command([string]$Action) {
    ++$script:commandSequence
    $text = @{ Sequence = $script:commandSequence; Action = $Action } | ConvertTo-Json -Compress
    $temporary = $commandPath + '.tmp'
    [IO.File]::WriteAllText($temporary, $text)
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        try { [IO.File]::Move($temporary, $commandPath, $true); return }
        catch [IO.IOException] { Start-Sleep -Milliseconds 20 }
    }
    throw 'Cannot publish acceptance command'
}

function Verify-Break([string]$Function, [int]$Line, [string]$Variable, [string]$Expected) {
    Wait-Until { $capture = Invoke-Controller 'state' | ConvertFrom-Json; return $capture.Mode -eq 2 -and $capture.Function -eq $Function -and $capture.Line -eq $Line } 25 "breakpoint $Function"
    $before = Invoke-Controller 'state' | ConvertFrom-Json
    [void](Invoke-Controller 'step')
    Wait-Until { $capture = Invoke-Controller 'state' | ConvertFrom-Json; return $capture.Mode -eq 2 -and $capture.Function -eq $Function -and $capture.Line -gt $Line } 15 "single step $Function"
    $after = Invoke-Controller 'state' | ConvertFrom-Json
    $value = Invoke-Controller 'evaluate' @($Variable) | ConvertFrom-Json
    if (-not $value.Valid -or $value.Value -ne $Expected) { throw "Unexpected local $Variable : $($value | ConvertTo-Json -Compress)" }
    $records.Add(@{ Break = $before; Step = $after; Local = $value })
    Write-Host "$Function : $Variable = $Expected"
    [void](Invoke-Controller 'continue')
}

$hostProcess = $null
$attached = $false
try {
    New-Item -ItemType Directory -Force -Path $acceptanceRoot | Out-Null
    $env:URSA_USER_DATA_ROOT = Join-Path $acceptanceRoot 'User'
    $env:TEMP = Join-Path $buildRoot 'Temp'
    $env:TMP = $env:TEMP
    $hostExecutable = Join-Path $buildRoot "$Configuration/UrsaManagedDebugAcceptance.exe"
    $hostProcess = Start-Process -FilePath $hostExecutable -ArgumentList ('"' + $runRoot + '"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $acceptanceRoot 'controlled-host.log') -RedirectStandardError (Join-Path $acceptanceRoot 'controlled-host-error.log')
    Wait-Until { return (Test-Path -LiteralPath $statePath) -and (Read-State).Ready -and -not (Read-State).Busy } 90 'initial editing session'
    $bridge = Join-Path $buildRoot "Editor/$Configuration/UrsaIdeBridge.exe"
    $devenv = Join-Path $vsRoot 'Common7/IDE/devenv.exe'
    $navigation = Start-Process -FilePath $bridge -ArgumentList @(('"' + $devenv + '"'), ('"' + $solution + '"'), ('"' + $gameSource + '"'), '11', '5') -WindowStyle Hidden -PassThru
    if (-not $navigation.WaitForExit(30000)) { $navigation.Kill(); throw 'IDE source navigation timed out' }
    if ($navigation.ExitCode -ne 0) { throw 'IDE could not open the controlled solution' }
    $ide = Invoke-Controller 'info' | ConvertFrom-Json
    [void](Invoke-Controller 'breakpoint' @($gameSource, '11'))
    [void](Invoke-Controller 'breakpoint' @($editorSource, '9'))
    $managedEngine = '{2E36F1D4-B23C-435D-AB41-18E608940038}'
    [void](Invoke-Controller 'attach' @([string]$hostProcess.Id, $managedEngine))
    $attached = $true
    Send-Command 'menu'
    Verify-Break 'DebugAcceptance.EditorProbe.Pulse' 9 'editorMarker' '41'
    Send-Command 'play'
    Verify-Break 'DebugAcceptance.DebugProbe.Awake' 11 'runtimeMarker' '10'
    Wait-Until { return (Read-State).RunningName -eq 'Debug:10' } 10 'first runtime callback'
    [void](Invoke-Controller 'detach')
    $attached = $false
    $oldManifest = Read-Json (Join-Path $runRoot 'Library/ScriptBuild/Manifests/Game.json')
    $oldSymbols = Get-FileHash (Join-Path $runRoot 'Library/ScriptBuild/Assemblies/Game/Ursa.Game.pdb') -Algorithm SHA256
    $text = [IO.File]::ReadAllText($gameSource)
    if (-not $text.Contains('CodeVersion = 1')) { throw 'Controlled source fixture changed unexpectedly' }
    [IO.File]::WriteAllText($gameSource, $text.Replace('CodeVersion = 1', 'CodeVersion = 2'))
    Start-Sleep -Seconds 3
    if ((Read-State).RunningName -ne 'Debug:10') { throw 'Run mixed old and new script versions' }
    Send-Command 'stop'
    Wait-Until {
        $state = Read-State
        $manifest = Read-Json (Join-Path $runRoot 'Library/ScriptBuild/Manifests/Game.json')
        return -not $state.Running -and -not $state.Busy -and $manifest.BuildFingerprint -ne $oldManifest.BuildFingerprint
    } 60 'formal recompilation and editing reload'
    $reloaded = Read-State
    if (-not $reloaded.Dirty -or $reloaded.Payload.Fields.value -ne 9) { throw 'Reload lost unsaved editing data' }
    [void](Invoke-Controller 'attach' @([string]$hostProcess.Id, $managedEngine))
    $attached = $true
    Send-Command 'menu'
    Verify-Break 'DebugAcceptance.EditorProbe.Pulse' 9 'editorMarker' '42'
    Send-Command 'play'
    Verify-Break 'DebugAcceptance.DebugProbe.Awake' 11 'runtimeMarker' '11'
    Wait-Until { return (Read-State).RunningName -eq 'Debug:11' } 10 'new runtime callback'
    [void](Invoke-Controller 'detach')
    $attached = $false
    Send-Command 'stop'
    Wait-Until { return -not (Read-State).Running -and -not (Read-State).Busy } 30 'final Stop'
    $newManifest = Read-Json (Join-Path $runRoot 'Library/ScriptBuild/Manifests/Game.json')
    $newSymbols = Get-FileHash (Join-Path $runRoot 'Library/ScriptBuild/Assemblies/Game/Ursa.Game.pdb') -Algorithm SHA256
    $evidence = @{ Project = $runRoot; Pid = $hostProcess.Id; IDE = $ide; Records = $records.ToArray();
        OldBuildFingerprint = $oldManifest.BuildFingerprint; NewBuildFingerprint = $newManifest.BuildFingerprint;
        OldPdbSha256 = $oldSymbols.Hash; NewPdbSha256 = $newSymbols.Hash; ReloadedState = $reloaded }
    $evidence | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $acceptanceRoot 'managed-debug-evidence.json') -Encoding utf8
    [void](Invoke-Controller 'clear')
    Send-Command 'close'
    if (-not $hostProcess.WaitForExit(15000)) { throw 'Controlled host did not close' }
    if ($hostProcess.ExitCode -ne 0) { throw "Controlled host exited with $($hostProcess.ExitCode)" }
    Write-Host 'Game and Editor breakpoints, stepping, locals and reloaded-version breakpoints passed.'
}
finally {
    if ($attached) { try { [void](Invoke-Controller 'detach') } catch { Write-Warning $_ } }
    if ($hostProcess -and -not $hostProcess.HasExited) {
        try { Send-Command 'close'; [void]$hostProcess.WaitForExit(5000) } catch { Write-Warning $_ }
        if (-not $hostProcess.HasExited) { $hostProcess.Kill() }
    }
}
