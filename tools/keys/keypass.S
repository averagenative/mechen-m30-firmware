/*
 * Screen-off media keys for the MECHEN M30 (Actions ATJ2127) common key filter.
 *
 * Every app filters key events through FUN_48694658 (bank 0x48694600, the same code in
 * browser, music, mainmenu, playlist and setting).  With the screen off, the stock filter
 * turns any key except power into the wake message 0x106 and discards it.  The wake
 * handler then records that key, and the filter also discards the key's long-press and
 * release events, so the press is lost entirely.
 *
 * This code sits in the free tail of the bank.  The first instruction of the filter's
 * screen-off wake path (0x486947d0, `li v0,255`) becomes `b keypass`.  For play/pause (01),
 * prev (02), next (03), volume up (05) and volume down (06) it jumps to the filter's
 * screen-on path instead: idle timers reset on key down, and the key passes to the app
 * with the screen left off.  Every other key re-runs the replaced instruction and
 * continues into the stock wake path.
 * On entry s1 is the filter's frame; 40(s1) holds the key message pointer
 * (key code at +2).  v0, v1 and t8 are scratch at this point in the stock code.
 * Linked at 0x48694b80.  The two branches back into the filter are pre-encoded MIPS16 B
 * instructions; patch_keys.py decodes them and checks their targets.
 */
	.set	mips16
	.text
	.globl	keypass
	.type	keypass, @function
keypass:
	lw	$2, 40($17)		/* key message */
	lbu	$2, 2($2)		/* key code */
	addiu	$3, $2, -1
	sltiu	$3, 3			/* T = key is 01..03 */
	btnez	1f
	addiu	$3, $2, -5
	sltiu	$3, 2			/* T = key is 05..06 */
	btnez	1f
	li	$2, 255			/* the stock instruction replaced by the hook */
	.half	0x161f			/* b 0x486947d2: stock wake path (send 0x106, discard key) */
1:	.insn				/* the label marks code, not data */
	.half	0x1638			/* b 0x48694806: stock screen-on path (pass key) */
	.size	keypass, . - keypass
