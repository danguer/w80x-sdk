/*
 * button_irq - toggle an LED from a GPIO falling-edge interrupt.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Its LEDs are active
 * low (led_on() = gpio_clear()).
 *
 * The kit has NO user button: BOOT and RESET are wired to special pins.
 * Wire a push button between BUTTON_PIN (default PB6) and GND; the internal
 * pull-up is used. Another free pin:  make clean all CFLAGS=-DBUTTON_PIN=PB7
 * (the GPIOB handler below only covers port B pins).
 *
 * LED2 blinks in the main loop to show the CPU is alive; each press
 * toggles LED1.
 */
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "boards/hlk_w801_kit.h"

#ifndef LED_PIN
#define LED_PIN       LED1   /* PB25 */
#endif
#ifndef HEARTBEAT_PIN
#define HEARTBEAT_PIN LED2   /* PB26 */
#endif
#ifndef BUTTON_PIN
#define BUTTON_PIN    PB6    /* external button to GND */
#endif

/* Handler name follows the pin's port: GPIOA_IRQHandler or GPIOB_IRQHandler. */
W80X_IRQ void GPIOB_IRQHandler(void)
{
    uint32_t pending = gpio_irq_status(GPIOB);

    if (pending & gpio_mask(BUTTON_PIN)) {
        led_toggle(LED_PIN);
    }

    gpio_irq_clear_mask(GPIOB, pending);
}

int main(void)
{
    clock_set_cpu_div(CLK_CPU_160MHZ);

    led_init(LED_PIN);                   /* output, LED off (pin high) */
    led_init(HEARTBEAT_PIN);
    gpio_mode(BUTTON_PIN, GPIO_INPUT_PULLUP);

    gpio_irq_enable(BUTTON_PIN, GPIO_IRQ_FALLING);
    irq_enable(GPIOB_IRQn);
    __enable_irq();

    for (;;) {
        led_on(HEARTBEAT_PIN);           /* gpio_clear: low = on */
        delay_ms(100);
        led_off(HEARTBEAT_PIN);          /* gpio_set: high = off */
        delay_ms(900);
    }
}
