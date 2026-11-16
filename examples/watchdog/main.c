/*
 * watchdog - feed the watchdog for a while, then "hang" and let it reset us.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Its LEDs are active
 * low (led_on() = gpio_clear()).
 *
 * What you should see, over and over (LED_PIN = LED6, WARN_PIN = LED1):
 *   1. At boot, LED6 shows why the chip reset:
 *        3 slow blinks  = power-on / RESET button
 *        10 fast blinks = watchdog reset
 *   2. About 5 s of normal 2 Hz blinking on LED6; the watchdog is fed each loop.
 *   3. The firmware stops feeding (a simulated hang) and LED6 stays on.
 *   4. One period (1 s) after the last feed, the early-warning interrupt
 *      turns LED1 on.
 *   5. One more period (1 s) later the watchdog resets the chip. The reset
 *      turns the LEDs off, and step 1 shows 10 fast blinks.
 */
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "w80x_wdg.h"
#include "boards/hlk_w801_kit.h"

#ifndef LED_PIN
#define LED_PIN     LED6     /* PB11 */
#endif
#ifndef WARN_PIN
#define WARN_PIN    LED1     /* PB25 */
#endif

#define WDG_TIMEOUT_MS  2000 /* reset at most 2 s after the last feed */

/* Early warning: one period passed without a feed. The flag stays set (we
 * must not feed here), so mask the IRQ, or it fires again forever. */
W80X_IRQ void WDG_IRQHandler(void)
{
    irq_disable(WDG_IRQn);
    led_on(WARN_PIN);
}

static void blink(int times, uint32_t ms)
{
    for (int i = 0; i < times; i++) {
        led_on(LED_PIN);  delay_ms(ms);
        led_off(LED_PIN); delay_ms(ms);
    }
}

int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);   /* set the clock *before* starting the watchdog */

    led_init(LED_PIN);                   /* output, LED off (pin high) */
    led_init(WARN_PIN);

    /* 1. Show why the chip reset. */
    if (reset_status() & CLK_RSTSTAT_WDG) {
        blink(10, 50);
    } else {
        blink(3, 1000);
    }
    reset_status_clear();

    /* 2. Run normally, feeding the watchdog. */
    wdg_start_ms(WDG_TIMEOUT_MS);
    irq_enable(WDG_IRQn);
    __enable_irq();

    for (int i = 0; i < 20; i++) {
        led_toggle(LED_PIN);
        delay_ms(250);                   /* well below WDG_TIMEOUT_MS / 2 */
        wdg_feed();
    }

    /* 3. Simulated hang: no more feeding. A busy loop, like real stuck code.
     * (If you sleep here with __wait() instead, also call
     * irq_wakeup_enable(WDG_IRQn), or the IRQ can't wake the core and you go
     * straight to the reset without the early warning.) */
    led_on(LED_PIN);
    for (;;) {
        __nop();
    }
}
