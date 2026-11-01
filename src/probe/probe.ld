OUTPUT_FORMAT(elf32-littlearm)
OUTPUT_ARCH(arm)
ENTRY(x6_module_open)
PHDRS {
    image PT_LOAD FLAGS(7);
}
SECTIONS {
    . = 0xe9100000;
    EXEC_MOD : {
        KEEP(*(.text.entry))
        *(.text*)
        *(.rodata*)
    } :image
    EXEC_DATA : ALIGN(4) { *(.data*) } :image
    EXEC_BSS (NOLOAD) : ALIGN(4) { *(.bss*) *(COMMON) } :image
    MAGIC 0xffff0000 : { KEEP(*(MAGIC)) } :NONE
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
