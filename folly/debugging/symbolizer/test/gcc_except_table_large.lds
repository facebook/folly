/* Partial linker script to force .gcc_except_table sdata8 encoding via
 * reverse relaxation (T259503227).
 *
 * Moves .gcc_except_table to high address (8GiB) AFTER all other sections,
 * so .text, .data, .data.rel.ro stay at their default positions. The >2GiB
 * gap between type table targets (.data for PIC indirect pointers,
 * .data.rel.ro for non-PIC typeinfo) and .gcc_except_table causes
 * PC-relative relocations to overflow 32 bits, triggering lld's reverse
 * relaxation to expand sdata4 -> sdata8 in LSDA type table entries.
 *
 * Side effect: FDE LSDA pointers in .eh_frame also overflow (they point
 * to .gcc_except_table at 0x200000000), exercising the eh_frame LSDA
 * reverse relaxation path as well.
 */
SECTIONS {
  .gcc_except_table 0x200000000 : { *(.gcc_except_table .gcc_except_table.*) }
}
INSERT AFTER .bss;
