# Portable bootstrap host probe — 2026-07-17

The repository's portable bootstrap was probed from the current Windows host.
Neither `docker` nor `podman` is installed or available on `PATH`, so the
Docker-backed command cannot be executed in this environment. The wrapper's
preflight exits before pulling an image and reports that a Docker-capable host
is required; no container result is being represented as evidence here.

The local fallback remains independently verified with the pinned Yaul
installation and explicit host Python path:

```sh
PYTHON=/c/Users/estee/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe \
  make -f Makefile.saturn.mk verify-all
```

That gate passed the 8 host regression tests, hello-image ELF verification,
hardware-test ELF verification, and classifier regeneration. A Docker-capable
host should run:

```sh
make -f Makefile.saturn.mk bootstrap
```

and preserve the image digest, container output, and resulting artifact hashes
alongside this record.
