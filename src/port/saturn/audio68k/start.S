/* Minimal MC68000 reset/vector shape.
 *
 * Close-port provenance: the vector-at-zero/reset-entry pattern is adapted
 * from ponut64/SCSP_poneSound PROJ/main.c and PROJ/linker at
 * 31782e4c61337327f23eb9aa45ecd37fe0944ea0 (MIT, copyright ponut64).
 * Local changes: a complete 1 KiB table, a driver-local stack, byte-exact BSS
 * clearing, and no PoneSound driver/control/ADX code or binary.
 */

    .section .vectors,"a"
    .globl __vectors
__vectors:
    .long __stack_top
    .long _start
    .rept 254
    .long _fault
    .endr

    .section .text.start,"ax"
    .globl _start
    .type _start,@function
_start:
    move.l  #__stack_top,%sp
    lea     __bss_start,%a0
    lea     __bss_end,%a1
1:
    cmpa.l  %a1,%a0
    bcc.s   2f
    clr.b   (%a0)+
    bra.s   1b
2:
    jsr     pcm68k_main

    .globl _fault
_fault:
    bra.s   _fault
    .size _start,.-_start

    .section .note.GNU-stack,"",@progbits
