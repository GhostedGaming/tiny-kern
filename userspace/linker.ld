ENTRY(_start)

PHDRS {
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = SEGMENT_START("text-segment", 0x400000);

    .text : {
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
        *(.srodata*)
        *(.data.rel.ro*)
        *(.eh_frame*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data*)
        *(.sdata*)
        *(.got*)
        *(.got.plt*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss*)
        *(.sbss*)
    } :data

    . = ALIGN(8);
    .preinit_array : {
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
    } :data

    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(EXCLUDE_FILE(*crtbegin*.o *crtend*.o) .ctors))
        __init_array_end = .;
    } :data

    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(EXCLUDE_FILE(*crtbegin*.o *crtend*.o) .dtors))
        __fini_array_end = .;
    } :data

    .ctors : {
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
    } :data

    .dtors : {
        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))
    } :data

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.rela*)
        *(.rel*)
        *(.interp*)
        *(.dynamic*)
    }

    .note.GNU-stack : {
        *(.note.GNU-stack)
    }
}
