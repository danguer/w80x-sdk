# Cycle counting

[API index](README.md)

- **Source:** [`w80x_utils.h`](../../w80x/w80x_utils.h)
- **Examples:** [`examples/timer`](../../examples/timer/main.c), [`examples/spi`](../../examples/spi/main.c), [`examples/rtc`](../../examples/rtc/main.c)

```c
uint32_t t0 = CPU_TICKS;
do_something();
uint32_t cyc = cpu_cycles_since(t0);          // one interval, up to one CORET wrap
printf("%lu us\n", (unsigned long)cpu_cycles_to_us(cyc));   // also cpu_cycles_to_ms()

uint32_t last = CPU_TICKS, total = 0;         // longer intervals: add up steps
while (total < cpu_ms_to_cycles(500)) {
    total += cpu_cycles_step(&last);
}
```

`CPU_TICKS` is the core timer (CORET), a 24-bit down counter at the CPU
clock. One `cpu_cycles_since()` measurement covers at most 2^24 - 1 cycles:
69.9 ms at 240 MHz, 210 ms at 80 MHz. Longer intervals wrap and come out too
short. For longer waits, call `cpu_cycles_step()` at least once per wrap and
add up the steps. It reads CORET once per call, so no cycles are lost between
steps.

The conversions use `SystemCoreClock`, so convert before changing the CPU
clock. They round down. `cpu_ms_to_cycles()` returns 32 bits, which covers
17.8 s at 240 MHz. Everything is inline, so a measurement costs only the
CORET read and a subtract.
