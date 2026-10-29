$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AcceptanceProcess.ps1')
$executable = (Get-Process -Id $PID).Path
$child = $null
try {
    $child = Start-Process -FilePath $executable -ArgumentList @('-NoProfile', '-Command', 'Start-Sleep -Seconds 60') -WindowStyle Hidden -PassThru
    $rejected = $false
    try { Stop-UrsaAcceptanceProcess -Process $child -ExpectedExecutable (Join-Path $PSScriptRoot 'Unrelated.exe') }
    catch { $rejected = $_ -match 'unrelated process' }
    if (-not $rejected -or $child.HasExited) { throw 'Process ownership guard failed.' }
    $identified = Get-UrsaStartedIde "Started Visual Studio PID $($child.Id)"
    if (-not $identified -or $identified.Id -ne $child.Id) { throw 'Bridge PID tracking failed.' }
    Stop-UrsaAcceptanceProcess -Process $child -ExpectedExecutable $executable
    if (-not $child.HasExited) { throw 'Owned acceptance process survived cleanup.' }
    Stop-UrsaAcceptanceProcess -Process $child -ExpectedExecutable $executable
    if (Get-UrsaStartedIde 'Using existing Visual Studio') { throw 'An existing IDE was marked as owned.' }
} finally {
    Stop-UrsaAcceptanceProcess -Process $child -ExpectedExecutable $executable
}
Write-Host 'Acceptance process ownership, failure cleanup and repeated cleanup passed.'
