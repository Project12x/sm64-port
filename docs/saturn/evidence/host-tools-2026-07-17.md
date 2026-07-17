# Host-side Saturn tools verification — 2026-07-17

The classifier and telemetry decoder are intentionally standard-library-only,
so they can run before a Saturn cross-toolchain is installed.

## Command

```sh
make -f Makefile.saturn.mk PYTHON=python3 verify-tools
```

On the Windows proof host the equivalent invocation used Python 3.12.10:

```text
.../Python312/python.exe tools/saturn/test_tools.py
```

## Result

```text
Ran 3 tests in 0.005s
OK
```

The tests cover:

- direct textured-quad classification only when shared vertices agree on UVs;
- source statistics including `gsSP1Quadrangle`; and
- big-endian decoding of a complete passing `0x5C` telemetry block.

This is host-tool regression evidence, not a claim about retail Saturn
geometry or timing. Retail hardware remains the Phase 1 authority.
