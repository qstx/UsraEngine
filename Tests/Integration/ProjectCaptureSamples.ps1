# Loaded by ProjectPipelineSamplesTests.ps1; reuses its isolated project and content.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CaptureTestWindow
{
    private delegate bool Callback(IntPtr window, IntPtr context);
    [DllImport("user32.dll")] private static extern bool EnumWindows(Callback callback, IntPtr context);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    public static bool SendKey(IntPtr window, uint key, bool pressed)
    {
        uint message = pressed ? 0x0100u : 0x0101u;
        IntPtr data = pressed ? new IntPtr(1) : new IntPtr(unchecked((int)0xC0000001));
        return PostMessage(window, message, new IntPtr(key), data);
    }
    public static IntPtr Find(uint pid)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, context) => { uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != pid) return true;
            var title = new StringBuilder(256); GetWindowText(window, title, title.Capacity);
            if (title.ToString() != "Ursa Script Player" && title.ToString() != "Ursa Editor") return true;
            found = window; return false; }, IntPtr.Zero);
        return found;
    }
}
'@

function Wait-CaptureCondition([scriptblock]$Condition, [int]$Seconds = 30) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while (-not (& $Condition)) {
        if ($timer.Elapsed.TotalSeconds -ge $Seconds) { throw 'Capture readiness/state deadline exceeded' }
        Start-Sleep -Milliseconds 20
    }
}

function Start-CaptureSample([string]$Executable, [string[]]$Arguments, [string]$Name) {
    $quoted = @($Arguments | ForEach-Object { '"' + $_ + '"' })
    $process = Start-Process -FilePath $Executable -ArgumentList $quoted -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput "$project/$Name.stdout.log" -RedirectStandardError "$project/$Name.stderr.log"
    try {
        $registration = "$env:URSA_USER_DATA_ROOT/UrsaCapture/instances/$($process.Id).json"
        Wait-CaptureCondition {
            if ($process.HasExited) { throw "Capture target exited: $Name (see $project/$Name.stderr.log)" }
            if (Test-Path -LiteralPath $registration) {
                try {
                    $stream = [IO.File]::Open($registration, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                        ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
                    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
                    try { return ($reader.ReadToEnd() | ConvertFrom-Json).Ready }
                    finally { $reader.Dispose() }
                } catch [IO.IOException] { return $false }
            }
            return $false
        } 90
        return $process
    } catch {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
        throw
    }
}

function Invoke-CaptureSample([Diagnostics.Process]$TargetProcess, [string]$Output, [string]$Expected = 'Success', [int]$Timeout = 10000) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $text = & $Capture --pid $TargetProcess.Id --output $Output --timeout-ms $Timeout 2> "$project/Capture.stderr.log"
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    $json = ($text -join "`n") | ConvertFrom-Json
    if ($json.Status -ne $Expected -or $json.Pid -ne $TargetProcess.Id -or (($code -eq 0) -ne ($Expected -eq 'Success'))) {
        throw "Unexpected capture result (expected $Expected): $text"
    }
    $resultSuffix = if ($Expected -eq 'Success') { '.result.json' } else { ".$Expected.result.json" }
    $text | Set-Content -LiteralPath ($Output + $resultSuffix) -Encoding utf8
    if ($Expected -eq 'Success') {
        $bitmap = [Drawing.Bitmap]::new($Output)
        try {
            if ($bitmap.Width -ne $json.Width -or $bitmap.Height -ne $json.Height) { throw 'PNG and result dimensions differ' }
        } finally { $bitmap.Dispose() }
    }
    return $json
}

function Test-CaptureSamples([string]$Executable, [string[]]$Arguments, [string]$Name, [bool]$Lifecycle = $false) {
    $process = Start-CaptureSample $Executable $Arguments $Name
    $peer = $null
    try {
        $first = Invoke-CaptureSample $process "$project/$Name-第一次 截图.png"
        $second = Invoke-CaptureSample $process "$project/$Name-Second.png"
        if ($first.SessionId -ne $second.SessionId -or $first.RequestId -eq $second.RequestId -or $second.FrameSerial -le $first.FrameSerial) {
            throw 'Repeated capture did not target new images of the same running instance'
        }
        $hash = (Get-FileHash -LiteralPath "$project/$Name-Second.png").Hash
        $lease = [IO.File]::Open("$project/$Name-Second.png", [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        try { $null = Invoke-CaptureSample $process "$project/$Name-Second.png" 'WriteFailed' }
        finally { $lease.Dispose() }
        if ((Get-FileHash -LiteralPath "$project/$Name-Second.png").Hash -ne $hash) { throw 'Failed write corrupted the existing PNG' }
        $null = Invoke-CaptureSample $process "$project/$Name-Recovered.png"
        if ($Lifecycle) {
            $peer = Start-CaptureSample $Executable $Arguments ($Name + '-Peer')
            $peerResult = Invoke-CaptureSample $peer "$project/$Name-Peer.png"
            if ($peerResult.SessionId -eq $first.SessionId) { throw 'Multiple processes shared a capture identity' }
            $handle = [CaptureTestWindow]::Find([uint32]$process.Id)
            if ($handle -eq [IntPtr]::Zero) { throw 'Capture target window is unavailable' }
            if (-not [CaptureTestWindow]::SetWindowPos($handle, [IntPtr]::Zero, 0, 0, 900, 600, 0x14)) {
                throw 'Could not resize capture target'
            }
            $resized = Invoke-CaptureSample $process "$project/$Name-Resized.png"
            if ($resized.Width -eq $second.Width -and $resized.Height -eq $second.Height) { throw 'Resize did not change captured dimensions' }
            $memorySamples = [System.Collections.Generic.List[object]]::new()
            $previousEffectPixel = $null
            foreach ($cycle in 1..20) {
                $null = [CaptureTestWindow]::ShowWindow($handle, 6)
                Wait-CaptureCondition {
                    if ($process.HasExited) { throw "Capture target exited while minimizing; see $project/$Name.stderr.log" }
                    return [CaptureTestWindow]::IsIconic($handle)
                }
                $null = Invoke-CaptureSample $process "$project/$Name-Minimized.png" 'Timeout' 300
                $null = [CaptureTestWindow]::ShowWindow($handle, 4)
                Wait-CaptureCondition { -not [CaptureTestWindow]::IsIconic($handle) }
                $null = [CaptureTestWindow]::ShowWindow($handle, 0)
                $null = Invoke-CaptureSample $process "$project/$Name-Restored.png"
                if (-not [CaptureTestWindow]::SendKey($handle, 0x43, $true)) { throw 'Could not send effect toggle key down' }
                Start-Sleep -Milliseconds 75
                if (-not [CaptureTestWindow]::SendKey($handle, 0x43, $false)) { throw 'Could not send effect toggle key up' }
                Start-Sleep -Milliseconds 75
                $null = Invoke-CaptureSample $process "$project/$Name-EffectCycle.png"
                $effectBitmap = [Drawing.Bitmap]::new("$project/$Name-EffectCycle.png")
                try {
                    $pixel = $effectBitmap.GetPixel([int]($effectBitmap.Width / 2), [int]($effectBitmap.Height / 2))
                    if ($null -ne $previousEffectPixel -and
                        ([Math]::Abs($pixel.R - $previousEffectPixel.R) +
                         [Math]::Abs($pixel.G - $previousEffectPixel.G) +
                         [Math]::Abs($pixel.B - $previousEffectPixel.B)) -lt 4) {
                        throw "Screen Tint did not alternate on effect cycle $cycle"
                    }
                    $previousEffectPixel = $pixel
                } finally { $effectBitmap.Dispose() }
                $process.Refresh()
                $memorySamples.Add([ordered]@{
                    cycle = $cycle
                    effectRgb = @([int]$pixel.R, [int]$pixel.G, [int]$pixel.B)
                    privateBytes = $process.PrivateMemorySize64
                    workingSetBytes = $process.WorkingSet64
                })
            }
            $baselinePrivateBytes = [long]$memorySamples[0].privateBytes
            $peakPrivateBytes = $baselinePrivateBytes
            foreach ($sample in $memorySamples) {
                $samplePrivateBytes = [long]$sample['privateBytes']
                if ($samplePrivateBytes -gt $peakPrivateBytes) { $peakPrivateBytes = $samplePrivateBytes }
            }
            $finalPrivateBytes = [long]$memorySamples[-1].privateBytes
            $memoryEvidence = [ordered]@{
                processId = $process.Id
                windowCycles = $memorySamples.Count
                baselinePrivateBytes = $baselinePrivateBytes
                peakPrivateBytes = $peakPrivateBytes
                finalPrivateBytes = $finalPrivateBytes
                allowedGrowthBytes = 128MB
                samples = @($memorySamples)
            }
            $memoryEvidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$project/$Name-Memory.json" -Encoding utf8
            if (($peakPrivateBytes - $baselinePrivateBytes) -gt 128MB) {
                throw "Process private memory grew by more than 128 MiB during the 20 window cycles; see $project/$Name-Memory.json"
            }
            Stop-Process -Id $process.Id -Force
            Wait-CaptureCondition { $process.HasExited }
            # A crash may leave registration files; they cannot satisfy a fresh request.
            $null = Invoke-CaptureSample $process "$project/$Name-Exited.png" 'Timeout' 150
            $null = Invoke-CaptureSample $peer "$project/$Name-Peer-After.png"
        }
        Write-Output "On-demand $Name passed; PID=$($process.Id), SessionId=$($first.SessionId)"
    } finally {
        foreach ($owned in @($process, $peer)) {
            if ($null -ne $owned) {
                if (-not $owned.HasExited) { Stop-Process -Id $owned.Id -Force }
                $owned.Dispose()
            }
        }
    }
}
