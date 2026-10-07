param([string]$DemoDirectory = '')
$ErrorActionPreference = 'Stop'
$taskEngine = Split-Path $PSScriptRoot -Parent
$taskRoots = @('Runtime','Engine','Editor') | ForEach-Object { Join-Path $taskEngine $_ }
if ($DemoDirectory) { $taskRoots += @('Snake','Editor','Material','Tools') | ForEach-Object { Join-Path $DemoDirectory $_ } }
$taskViolations = @()
foreach ($taskRoot in $taskRoots)
{
    foreach ($taskFile in Get-ChildItem -LiteralPath $taskRoot -Recurse -File | Where-Object Extension -In @('.cpp','.hpp'))
    {
        if ($taskFile.FullName -eq (Join-Path $taskEngine 'Runtime/Vfs/DirectoryFileProvider.cpp')) { continue }
        $taskLines = [IO.File]::ReadAllLines($taskFile.FullName)
        for ($taskIndex = 0; $taskIndex -lt $taskLines.Length; ++$taskIndex)
        {
            if ($taskLines[$taskIndex] -match 'std::(i|o|f)fstream|\b(fopen|fread|fwrite|ReadFile|WriteFile|WriteResourceAtomically)\s*\(|std::filesystem::(remove|rename|copy|create_)|\b(Locate|WritablePath|ResolveToDisk|ResolveForWrite)\s*\(')
            {
                # Directory-provider path helpers do not escape through VFS APIs.
                if ($taskFile.FullName -eq (Join-Path $taskEngine 'Runtime/Vfs/DirectoryFileProvider.hpp')) { continue }
                $taskViolations += "$($taskFile.FullName):$($taskIndex + 1):$($taskLines[$taskIndex])"
            }
        }
    }
}
if ($taskViolations.Count) { $taskViolations; throw 'Production resource I/O bypasses VFS' }
Write-Output 'Production resource I/O audit passed; host streams and mutations are confined to DirectoryFileProvider.'
