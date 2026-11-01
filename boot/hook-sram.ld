ENTRY(hook)
SECTIONS { . = 0x4200; .text : { *(.text.entry) *(.text*) *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }
