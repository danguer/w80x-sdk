/*
 * startup.S - XT804 vector table and reset entry for the W80x.
 *
 * The secure bootloader (in flash at 0x08002000) validates the image header
 * and starts the application from the first word of the vector table, which
 * sits at the image run address (default 0x080D0400).
 *
 * Reset_Handler only sets PSR and the stack, then calls w80x_start() (C),
 * which copies .data / vectors, clears .bss and calls main().
 *
 * SPDX-License-Identifier: MIT
 */

    .section .vectors, "a"
    .align  10                      /* VBR requires 1 KB alignment */
    .globl  __Vectors
    .type   __Vectors, @object
__Vectors:
    /* 0..31: CPU exceptions. Faults go to Default_Handler (never returns). */
    .long   Reset_Handler           /*  0 reset            */
    .rept   21
    .long   Default_Handler         /*  1..21              */
    .endr
    .long   w80x_irq_entry          /* 22 trap/tspend      */
    .rept   9
    .long   Default_Handler         /* 23..31              */
    .endr

    /* 32..63: external interrupts (IRQn 0..31), all through w80x_irq_entry,
     * which calls the C handler from w80x_irq_handlers below */
    .rept   32
    .long   w80x_irq_entry
    .endr
    .size   __Vectors, . - __Vectors

    .text
    .align  2
    .globl  Reset_Handler
    .type   Reset_Handler, %function
Reset_Handler:
    lrw     r0, 0x80000200          /* supervisor mode, IRQs/exceptions off */
    mtcr    r0, psr
    lrw     r0, __stack_top
    mov     sp, r0
    jbsr    w80x_start
.L_hang:
    br      .L_hang
    .size   Reset_Handler, . - Reset_Handler

/*
 * Common interrupt entry. Handlers are plain C functions: GCC's own
 * __attribute__((isr)) is not used because GCC 13 (Xuantie-800) only saves
 * the ipush set (r0-r3, r12, r13) in it, so a handler that calls a function
 * or uses r18+ / the FPU silently corrupts the interrupted code.
 *
 * Saved here: every register a C function may clobber: r0-r3, r12, r13,
 * r15, r16-r31 (r16/r17 are callee-saved but stm wants a range) and all
 * of fr0-fr15 (GCC treats every FPU register as caller-saved). EPC/EPSR are
 * not saved: handlers run with interrupts off, so they do not nest.
 */
    .text
    .align  2
    .globl  w80x_irq_entry
    .type   w80x_irq_entry, %function
w80x_irq_entry:
    subi    sp, sp, 156
    stm     r0-r3, (sp)
    stw     r12, (sp, 16)
    stw     r13, (sp, 20)
    stw     r15, (sp, 24)
    addi    r0, sp, 28
    stm     r16-r31, (r0)
    addi    r0, sp, 92
    fstms   fr0-fr15, (r0)

    mfcr    r0, psr                 /* vector number = PSR[23:16] */
    zext    r0, r0, 23, 16
    lsli    r0, r0, 2
    lrw     r1, w80x_irq_handlers
    addu    r1, r1, r0
    ldw     r1, (r1, 0)
    jsr     r1

    addi    r0, sp, 92
    fldms   fr0-fr15, (r0)
    addi    r0, sp, 28
    ldm     r16-r31, (r0)
    ldw     r15, (sp, 24)
    ldw     r13, (sp, 20)
    ldw     r12, (sp, 16)
    ldm     r0-r3, (sp)
    addi    sp, sp, 156
    rte
    .size   w80x_irq_entry, . - w80x_irq_entry

    /* C handler per vector number (only 22 and 32..63 are used) */
    .section .rodata
    .align  2
    .type   w80x_irq_handlers, @object
w80x_irq_handlers:
    .rept   22
    .long   Default_Handler         /*  0..21 (not reached) */
    .endr
    .long   tspend_handler          /* 22 */
    .rept   9
    .long   Default_Handler         /* 23..31 (not reached) */
    .endr
    .long   SDIO_IRQHandler            /*  0 */
    .long   MAC_IRQHandler             /*  1 */
    .long   RF_Cfg_IRQHandler          /*  2 */
    .long   SEC_IRQHandler             /*  3 */
    .long   DMA_Channel0_IRQHandler    /*  4 */
    .long   DMA_Channel1_IRQHandler    /*  5 */
    .long   DMA_Channel2_IRQHandler    /*  6 */
    .long   DMA_Channel3_IRQHandler    /*  7 */
    .long   DMA_Channel4_7_IRQHandler  /*  8 */
    .long   DMA_BURST_IRQHandler       /*  9 */
    .long   I2C_IRQHandler             /* 10 */
    .long   ADC_IRQHandler             /* 11 */
    .long   SPI_LS_IRQHandler          /* 12 */
    .long   SPI_HS_IRQHandler          /* 13 */
    .long   GPIOA_IRQHandler           /* 14 */
    .long   GPIOB_IRQHandler           /* 15 */
    .long   UART0_IRQHandler           /* 16 */
    .long   UART1_IRQHandler           /* 17 */
    .long   UART2_IRQHandler           /* 18 */
    .long   UART3_5_IRQHandler         /* 19 */
    .long   BLE_IRQHandler             /* 20 */
    .long   BT_IRQHandler              /* 21 */
    .long   PWM_IRQHandler             /* 22 */
    .long   I2S_IRQHandler             /* 23 */
    .long   SDIO_HOST_IRQHandler       /* 24 */
    .long   CORET_IRQHandler           /* 25 */
    .long   RSA_IRQHandler             /* 26 */
    .long   GPSEC_IRQHandler           /* 27 */
    .long   FLASH_IRQHandler           /* 28 */
    .long   PMU_IRQHandler             /* 29 */
    .long   TIMER_IRQHandler           /* 30 */
    .long   WDG_IRQHandler             /* 31 */
    .size   w80x_irq_handlers, . - w80x_irq_handlers

    /* The weak default handlers (tspend_handler, *_IRQHandler) are defined in
     * system.c. Don't define them here with `.weak` + `.set`: the C-SKY
     * assembler resolves such an alias while assembling, so every vector
     * pointed at Default_Handler and handlers defined in C were never used. */
