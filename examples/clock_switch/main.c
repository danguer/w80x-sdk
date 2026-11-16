/*
 * clock_switch - step the CPU clock through 240/160/80/40/12 MHz.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Its LEDs are active
 * low (led_on() = gpio_clear()).
 *
 * LED1..LED5 show the current step (1 = 240 MHz ... 5 = 12 MHz) as a bar.
 * LED7 then blinks with delay_ms(), which keeps the same rate at every
 * frequency because the delays follow SystemCoreClock. Finally LED6 stays on
 * for a fixed-length busy loop: the pulse visibly stretches as the CPU slows
 * down, which shows the clock change really happened.
 */
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "boards/hlk_w801_kit.h"

static const clk_cpu_div_t steps[] = {
    CLK_CPU_240MHZ, CLK_CPU_160MHZ, CLK_CPU_80MHZ, CLK_CPU_40MHZ, CLK_CPU_12MHZ,
};
#define NSTEPS (sizeof(steps) / sizeof(steps[0]))

static void busy_loop(uint32_t n)
{
    while (n--) {
        __nop();
    }
}

int main(void)
{
    board_leds_init();                   /* all 7 LEDs: output, off */

    for (;;) {
        for (unsigned i = 0; i < NSTEPS; i++) {
            clock_set_cpu_div(steps[i]);

            /* step indicator: LED1..LED(i+1) on */
            for (unsigned l = 0; l < NSTEPS; l++) {
                led_write(board_leds[l], l <= i);
            }

            /* constant-rate blink at every frequency */
            for (int b = 0; b < 3; b++) {
                led_on(LED7);  delay_ms(100);
                led_off(LED7); delay_ms(200);
            }

            /* fixed cycle count: ~0.1 s at 240 MHz, ~2 s at 12 MHz */
            led_on(LED6);
            busy_loop(8000000);
            led_off(LED6);
            delay_ms(1000);
        }
    }
}
