/* Fixed sound-RAM image layout for the Saturn MC68000 PCM service.
 *
 * Close-port note: the zero-based vector section followed by program text is
 * adapted from ponut64/SCSP_poneSound PROJ/linker at
 * 31782e4c61337327f23eb9aa45ecd37fe0944ea0 (MIT, copyright ponut64).
 * This version deliberately caps all loadable driver state before 0x4000;
 * PoneSound's 0x408 entry, high stack, and large work areas are not retained.
 */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY
{
    driver (rwx) : ORIGIN = 0x00000000, LENGTH = 0x00004000
}

__mailbox_start = 0x00004000;
__pcm_bank_start = 0x00008000;
__stack_bottom = 0x00003C00;
__stack_top = 0x00003FFC;

SECTIONS
{
    __image_start = ORIGIN(driver);

    .vectors 0x00000000 :
    {
        KEEP(*(.vectors))
    } > driver

    .text 0x00000400 :
    {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
    } > driver

    .data :
    {
        *(.data .data.*)
    } > driver

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        __bss_end = .;
    } > driver

    __driver_end = ALIGN(4);
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(SIZEOF(.vectors) == 0x400, "68K vector table must be exactly 1 KiB")
ASSERT(__driver_end <= __stack_bottom, "68K image collides with reserved stack")
ASSERT(__driver_end <= __mailbox_start, "68K image overlaps PCM mailbox")
