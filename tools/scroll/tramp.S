/* Trampolines into the stock dirlist next/prev, linked at 0x48714a80: replay the
 * overwritten first instruction (save 24,ra,s1) and branch to the next one.
 * The branches are pre-encoded MIPS16 B instructions (verified by disassembly). */
	.set	mips16
	.text
	.globl	stock_next
	.type	stock_next, @function
	.ent	stock_next
stock_next:			/* 0x48714a80 */
	save	24, $31, $17
	.half	0x1513		/* b 0x487144aa */
	.end	stock_next

	.globl	stock_prev
	.type	stock_prev, @function
	.ent	stock_prev
stock_prev:			/* 0x48714a84 */
	save	24, $31, $17
	.half	0x1623		/* b 0x487146ce */
	.end	stock_prev
