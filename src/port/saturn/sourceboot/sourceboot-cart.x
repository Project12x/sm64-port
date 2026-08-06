/*
 * E2 sourceboot split-memory linker script.
 *
 * Based on Yaul's MIT-licensed sh-elf/lib/ldscripts/yaul.x at pinned
 * libyaul revision 6012f79f237773378c8014e70d8998ad95a38d98.  This is a
 * configuration adaptation, not a runtime fork: it preserves Yaul's normal
 * work-RAM layout and adds a cartridge VMA for immutable SM64 source data.
 */

OUTPUT_FORMAT ("elf32-sh")
OUTPUT_ARCH (sh)
EXTERN (_start)
ENTRY (_start)
SEARCH_DIR ("$YAUL_INSTALL_ROOT/$YAUL_ARCH_SH_PREFIX/lib");
/* Build-generated capacity symbols are supplied through the generated
 * include directory.  The link must fail if the source storage and manifest
 * disagree; no hand-maintained BOB capacity is accepted. */
INCLUDE saturn_geo_depth_manifest.ld

MEMORY {
  ram   (Wx) : ORIGIN = 0x06004000, LENGTH = 0x000FC000
  lwram (W)  : ORIGIN = 0x00200000, LENGTH = 0x00100000
  cart  (R)  : ORIGIN = 0x22400000, LENGTH = 0x00400000
}

SECTIONS
{
  .text :
  {
     *(.text)
     *(.text.*)
     *(.gnu.linkonce.t.*)

     INCLUDE ldscripts/yaul-c++.x

     . = ALIGN (4);
  } > ram

  /* These few constants must be available before the cart image has been
   * copied from CD.  The loader deliberately avoids ordinary string literals
   * and keeps its ISO filename and failure text here. */
  .bootdata :
  {
     . = ALIGN (16);
     *(.bootdata)
     *(.bootdata.*)
  } > ram

  /* Every object built from this source tree is named *@sm64-port@*.o by
   * Yaul's build-path conversion.  Put its immutable source data in the cart
   * bank, but keep libyaul/libgcc constants in work RAM so boot services are
   * usable before the bank is resident. */
  .cart_rodata :
  {
     . = ALIGN (16);
     ___sourceboot_cart_rodata_start = .;
     *sm64-port?*(.rdata)
     *sm64-port?*(.rodata)
     *sm64-port?*(.rodata.*)
     *sm64-port?*(.gnu.linkonce.r.*)
     . = ALIGN (16);
     ___sourceboot_cart_rodata_end = .;
  } > cart

  .rodata :
  {
     . = ALIGN (16);

     *(.rdata)
     *(.rodata)
     *(.rodata.*)
     *(.gnu.linkonce.r.*)
  } > ram

  .data :
  {
     . = ALIGN (16);

     *(.data)
     *(.data.*)
     *(.gnu.linkonce.d.*)
     SORT (CONSTRUCTORS)
     *(.sdata)
     *(.sdata.*)
     *(.gnu.linkonce.s.*)
  } > ram

  .bss :
  {
     . = ALIGN (16);
     PROVIDE (___bss_start = .);

     *(.bss)
     *(.bss.*)
     *(.gnu.linkonce.b.*)
     *(.sbss)
     *(.sbss.*)
     *(.gnu.linkonce.sb.*)
     *(.scommon)
     *(COMMON)

     . = ALIGN (16);
     PROVIDE (___bss_end = .);
  } > ram

  .uncached (0x20000000 | ___bss_end) : AT (___bss_end)
  {
     *(.uncached)
     *(.uncached.*)

     . = ALIGN (4);
  }

  /* Back to cached addresses */
  ___end = ___bss_end + SIZEOF (.uncached);

  /* HWRAM floor. The `ram` region's own LENGTH only makes the linker
   * fail once ___end passes 0x06100000 -- i.e. it enforces a margin of
   * >= 0, which is NOT the real requirement. libyaul's __mm_init()
   * (kernel/mm/internal.c) unconditionally builds the user TLSF heap
   * over [___end, 0x06100000) before main() runs, and TLSF writes a
   * multi-KiB control_t at the base of that span. With a margin in
   * (0, sizeof(control_t)) the link succeeds, `make verify` passes, and
   * the program then corrupts memory before reaching main() -- and
   * because HWRAM mirrors back to 0x06000000 at the top, the damage
   * lands in low memory. This project has already been bitten by
   * exactly that (see the SOURCEBOOT_MAIN_POOL_BYTES history comment in
   * main.c: an SH-2 exception cascade before main(), diagnosed only
   * after the fact).
   *
   * 0x1B00 bytes is a deliberate over-estimate of the measured ~3,188-byte
   * control block, leaving room for TLSF's own alignment padding. Raise
   * it, don't lower it. If this fires, the fix is to shrink a static
   * HWRAM consumer -- see the budget comment on
   * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES in
   * src/port/saturn/gfx/saturn_fast3d_frontend.h -- not to weaken this
   * assert. */
  PROVIDE (__sourceboot_required_hwram_margin = 0x1B00);
  ASSERT (___end <= ORIGIN (ram) + LENGTH (ram),
          "HWRAM sections extend past the physical top of work RAM. Move bulk CPU-only state to LWRAM before evaluating the required heap margin.")
  ASSERT (ORIGIN (ram) + LENGTH (ram) - ___end >= __sourceboot_required_hwram_margin,
          "HWRAM margin below libyaul's TLSF control-block floor: the heap libyaul builds at ___end would overrun the top of HWRAM and mirror into low memory. Shrink a static HWRAM consumer.")

  /* Legacy sink: command staging is ordinary HWRAM .bss. Keep this output
   * section solely to turn a future `.lwram_cmdts` input into a link error.
   * LWRAM coordination note: castleviewer (a separate binary, never
   * co-linked with sourceboot) already claims a fixed LWRAM range via a
   * raw pointer at 0x000C0000 (src/port/saturn/castleviewer/collision_pool.c),
   * invisible to any linker script. This 1MB region reservation here
   * doesn't overlap it today, but if LWRAM usage grows or these targets
   * are ever consolidated, the two conventions (linker-tracked vs.
   * raw-pointer) will need reconciling -- there is currently no single
   * source of truth for LWRAM allocation across this codebase's binaries. */
  .lwram_cmdts (NOLOAD) :
  {
    *(.lwram_cmdts)
  } > lwram
  ASSERT (SIZEOF(.lwram_cmdts) == 0,
          "VDP1 command staging must remain in HWRAM; .lwram_cmdts is forbidden")

  /* LWRAM-resident bulk work data (SM64 main pool). CPU access only --
   * SCU DMA cannot touch LWRAM (see SGL_REFERENCE_NOTES.md); nothing may
   * SCU-DMA from data placed here. NOLOAD: never crt0-zeroed; consumers
   * must not rely on zero-initialized contents. */
  .lwram_bss (NOLOAD) :
  {
    . = ALIGN (16);
    *(.lwram_bss)
  } > lwram

  .lwram_actor_runtime (NOLOAD) :
  {
    . = ALIGN (16);
    __lwram_actor_runtime_start = .;
    KEEP(*(.lwram_actor_runtime))
    __lwram_actor_runtime_end = .;
  } > lwram
  ASSERT (SIZEOF(.lwram_actor_runtime) == 0x10000,
          "actor runtime must remain exactly 0x10000 bytes")
  ASSERT ((__lwram_actor_runtime_start & 0xF) == 0,
          "actor runtime must be 16-byte aligned")

  .lwram_geo_traversal (NOLOAD) :
  {
    . = ALIGN (16);
    __lwram_geo_traversal_start = .;
    KEEP(*(.lwram_geo_traversal))
    __lwram_geo_traversal_end = .;
  } > lwram
  ASSERT (SIZEOF(.lwram_geo_traversal) ==
          __sourceboot_geo_traversal_expected_size,
          "geo traversal storage differs from generated full-game capacity")
  ASSERT ((__lwram_geo_traversal_start & 0xF) == 0,
          "geo traversal storage must be 16-byte aligned")
  ASSERT (__lwram_actor_runtime_end <= __lwram_geo_traversal_start,
          "geo traversal storage overlaps actor runtime")

  .lwram_camera_capture (NOLOAD) :
  {
    /* Do not align an empty optional section: GNU ld counts the alignment
     * fill in SIZEOF, which made a route-0 build report a phantom 0x10-byte
     * SCC1 capture after .lwram_bss grew.  The populated capture object is
     * already declared aligned(32), so its input-section alignment remains
     * enforced without manufacturing bytes when no capture is linked. */
    __lwram_camera_capture_start = .;
    KEEP(*(.lwram_camera_capture))
    __lwram_camera_capture_end = .;
  } > lwram

  ASSERT (SIZEOF(.lwram_camera_capture) == 0 ||
          SIZEOF(.lwram_camera_capture) == 0x2F7C0,
          "SCC1 capture must be absent or exactly 0x2F7C0 bytes")
  /* The slave SH-2 stack is private CPU state and grows downward. Keep it in
   * the final 16 KiB of LWRAM so its dynamic frames cannot collide with the
   * linker-owned static work arenas above. IP_SLAVE_STACK_ADDR is the physical
   * top (LWRAM origin + length); the explicit base symbol lets the map gate
   * distinguish this stack reservation from general free-space slack. */
  PROVIDE (__sourceboot_lwram_slave_stack_base =
            ORIGIN (lwram) + LENGTH (lwram) - 0x4000);
  PROVIDE (__sourceboot_lwram_slave_stack_top =
            ORIGIN (lwram) + LENGTH (lwram));
  ASSERT ((__sourceboot_lwram_slave_stack_base & 0xF) == 0,
          "LWRAM slave-stack base must be 16-byte aligned")
  ASSERT (__lwram_camera_capture_end <= __sourceboot_lwram_slave_stack_base,
          "LWRAM static work arenas overlap the reserved slave stack")
  ASSERT (__lwram_geo_traversal_end <= __sourceboot_lwram_slave_stack_base,
          "geo traversal storage overlaps the reserved slave stack")
  PROVIDE (__sourceboot_required_lwram_margin = 0x4000);
  ASSERT (__lwram_camera_capture_end <= ORIGIN (lwram) + LENGTH (lwram),
          "LWRAM sections extend past the physical top of work RAM")
  ASSERT (ORIGIN (lwram) + LENGTH (lwram) - __lwram_camera_capture_end >= __sourceboot_required_lwram_margin,
          "LWRAM leaves less than the measured final margin")
}
