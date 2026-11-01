ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : { harness.o(.text .text.startup) }
    .data : { harness.o(.data .data.* .rodata .rodata.* .bss .bss.* COMMON) }
    . = 0x00500000; .stub_rand  : { *(.stub_rand) }
    . = 0x00501000; .stub_reset : { *(.stub_reset) }
    . = 0x00502000; .stub_count : { *(.stub_count) }
    . = 0x00503000; .stub_seek  : { *(.stub_seek) }
    . = 0x00504000; .stub_nextx : { *(.stub_nextx) }
    . = 0x00505000; .stub_vfs   : { *(.stub_vfs) }
    . = 0x00506000; .stub_prevx : { *(.stub_prevx) }
    . = 0x10000000; .testram : { *(.testram) }
    . = 0x7b068484; .unitA : { a_test.o(.text) }
    . = 0x7b1a84b4; .unitC : { c_test.o(.text) }
    . = 0x7b1a82ac; .unitB : { b_test.o(.text) }
    . = 0x7b1a8754; .unitD : { d_test.o(.text) }
    . = 0x7b0a8458; .unitE : { e_test.o(.text) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
