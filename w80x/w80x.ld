/*
 * w80x.ld - Linker script for W80x applications.
 *
 * This file is run through the C preprocessor by w80x.mk, so the image
 * location stays in one place (IMG_HEADER_ADDR in the Makefile).
 *
 * Flash layout (2 MB, stock WinnerMicro layout):
 *   0x08000000  ROM params / reserved
 *   0x08002000  secboot header, 0x08002400 secboot code
 *   0x08010000  OTA/update area
 *   0x080D0000  application image header (64 bytes, 1 KB reserved)
 *   0x080D0400  application code  <-- ORIGIN of FLASH below
 *   0x081F0000  system parameters (do not overwrite)
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IMG_RUN_ADDR
#define IMG_RUN_ADDR 0x080D0400
#endif
#ifndef APP_FLASH_END
#define APP_FLASH_END 0x081F0000
#endif

MEMORY
{
    FLASH (rx)  : ORIGIN = IMG_RUN_ADDR, LENGTH = APP_FLASH_END - IMG_RUN_ADDR
    VRAM  (rw)  : ORIGIN = 0x20000000,   LENGTH = 0x100     /* vector copy */
    RAM   (rwx) : ORIGIN = 0x20000100,   LENGTH = 0x48000 - 0x100
}

ENTRY(Reset_Handler)

__stack_size = DEFINED(__stack_size) ? __stack_size : 0x1000;

SECTIONS
{
    .text :
    {
        KEEP(*(.vectors))
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.* .rdata .rdata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
    } > FLASH

    .init_array :
    {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } > FLASH

    .vdata (NOLOAD) :
    {
        __vectors_ram_start = .;
        . += 0x100;                     /* 64 vectors * 4 bytes */
        __vectors_ram_end = .;
    } > VRAM

    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.* .sdata .sdata.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
        __end = .;
        end = .;
    } > RAM

    __heap_start = __bss_end;
    __stack_top  = ORIGIN(RAM) + LENGTH(RAM);
    __heap_end   = __stack_top - __stack_size;
    ASSERT(__heap_start <= __heap_end, "RAM overflow: .data + .bss + stack do not fit")

    /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.comment) *(.eh_frame) }
}
