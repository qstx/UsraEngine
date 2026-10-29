function Get-UrsaWindowsToolchain {
    [CmdletBinding()]
    param([string]$CMakePath = 'cmake', [string]$BuildDirectory = '')
    $ErrorActionPreference = 'Stop'
    $cmakeCommand = (Get-Command $CMakePath -ErrorAction Stop).Source
    $versionOutput = & $cmakeCommand --version
    $versionText = @($versionOutput)[0]
    if ($LASTEXITCODE -ne 0 -or $versionText -notmatch 'cmake version (\d+\.\d+\.\d+)') {
        throw "Cannot inspect CMake: $cmakeCommand"
    }
    if ([version]$Matches[1] -lt [version]'4.4.5') { throw "CMake 4.4.5 or newer is required; found $versionText at $cmakeCommand" }
    if ($BuildDirectory) {
        $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
        if (Test-Path -LiteralPath $cachePath) {
            $cache = [IO.File]::ReadAllText($cachePath)
            if ($cache -notmatch '(?m)^CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026\r?$' -or
                $cache -match '(?m)^CMAKE_GENERATOR_TOOLSET:INTERNAL=(?!v145(?:,|\r?$))\S+') {
                throw "Incompatible build cache at $cachePath. Choose a new build directory for VS2026/v145, or explicitly clean the old cache. No files were removed."
            }
        }
    }
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $locator)) { throw 'VS2026 with Desktop development with C++ is required (vswhere is missing).' }
    $installation = & $locator -latest -version '[18.0,19.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'VS2026 with Desktop development with C++ and the v145 tools is required.' }
    $installation = @($installation)[0].Trim()
    $toolsVersion = Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt'
    if (-not (Test-Path -LiteralPath $toolsVersion) -or [IO.File]::ReadAllText($toolsVersion).Trim() -notmatch '^14\.5\d\.') {
        throw "VS2026 v145 C++ tools are missing at $installation"
    }
    [pscustomobject]@{ CMake = $cmakeCommand; Version = $versionText; VisualStudio = $installation; Generator = 'Visual Studio 18 2026'; Toolset = 'v145' }
}
