param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$VisualStudioInstallDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$workspace = [IO.File]::ReadAllText((Join-Path $buildRoot 'UnifiedWorkspace.json')) | ConvertFrom-Json
if (-not $workspace.Ready) { throw 'Generate a ready unified workspace before running F5 acceptance.' }
$engineRoot = $workspace.EngineRoot
$solution = Join-Path $buildRoot 'UrsaEngine.slnx'
$source = Join-Path $engineRoot 'Managed/Ursa.Rendering/RenderPipeline.cs'
$line = (Select-String -LiteralPath $source -SimpleMatch 'ObjectDisposedException.ThrowIf(disposed, this);').LineNumber
$root = Join-Path $buildRoot ('IdeAcceptance/PrePlayLaunch-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
[xml]$copy = [IO.File]::ReadAllText($solution)
foreach ($node in $copy.SelectNodes('//Project[@Path]|//BuildDependency[@Project]')) {
    $attribute = if($node.Name -eq 'Project') { 'Path' } else { 'Project' }
    $path = $node.GetAttribute($attribute)
    if (-not [IO.Path]::IsPathRooted($path)) { $path = [IO.Path]::GetFullPath((Join-Path $buildRoot $path)) }
    $node.SetAttribute($attribute,$path.Replace('\','/'))
}
$solution = Join-Path $root 'UrsaEngine.slnx'
[IO.File]::WriteAllText($solution, $copy.OuterXml, [Text.UTF8Encoding]::new($false))
. (Join-Path $engineRoot 'Tests/Editor/Scripting/AcceptanceProcess.ps1')
$originalEnvironment = @{}
foreach($name in @('URSA_USER_DATA_ROOT','TEMP','TMP')) { $originalEnvironment[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
$env:URSA_USER_DATA_ROOT = Join-Path $root 'User'
$env:TEMP = Join-Path $buildRoot 'Temp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$controller = Join-Path $buildRoot 'IdeAcceptance/VisualStudioController/bin/Release/net10.0-windows/VisualStudioAcceptance.dll'
$devenv = Join-Path $VisualStudioInstallDirectory 'Common7/IDE/devenv.exe'
$editorExecutable = Join-Path $buildRoot "Editor/$Configuration/UrsaEditor.exe"
$ownedIde = $null
$editor = $null
$createdBreakpoint = $false
$launch = $null

function Controller([string]$Action, [string[]]$Extra = @()) {
    $output = Join-Path $root 'controller-output.log'
    $errors = Join-Path $root 'controller-errors.log'
    $arguments = @(('"' + $controller + '"'), $Action, ('"' + $solution + '"'))
    foreach ($value in $Extra) { $arguments += '"' + $value + '"' }
    $child = Start-Process dotnet -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError $errors
    if (-not $child.WaitForExit(60000)) { $child.Kill(); throw "Controller timed out: $Action" }
    if ($child.ExitCode -ne 0) { throw ([IO.File]::ReadAllText($errors)) }
    return [IO.File]::ReadAllText($output)
}

try {
    $controllerRoot = Join-Path $buildRoot 'IdeAcceptance/VisualStudioController'
    & dotnet build (Join-Path $PSScriptRoot 'VisualStudioAcceptance/VisualStudioAcceptance.csproj') -c Release --nologo --disable-build-servers -m:1 -nr:false "-p:VisualStudioInstallDirectory=$VisualStudioInstallDirectory" "-p:UrsaAcceptanceBuildRoot=$controllerRoot"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot build the F5 acceptance controller.' }
    if (-not (Test-Path -LiteralPath $editorExecutable)) { throw "Build UrsaEditor/$Configuration before this acceptance." }
    $ownedIde = Start-Process $devenv -ArgumentList ('"' + $solution + '"') -WindowStyle Hidden -PassThru
    $ideLimit = [DateTime]::UtcNow.AddSeconds(120)
    do {
        try { [void](Controller 'projects'); break }
        catch { if([DateTime]::UtcNow -ge $ideLimit) { throw }; Start-Sleep -Milliseconds 300 }
    } while ($true)
    Write-Host "Owned Visual Studio PID $($ownedIde.Id) loaded."
    $launch = Controller 'launch' @('UrsaEditor',$Configuration) | ConvertFrom-Json
    $editor = Get-Process -Id $launch.Pid
    Write-Host "Launched PID $($launch.Pid), debugger type '$($launch.DebuggerType)'"
    $limit = [DateTime]::UtcNow.AddSeconds(90)
    $hostLog = Join-Path $root 'User/UrsaEditor/Logs/UrsaEditor.log'
    do {
        if ($editor.HasExited) { throw 'Editor exited before managed startup' }
        if ((Test-Path -LiteralPath $hostLog) -and [IO.File]::ReadAllText($hostLog).Contains('[Script] Ursa 托管运行时已初始化')) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $limit)
    if (-not (Test-Path -LiteralPath $hostLog) -or -not [IO.File]::ReadAllText($hostLog).Contains('[Script] Ursa 托管运行时已初始化')) { throw 'Main-thread managed startup did not complete' }
    $membership = Controller 'project-file' @($source) | ConvertFrom-Json
    $createdBreakpoint = (Controller 'breakpoint' @($source, [string]$line) | ConvertFrom-Json).Created
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    do {
        $state = Controller 'state' | ConvertFrom-Json
        if ($state.Mode -eq 2 -and $state.Function -like 'Ursa.Rendering.RenderPipeline.RenderInternal*') { break }
        Start-Sleep -Milliseconds 150
    } while ([DateTime]::UtcNow -lt $deadline)
    $evidence = [ordered]@{ Launch=$launch; State=$state; PlayRequested=$false; HostLog=$hostLog; Membership=$membership; Passed=($state.Mode -eq 2 -and $state.Function -like 'Ursa.Rendering.RenderPipeline.RenderInternal*' -and $state.Line -eq $line -and $state.Project.EndsWith('.csproj',[StringComparison]::OrdinalIgnoreCase)) }
    if ($evidence.Passed) {
        $evidence['CameraCount'] = Controller 'evaluate' @('cameras.Count') | ConvertFrom-Json
        $evidence['FrameworkLocation'] = Controller 'evaluate' @('typeof(Ursa.Rendering.RenderPipeline).Assembly.Location') | ConvertFrom-Json
        if (-not $evidence.CameraCount.Valid -or $evidence.CameraCount.Value -ne '1' -or -not $evidence.FrameworkLocation.Valid) {
            throw 'Cannot inspect the editing render camera and framework module.'
        }
        $loadedAssembly = $evidence.FrameworkLocation.Value | ConvertFrom-Json
        $evidence['FrameworkSha256'] = (Get-FileHash -LiteralPath $loadedAssembly -Algorithm SHA256).Hash
        $evidence['FormalFrameworkSha256'] = (Get-FileHash -LiteralPath (Join-Path $buildRoot "Managed/$Configuration/Ursa.Rendering.dll") -Algorithm SHA256).Hash
        $evidence.Passed = $evidence.FrameworkSha256 -eq $evidence.FormalFrameworkSha256
    }
    $evidence | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $root 'Evidence.json') -Encoding utf8
    if (-not $evidence.Passed) { throw 'Cold F5 did not hit RenderInternal before Play' }
    Write-Host 'Cold F5: RenderInternal breakpoint added after startup hit before Play.'
} finally {
    try {
        if ($ownedIde) {
            if ($createdBreakpoint) { try { [void](Controller 'clear' @($source,[string]$line)) } catch { Write-Warning $_ } }
            if ($launch) {
                try { [void](Controller 'stop') } catch { Write-Warning $_ }
                try { [void](Controller 'startup' @($launch.PreviousStartup)) } catch { Write-Warning $_ }
            }
        }
    } finally {
        try {
            Stop-UrsaAcceptanceProcess -Process $editor -ExpectedExecutable $editorExecutable
        } finally {
            try {
                Stop-UrsaAcceptanceProcess -Process $ownedIde -ExpectedExecutable $devenv
            } finally {
                foreach($name in $originalEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name,$originalEnvironment[$name],'Process') }
                Write-Host "Evidence directory: $root"
            }
        }
    }
}
