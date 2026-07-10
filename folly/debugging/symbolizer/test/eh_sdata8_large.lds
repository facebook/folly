/* Partial linker script to force .eh_frame sdata8 encoding via reverse
 * relaxation (D95576043).
 *
 * Moves .eh_frame and .eh_frame_hdr to high addresses (8GiB) AFTER all
 * other sections, so nothing else gets displaced. The >2GiB gap between
 * .text (at normal low address) and .eh_frame (at 0x200000000) causes
 * FDE initial_location PC-relative relocations to overflow 32 bits,
 * triggering lld's reverse relaxation to expand sdata4 -> sdata8.
 *
 * Uses INSERT AFTER .bss (the last section) so .text, .rodata,
 * .gcc_except_table, .data, etc. all stay at their default positions.
 */
SECTIONS {
  .eh_frame 0x200000000 : { *(.eh_frame) }
  .eh_frame_hdr : { *(.eh_frame_hdr) }
}
INSERT AFTER .bss;
