param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$VisualStudioInstallDirectory,
    [string]$Configuration = 'Debug',
    [switch]$Unified,
    [string]$FrameworkPrefix = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AcceptanceProcess.ps1')
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$cacheText = [IO.File]::ReadAllText((Join-Path $buildRoot 'CMakeCache.txt'))
$originalProjectRoot = [regex]::Match($cacheText, '(?m)^URSA_PROJECT_DIR:PATH=(.*)\r?$').Groups[1].Value.Trim()
if ($Unified -and -not $originalProjectRoot) { throw 'Unified debug acceptance requires an existing project association.' }
$originalEnvironment = @{}
foreach ($name in @('URSA_USER_DATA_ROOT', 'TEMP', 'TMP')) { $originalEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
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
$ownedBreakpoints = [Collections.Generic.List[string]]::new()
$script:commandSequence = 0

function Add-AcceptanceBreakpoint([string]$File, [int]$Line) {
    $result = Invoke-Controller 'breakpoint' @($File, [string]$Line) | ConvertFrom-Json
    if ($result.Created) { $ownedBreakpoints.Add($File); $ownedBreakpoints.Add([string]$Line) }
}

function Invoke-Controller([string]$Action, [string[]]$Extra = @()) {
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        $output = Join-Path $acceptanceRoot 'controller-output.log'
        $errors = Join-Path $acceptanceRoot 'controller-error.log'
        $arguments = @(('"' + $controller + '"'), $Action, ('"' + $solution + '"'))
        foreach ($value in $Extra) { $arguments += '"' + $value + '"' }
        $process = Start-Process -FilePath (Get-Command dotnet).Source -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError $errors
        $timeout = if ($Action -in @('attach', 'detach')) { 60000 } else { 30000 }
        if (-not $process.WaitForExit($timeout)) { $process.Kill(); [void]$process.WaitForExit(5000); throw "Controller timed out: $Action" }
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

function Verify-Break([string]$Function, [int]$Line, [string]$Variable, [string]$Expected, [switch]$RemoveAfter) {
    Wait-Until { $capture = Invoke-Controller 'state' | ConvertFrom-Json; return $capture.Mode -eq 2 -and $capture.Function -like $Function -and $capture.Line -eq $Line } 25 "breakpoint $Function"
    $before = Invoke-Controller 'state' | ConvertFrom-Json
    if (($Function.StartsWith('DebugAcceptance.') -or $Function.StartsWith('Ursa.')) -and -not $before.Project.EndsWith('.csproj', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Breakpoint source is outside the C# solution project: $($before.File)"
    }
    [void](Invoke-Controller 'step')
    Wait-Until { $capture = Invoke-Controller 'state' | ConvertFrom-Json; return $capture.Mode -eq 2 -and $capture.Function -like $Function -and $capture.Line -gt $Line } 15 "single step $Function"
    $after = Invoke-Controller 'state' | ConvertFrom-Json
    $value = Invoke-Controller 'evaluate' @($Variable) | ConvertFrom-Json
    if (-not $value.Valid -or $value.Value -ne $Expected) {
        [void](Invoke-Controller 'continue')
        throw "Unexpected local $Variable : $($value | ConvertTo-Json -Compress)"
    }
    $records.Add(@{ Break = $before; Step = $after; Local = $value })
    Write-Host "$Function : $Variable = $Expected"
    if ($RemoveAfter) { [void](Invoke-Controller 'clear' @($before.File, [string]$Line)) }
    [void](Invoke-Controller 'continue')
}

$hostProcess = $null
$ownedIde = $null
$navigationStarted = $false
$navigationOutput = Join-Path $acceptanceRoot 'debug-navigation-output.log'
$devenv = Join-Path $vsRoot 'Common7/IDE/devenv.exe'
$attached = $false
try {
    New-Item -ItemType Directory -Force -Path $acceptanceRoot | Out-Null
    $env:URSA_USER_DATA_ROOT = Join-Path $acceptanceRoot 'User'
    $env:TEMP = Join-Path $buildRoot 'Temp'
    $env:TMP = $env:TEMP
    $hostExecutable = Join-Path $buildRoot "$Configuration/UrsaManagedDebugAcceptance.exe"
    $hostProcess = Start-Process -FilePath $hostExecutable -ArgumentList ('"' + $runRoot + '"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $acceptanceRoot 'controlled-host.log') -RedirectStandardError (Join-Path $acceptanceRoot 'controlled-host-error.log')
    Wait-Until { return (Test-Path -LiteralPath $statePath) -and (Read-State).Ready -and -not (Read-State).Busy } 90 'initial editing session'
    if ($Unified) {
        $solution = & (Join-Path $PSScriptRoot 'GenerateUnifiedIdeFixture.ps1') -BuildDirectory $buildRoot -ProjectDirectory $runRoot
    }
    $bridge = Join-Path $buildRoot "Editor/$Configuration/UrsaIdeBridge.exe"
    $navigation = Start-Process -FilePath $bridge -ArgumentList @(('"' + $devenv + '"'), ('"' + $solution + '"'), ('"' + $gameSource + '"'), '11', '5') -WindowStyle Hidden -PassThru -RedirectStandardOutput $navigationOutput
    $navigationStarted = $true
    if (-not $navigation.WaitForExit(90000)) { $navigation.Kill(); throw 'IDE source navigation timed out' }
    $ownedIde = Get-UrsaStartedIde ([IO.File]::ReadAllText($navigationOutput))
    if ($navigation.ExitCode -ne 0) { throw 'IDE could not open the controlled solution' }
    $ide = Invoke-Controller 'info' | ConvertFrom-Json
    $ide | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $acceptanceRoot 'MixedIdeInfo.json') -Encoding utf8
    $members = @((Invoke-Controller 'project-file' @($gameSource) | ConvertFrom-Json),
        (Invoke-Controller 'project-file' @($editorSource) | ConvertFrom-Json))
    $members | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $acceptanceRoot 'MixedProjectMembership.json') -Encoding utf8
    Add-AcceptanceBreakpoint $gameSource 11
    Add-AcceptanceBreakpoint $editorSource 9
    $managedEngine = '{2E36F1D4-B23C-435D-AB41-18E608940038}'
    if ($Unified) {
        $managedEngine += ';{3B476D35-A401-11D2-AAD4-00C04F990171}'
        $nativeSource = Join-Path $PSScriptRoot 'ManagedDebugAcceptance.cpp'
        $nativeLine = (Select-String -LiteralPath $nativeSource -SimpleMatch 'const Int32 nativeMarker = seed + 7;').LineNumber
        Add-AcceptanceBreakpoint $nativeSource $nativeLine
    }
    $attached = $true
    [void](Invoke-Controller 'attach' @([string]$hostProcess.Id, $managedEngine))
    if ($Unified) {
        Send-Command 'native'
        Verify-Break '*NativeDebugProbe*' $nativeLine 'nativeMarker' '42'
    }
    $prePlay = Read-State
    if ($prePlay.Running) { throw 'Render breakpoint acceptance already entered Play.' }
    if ($Unified) {
        $renderSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../Managed/Ursa.Rendering/RenderPipeline.cs'))
        $renderLine = (Select-String -LiteralPath $renderSource -SimpleMatch 'ObjectDisposedException.ThrowIf(disposed, this);').LineNumber
        [void](Invoke-Controller 'project-file' @($renderSource))
        Add-AcceptanceBreakpoint $renderSource $renderLine
        Verify-Break 'Ursa.Rendering.RenderPipeline.RenderInternal*' $renderLine 'cameras.Count' '1' -RemoveAfter
    }
    Send-Command 'menu'
    Verify-Break 'DebugAcceptance.EditorProbe.Pulse' 9 'editorMarker' '41'
    Send-Command 'play'
    Verify-Break 'DebugAcceptance.DebugProbe.Awake' 11 'runtimeMarker' '10'
    Wait-Until { return (Read-State).RunningName -eq ($FrameworkPrefix + 'Debug:10') } 10 'first runtime callback'
    if (-not $Unified) {
        [void](Invoke-Controller 'detach')
        Wait-Until { $capture = Invoke-Controller 'state' | ConvertFrom-Json; return $capture.Mode -eq 1 -and $capture.Processes.Count -eq 0 } 30 'debugger detach completion'
        $attached = $false
    }
    $oldManifest = Read-Json (Join-Path $runRoot 'Library/ScriptBuild/Manifests/Game.json')
    $oldEditingSession = (Read-State).DebugInfo
    $oldSymbols = Get-FileHash (Join-Path $runRoot 'Library/ScriptBuild/Assemblies/Game/Ursa.Game.pdb') -Algorithm SHA256
    $text = [IO.File]::ReadAllText($gameSource)
    if (-not $text.Contains('CodeVersion = 1')) { throw 'Controlled source fixture changed unexpectedly' }
    [IO.File]::WriteAllText($gameSource, $text.Replace('CodeVersion = 1', 'CodeVersion = 2'))
    $sourceRefresh = Invoke-Controller 'refresh-source' @($gameSource) | ConvertFrom-Json
    Start-Sleep -Seconds 3
    if ((Read-State).RunningName -ne ($FrameworkPrefix + 'Debug:10')) { throw 'Run mixed old and new script versions' }
    Send-Command 'stop'
    Wait-Until {
        $state = Read-State
        $manifest = Read-Json (Join-Path $runRoot 'Library/ScriptBuild/Manifests/Game.json')
        return -not $state.Running -and -not $state.Busy -and $state.DebugInfo -ne $oldEditingSession -and $manifest.BuildFingerprint -ne $oldManifest.BuildFingerprint
    } 60 'formal recompilation and editing reload'
    $reloaded = Read-State
    if (-not $reloaded.Dirty -or $reloaded.Payload.Fields.value -ne 9) { throw 'Reload lost unsaved editing data' }
    if ($ownedBreakpoints.Contains($gameSource)) {
        [void](Invoke-Controller 'clear' @($gameSource, '11'))
        [void](Invoke-Controller 'breakpoint' @($gameSource, '11'))
    }
    if (-not $Unified) {
        $attached = $true
        [void](Invoke-Controller 'attach' @([string]$hostProcess.Id, $managedEngine))
    }
    Send-Command 'menu'
    Verify-Break 'DebugAcceptance.EditorProbe.Pulse' 9 'editorMarker' '42'
    Send-Command 'play'
    Verify-Break 'DebugAcceptance.DebugProbe.Awake' 11 'runtimeMarker' '11'
    Wait-Until { return (Read-State).RunningName -eq ($FrameworkPrefix + 'Debug:11') } 10 'new runtime callback'
    if (-not $Unified) {
        [void](Invoke-Controller 'detach')
        Wait-Until { $capture = Invoke-Controller 'state' | ConvertFrom-Json; return $capture.Mode -eq 1 -and $capture.Processes.Count -eq 0 } 30 'final debugger detach completion'
        $attached = $false
    }
    Send-Command 'stop'
    Wait-Until { return -not (Read-State).Running -and -not (Read-State).Busy } 30 'final Stop'
    $newManifest = Read-Json (Join-Path $runRoot 'Library/ScriptBuild/Manifests/Game.json')
    $newSymbols = Get-FileHash (Join-Path $runRoot 'Library/ScriptBuild/Assemblies/Game/Ursa.Game.pdb') -Algorithm SHA256
    $evidence = @{ Project = $runRoot; Solution=$solution; Pid = $hostProcess.Id; IDE = $ide; Records = $records.ToArray(); Membership=$members; SourceRefresh=$sourceRefresh;
        FrameworkPrefix=$FrameworkPrefix; FrameworkSha256=(Get-FileHash (Join-Path $buildRoot "Managed/$Configuration/Ursa.Runtime.dll")).Hash; ReloadWhileAttached=[bool]$Unified; PrePlayRenderState=$prePlay;
        OldBuildFingerprint = $oldManifest.BuildFingerprint; NewBuildFingerprint = $newManifest.BuildFingerprint;
        OldPdbSha256 = $oldSymbols.Hash; NewPdbSha256 = $newSymbols.Hash; ReloadedState = $reloaded }
    $evidence | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $acceptanceRoot 'managed-debug-evidence.json') -Encoding utf8
    [void](Invoke-Controller 'clear' $ownedBreakpoints.ToArray())
    $ownedBreakpoints.Clear()
    Send-Command 'close'
    if (-not $hostProcess.WaitForExit(15000)) { throw 'Controlled host did not close' }
    if ($hostProcess.ExitCode -ne 0) { throw "Controlled host exited with $($hostProcess.ExitCode)" }
    $attached = $false
    Write-Host 'Game and Editor breakpoints, stepping, locals and reloaded-version breakpoints passed.'
} catch {
    @{ Error=$_.ToString(); Records=$records.ToArray();
        ControllerOutput=$(if (Test-Path (Join-Path $acceptanceRoot 'controller-output.log')) { [IO.File]::ReadAllText((Join-Path $acceptanceRoot 'controller-output.log')) });
        ControllerError=$(if (Test-Path (Join-Path $acceptanceRoot 'controller-error.log')) { [IO.File]::ReadAllText((Join-Path $acceptanceRoot 'controller-error.log')) }) } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $acceptanceRoot 'MixedDebugFailure.json') -Encoding utf8
    throw
} finally {
    if ($attached -and -not $Unified) { try { [void](Invoke-Controller 'detach') } catch { Write-Warning $_ } }
    if ($ownedBreakpoints.Count) { try { [void](Invoke-Controller 'clear' $ownedBreakpoints.ToArray()) } catch { Write-Warning $_ } }
    try {
        try {
            if ($hostProcess -and -not $hostProcess.HasExited) {
                try { Send-Command 'close'; [void]$hostProcess.WaitForExit(3000) } catch { Write-Warning $_ }
                Stop-UrsaAcceptanceProcess -Process $hostProcess -ExpectedExecutable $hostExecutable
            }
        } finally {
            if (-not $ownedIde -and $navigationStarted -and (Test-Path -LiteralPath $navigationOutput)) {
                $ownedIde = Get-UrsaStartedIde ([IO.File]::ReadAllText($navigationOutput))
            }
            Stop-UrsaAcceptanceProcess -Process $ownedIde -ExpectedExecutable $devenv
        }
    } finally {
        foreach ($name in $originalEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $originalEnvironment[$name], 'Process') }
    }
}
