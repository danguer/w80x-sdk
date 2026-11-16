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
__noinit_size = DEFINED(__noinit_size) ? __noinit_size : 0x400;

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

    /*
     * Not loaded or cleared by startup (W80X_NOINIT in w80x.h). Placed at the
     * top of RAM, just below the stack, in a fixed __noinit_size block. On a
     * W801 the bootloader overwrites low RAM during a watchdog reset but
     * keeps everything from 0x20016000 up: checked with the classic secboot
     * (cjacker/wm_sdk_w80x tools/w800 @197ad91) and the wm_iot_sdk bootloader
     * (examples/confirm_reset_values, docs/mcu/clock.md). A fixed address
     * also keeps the data in place when a new build changes .data/.bss.
     */
    __stack_top  = ORIGIN(RAM) + LENGTH(RAM);
    .noinit (__stack_top - __stack_size - __noinit_size) (NOLOAD) :
    {
        __noinit_start = .;
        *(.noinit .noinit.*)
        . = ALIGN(4);
        __noinit_end = .;
    } > RAM
    ASSERT(__noinit_end <= __stack_top - __stack_size, "W80X_NOINIT data is larger than __noinit_size")

    __heap_start = __end;
    __heap_end   = __noinit_start;
    ASSERT(__heap_start <= __heap_end, "RAM overflow: .data + .bss + noinit + stack do not fit")

    /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.comment) *(.eh_frame) }
}
