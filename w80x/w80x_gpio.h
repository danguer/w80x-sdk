/*
 * w80x_gpio.h - Header-only GPIO driver (manual chapter 9).
 *
 * Pins are encoded as a single byte: (port << 5) | bit
 *     PA0..PA15  -> 0..15
 *     PB0..PB31  -> 32..63
 *
 *   gpio_mode(PB5, GPIO_OUTPUT);
 *   gpio_toggle(PB5);
 *   gpio_mode(PA0, GPIO_INPUT_PULLUP);
 *   if (!gpio_read(PA0)) { ... }
 *
 * Notes from the manual:
 *  - After reset every pin has its alternate function enabled (AF_SEL=1);
 *    gpio_mode() switches the pin back to plain GPIO.
 *  - Pull-up register is active LOW (0 = pull-up on); pull-down is active high.
 *  - DATA_EN masks which DATA bits are readable/writable. It resets to all
 *    ones and this driver leaves it that way; writes are read-modify-write
 *    inside a short IRQ-safe critical section.
 *
 * Measured on the HLK-W801-KIT (240 MHz CPU, 40 MHz APB):
 *  - Writing DATA while the pin is an input is lost: gpio_set() followed by
 *    gpio_mode(pin, GPIO_OUTPUT) drives 0. Use gpio_output(pin, level),
 *    which writes the level again once the pin is an output; the pin may
 *    show the old level for a few bus cycles in between.
 *  - gpio_read() sees the pad on GPIO inputs and on alternate-function
 *    inputs (e.g. SPI MISO), about 250 ns after the level changes, so a
 *    read right after writing a pin wired to it returns the old level.
 *  - gpio_read() returns 0 on a pin driven by a peripheral (SPI SCK and CS
 *    read 0 while high, checked with a logic analyzer). It can't be used to
 *    watch a peripheral's output.
 *  - Reserved/special pins: PA0 = BOOTMODE, PA1/PA4 = JTAG,
 *    PB19/PB20 = UART0 TX/RX (used by the bootloader for flashing).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_GPIO_H
#define W80X_GPIO_H

#include "w80x.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPIO_PORT_A  0u
#define GPIO_PORT_B  1u
#define GPIO_PIN(port, n)  ((uint8_t)(((port) << 5) | ((n) & 31u)))

enum {
    PA0 = GPIO_PIN(0, 0),  PA1,  PA2,  PA3,  PA4,  PA5,  PA6,  PA7,
    PA8,  PA9,  PA10, PA11, PA12, PA13, PA14, PA15,
    PB0 = GPIO_PIN(1, 0),  PB1,  PB2,  PB3,  PB4,  PB5,  PB6,  PB7,
    PB8,  PB9,  PB10, PB11, PB12, PB13, PB14, PB15,
    PB16, PB17, PB18, PB19, PB20, PB21, PB22, PB23,
    PB24, PB25, PB26, PB27, PB28, PB29, PB30, PB31,
};

/* "No pin": pass to spi_pins() / uart_pins() to leave a signal unrouted. */
#define GPIO_NO_PIN  0xFFu

/* Mode flags (combine: GPIO_OUTPUT | GPIO_PULLUP is allowed). */
#define GPIO_INPUT           0x00u
#define GPIO_OUTPUT          0x01u
#define GPIO_PULLUP          0x02u
#define GPIO_PULLDOWN        0x04u
#define GPIO_INPUT_PULLUP    (GPIO_INPUT | GPIO_PULLUP)
#define GPIO_INPUT_PULLDOWN  (GPIO_INPUT | GPIO_PULLDOWN)

/* Pin levels, for gpio_output() / gpio_write() / gpio_read(). */
#define GPIO_LOW             0
#define GPIO_HIGH            1

/* Alternate functions: see the pin multiplexing table in the datasheet. */
typedef enum { GPIO_AF1 = 0, GPIO_AF2 = 1, GPIO_AF3 = 2, GPIO_AF4 = 3 } gpio_af_t;

/* Interrupt triggers */
typedef enum {
    GPIO_IRQ_RISING = 0,
    GPIO_IRQ_FALLING,
    GPIO_IRQ_BOTH,
    GPIO_IRQ_HIGH,
    GPIO_IRQ_LOW,
} gpio_irq_t;

W80X_INLINE GPIO_TypeDef *gpio_port(uint8_t pin)
{
    return (pin & 0x20u) ? GPIOB : GPIOA;
}
W80X_INLINE uint32_t gpio_mask(uint8_t pin) { return 1UL << (pin & 31u); }

/* Read-modify-write helper, safe against interrupts. */
W80X_INLINE void gpio__rmw(__IO uint32_t *reg, uint32_t clear, uint32_t set)
{
    uint32_t p = irq_save();
    *reg = (*reg & ~clear) | set;
    irq_restore(p);
}

/* ---- configuration ---------------------------------------------------- */

W80X_INLINE void gpio_mode(uint8_t pin, uint32_t mode)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = gpio_mask(pin);

    gpio__rmw(&g->AF_SEL, m, 0);                                    /* plain GPIO   */
    gpio__rmw(&g->PULL_EN, m, (mode & GPIO_PULLUP)   ? 0 : m);      /* active low   */
    gpio__rmw(&g->DN_EN,   m, (mode & GPIO_PULLDOWN) ? m : 0);
    gpio__rmw(&g->DIR,     m, (mode & GPIO_OUTPUT)   ? m : 0);
}

/* Make the pin a push-pull output at `level`. DATA writes are lost while
 * the pin is an input, so the level is written again after the direction
 * changes. */
W80X_INLINE void gpio_output(uint8_t pin, int level)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = gpio_mask(pin);

    gpio__rmw(&g->DATA, m, level ? m : 0);   /* in case it does latch */
    gpio_mode(pin, GPIO_OUTPUT);
    gpio__rmw(&g->DATA, m, level ? m : 0);
}

/* Route the pin to one of its alternate functions (UART, SPI, PWM, ...). */
W80X_INLINE void gpio_af(uint8_t pin, gpio_af_t af)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = gpio_mask(pin);

    gpio__rmw(&g->AF_S0, m, (af & 1u) ? m : 0);
    gpio__rmw(&g->AF_S1, m, (af & 2u) ? m : 0);
    gpio__rmw(&g->AF_SEL, 0, m);
}

/* Analog function (ADC / touch): AF off, input, pull-up and pull-down off. */
W80X_INLINE void gpio_analog(uint8_t pin)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = gpio_mask(pin);

    gpio__rmw(&g->AF_SEL, m, 0);
    gpio__rmw(&g->DIR, m, 0);
    gpio__rmw(&g->PULL_EN, 0, m);
    gpio__rmw(&g->DN_EN, m, 0);
}

/* ---- data --------------------------------------------------------------- */

W80X_INLINE int  gpio_read(uint8_t pin)  { return (gpio_port(pin)->DATA & gpio_mask(pin)) != 0; }
W80X_INLINE void gpio_set(uint8_t pin)   { gpio__rmw(&gpio_port(pin)->DATA, 0, gpio_mask(pin)); }
W80X_INLINE void gpio_clear(uint8_t pin) { gpio__rmw(&gpio_port(pin)->DATA, gpio_mask(pin), 0); }
W80X_INLINE void gpio_write(uint8_t pin, int v)
{
    uint32_t m = gpio_mask(pin);
    gpio__rmw(&gpio_port(pin)->DATA, m, v ? m : 0);
}
W80X_INLINE void gpio_toggle(uint8_t pin)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t p = irq_save();
    g->DATA ^= gpio_mask(pin);
    irq_restore(p);
}

/* Whole-port access: only bits in `mask` are changed. */
W80X_INLINE uint32_t gpio_port_read(GPIO_TypeDef *g) { return g->DATA; }
W80X_INLINE void gpio_port_write(GPIO_TypeDef *g, uint32_t mask, uint32_t value)
{
    gpio__rmw(&g->DATA, mask, value & mask);
}

/* ---- interrupts ----------------------------------------------------------- */

/* Configure the trigger and enable the pin's interrupt.
 * You still need irq_enable(GPIOA_IRQn / GPIOB_IRQn) and __enable_irq(). */
W80X_INLINE void gpio_irq_enable(uint8_t pin, gpio_irq_t trig)
{
    GPIO_TypeDef *g = gpio_port(pin);
    uint32_t m = gpio_mask(pin);
    int level = (trig == GPIO_IRQ_HIGH) || (trig == GPIO_IRQ_LOW);
    int both  = (trig == GPIO_IRQ_BOTH);
    int high  = (trig == GPIO_IRQ_RISING) || (trig == GPIO_IRQ_HIGH);

    gpio__rmw(&g->IE,  m, 0);
    gpio__rmw(&g->IS,  m, level ? m : 0);
    gpio__rmw(&g->IBE, m, both  ? m : 0);
    gpio__rmw(&g->IEV, m, high  ? m : 0);
    g->IC = m;                                   /* drop stale status */
    gpio__rmw(&g->IE,  0, m);
}

W80X_INLINE void     gpio_irq_disable(uint8_t pin) { gpio__rmw(&gpio_port(pin)->IE, gpio_mask(pin), 0); }
W80X_INLINE int      gpio_irq_pending(uint8_t pin) { return (gpio_port(pin)->MIS & gpio_mask(pin)) != 0; }
W80X_INLINE void     gpio_irq_clear(uint8_t pin)   { gpio_port(pin)->IC = gpio_mask(pin); }
W80X_INLINE uint32_t gpio_irq_status(GPIO_TypeDef *g)           { return g->MIS; }
W80X_INLINE void     gpio_irq_clear_mask(GPIO_TypeDef *g, uint32_t m) { g->IC = m; }

#ifdef __cplusplus
}
#endif

#endif /* W80X_GPIO_H */
