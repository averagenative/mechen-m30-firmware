ENTRY(_start)
SECTIONS
{
    . = 0x00400000;
    .text : { harness.o(.text .text.startup) }
    .data : { harness.o(.data .data.* .rodata .rodata.* .bss .bss.* COMMON) }
    . = 0x00700000; .stub_libc   : { *(.stub_libc) }
    . = 0x00700400; .stub_fs     : { *(.stub_fs) }
    . = 0x00700800; .stub_append : { *(.stub_append) }
    . = 0x00700c00; .stub_nop    : { *(.stub_nop) }
    . = 0x00701000; .stub_text   : { *(.stub_text) }
    . = 0x10000000; .testram (NOLOAD) : { *(.testram) }
    . = 0x400d4c00; .bank0 : { *(.bank_400d4c00) }
    . = 0x40114c00; .bank1 : { *(.bank_40114c00) }
    . = 0x40194c00; .bank2 : { *(.bank_40194c00) }
    . = 0x40294c00; .bank3 : { *(.bank_40294c00) }
    . = 0x48014400; .bank4 : { *(.bank_48014400) }
    . = 0x71812500; .bank5 : { *(.bank_71812500) }
    . = 0x20000000; .expect : { *(.expect) }
    . = 0x20400000; .disk (NOLOAD) : { *(.disk) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
