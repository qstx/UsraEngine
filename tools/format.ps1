param(
    [switch]$Check,
    [string]$ClangFormatPath
)

$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$formatter = if ($ClangFormatPath) {
    (Resolve-Path -LiteralPath $ClangFormatPath).Path
} else {
    $command = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($command) {
        $command.Source
    } else {
        $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) {
            throw 'clang-format was not found. Add it to PATH or pass -ClangFormatPath.'
        }
        $installation = & $vswhere -latest -products * -property installationPath | Select-Object -First 1
        $candidate = Join-Path $installation 'VC\Tools\Llvm\x64\bin\clang-format.exe'
        if (-not (Test-Path -LiteralPath $candidate)) {
            throw 'Visual Studio clang-format was not found. Pass -ClangFormatPath.'
        }
        $candidate
    }
}
$version = & $formatter --version
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'clang-format version 19\.') {
    throw 'This repository uses clang-format 19. Pass its path with -ClangFormatPath.'
}

$files = foreach ($module in @('runtime', 'engine', 'editor', 'tests')) {
    Get-ChildItem -LiteralPath (Join-Path $repository $module) -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp', '.hpp', '.h', '.c') }
}

$failed = 0
foreach ($file in $files) {
    if ($Check) {
        [xml]$replacements = ((& $formatter --style=file --output-replacements-xml $file.FullName) -join "`n")
        if ($LASTEXITCODE -ne 0) {
            ++$failed
            continue
        }
        $source = [System.IO.File]::ReadAllBytes($file.FullName)
        foreach ($replacement in $replacements.SelectNodes('/replacements/replacement')) {
            $offset = [int]$replacement.Attributes['offset'].Value
            $length = [int]$replacement.Attributes['length'].Value
            $formatted = [System.Text.Encoding]::UTF8.GetBytes($replacement.InnerText)
            $different = $length -ne $formatted.Length
            for ($index = 0; -not $different -and $index -lt $length; ++$index) {
                $different = $source[$offset + $index] -ne $formatted[$index]
            }
            if ($different) {
                Write-Error "Formatting differs: $($file.FullName)" -ErrorAction Continue
                ++$failed
                break
            }
        }
    } else {
        & $formatter -i --style=file $file.FullName
        if ($LASTEXITCODE -ne 0) {
            ++$failed
        }
    }
}

if ($failed -ne 0) {
    throw "clang-format failed for $failed file(s)."
}
$action = if ($Check) { 'Checked' } else { 'Formatted' }
Write-Output "$action $($files.Count) Engine C++ files."
