# GPIO

[API index](README.md)

- **Source:** [`w80x_gpio.h`](../../w80x/w80x_gpio.h)
- **Examples:** [`examples/blink`](../../examples/blink/main.c), [`examples/button_irq`](../../examples/button_irq/main.c)

```c
gpio_mode(PB5, GPIO_OUTPUT);
gpio_mode(PA2, GPIO_INPUT_PULLUP);       // also GPIO_INPUT, GPIO_INPUT_PULLDOWN, GPIO_OUTPUT|GPIO_PULLUP
gpio_set(PB5); gpio_clear(PB5); gpio_toggle(PB5); gpio_write(PB5, 1);
int v = gpio_read(PA2);
gpio_port_write(GPIOB, 0x0F, 0x05);      // masked port write
gpio_af(PB19, GPIO_AF1);                 // route to an alternate function
gpio_analog(PA1);

gpio_irq_enable(PA2, GPIO_IRQ_FALLING);  // RISING, FALLING, BOTH, HIGH, LOW
irq_enable(GPIOA_IRQn);
__enable_irq();
W80X_IRQ void GPIOA_IRQHandler(void) { gpio_irq_clear(PA2); /* ... */ }
```

Pins are plain integers (`PA0..PA15` = 0..15, `PB0..PB31` = 32..63), so they
can be stored in tables or passed around.

Notes from the register manual:
- Every pin starts with its alternate function enabled. `gpio_mode()` switches it to GPIO.
- The pull-up register is active-low and the pull-down register is active-high. The driver hides this.
- Some pins have fixed roles: PA0 = BOOTMODE, PA1/PA4 = JTAG, PB19/PB20 = UART0 (used for flashing).
