"""Static ownership contract for the generated iterative geo arena."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAKEFILE = ROOT / "src/port/saturn/sourceboot/Makefile"
LINKER = ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x"
STORAGE = ROOT / "src/port/saturn/runtime/saturn_geo_walk_storage.c"


def main() -> None:
    make = MAKEFILE.read_text(encoding="utf-8")
    linker = LINKER.read_text(encoding="utf-8")
    storage = STORAGE.read_text(encoding="utf-8")
    required_make = (
        "SOURCEBOOT_GEO_DEPTH_HEADER",
        "SOURCEBOOT_GEO_DEPTH_LINKER",
        "source-geo-depth",
        "saturn_geo_walk_storage.c",
    )
    for marker in required_make:
        assert marker in make, marker
    required_linker = (
        ".lwram_geo_traversal (NOLOAD)",
        "__lwram_geo_traversal_start",
        "__lwram_geo_traversal_end",
        "__sourceboot_geo_traversal_expected_size",
        "SIZEOF(.lwram_geo_traversal)",
        "__lwram_actor_runtime_end <= __lwram_geo_traversal_start",
        "__lwram_geo_traversal_end <= __sourceboot_lwram_slave_stack_base",
    )
    for marker in required_linker:
        assert marker in linker, marker
    assert 'section(".lwram_geo_traversal")' in storage
    assert "SM64_SATURN_GEO_TRAVERSAL_CAPACITY" in storage
    print("geo walk storage contract: PASS")


if __name__ == "__main__":
    main()
