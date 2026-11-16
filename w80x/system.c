/*
 * system.c - C runtime start, SystemInit, CORET-based delays, default handler.
 * SPDX-License-Identifier: MIT
 */
#include "w80x.h"

uint32_t SystemCoreClock = W80X_PLL_HZ / 6u;   /* refreshed in SystemInit */

/* Linker symbols (w80x.ld) */
extern uint32_t __Vectors[];
extern uint32_t __vectors_ram_start[], __vectors_ram_end[];
extern uint32_t __data_load[], __data_start[], __data_end[];
extern uint32_t __bss_start[], __bss_end[];
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

int main(void);

/* Captured by Default_Handler to ease debugging (inspect with a debugger). */
volatile uint32_t w80x_fault_epc;
volatile uint32_t w80x_fault_epsr;

/* Called from Default_Handler with the exception/vector number, the PC it
 * happened at (EPC) and the PSR before it (EPSR). Weak: define it in the
 * application to report faults, e.g. print them on a polled UART. */
__attribute__((weak)) void w80x_fault_hook(uint32_t vec, uint32_t epc, uint32_t epsr)
{
    (void)vec;
    (void)epc;
    (void)epsr;
}

void Default_Handler(void)
{
    __asm__ volatile("mfcr %0, epc"  : "=r"(w80x_fault_epc));
    __asm__ volatile("mfcr %0, epsr" : "=r"(w80x_fault_epsr));
    uint32_t psr;
    __asm__ volatile("mfcr %0, psr" : "=r"(psr));
    w80x_fault_hook((psr >> 16) & 0xFFu, w80x_fault_epc, w80x_fault_epsr);
    for (;;) {
    }
}

/* Weak defaults for every vector in startup.S. Define a function with the same
 * name (W80X_IRQ void GPIOA_IRQHandler(void) {...}) to override one; it is
 * called from w80x_irq_entry in startup.S. They are
 * declared here, in C, so the *linker* picks the strong definition. */
#define W80X_WEAK_IRQ(name) \
    void name(void) __attribute__((weak, alias("Default_Handler")))

W80X_WEAK_IRQ(tspend_handler);
W80X_WEAK_IRQ(SDIO_IRQHandler);
W80X_WEAK_IRQ(MAC_IRQHandler);
W80X_WEAK_IRQ(RF_Cfg_IRQHandler);
W80X_WEAK_IRQ(SEC_IRQHandler);
W80X_WEAK_IRQ(DMA_Channel0_IRQHandler);
W80X_WEAK_IRQ(DMA_Channel1_IRQHandler);
W80X_WEAK_IRQ(DMA_Channel2_IRQHandler);
W80X_WEAK_IRQ(DMA_Channel3_IRQHandler);
W80X_WEAK_IRQ(DMA_Channel4_7_IRQHandler);
W80X_WEAK_IRQ(DMA_BURST_IRQHandler);
W80X_WEAK_IRQ(I2C_IRQHandler);
W80X_WEAK_IRQ(ADC_IRQHandler);
W80X_WEAK_IRQ(SPI_LS_IRQHandler);
W80X_WEAK_IRQ(SPI_HS_IRQHandler);
W80X_WEAK_IRQ(GPIOA_IRQHandler);
W80X_WEAK_IRQ(GPIOB_IRQHandler);
W80X_WEAK_IRQ(UART0_IRQHandler);
W80X_WEAK_IRQ(UART1_IRQHandler);
W80X_WEAK_IRQ(UART2_IRQHandler);
W80X_WEAK_IRQ(UART3_5_IRQHandler);
W80X_WEAK_IRQ(BLE_IRQHandler);
W80X_WEAK_IRQ(BT_IRQHandler);
W80X_WEAK_IRQ(PWM_IRQHandler);
W80X_WEAK_IRQ(I2S_IRQHandler);
W80X_WEAK_IRQ(SDIO_HOST_IRQHandler);
W80X_WEAK_IRQ(CORET_IRQHandler);
W80X_WEAK_IRQ(RSA_IRQHandler);
W80X_WEAK_IRQ(GPSEC_IRQHandler);
W80X_WEAK_IRQ(FLASH_IRQHandler);
W80X_WEAK_IRQ(PMU_IRQHandler);
W80X_WEAK_IRQ(TIMER_IRQHandler);
W80X_WEAK_IRQ(WDG_IRQHandler);

static void copy_words(uint32_t *dst, const uint32_t *src, const uint32_t *end)
{
    while (dst < end) {
        *dst++ = *src++;
    }
}

/* Default w80x_early_init(): nothing to do. */
__attribute__((weak)) void w80x_early_init(void)
{
}

/* Called from Reset_Handler with a valid stack; .data/.bss not yet set up. */
void __attribute__((noreturn, used)) w80x_start(void)
{
    w80x_early_init();

    /* Vector table copy into SRAM (VBR points there; patchable at runtime). */
    copy_words(__vectors_ram_start, __Vectors, __vectors_ram_end);
    copy_words(__data_start, __data_load, __data_end);
    for (uint32_t *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }

    SystemInit();

    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) {
        (*f)();
    }

    main();
    for (;;) {
    }
}

void SystemInit(void)
{
    __set_VBR((uint32_t)(uintptr_t)__vectors_ram_start);
    __set_CHR(__get_CHR() | CHR_IAE);

    /* The bootloader may leave interrupts configured: start clean. */
    VIC->ICER[0] = 0xFFFFFFFFu;
    VIC->IWDR[0] = 0xFFFFFFFFu;   /* no wake-up sources (irq_wakeup_enable) */
    VIC->ICPR[0] = 0xFFFFFFFFu;
    VIC->IABR[0] = 0;

    __enable_excp();          /* exceptions on; IRQs stay off until __enable_irq() */

    /* Pick up whatever clock the bootloader left us with. */
    SystemCoreClock = W80X_PLL_HZ / ((CLK->DIV & CLK_DIV_CPU_Msk) >> CLK_DIV_CPU_Pos);

    /* CORET free-running at CPU clock, used as the delay time base. */
    CORET->CTRL = 0;
    CORET->LOAD = CORET_MAX;
    CORET->VAL  = 0;
    CORET->CTRL = CORET_CTRL_CLKSOURCE | CORET_CTRL_ENABLE;
}

/* ---- delays -------------------------------------------------------------- */

void delay_ticks(uint32_t ticks)
{
    uint32_t prev = CPU_TICKS;

    while (ticks) {
        uint32_t now = CPU_TICKS;
        uint32_t elapsed = (prev - now) & CORET_MAX;   /* down-counter */
        prev = now;
        if (elapsed >= ticks) {
            break;
        }
        ticks -= elapsed;
    }
}

void delay_us(uint32_t us)
{
    uint32_t per_us = SystemCoreClock / 1000000u;
    while (us > 1000u) {          /* avoid 32-bit overflow on long waits */
        delay_ticks(per_us * 1000u);
        us -= 1000u;
    }
    delay_ticks(per_us * us);
}

void delay_ms(uint32_t ms)
{
    uint32_t per_ms = SystemCoreClock / 1000u;
    while (ms--) {
        delay_ticks(per_ms);
    }
}
