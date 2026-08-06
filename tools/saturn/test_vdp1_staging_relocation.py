"""Source/link contract for Task 14 VDP1 staging relocation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src/port/saturn/sourceboot/main.c"
LINKER = ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x"


def assert_contract(main: str, linker: str) -> None:
    declaration = main[main.index("static vdp1_cmdt_t sourceboot_vdp1_cmdts"):
                       main.index("static sm64_saturn_vdp1_backend_t")]
    assert "__aligned(32)" in declaration
    assert 'section(".lwram_cmdts")' not in declaration
    assert "sourceboot_vdp1_cmdts[0]" in main
    assert "sourceboot_vdp1_cmdts[1]" in main
    assert "sm64_saturn_vdp1_backend_init_with_storage" in main
    assert "sm64_saturn_vdp1_frame_bank_submit_transfers" in main
    assert "*(.lwram_cmdts)" in linker
    assert "SIZEOF(.lwram_cmdts) == 0" in linker


def main() -> None:
    source, linker = MAIN.read_text(), LINKER.read_text()
    assert_contract(source, linker)
    for mutation in (
        source.replace("__aligned(32)", '__attribute__((section(".lwram_cmdts")))', 1),
        source.replace("__aligned(32)", "__aligned(16)", 1),
    ):
        assert mutation != source
        try:
            assert_contract(mutation, linker)
        except AssertionError:
            pass
        else:
            raise AssertionError("staging placement mutation escaped gate")
    mutated_linker = linker.replace("*(.lwram_cmdts)", "", 1)
    try:
        assert_contract(source, mutated_linker)
    except AssertionError:
        pass
    else:
        raise AssertionError("legacy linker input mutation escaped gate")


if __name__ == "__main__":
    main()
