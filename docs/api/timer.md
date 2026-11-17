# Timers

[API index](README.md)

- **Source:** [`w80x_timer.h`](../../w80x/w80x_timer.h)
- **Example:** [`examples/timer`](../../examples/timer/main.c)

```c
timer_init();
timer_start(TIMER0, 500, TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);   // every 500 ms
irq_enable(TIMER_IRQn);

W80X_IRQ void TIMER_IRQHandler(void)          // shared by all six timers
{
    uint32_t fired = timer_irq_status();      // bit n = timer n
    timer_irq_clear(fired);
    if (fired & BIT(TIMER0)) { /* ... */ }
}

timer_delay_us(250);                          // waits on TIMER5 (TIMER_DELAY_ID)
timer_delay_ms(1500);
```

Six 32-bit timers on a 1 us tick from the APB clock, counting in us or ms.
`timer_delay_us/ms()` measure wall-clock time, so interrupts during the wait
don't stretch them, and they reach 2^32 us or ms. `delay_us/ms()` (CORET)
remain for short busy waits. TIMER5 is reserved for the delay functions;
compile with `-DTIMER_DELAY_ID=TIMERn` to use another. Call `timer_init()` again
after `clock_set_cpu_div()` (the delay functions do it themselves).
