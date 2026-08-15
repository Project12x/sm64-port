"""Source/link contract for VDP1 command staging placement and capacity.

Origin: Task 14 relocated the command staging out of `.lwram_cmdts` (SCU
DMA from LWRAM is the documented lockup class); the linker script now
rejects any `.lwram_cmdts` input section and the banks live in HWRAM via
the dedicated `.sourceboot_vdp1_cmdts` input section.

Sprint 2 T2.2 updates the pinned capacity 2048 -> 1664 against T2.1's
measured run-long bank peak of 653 commands including setup
(docs/saturn/evidence/reports/sprint2-t2_1-peak-capture.md, Peak 1), and
repairs this previously-orphaned contract to pin the CURRENT deliberate
placement: the declaration's own-section attribute predates T2.2 but the
old assertion still forbade any section attribute, so the suite failed at
base HEAD a90f1628 before any T2.2 change (verified by stash).
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "src/port/saturn/sourceboot/main.c"
LINKER = ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x"


def assert_contract(main: str, linker: str) -> None:
    assert "#define SOURCEBOOT_VDP1_COMMAND_CAPACITY 1664U" in main
    declaration = main[main.index("static vdp1_cmdt_t sourceboot_vdp1_cmdts"):
                       main.index("static sm64_saturn_vdp1_backend_t")]
    assert "sourceboot_vdp1_cmdts[2][SOURCEBOOT_VDP1_COMMAND_CAPACITY]" in declaration
    assert "__aligned(32)" in declaration
    assert 'section(".sourceboot_vdp1_cmdts")' in declaration
    assert "lwram" not in declaration
    assert 2 * 1664 * 32 == 0x1A000
    assert "sourceboot_vdp1_cmdts[0]" in main
    assert "sourceboot_vdp1_cmdts[1]" in main
    assert "sm64_saturn_vdp1_backend_init_with_storage" in main
    assert "sm64_saturn_vdp1_frame_bank_submit_transfers" in main
    assert "*(.sourceboot_vdp1_cmdts)" in linker
    assert "*(.lwram_cmdts)" in linker
    assert "SIZEOF(.lwram_cmdts) == 0" in linker


def main() -> None:
    source, linker = MAIN.read_text(), LINKER.read_text()
    assert_contract(source, linker)
    for mutation in (
        source.replace('section(".sourceboot_vdp1_cmdts")',
                       'section(".lwram_cmdts")', 1),
        source.replace("__aligned(32)", "__aligned(16)", 1),
        source.replace("SOURCEBOOT_VDP1_COMMAND_CAPACITY 1664U",
                       "SOURCEBOOT_VDP1_COMMAND_CAPACITY 512U", 1),
        source.replace('section(".sourceboot_vdp1_cmdts")',
                       'section(".some_other_section")', 1),
    ):
        assert mutation != source
        try:
            assert_contract(mutation, linker)
        except (AssertionError, ValueError):
            pass
        else:
            raise AssertionError("staging placement mutation escaped gate")
    for mutated_linker in (
        linker.replace("*(.lwram_cmdts)", "", 1),
        linker.replace("*(.sourceboot_vdp1_cmdts)", "", 1),
    ):
        assert mutated_linker != linker
        try:
            assert_contract(source, mutated_linker)
        except AssertionError:
            pass
        else:
            raise AssertionError("legacy linker input mutation escaped gate")


if __name__ == "__main__":
    main()
