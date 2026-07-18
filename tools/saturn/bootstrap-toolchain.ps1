[CmdletBinding()]
param(
    [string]$DockerImage = $(if ($env:YAUL_DOCKER_IMAGE) { $env:YAUL_DOCKER_IMAGE } else { "ijacquez/yaul:1.0.15" })
)

$HostPython = $env:SATURN_HOST_PYTHON
if (-not $HostPython) {
    foreach ($candidate in @('python3', 'python', 'py')) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) {
            $HostPython = $candidate
            break
        }
    }
}
if (-not $HostPython) {
    throw "Python 3 is required for the host-side Saturn tool regression. Set SATURN_HOST_PYTHON and retry."
}

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "Docker is required for the portable Saturn bootstrap."
}

Write-Host "Pulling Yaul development image: $DockerImage"
& docker pull $DockerImage
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dockerArgs = @(
    "run", "--rm", "-i",
    "--volume", "${root}:/work",
    "--workdir", "/work",
    "--env", "YAUL_INSTALL_ROOT=/opt/tool-chains/sh2eb-elf",
    "--env", "YAUL_PROG_SH_PREFIX=sh2eb-elf",
    "--env", "YAUL_ARCH_SH_PREFIX=sh2eb-elf",
    "--env", "YAUL_ARCH_M68K_PREFIX=m68keb-elf",
    "--env", "YAUL_BUILD_ROOT=/work/build/libyaul",
    "--env", "YAUL_BUILD=build",
    "--env", "YAUL_CDB=0",
    "--env", "SILENT=1",
    $DockerImage,
    "/bin/bash", "-lc",
    "set -eu; git config --global --add safe.directory /work; git submodule update --init third_party/libyaul; make -C third_party/libyaul install-release install-tools; make -f Makefile.saturn.mk hello verify-hello hwtest verify-hwtest"
)

& docker @dockerArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $HostPython (Join-Path $root "tools/saturn/test_tools.py")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Push-Location $root
try {
    & $HostPython "tools/saturn/asset_classifier.py" `
        --root . `
        --primitives "tools/saturn/fixtures/primitives-six-way.json" `
        --report "docs/saturn/evidence/reports/asset-classifier-sm64.json"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}
exit $LASTEXITCODE
