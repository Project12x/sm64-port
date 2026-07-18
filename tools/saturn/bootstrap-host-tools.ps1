param(
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$venv = Join-Path $repoRoot ".venv-saturn-tools"
$venvPython = Join-Path $venv "Scripts\python.exe"

if (-not (Test-Path $venvPython)) {
    & $Python -m venv $venv
}
& $venvPython -m pip install --require-hashes -r (Join-Path $PSScriptRoot "requirements.txt")
& $venvPython -c "import networkx; print('Saturn host tools ready: NetworkX ' + networkx.__version__)"
