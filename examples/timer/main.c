/*
 * timer - periodic timer interrupts and timer-based delays.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Output on UART0,
 * 115200 baud (`make monitor`).
 *
 *   - TIMER0: every 1 ms, interrupt counts milliseconds.
 *   - TIMER1: every 250 ms, interrupt toggles LED1.
 *   - Main loop: timer_delay_ms(1000) toggles LED2 and prints how many
 *     TIMER0 ticks one delay took (expect 1000, maybe 999 or 1001).
 *   - Once at start: timer_delay_us(100) measured with the CPU cycle counter.
 *
 * TIMER5 is the delay timer (TIMER_DELAY_ID); the other timers are free.
 */
#include <stdio.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_utils.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_timer.h"
#include "boards/hlk_w801_kit.h"

static volatile uint32_t ms_ticks;

W80X_IRQ void TIMER_IRQHandler(void)
{
    uint32_t fired = timer_irq_status();
    timer_irq_clear(fired);

    if (fired & BIT(TIMER0)) {
        ms_ticks++;
    }
    if (fired & BIT(TIMER1)) {
        led_toggle(LED1);
    }
}

/* CORET: 24-bit down counter at the CPU clock (wraps every ~70 ms at 240 MHz). */
int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);
    led_init(LED1);
    led_init(LED2);
    uart0_init(115200);
    stdio_set_uart(UART0);

    timer_init();                        /* after the clock change */
    printf("\n\nTimer demo on the %s, CPU %lu Hz, APB %lu Hz, prescaler %lu\n",
           BOARD_NAME, (unsigned long)clock_cpu_hz(), (unsigned long)clock_apb_hz(),
           (unsigned long)TIMER->CFG);

    /* short delay, measured in CPU cycles */
    uint32_t t0 = CPU_TICKS;
    timer_delay_us(100);
    uint32_t c = cpu_cycles_since(t0);
    printf("timer_delay_us(100): %lu cycles = %lu us\n",
           (unsigned long)c, (unsigned long)cpu_cycles_to_us(c));

    timer_start(TIMER0, 1,   TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);
    timer_start(TIMER1, 250, TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);
    irq_enable(TIMER_IRQn);
    __enable_irq();

    for (;;) {
        uint32_t ticks0 = ms_ticks;
        timer_delay_ms(1000);
        uint32_t ticks = ms_ticks - ticks0;

        led_toggle(LED2);
        printf("timer_delay_ms(1000): %lu ms ticks, uptime %lu ms\n",
               (unsigned long)ticks, (unsigned long)ms_ticks);
    }
}
