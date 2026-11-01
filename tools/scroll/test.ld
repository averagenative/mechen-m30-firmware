ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : { harness.o(.text .text.startup) }
    .data : { harness.o(.data .data.* .rodata .rodata.* .sdata .sbss .bss .bss.* COMMON) slow_flag = .; LONG(SLOW_ONLY) }
    . = 0x00700000;
    .stubs : { *(.text.stubs) }
    . = 0x00700100;
    .stubs2 : { *(.text.stubs2) }
    . = 0x10000000;
    .testram : { *(.testram) }
    . = 0x48714400;
    .bank : { bank.o(.data) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
