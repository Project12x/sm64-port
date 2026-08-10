[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Tool,

    [Parameter(ValueFromRemainingArguments = $true, Position = 1)]
    [string[]]$ToolArguments
)

$ErrorActionPreference = 'Stop'

$msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
$mingwBin = Join-Path $msysRoot 'mingw64\bin'
$usrBin = Join-Path $msysRoot 'usr\bin'

$requiredDlls = @(
    (Join-Path $usrBin 'msys-2.0.dll'),
    (Join-Path $usrBin 'msys-gcc_s-seh-1.dll'),
    (Join-Path $mingwBin 'libgmp-10.dll'),
    (Join-Path $mingwBin 'libmpfr-6.dll'),
    (Join-Path $mingwBin 'libisl-23.dll'),
    (Join-Path $mingwBin 'libwinpthread-1.dll'),
    (Join-Path $mingwBin 'libgcc_s_seh-1.dll'),
    (Join-Path $mingwBin 'zlib1.dll'),
    (Join-Path $mingwBin 'libiconv-2.dll'),
    (Join-Path $mingwBin 'libintl-8.dll')
)

$missing = @($requiredDlls | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -gt 0) {
    $details = ($missing | ForEach-Object { "  - $_" }) -join [Environment]::NewLine
    throw "MSYS2 runtime dependencies are missing:`n$details`nInstall/update the MSYS2 mingw64 toolchain or set MSYS2_ROOT to its installation directory."
}

# Keep both DLL directories ahead of inherited PATH. This makes the same
# dependency set visible to GCC, objdump, and their helper processes.
$pathParts = @($mingwBin, $usrBin) + @($env:PATH -split ';' | Where-Object { $_ })
$env:PATH = ($pathParts | Select-Object -Unique) -join ';'

$resolvedTool = $null
if ($Tool -eq 'mingw32-make') {
    # Sourceboot uses GNU Make grouped targets (`&:`), which require 4.3 or
    # newer. MSYS2 names its compatible executable make.exe; do not fall
    # through to an unrelated Qt mingw32-make from the inherited desktop PATH.
    $resolvedTool = Join-Path $usrBin 'make.exe'
    if (-not (Test-Path -LiteralPath $resolvedTool)) {
        throw "MSYS2 GNU Make is missing: $resolvedTool"
    }
    $versionOutput = @(& $resolvedTool --version 2>&1)
    $versionStatus = $LASTEXITCODE
    $versionMatch = [regex]::Match(($versionOutput -join "`n"), 'GNU Make (\d+)\.(\d+)')
    if ($versionStatus -ne 0 -or -not $versionMatch.Success) {
        throw "Unable to determine MSYS2 GNU Make version: $resolvedTool"
    }
    $makeMajor = [int]$versionMatch.Groups[1].Value
    $makeMinor = [int]$versionMatch.Groups[2].Value
    if ($makeMajor -lt 4 -or ($makeMajor -eq 4 -and $makeMinor -lt 3)) {
        throw "MSYS2 GNU Make 4.3 or newer is required: $resolvedTool"
    }
} elseif (Test-Path -LiteralPath $Tool) {
    $resolvedTool = (Resolve-Path -LiteralPath $Tool).Path
} else {
    $command = Get-Command $Tool -ErrorAction SilentlyContinue
    if ($command) { $resolvedTool = $command.Source }
}
if (-not $resolvedTool) {
    throw "Tool '$Tool' was not found after MSYS2 PATH setup."
}

& $resolvedTool @ToolArguments
exit $LASTEXITCODE
