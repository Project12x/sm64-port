# Unified local verification gate — 2026-07-17

The documented MSYS2 fallback now has one command covering the host tools and
both Saturn image verifiers:

```sh
PYTHON=/c/Users/estee/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe \
  make -f Makefile.saturn.mk verify-all
```

Using the pinned local `sh-elf`/Yaul installation, the gate passed:

```text
Ran 8 tests ... OK
Verified ELF32 big-endian SH-2 image at 0x06004000
Verified ELF32 big-endian SH-2 hardware-test image at 0x06004000
```

| Artifact | SHA-256 |
|---|---|
| Hello ISO | `2fa969a031ca31853e1059fe3cead2b3884f89502a5f4aa0db7e83a14c20e017` |
| Hardware-test ISO | `d36b3a0591d77da499ef73461018e789800d780d88edfe24f1372005a043009b` |
| Classifier report | `c5f9a90beb83ac0e26e3966527700dc7a1b64c3032caa1fea5c99d8f208742a2` |

The Docker wrapper remains the portable path; this record proves the local
fallback procedure is also one-command reproducible on the current host when
the host Python path is explicit.
