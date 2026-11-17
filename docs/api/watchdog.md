# Watchdog

[API index](README.md)

- **Source:** [`w80x_wdg.h`](../../w80x/w80x_wdg.h)
- **Example:** [`examples/watchdog`](../../examples/watchdog/main.c)

```c
if (reset_status() & CLK_RSTSTAT_WDG) { /* the last reset came from the watchdog */ }
reset_status_clear();

wdg_start_ms(2000);       // reset at most 2 s after the last feed
for (;;) { work(); wdg_feed(); }   // feed at least every 1 s (timeout / 2)

wdg_stop();
wdg_system_reset();       // software reboot (does not return)
```

The hardware resets the chip when a timeout finds the previous timeout's
interrupt still uncleared. Feeding clears it and restarts the countdown, so the
interrupt comes one period and the reset two periods after the last feed.
`wdg_start_ms()` sets the period to half the timeout.
The counter runs on the APB clock: call `wdg_start_ms()` again after
`clock_set_cpu_div()`. Enabling `WDG_IRQn` gives an early warning one period
(half the timeout) before the reset. See the header for the rules a handler must follow.
