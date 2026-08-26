"""Mutation-test the geo-walk digest contract.

The digest is a load-bearing oracle: a weak one would certify a replacement
geo walk that is actually wrong. Each mutation below removes exactly one
property the test claims to enforce. Every mutation MUST make the test fail; a
SURVIVED mutation means that property is asserted but not actually tested.

Restores from an in-memory copy, never from git -- an earlier harness in this
project restored with `git checkout` and silently wiped the uncommitted file
under test, so every mutant compiled the original and read as SURVIVED.
"""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "src/port/saturn/gfx/saturn_geo_walk_digest.c"
TEST = ROOT / "tools/saturn/geo_walk_digest_test.c"
EXE = ROOT / "build/saturn/host-tests/geo-walk-digest-mutant.exe"

MUTATIONS = [
    ("drop tag fold",
     "    fold_u32_raw(digest, tag);\n    /* The length is folded",
     "    /* MUTANT: tag not folded */\n    /* The length is folded"),
    ("drop event count fold in finish",
     "    fold_u32_raw(digest, digest->event_count);\n    digest->active = 0U;",
     "    /* MUTANT: count not folded */\n    digest->active = 0U;"),
    ("drop generation fold in begin",
     "    fold_u32_raw(digest, generation);\n}",
     "    /* MUTANT: generation not folded */\n}"),
    ("native-endian word fold",
     "    fold_byte(digest, (uint8_t)((value >> 24) & 0xFFU));\n"
     "    fold_byte(digest, (uint8_t)((value >> 16) & 0xFFU));\n"
     "    fold_byte(digest, (uint8_t)((value >> 8) & 0xFFU));\n"
     "    fold_byte(digest, (uint8_t)(value & 0xFFU));",
     "    fold_byte(digest, (uint8_t)(value & 0xFFU));\n"
     "    fold_byte(digest, (uint8_t)((value >> 8) & 0xFFU));\n"
     "    fold_byte(digest, (uint8_t)((value >> 16) & 0xFFU));\n"
     "    fold_byte(digest, (uint8_t)((value >> 24) & 0xFFU));"),
    ("ignore active flag (fold after finish)",
     "    if (digest == NULL || !digest->active) return;\n    fold_u32_raw(digest, tag);\n    fold_u32_raw(digest, 4U);",
     "    if (digest == NULL) return;\n    fold_u32_raw(digest, tag);\n    fold_u32_raw(digest, 4U);"),
    ("accept NULL bytes with nonzero size",
     "    if (bytes == NULL && size != 0U) return;",
     "    if (bytes == NULL && size != 0U) size = 0U;"),
    ("matrix folds only the first row",
     "    for (uint32_t row = 0U; row < 4U; row++)\n"
     "        for (uint32_t column = 0U; column < 4U; column++)\n"
     "            fold_u32_raw(digest, (uint32_t)matrix[row][column]);",
     "    for (uint32_t row = 0U; row < 1U; row++)\n"
     "        for (uint32_t column = 0U; column < 4U; column++)\n"
     "            fold_u32_raw(digest, (uint32_t)matrix[row][column]);"),
]


def build_and_run():
    build = subprocess.run(
        ["gcc", "-std=c11", "-Wall", "-Wextra", "-I", str(ROOT / "src/port/saturn/gfx"),
         str(TEST), str(SRC), "-o", str(EXE)],
        capture_output=True, text=True)
    if build.returncode != 0:
        return "COMPILE-FAIL", build.stderr.strip().splitlines()[-1:]
    run = subprocess.run([str(EXE)], capture_output=True, text=True)
    return ("PASS" if run.returncode == 0 else "FAIL"), []


# Mutants that are PROVABLY equivalent and must therefore survive. Keeping them
# here rather than deleting them preserves the reasoning and turns it into a
# live check: if a future event is folded WITHOUT a tag, the length fold stops
# being redundant and this mutant starts getting killed -- at which point the
# assertion below fires and says so.
EQUIVALENT = [
    ("drop length fold -- tags already pin payload boundaries",
     "    fold_u32_raw(digest, size);\n    const uint8_t *cursor",
     "    /* MUTANT: length not folded */\n    const uint8_t *cursor"),
]

original = SRC.read_text(encoding="utf-8")

status, detail = build_and_run()
if status != "PASS":
    raise SystemExit(f"baseline must PASS before mutating, got {status} {detail}")
print("baseline: PASS\n")

survived = []
try:
    for name, old, new in MUTATIONS:
        if original.count(old) != 1:
            print(f"  {name:42} SKIPPED (anchor matched {original.count(old)}x)")
            survived.append(name + " (anchor)")
            continue
        SRC.write_text(original.replace(old, new, 1), encoding="utf-8")
        status, detail = build_and_run()
        verdict = {"FAIL": "killed", "PASS": "SURVIVED",
                   "COMPILE-FAIL": "INVALID (did not compile)"}[status]
        print(f"  {name:42} {verdict}")
        if status != "FAIL":
            survived.append(name)
    print()
    for name, old, new in EQUIVALENT:
        assert original.count(old) == 1, name
        SRC.write_text(original.replace(old, new, 1), encoding="utf-8")
        status, _ = build_and_run()
        print(f"  [equivalent] {name:52} "
              f"{'survived as expected' if status == 'PASS' else 'KILLED -- no longer equivalent, review'}")
        if status != "PASS":
            survived.append(name + " (expected-equivalent mutant was killed)")
finally:
    SRC.write_text(original, encoding="utf-8")

status, _ = build_and_run()
print(f"\nrestored baseline: {status}")
print(f"killed {len(MUTATIONS) - len(survived)}/{len(MUTATIONS)}")
if survived:
    print("SURVIVED (property asserted but not enforced):")
    for name in survived:
        print("  -", name)
