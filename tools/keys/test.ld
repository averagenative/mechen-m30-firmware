ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : { harness.o(.text .text.startup) }
    .data : { harness.o(.data .data.* .rodata .rodata.* .sdata .sbss .bss .bss.* COMMON) banks.o(.rodata) }
    . = 0x00700000;
    .stubs : { *(.text.stubs) }
    . = 0x10000000;
    .testram : { *(.testram) }
    . = 0x48694600;
    .bankrun : { banks.o(.bankrun) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
