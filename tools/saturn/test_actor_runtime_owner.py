"""Source contract for the bounded sourceboot actor-runtime reservation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/port/saturn/sourceboot/main.c").read_text()
LINKER = (ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x").read_text()
BATCH = (ROOT / "src/port/saturn/gfx/saturn_actor_batch.h").read_text()


def assert_owner(main: str, linker: str) -> None:
    assert main.count("sourceboot_actor_runtime\n") == 1
    assert main.count('section(".lwram_actor_runtime")') == 1
    assert '__attribute__((section(".lwram_actor_runtime"), used)) __aligned(16)' in main
    assert "CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_actor_runtime" in main
    assert "sizeof(sourceboot_actor_runtime)" in main
    assert "sourceboot_actor_runtime.observer" in main
    assert "sourceboot_actor_runtime.instances" in main
    for member in ("instances", "observer", "queue", "batches", "outputs"):
        assert member in BATCH
    assert "__lwram_actor_runtime_start" in linker
    assert ".lwram_actor_runtime (NOLOAD)" in linker
    assert "__lwram_actor_runtime_end" in linker
    assert "SIZEOF(.lwram_actor_runtime) == 0x10000" in linker
    assert "KEEP(*(.lwram_actor_runtime))" in linker
    assert "sourceboot_actor_observer;" not in main
    assert "sourceboot_actor_instances;" not in main


def main() -> None:
    assert_owner(MAIN, LINKER)
    owner_declaration = '__attribute__((section(".lwram_actor_runtime"), used)) __aligned(16)'
    for source, linker in (
        (MAIN, LINKER.replace(".lwram_actor_runtime (NOLOAD)", ".lwram_actor_runtime", 1)),
        (MAIN.replace(owner_declaration,
                      '__attribute__((section(".lwram_actor_runtime"), used)) __aligned(8)', 1), LINKER),
        (MAIN.replace("sizeof(sourceboot_actor_runtime)", "16U", 1), LINKER),
        (MAIN + '\nstatic sm64_saturn_actor_runtime_storage_t extra_owner __attribute__((section(".lwram_actor_runtime")));\n', LINKER),
    ):
        try:
            assert_owner(source, linker)
        except AssertionError:
            pass
        else:
            raise AssertionError("actor owner mutation escaped source gate")


if __name__ == "__main__":
    main()
