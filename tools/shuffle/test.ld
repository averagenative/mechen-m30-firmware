ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : { harness.o(.text .text.startup) }
    .data : { harness.o(.data .data.* .rodata .rodata.* .bss .bss.* COMMON) }
    . = 0x00500000;
    .rand : { *(.text.rand) }
    . = 0x10000000;
    .testram : { *(.testram) }
    . = 0x7b1a808c;
    .blob : { shuffle_test.o(.text) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
