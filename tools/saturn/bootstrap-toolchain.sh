#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
IMAGE=${YAUL_DOCKER_IMAGE:-ijacquez/yaul:1.0.15}

if ! command -v docker >/dev/null 2>&1; then
    printf '%s\n' 'Docker is required for the portable Saturn bootstrap.' >&2
    printf '%s\n' 'Install Docker Desktop or run this script from a Linux host with Docker.' >&2
    exit 1
fi

printf 'Pulling Yaul development image: %s\n' "$IMAGE"
docker pull "$IMAGE"

# The container layout follows the MIT-licensed yaul-org/libyaul-docker
# Dockerfile at e0b4c2d63f1a39f213a67c6ca31e6bc582976de6. The pinned local
# libyaul submodule is still installed inside the container before building.
docker run --rm -i \
    --volume "$ROOT:/work" \
    --workdir /work \
    --env YAUL_INSTALL_ROOT=/opt/tool-chains/sh2eb-elf \
    --env YAUL_PROG_SH_PREFIX=sh2eb-elf \
    --env YAUL_ARCH_SH_PREFIX=sh2eb-elf \
    --env YAUL_ARCH_M68K_PREFIX=m68keb-elf \
    --env YAUL_BUILD_ROOT=/work/build/libyaul \
    --env YAUL_BUILD=build \
    --env YAUL_CDB=0 \
    --env SILENT=1 \
    "$IMAGE" /bin/bash -lc '
        set -eu
        git config --global --add safe.directory /work
        git submodule update --init third_party/libyaul
        make -C third_party/libyaul install-release install-tools
        make -f Makefile.saturn.mk hello verify-hello hwtest verify-hwtest verify-tools
    '
