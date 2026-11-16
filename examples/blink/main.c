/*
 * blink - toggle an LED at 2 Hz with the CPU running at 240 MHz.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Its LEDs are active
 * low: gpio_clear() turns one on, gpio_set() turns it off.
 *
 * Build:  make            Flash:  make flash PORT=/dev/ttyUSB0
 * Change the LED:          make clean all CFLAGS=-DLED_PIN=LED3
 */
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "boards/hlk_w801_kit.h"

#ifndef LED_PIN
#define LED_PIN LED1     /* PB25 */
#endif

int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);   /* 480 MHz PLL / 2 */
    clock_enable(CLK_GATE_GPIO);         /* on by default, shown for clarity */

    gpio_output(LED_PIN, GPIO_HIGH);     /* high = LED off */

    for (;;) {
        gpio_clear(LED_PIN);             /* low = LED on */
        delay_ms(250);
        gpio_set(LED_PIN);               /* high = LED off */
        delay_ms(250);
    }
}
