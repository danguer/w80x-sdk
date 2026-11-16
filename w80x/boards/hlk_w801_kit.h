/*
 * boards/hlk_w801_kit.h - HLK-W801-KIT-V1.1 (Hi-Link) development board.
 *
 * Docs: https://h.hlktech.com/Mobile/download/fdetail/170.html
 *
 * - 7 user LEDs. Their anodes go to 3.3 V, so they are ACTIVE LOW:
 *   drive the pin low (gpio_clear) to light the LED, high (gpio_set) to turn it off.
 *   The led_*() helpers below hide this.
 * - No user button: BOOT and RESET are wired to special pins (PA0 = BOOTMODE,
 *   chip reset). Examples that need a button expect one wired between
 *   BUTTON_PIN and GND.
 * - UART0 (PB19 TX / PB20 RX) goes to the USB-serial chip, used for flashing.
 *
 * Usage:  #include "boards/hlk_w801_kit.h"
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BOARD_HLK_W801_KIT_H
#define BOARD_HLK_W801_KIT_H

#include "w80x.h"
#include "w80x_gpio.h"

#define BOARD_NAME  "HLK-W801-KIT-V1.1"

/* Numbering follows the board schematic. */
#define LED1        PB25
#define LED2        PB26
#define LED3        PB18
#define LED4        PB16
#define LED5        PB17
#define LED6        PB11
#define LED7        PB5
#define LED_COUNT   7

static const uint8_t board_leds[LED_COUNT] = { LED1, LED2, LED3, LED4, LED5, LED6, LED7 };

/* Active low: on = 0, off = 1. */
W80X_INLINE void led_on(uint8_t pin)     { gpio_clear(pin); }
W80X_INLINE void led_off(uint8_t pin)    { gpio_set(pin); }
W80X_INLINE void led_toggle(uint8_t pin) { gpio_toggle(pin); }
W80X_INLINE void led_write(uint8_t pin, int on) { gpio_write(pin, !on); }

/* Make the pin an output with the LED off. */
W80X_INLINE void led_init(uint8_t pin) { gpio_output(pin, GPIO_HIGH); }

W80X_INLINE void board_leds_init(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        led_init(board_leds[i]);
    }
}

#endif /* BOARD_HLK_W801_KIT_H */
