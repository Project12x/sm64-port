[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RetroArch,
    [Parameter(Mandatory = $true)][string]$Core,
    [Parameter(Mandatory = $true)][string]$Config,
    [Parameter(Mandatory = $true)][string]$Options,
    [Parameter(Mandatory = $true)][string]$Game,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$Manifest,
    [int]$Frames = 3600,
    [string]$Bios,
    [string]$Iso
)

$ErrorActionPreference = "Stop"
if ($Frames -lt 1) { throw "Frames must be positive." }
foreach ($path in @($RetroArch, $Core, $Config, $Options, $Game)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required capture input not found: $path"
    }
}
if ($Bios -and -not (Test-Path -LiteralPath $Bios -PathType Leaf)) {
    throw "BIOS not found: $Bios"
}
if ($Iso -and -not (Test-Path -LiteralPath $Iso -PathType Leaf)) {
    throw "ISO not found: $Iso"
}

$outputPath = [System.IO.Path]::GetFullPath($Output)
$manifestPath = [System.IO.Path]::GetFullPath($Manifest)
New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null
New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($manifestPath)) | Out-Null

$arguments = @(
    '-c', $Config,
    '--appendconfig', $Options,
    '-L', $Core,
    '--max-frames', [string]$Frames,
    '--max-frames-ss',
    '--max-frames-ss-path', $outputPath,
    $Game
)
$process = Start-Process -FilePath $RetroArch -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode -ne 0) {
    throw "RetroArch exited with status $($process.ExitCode)."
}
if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
    throw "RetroArch did not produce the requested screenshot: $outputPath"
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

$hashes = [ordered]@{
    screenshot = Get-Sha256 $outputPath
    core = Get-Sha256 $Core
    options = Get-Sha256 $Options
    game = Get-Sha256 $Game
}
if ($Bios) { $hashes.bios = Get-Sha256 $Bios }
if ($Iso) { $hashes.iso = Get-Sha256 $Iso }

$manifestObject = [ordered]@{
    schema = 1
    evidence_kind = 'retroarch-emulator'
    core_name = 'Kronos'
    frames = $Frames
    inputs = [ordered]@{
        retroarch = [System.IO.Path]::GetFullPath($RetroArch)
        core = [System.IO.Path]::GetFullPath($Core)
        config = [System.IO.Path]::GetFullPath($Config)
        options = [System.IO.Path]::GetFullPath($Options)
        game = [System.IO.Path]::GetFullPath($Game)
        screenshot = $outputPath
    }
    hashes = $hashes
}
$manifestObject | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output ($manifestObject | ConvertTo-Json -Depth 6)
