"""Source contract for the bounded sourceboot actor-runtime reservation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/port/saturn/sourceboot/main.c").read_text()
LINKER = (ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x").read_text()
BATCH = (ROOT / "src/port/saturn/gfx/saturn_actor_batch.h").read_text()


def main() -> None:
    assert MAIN.count("sourceboot_actor_runtime\n") == 1
    assert 'section(".lwram_actor_runtime"), used' in MAIN
    assert "__aligned(16)" in MAIN
    assert "CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_actor_runtime" in MAIN
    assert "sourceboot_actor_runtime.observer" in MAIN
    assert "sourceboot_actor_runtime.instances" in MAIN
    for member in ("instances", "observer", "queue", "batches", "outputs"):
        assert member in BATCH
    assert "__lwram_actor_runtime_start" in LINKER
    assert "__lwram_actor_runtime_end" in LINKER
    assert "SIZEOF(.lwram_actor_runtime) == 0x10000" in LINKER
    assert "KEEP(*(.lwram_actor_runtime))" in LINKER
    assert "sourceboot_actor_observer;" not in MAIN
    assert "sourceboot_actor_instances;" not in MAIN


if __name__ == "__main__":
    main()
