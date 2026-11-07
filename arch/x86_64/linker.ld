ENTRY(_start)

SECTIONS
{
    . = 1M;

    .boot ALIGN(4096) : {
        *(.multiboot_header)
    }

    .text ALIGN(4096) : {
        *(.text*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata*)
    }

    .data ALIGN(4096) : {
        *(.data*)
    }

    .bss ALIGN(4096) : {
        *(.bss*)
        *(COMMON)
    }

    kernel_end = ALIGN(4096);
}
