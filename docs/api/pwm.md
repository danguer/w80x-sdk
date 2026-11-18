# PWM

[API index](README.md)

- **Source:** [`w80x_pwm.h`](../../w80x/w80x_pwm.h)
- **Registers:** [PWM controller](../mcu/pwm.md)
- **Example:** [`examples/pwm`](../../examples/pwm/main.c)

```c
pwm_init();                                   // clock on, controller reset
pwm_pin(PB25, GPIO_AF2);                      // PWM3 on PB25 (LED1 on the HLK kit)
uint32_t hz = pwm_setup(PWM3, 1000, PWM_INVERT);  // 1 kHz, inverted; returns the rate set
pwm_set_duty(PWM3, PWM_DUTY_MAX / 4);         // 25 %
pwm_start(PWM3);

// a fixed number of pulses, then an interrupt
pwm_setup(PWM4, 4, PWM_ONESHOT);
pwm_set_count(PWM4, 5);
pwm_irq_enable(PWM4, 1);
irq_enable(PWM_IRQn);
pwm_start(PWM4);

W80X_IRQ void PWM_IRQHandler(void)            // shared by all channels
{
    uint32_t fired = pwm_irq_status();        // bit n = channel n
    pwm_irq_clear(fired);
}

// complementary pair with dead time: CH1 = inverted CH0
pwm_setup(PWM0, 20000, PWM_EDGE);
pwm_complementary(PWM_PAIR01, 40, PWM_DEAD_DIV1);  // 40 clocks = 1 us at 40 MHz
pwm_start_mask(BIT(PWM0) | BIT(PWM1));
```

Five output channels, PWM0..PWM4. Each one divides the APB clock (40 MHz
by default) by a 16-bit divider and counts an 8-bit period, so the duty has
at most 256 steps. `pwm_setup()` uses the longest period (255) and adjusts
the divider for the rate. Above APB / 256 (156 kHz) it shortens the period
instead, which means coarser duty steps, up to APB / 2 (20 MHz). The
slowest rate is about 2.4 Hz. It returns the rate it set, which can differ
from the request because both values are integers (rounded to Hz), or 0
if the request is out of range. `PWM_CENTER` gives center-aligned output: the same steps at
half the rate.

`pwm_set_duty()` takes 0..`PWM_DUTY_MAX` (0xFFFF). The hardware can't make
0 %: its shortest high time is one count, so 0 gives 1/256 at period 255.
For a steady low, stop the channel or switch the pin to a GPIO output.
`PWM_DUTY_MAX` gives a steady high. `pwm_set_cmp()` writes the compare
register directly (high for cmp + 1 counts, edge-aligned). Set the duty
after `pwm_setup()`, which resets it to 50 %. `PWM_INVERT` inverts the
output, which suits the active-low LEDs on the HLK kit.

`PWM_ONESHOT` stops the channel after `pwm_set_count()` periods (1..255)
and sets its bit in `pwm_irq_status()`; `pwm_running()` then reads 0 and
`pwm_start()` sends the next burst (seen on the board). Whether the bit is also set every
count periods in the normal (auto-reload) mode is not yet confirmed.

Pairs: `pwm_complementary()` makes CH1 the inverse of CH0 (or CH3 of CH2),
with optional dead time; `pwm_pair_sync()` gives the follower the same
waveform; `pwm_all_sync()` makes every channel follow CH0;
`pwm_independent()` undoes all three. The follower's counter must run as
well, so start the channels together with `pwm_start_mask()`.
`pwm_brake()` sets the level a channel outputs while the brake input pin
is active.

Pins (W801/W806):

| Channel | AF1 | AF2 | AF3 |
|---|---|---|---|
| PWM0 | PB0 | PB12, PB19 | PA2, PA10 |
| PWM1 | PB1 | PB13, PB20 | PA3, PA11 |
| PWM2 | PB2 | PB14, PB24 | PA0, PA12 |
| PWM3 | PB3 | PB15, PB25 | PA1, PA13 |
| PWM4 | PA7 | PB16, PB26 | PA4, PA14 |
| Brake input | PA8 | PB17 | PA5, PA15, PB8 |

PB19/PB20 are UART0 (the console) and PA1 is JTAG. On the HLK kit, LED1
(PB25) is PWM3 and LED2 (PB26) and LED4 (PB16) are PWM4.

The clock is APB: call `pwm_setup()` again after a CPU clock change that
changes APB (see [Clocks](clock.md)).

Input capture (channels 0 and 4) is not covered yet; its registers are
defined in `w80x.h`.

While a pin is routed to the PWM, `gpio_read()` on it returns 0 whatever
the output does, so the output can't be checked by reading the pin back.

`examples/pwm` has run on the HLK kit: the rate table and the counted
pulses on LED2 with their interrupt work. The other modes (center-aligned,
complementary, sync, brake) are not yet tested.
