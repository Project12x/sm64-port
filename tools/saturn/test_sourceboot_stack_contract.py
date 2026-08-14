"""Pin sourceboot's dual-SH2 stack ownership and linker reservation."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
MAKEFILE = ROOT / "src/port/saturn/sourceboot/Makefile"
LINKER = ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x"


def test_slave_stack_is_lwram_tail_and_master_stack_is_unchanged():
    make = MAKEFILE.read_text(encoding="utf-8")
    assert re.search(r"^IP_MASTER_STACK_ADDR\s*:=\s*0x06004000\s*$", make, re.M)
    assert re.search(r"^IP_SLAVE_STACK_ADDR\s*:=\s*0x00300000\s*$", make, re.M)


def test_linker_reserves_aligned_final_sixteen_kib_for_slave_stack():
    linker = LINKER.read_text(encoding="utf-8")
    assert "__sourceboot_lwram_slave_stack_base" in linker
    assert "__sourceboot_lwram_slave_stack_top" in linker
    assert "LENGTH (lwram) - 0x4000" in linker
    assert "__lwram_camera_capture_end <= __sourceboot_lwram_slave_stack_base" in linker
    assert "__sourceboot_lwram_slave_stack_base & 0xF" in linker

