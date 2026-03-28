/* Partial linker script to force .eh_frame_hdr sdata8 encoding.
 *
 * Moves .eh_frame_hdr to a high address (8GiB) AFTER all other sections,
 * so nothing else gets displaced. The >2GiB gap between .text (at normal
 * low address) and .eh_frame_hdr (at 0x200000000) triggers lld's
 * auto-detection (D97823369) to use DW_EH_PE_sdata8.
 *
 * Uses INSERT AFTER .bss (the last section) so .text, .rodata,
 * .gcc_except_table, .data, etc. all stay at their default positions.
 *
 * Requires -large-eh-encoding so .eh_frame FDE/LSDA/personality
 * encodings are sdata8, able to reference .text across the gap.
 */
SECTIONS {
  .eh_frame_hdr 0x200000000 : { *(.eh_frame_hdr) }
}
INSERT AFTER .bss;
