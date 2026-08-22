# Build a sealed sourceboot image with the canonical flag set.
#
# WHY THIS EXISTS: building this project by hand costs an hour of rediscovery
# every time. Five separate traps, all of which produce misleading errors:
#
#   1. `make` launched from an agent Bash tool does not import the environment
#      into make variables at all -- even `FOO=bar make` yields an empty
#      $(FOO), because the harness passes an allowlisted env. So sourcing
#      .yaul.env alone NEVER satisfies check-sdk ("YAUL_INSTALL_ROOT is not
#      set"). Use this script (or the with-msys-toolchain.ps1 wrapper) instead.
#   2. Because $(OS) is likewise unset, Yaul's `ifeq '$(OS)' "Windows_NT"`
#      fails, EXE_EXT comes out blank, every toolchain path loses its .exe,
#      and gen_toolchain_attestation.py dies with "toolchain binary is not a
#      file: ...\sh-elf-gcc".
#   3. Toolchain env vars must be MSYS-style (/d/Code/...). Recipes run through
#      /bin/sh, which eats backslashes as escapes: D:\Code\... arrives as
#      D:CodeRetroDev... and every compile fails.
#   4. ...but $YAUL_INSTALL_ROOT/bin must ALSO be on PATH in *Windows* form or
#      the final link fails with "collect2: fatal error: cannot find 'ld'".
#   5. The target profile is a sealed contract: bootstrap_sourceboot_identity_spec.py
#      rejects any deviation from its release_config, including flags you never
#      passed (Makefile defaults differ from the profile). To change a feature
#      flag, fork the profile -- and write the fork with
#      tools/saturn/hermetic_manifest.canonical_json_bytes, or it is rejected as
#      "target profile is stale or invalid".
#
# The flag list below is the canonical one from
# .superpowers/sdd/2026-08-17-vdp1-overwrite-fence/task-3-brief.md.
# Override any of it with -ExtraFlags, and point at a forked profile with
# -Profile.
[CmdletBinding()]
param(
    [string]$Worktree = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path,
    [string]$YaulInstallRoot = 'D:\Code\RetroDev\sm64-saturn-port\work\yaul-install',
    [string]$Profile,
    [string]$Log,
    [int]$Jobs = 8,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraFlags
)

$ErrorActionPreference = 'Continue'
Set-Location $Worktree

function ConvertTo-MsysPath([string]$p) {
    $full = [System.IO.Path]::GetFullPath($p)
    $drive = $full.Substring(0, 1).ToLower()
    return '/' + $drive + ($full.Substring(2) -replace '\\', '/')
}

# MSYS-style for make/sh (trap 3); Windows-style PATH entry for ld (trap 4).
$env:YAUL_INSTALL_ROOT = ConvertTo-MsysPath $YaulInstallRoot
$env:YAUL_BUILD_ROOT = ConvertTo-MsysPath (Join-Path $Worktree 'build\saturn\yaul')
$env:YAUL_PROG_SH_PREFIX = 'sh-elf'
$env:YAUL_ARCH_SH_PREFIX = 'sh-elf'
$env:YAUL_ARCH_M68K_PREFIX = 'm68keb-elf'
$env:YAUL_BUILD = 'release'
$env:YAUL_OPTION_MALLOC_IMPL = 'tlsf'
$env:DEBUG_RELEASE = '1'
$env:PATH = (Join-Path $YaulInstallRoot 'bin') + ';' + $env:PATH

$flags = @(
  '-f', 'Makefile.saturn.mk', "-j$Jobs", 'sourceboot',
  'SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1',
  'SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0',
  'SATURN_FEATURE_SEMANTIC_AUDIO=1',
  'SATURN_RENDERER_PIPELINE=4',
  'SATURN_SOURCEBOOT_LEVEL_ID=9',
  'SATURN_SOURCEBOOT_AREA_ID=1',
  'SATURN_SOURCEBOOT_ROUTE_ID=0',
  'SATURN_DEMO_PATH=1',
  'SATURN_SOURCEBOOT_ROUTE_REPLAY=1',
  'SATURN_SOURCEBOOT_LIVE_INPUT=1',
  'SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600',
  'SATURN_SOURCEBOOT_CAMERA_ROUTE=0',
  'SATURN_CAMERA_VARIANT=3',
  'SATURN_ATAN2_VARIANT=2',
  'SATURN_DIAGNOSTIC_MODE=0',
  'SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0',
  'SATURN_CART_MBIT=32',
  'SATURN_SOURCE_CART_STAGE_SECTORS=8',
  'SATURN_DEMO_VIEW_RADIUS=6000',
  'SATURN_SLAVE_RENDER=1',
  'SATURN_DEMO_POLY_TIER=2',
  'SATURN_DEMO_HOT_PROMOTION=1',
  'SATURN_DEMO_NEAR_CLIP=1',
  'SATURN_DEMO_BSP_ORDER=1',
  'SATURN_DEMO_BSP_FRAGMENTS=0',
  'SATURN_DEMO_FRAGMENT_MODE=0',
  'SATURN_DEMO_BSP_FRAGMENT_FLAT=0',
  'SATURN_OBJECT_POOL_CAPACITY=208',
  'SATURN_CAMERA_IDLE_START_TICK=0',
  'SATURN_CAMERA_IDLE_DISCOVERY=0',
  'SATURN_CAMERA_RANGE_CAPTURE=0',
  'SATURN_FAST3D_Q16_TRACE=0'
)
if ($Profile) {
    $flags += "SOURCEBOOT_TARGET_PROFILE=$(ConvertTo-MsysPath $Profile)"
}
if ($ExtraFlags) { $flags += $ExtraFlags }

Write-Host "sourceboot build: $($flags.Count) make arguments"
if ($Log) {
    "=== BUILD START $(Get-Date -Format HH:mm:ss) ===" | Set-Content $Log
    & (Join-Path $PSScriptRoot 'with-msys-toolchain.ps1') mingw32-make @flags *>&1 | Add-Content $Log
    $code = $LASTEXITCODE
    "=== EXIT $code at $(Get-Date -Format HH:mm:ss) ===" | Add-Content $Log
} else {
    & (Join-Path $PSScriptRoot 'with-msys-toolchain.ps1') mingw32-make @flags
    $code = $LASTEXITCODE
}
exit $code
