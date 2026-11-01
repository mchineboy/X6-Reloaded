OUTPUT_FORMAT(elf32-littlearm)
OUTPUT_ARCH(arm)
ENTRY(main)
SECTIONS {
    . = 0x0;
    .text : { *(.text.entry) *(.text*) *(.rodata*) }
    .data : ALIGN(4) { *(.data*) }
    .bss (NOLOAD) : ALIGN(4) { *(.bss*) *(COMMON) }
    MAGIC 0 (INFO) : { KEEP(*(MAGIC)) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.eh_frame*) *(.ARM.attributes) }
}
