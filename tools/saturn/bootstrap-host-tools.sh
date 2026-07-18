#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
venv="$repo_root/.venv-saturn-tools"
python_bin=${PYTHON:-python3}

if [ ! -x "$venv/bin/python" ]; then
    "$python_bin" -m venv "$venv"
fi
"$venv/bin/python" -m pip install --require-hashes \
    -r "$repo_root/tools/saturn/requirements.txt"
"$venv/bin/python" -c \
    "import networkx; print('Saturn host tools ready: NetworkX ' + networkx.__version__)"
