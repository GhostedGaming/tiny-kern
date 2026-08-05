ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5); /* R-X */
    data PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
        *(.srodata*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data*)
        *(.sdata*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss*)
        *(.sbss*)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
        *(.rela*)
        *(.rel*)
    }
}
